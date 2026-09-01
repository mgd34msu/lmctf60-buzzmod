#ifndef SG_TACTIC_CONTRACT_H
#define SG_TACTIC_CONTRACT_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "sg_localization_runtime.h"
#include "sg_rune_compact_field_service.h"

#define SG_TACTIC_CAPABILITY_BIT(capability) (UINT32_C(1) << (capability))
#define SG_TACTIC_CAPABILITY_MASK \
	((UINT32_C(1) << SG_TACTIC_CAPABILITY_COUNT) - UINT32_C(1))
#define SG_TACTIC_PHASE_BIT(phase) (UINT32_C(1) << (phase))
#define SG_TACTIC_PHASE_MASK \
	((UINT32_C(1) << SG_TACTIC_PHASE_COUNT) - UINT32_C(1))

/* Modifiers are signed Q52.12 deformations. The cap keeps a transient
 * preference bounded even when an owner receives a large batch of evidence. */
#define SG_TACTIC_MODIFIER_MAX_DEFORMATION_UNITS (INT64_C(1) << 52)

typedef enum sg_tactic_phase_e
{
	SG_TACTIC_PHASE_GROUND = 0,
	SG_TACTIC_PHASE_CROUCH,
	SG_TACTIC_PHASE_JUMP,
	SG_TACTIC_PHASE_AIR,
	SG_TACTIC_PHASE_SWIM,
	SG_TACTIC_PHASE_MOVER,
	SG_TACTIC_PHASE_HOOK,
	SG_TACTIC_PHASE_MECHANISM,
	SG_TACTIC_PHASE_RECOVERY,
	SG_TACTIC_PHASE_COUNT
} sg_tactic_phase_t;

typedef enum sg_tactic_capability_e
{
	SG_TACTIC_CAPABILITY_WALK = 0,
	SG_TACTIC_CAPABILITY_CROUCH,
	SG_TACTIC_CAPABILITY_JUMP,
	SG_TACTIC_CAPABILITY_DROP,
	SG_TACTIC_CAPABILITY_SWIM,
	SG_TACTIC_CAPABILITY_AIR_CONTROL,
	SG_TACTIC_CAPABILITY_HOOK,
	SG_TACTIC_CAPABILITY_STRAFE,
	SG_TACTIC_CAPABILITY_WAIT,
	SG_TACTIC_CAPABILITY_MECHANISM,
	SG_TACTIC_CAPABILITY_TELEPORT,
	SG_TACTIC_CAPABILITY_PUSH,
	SG_TACTIC_CAPABILITY_MOVER,
	SG_TACTIC_CAPABILITY_COUNT
} sg_tactic_capability_t;

typedef enum sg_tactic_result_status_e
{
	SG_TACTIC_RESULT_PROGRESS = 0,
	SG_TACTIC_RESULT_HOLD,
	SG_TACTIC_RESULT_RETRY,
	SG_TACTIC_RESULT_FAILURE
} sg_tactic_result_status_t;

typedef enum sg_tactic_failure_reason_e
{
	SG_TACTIC_FAILURE_NONE = 0,
	SG_TACTIC_FAILURE_NO_GRADIENT,
	SG_TACTIC_FAILURE_NO_LEGAL_CAPABILITY,
	SG_TACTIC_FAILURE_LIVE_STATE,
	SG_TACTIC_FAILURE_TRACE_REQUIRED
} sg_tactic_failure_reason_t;

typedef enum sg_tactic_modifier_kind_e
{
	SG_TACTIC_MODIFIER_THREAT = 0,
	SG_TACTIC_MODIFIER_COVER,
	SG_TACTIC_MODIFIER_WEAPON_OPPORTUNITY,
	SG_TACTIC_MODIFIER_OBSTRUCTION,
	SG_TACTIC_MODIFIER_TEAMMATE,
	SG_TACTIC_MODIFIER_KIND_COUNT
} sg_tactic_modifier_kind_t;

typedef enum sg_tactic_modifier_target_kind_e
{
	SG_TACTIC_MODIFIER_TARGET_EXACT_SUCCESSOR = 0,
	SG_TACTIC_MODIFIER_TARGET_ANY_SUCCESSOR,
	SG_TACTIC_MODIFIER_TARGET_KIND_COUNT
} sg_tactic_modifier_target_kind_t;

typedef enum sg_tactic_capability_flag_e
{
	SG_TACTIC_CAPABILITY_REQUIRES_SUPPORT = 1,
	SG_TACTIC_CAPABILITY_REQUIRES_WATER = 2,
	SG_TACTIC_CAPABILITY_REQUIRES_AIR = 4,
	SG_TACTIC_CAPABILITY_REQUIRES_LIVE_TRACE = 8,
	SG_TACTIC_CAPABILITY_MECHANISM_BOUNDARY = 16
} sg_tactic_capability_flag_t;

/* A field query's terminal result is consumed by strategy. Tactics receive
 * only this copy of its nonterminal v12 step, never a result-kind union. The
 * embedded field step preserves the PORTAL/DIRECT/STANCE payload union.
 *
 * `v12.cost_to_go` and `v12.next_cost_to_go` are canonical field costs for
 * the source and exact successor states.  A candidate's `local_cost` is an
 * independently authenticated, exact live evaluation of the matching fiber.
 * It proves a strict live descent only when `next_cost_to_go + local_cost` is
 * strictly below the canonical source cost; it is never a residual inferred
 * from the two canonical costs. */
typedef struct sg_tactic_transition_s
{
	sg_rune_compact_field_step_t v12;
	sg_host_hook_phase_t target_hook_phase;
	uint32_t reserved;
} sg_tactic_transition_t;

/* A cell alone is insufficient: stance and hook chronology select different
 * v12 successor states. */
typedef struct sg_tactic_successor_state_s
{
	sg_rune_compact_cell_index_t cell;
	sg_rune_compact_field_stance_t stance;
	sg_host_hook_phase_t hook_phase;
} sg_tactic_successor_state_t;

typedef struct sg_tactic_live_state_s
{
	uint64_t rune_identity;
	uint64_t pose_revision;
	uint64_t now_ms;
	sg_rune_compact_cell_index_t cell;
	sg_tactic_phase_t phase;
	sg_rune_stance_validity_t stance;
	uint8_t supported;
	uint8_t waterlevel;
	uint16_t reserved;
	sg_host_hook_phase_t hook_phase;
	float origin[3];
	float velocity[3];
} sg_tactic_live_state_t;

typedef struct sg_tactic_gradient_s
{
	uint64_t field_generation;
	uint64_t pose_revision;
	uint64_t sampled_at_ms;
	sg_rune_compact_cell_index_t current_cell;
	sg_tactic_transition_t transition;
	float position_derivative[3];
	float velocity_derivative[3];
	float time_derivative;
	float descent_direction[3];
} sg_tactic_gradient_t;

/* The frame owner mints this capability. Its authority validates that it names
 * this subject life, model/RUNE topology, exact field lease, frame/time, and
 * localized state before any probe runs. */
typedef struct sg_tactic_frame_capability_s
{
	sg_localization_subject_t subject;
	uint64_t model_identity;
	uint64_t rune_identity;
	uint64_t topology_revision;
	sg_rune_compact_field_handle_t field_handle;
	uint64_t frame_sequence;
	uint64_t observed_at_ms;
	sg_tactic_successor_state_t localized;
	uint64_t owner_epoch;
	uint64_t token;
} sg_tactic_frame_capability_t;

typedef struct sg_tactic_modifier_s
{
	sg_tactic_modifier_kind_t kind;
	uint32_t source_id;
	int64_t cost_delta_units;
	uint32_t capability_mask;
	sg_tactic_modifier_target_kind_t target_kind;
	sg_tactic_successor_state_t target;
	uint64_t expires_at_ms;
	uint8_t active;
	uint8_t reserved[7];
} sg_tactic_modifier_t;

/* Metadata does not change ordinary movement admissibility. It matters only
 * to a mechanism handoff or an authenticated temporary block at its portal. */
typedef struct sg_tactic_mechanism_request_s
{
	uint64_t mechanism_revision;
	uint64_t handoff_id;
	uint32_t mechanism_id;
	uint32_t controller_id;
	sg_rune_compact_portal_index_t portal;
	sg_rune_compact_cell_index_t entry_cell;
	sg_rune_compact_cell_index_t exit_cell;
	uint32_t trigger_id;
	uint32_t dwell_ms;
	uint8_t requires_live_trace;
	uint8_t reserved[7];
} sg_tactic_mechanism_request_t;

typedef struct sg_tactic_temporary_block_evidence_s
{
	uint64_t observed_at_ms;
	uint64_t mechanism_revision;
	uint64_t handoff_id;
	sg_rune_compact_portal_index_t portal;
	sg_rune_compact_cell_index_t entry_cell;
	sg_rune_compact_cell_index_t exit_cell;
} sg_tactic_temporary_block_evidence_t;

struct sg_tactic_request_s;
struct sg_tactic_capability_descriptor_s;
struct sg_tactic_candidate_s;

typedef int (*sg_tactic_validate_frame_fn)(const void *context,
	const struct sg_tactic_request_s *request,
	const sg_tactic_frame_capability_t *frame);
typedef int (*sg_tactic_validate_modifier_fn)(const void *context,
	const struct sg_tactic_request_s *request,
	const sg_tactic_modifier_t *modifier);
typedef int (*sg_tactic_validate_mechanism_fn)(const void *context,
	const struct sg_tactic_request_s *request,
	const sg_tactic_mechanism_request_t *mechanism);
typedef int (*sg_tactic_validate_temporary_block_fn)(const void *context,
	const struct sg_tactic_request_s *request,
	const sg_tactic_temporary_block_evidence_t *evidence);
typedef int (*sg_tactic_validate_probe_fn)(const void *context,
	const struct sg_tactic_request_s *request,
	const struct sg_tactic_capability_descriptor_s *descriptor,
	const struct sg_tactic_candidate_s *candidate,
	sg_rune_compact_field_cost_t nominal_cost);

typedef struct sg_tactic_authority_s
{
	const void *context;
	sg_tactic_validate_frame_fn validate_frame;
	sg_tactic_validate_modifier_fn validate_modifier;
	sg_tactic_validate_mechanism_fn validate_mechanism;
	sg_tactic_validate_temporary_block_fn validate_temporary_block;
	sg_tactic_validate_probe_fn validate_probe;
} sg_tactic_authority_t;

typedef struct sg_tactic_request_s
{
	sg_tactic_live_state_t live;
	sg_tactic_gradient_t gradient;
	const sg_tactic_frame_capability_t *frame;
	uint32_t legal_capability_mask;
	const sg_tactic_modifier_t *modifiers;
	size_t modifier_count;
	const sg_tactic_mechanism_request_t *mechanism;
	const sg_tactic_temporary_block_evidence_t *temporary_block;
	const sg_tactic_authority_t *authority;
} sg_tactic_request_t;

typedef struct sg_tactic_candidate_s
{
	sg_tactic_capability_t capability;
	sg_tactic_successor_state_t successor;
	sg_tactic_phase_t predicted_phase;
	float displacement[3];
	float velocity_delta[3];
	float duration_seconds;
	sg_rune_compact_field_cost_t local_cost;
	uint8_t exact_live_validation_required;
	uint8_t reserved[3];
} sg_tactic_candidate_t;

typedef int (*sg_tactic_probe_fn)(void *context,
	const sg_tactic_request_t *request,
	sg_tactic_candidate_t *candidate_out);

typedef struct sg_tactic_capability_descriptor_s
{
	sg_tactic_capability_t capability;
	uint32_t phase_mask;
	uint32_t flags;
	uint16_t priority;
	uint16_t reserved;
	sg_tactic_probe_fn probe;
	void *context;
} sg_tactic_capability_descriptor_t;

typedef struct sg_tactic_result_s
{
	sg_tactic_result_status_t status;
	sg_tactic_failure_reason_t failure;
	sg_tactic_capability_t capability;
	sg_tactic_successor_state_t successor;
	sg_tactic_phase_t target_phase;
	sg_rune_compact_field_cost_t nominal_cost;
	sg_tactic_mechanism_request_t mechanism_handoff;
	float progress;
	uint8_t exact_live_validation_required;
	uint8_t mechanism_handoff_valid;
	uint8_t reserved[2];
} sg_tactic_result_t;

static inline int SG_TacticFloatValid(float value)
{
	return isfinite(value);
}

static inline int SG_TacticCellValid(sg_rune_compact_cell_index_t cell)
{
	return cell.value != SG_RUNE_COMPACT_INDEX_NONE;
}

static inline int SG_TacticPortalValid(sg_rune_compact_portal_index_t portal)
{
	return portal.value != SG_RUNE_COMPACT_INDEX_NONE;
}

static inline int SG_TacticStanceValid(sg_rune_stance_validity_t stance)
{
	return stance == SG_RUNE_STANCE_VALID_STANDING ||
		stance == SG_RUNE_STANCE_VALID_CROUCHING;
}

static inline int SG_TacticFieldStanceValid(
	sg_rune_compact_field_stance_t stance)
{
	return stance >= SG_RUNE_COMPACT_FIELD_STANDING &&
		stance < SG_RUNE_COMPACT_FIELD_STANCE_COUNT;
}

static inline sg_rune_compact_field_stance_t SG_TacticFieldStanceFromLive(
	sg_rune_stance_validity_t stance)
{
	return stance == SG_RUNE_STANCE_VALID_CROUCHING ?
		SG_RUNE_COMPACT_FIELD_CROUCHING : SG_RUNE_COMPACT_FIELD_STANDING;
}

static inline int SG_TacticHookPhaseValid(sg_host_hook_phase_t phase)
{
	return phase >= SG_HOST_HOOK_IDLE && phase <= SG_HOST_HOOK_COAST;
}

static inline int SG_TacticPhaseValid(sg_tactic_phase_t phase)
{
	return phase >= SG_TACTIC_PHASE_GROUND && phase < SG_TACTIC_PHASE_COUNT;
}

static inline int SG_TacticCapabilityValid(sg_tactic_capability_t capability)
{
	return capability >= SG_TACTIC_CAPABILITY_WALK &&
		capability < SG_TACTIC_CAPABILITY_COUNT;
}

static inline int SG_TacticSuccessorStateValid(
	const sg_tactic_successor_state_t *state)
{
	return state != NULL && SG_TacticCellValid(state->cell) &&
		SG_TacticFieldStanceValid(state->stance) &&
		SG_TacticHookPhaseValid(state->hook_phase);
}

static inline int SG_TacticSuccessorStateEqual(
	const sg_tactic_successor_state_t *left,
	const sg_tactic_successor_state_t *right)
{
	return left != NULL && right != NULL &&
		left->cell.value == right->cell.value && left->stance == right->stance &&
		left->hook_phase == right->hook_phase;
}

static inline int SG_TacticTransitionSuccessor(
	sg_rune_compact_cell_index_t current_cell,
	const sg_tactic_transition_t *transition,
	sg_tactic_successor_state_t *successor_out)
{
	sg_tactic_successor_state_t successor;

	if (!SG_TacticCellValid(current_cell) || transition == NULL ||
		successor_out == NULL ||
		!SG_TacticFieldStanceValid(transition->v12.target_stance) ||
		!SG_TacticHookPhaseValid(transition->target_hook_phase))
		return 0;
	successor.stance = transition->v12.target_stance;
	successor.hook_phase = transition->target_hook_phase;
	switch (transition->v12.kind)
	{
	case SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL:
		successor.cell = transition->v12.value.portal.next_cell;
		break;
	case SG_RUNE_COMPACT_FIELD_TRANSITION_DIRECT:
		successor.cell = transition->v12.value.direct.next_cell;
		break;
	case SG_RUNE_COMPACT_FIELD_TRANSITION_STANCE:
		successor.cell = current_cell;
		break;
	case SG_RUNE_COMPACT_FIELD_TRANSITION_KIND_COUNT:
	default:
		return 0;
	}
	if (!SG_TacticSuccessorStateValid(&successor))
		return 0;
	*successor_out = successor;
	return 1;
}

static inline int SG_TacticTransitionValid(
	sg_rune_compact_cell_index_t current_cell,
	const sg_tactic_transition_t *transition)
{
	sg_tactic_successor_state_t successor;

	if (transition == NULL || !SG_TacticCellValid(current_cell) ||
		transition->v12.kind >= SG_RUNE_COMPACT_FIELD_TRANSITION_KIND_COUNT ||
		transition->v12.cost_to_go.units ==
		SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE ||
		transition->v12.next_cost_to_go.units ==
		SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE ||
		transition->v12.next_cost_to_go.units >= transition->v12.cost_to_go.units ||
		!SG_TacticFieldStanceValid(transition->v12.target_stance) ||
		!SG_TacticHookPhaseValid(transition->target_hook_phase))
		return 0;
	switch (transition->v12.kind)
	{
	case SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL:
		if (!SG_TacticPortalValid(transition->v12.value.portal.next_portal) ||
			!SG_TacticCellValid(transition->v12.value.portal.next_cell) ||
			!SG_TacticFloatValid(transition->v12.value.portal.local_cost) ||
			transition->v12.value.portal.local_cost < 0.0f)
			return 0;
		break;
	case SG_RUNE_COMPACT_FIELD_TRANSITION_DIRECT:
		if (!SG_TacticCellValid(transition->v12.value.direct.next_cell) ||
			!SG_TacticFloatValid(transition->v12.value.direct.local_cost) ||
			transition->v12.value.direct.local_cost < 0.0f)
			return 0;
		break;
	case SG_RUNE_COMPACT_FIELD_TRANSITION_STANCE:
		break;
	case SG_RUNE_COMPACT_FIELD_TRANSITION_KIND_COUNT:
	default:
		return 0;
	}
	return SG_TacticTransitionSuccessor(current_cell, transition, &successor);
}

/* Compose a live, owner-authenticated candidate cost with the canonical
 * successor field cost.  The field's own strict successor descent is checked
 * by SG_TacticTransitionValid; this additionally rejects a candidate that
 * merely equals the canonical source cost. */
static inline int SG_TacticLiveDescentValid(
	const sg_tactic_transition_t *transition,
	sg_rune_compact_field_cost_t local_cost,
	sg_rune_compact_field_cost_t *nominal_out)
{
	const uint64_t source = transition != NULL ?
		transition->v12.cost_to_go.units :
		SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE;
	const uint64_t successor = transition != NULL ?
		transition->v12.next_cost_to_go.units :
		SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE;

	if (transition == NULL || nominal_out == NULL ||
		source == SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE ||
		successor == SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE ||
		local_cost.units == SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE ||
		successor >= source ||
		local_cost.units >= SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE - successor)
		return 0;
	nominal_out->units = successor + local_cost.units;
	return nominal_out->units < source;
}

static inline int SG_TacticLiveStateValid(const sg_tactic_live_state_t *live)
{
	uint32_t axis;

	if (!live || live->rune_identity == 0U || live->pose_revision == 0U ||
		live->now_ms == 0U || !SG_TacticCellValid(live->cell) ||
		!SG_TacticPhaseValid(live->phase) || !SG_TacticStanceValid(live->stance) ||
		live->supported > 1U || live->waterlevel > 3U ||
		!SG_TacticHookPhaseValid(live->hook_phase))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!SG_TacticFloatValid(live->origin[axis]) ||
			!SG_TacticFloatValid(live->velocity[axis]))
			return 0;
	return 1;
}

static inline int SG_TacticGradientValid(const sg_tactic_gradient_t *gradient,
	const sg_tactic_live_state_t *live)
{
	uint32_t axis;

	if (!gradient || !SG_TacticLiveStateValid(live) ||
		gradient->field_generation == 0U ||
		gradient->pose_revision != live->pose_revision ||
		gradient->sampled_at_ms != live->now_ms ||
		gradient->current_cell.value != live->cell.value ||
		!SG_TacticTransitionValid(gradient->current_cell, &gradient->transition) ||
		!SG_TacticFloatValid(gradient->time_derivative))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!SG_TacticFloatValid(gradient->position_derivative[axis]) ||
			!SG_TacticFloatValid(gradient->velocity_derivative[axis]) ||
			!SG_TacticFloatValid(gradient->descent_direction[axis]))
			return 0;
	return 1;
}

static inline int SG_TacticFrameCapabilityValid(
	const sg_tactic_frame_capability_t *frame)
{
	const sg_rune_compact_field_handle_t *handle = frame != NULL ?
		&frame->field_handle : NULL;

	return frame != NULL && frame->subject.reserved == 0U &&
		frame->subject.client_id != UINT32_MAX &&
		frame->subject.spawn_generation != 0U && frame->model_identity != 0U &&
		frame->rune_identity != 0U && frame->topology_revision != 0U &&
		handle->service_identity != 0U && handle->service_generation != 0U &&
		handle->rune_identity == frame->rune_identity &&
		handle->topology_revision == frame->topology_revision &&
		handle->target_id != 0U && handle->target_generation != 0U &&
		handle->field_generation != 0U && frame->frame_sequence != 0U &&
		frame->observed_at_ms != 0U &&
		SG_TacticSuccessorStateValid(&frame->localized) &&
		frame->owner_epoch != 0U && frame->token != 0U;
}

static inline int SG_TacticMechanismRequestValid(
	const sg_tactic_mechanism_request_t *mechanism)
{
	return mechanism != NULL && mechanism->mechanism_revision != 0U &&
		mechanism->handoff_id != 0U && mechanism->mechanism_id != 0U &&
		mechanism->controller_id != 0U && SG_TacticPortalValid(mechanism->portal) &&
		SG_TacticCellValid(mechanism->entry_cell) &&
		SG_TacticCellValid(mechanism->exit_cell) && mechanism->trigger_id != 0U &&
		mechanism->requires_live_trace <= 1U;
}

static inline int SG_TacticTemporaryBlockEvidenceValid(
	const sg_tactic_temporary_block_evidence_t *evidence)
{
	return evidence != NULL && evidence->observed_at_ms != 0U &&
		evidence->mechanism_revision != 0U && evidence->handoff_id != 0U &&
		SG_TacticPortalValid(evidence->portal) &&
		SG_TacticCellValid(evidence->entry_cell) &&
		SG_TacticCellValid(evidence->exit_cell);
}

static inline int SG_TacticModifierValid(const sg_tactic_modifier_t *modifier)
{
	return modifier != NULL && modifier->kind >= SG_TACTIC_MODIFIER_THREAT &&
		modifier->kind < SG_TACTIC_MODIFIER_KIND_COUNT && modifier->source_id != 0U &&
		modifier->cost_delta_units >= -SG_TACTIC_MODIFIER_MAX_DEFORMATION_UNITS &&
		modifier->cost_delta_units <= SG_TACTIC_MODIFIER_MAX_DEFORMATION_UNITS &&
		modifier->capability_mask != 0U &&
		(modifier->capability_mask & ~SG_TACTIC_CAPABILITY_MASK) == 0U &&
		modifier->target_kind >= SG_TACTIC_MODIFIER_TARGET_EXACT_SUCCESSOR &&
		modifier->target_kind < SG_TACTIC_MODIFIER_TARGET_KIND_COUNT &&
		SG_TacticSuccessorStateValid(&modifier->target) &&
		modifier->expires_at_ms != 0U && modifier->active <= 1U;
}

static inline int SG_TacticCandidateValid(const sg_tactic_candidate_t *candidate)
{
	uint32_t axis;

	if (!candidate || !SG_TacticCapabilityValid(candidate->capability) ||
		!SG_TacticSuccessorStateValid(&candidate->successor) ||
		!SG_TacticPhaseValid(candidate->predicted_phase) ||
		candidate->duration_seconds < 0.0f ||
		!SG_TacticFloatValid(candidate->duration_seconds) ||
		candidate->local_cost.units == SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE ||
		candidate->exact_live_validation_required > 1U)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!SG_TacticFloatValid(candidate->displacement[axis]) ||
			!SG_TacticFloatValid(candidate->velocity_delta[axis]))
			return 0;
	return 1;
}

static inline int SG_TacticDescriptorValid(
	const sg_tactic_capability_descriptor_t *descriptor)
{
	const uint32_t flag_mask = SG_TACTIC_CAPABILITY_REQUIRES_SUPPORT |
		SG_TACTIC_CAPABILITY_REQUIRES_WATER |
		SG_TACTIC_CAPABILITY_REQUIRES_AIR |
		SG_TACTIC_CAPABILITY_REQUIRES_LIVE_TRACE |
		SG_TACTIC_CAPABILITY_MECHANISM_BOUNDARY;

	return descriptor != NULL && SG_TacticCapabilityValid(descriptor->capability) &&
		descriptor->phase_mask != 0U &&
		(descriptor->phase_mask & ~SG_TACTIC_PHASE_MASK) == 0U &&
		(descriptor->flags & ~flag_mask) == 0U && descriptor->probe != NULL;
}

static inline int SG_TacticModifierAppliesToCandidate(
	const sg_tactic_request_t *request, const sg_tactic_modifier_t *modifier,
	const sg_tactic_candidate_t *candidate)
{
	return request != NULL && SG_TacticModifierValid(modifier) &&
		SG_TacticCandidateValid(candidate) && modifier->active == 1U &&
		modifier->expires_at_ms > request->live.now_ms &&
		(modifier->capability_mask &
		 SG_TACTIC_CAPABILITY_BIT(candidate->capability)) != 0U &&
		(modifier->target_kind == SG_TACTIC_MODIFIER_TARGET_ANY_SUCCESSOR ||
		 SG_TacticSuccessorStateEqual(&modifier->target, &candidate->successor));
}

static inline int SG_TacticRequestValid(const sg_tactic_request_t *request)
{
	size_t index;

	if (request == NULL || !SG_TacticLiveStateValid(&request->live) ||
		!SG_TacticGradientValid(&request->gradient, &request->live) ||
		!SG_TacticFrameCapabilityValid(request->frame) ||
		request->frame->rune_identity != request->live.rune_identity ||
		request->frame->field_handle.field_generation !=
			request->gradient.field_generation ||
		request->frame->frame_sequence != request->live.pose_revision ||
		request->frame->frame_sequence != request->gradient.pose_revision ||
		request->frame->observed_at_ms != request->live.now_ms ||
		request->frame->observed_at_ms != request->gradient.sampled_at_ms ||
		request->frame->localized.cell.value != request->live.cell.value ||
		request->frame->localized.stance !=
			SG_TacticFieldStanceFromLive(request->live.stance) ||
		request->frame->localized.hook_phase != request->live.hook_phase ||
		request->legal_capability_mask == 0U ||
		(request->legal_capability_mask & ~SG_TACTIC_CAPABILITY_MASK) != 0U ||
		(request->modifier_count != 0U && request->modifiers == NULL) ||
		request->authority == NULL || request->authority->validate_frame == NULL ||
		request->authority->validate_probe == NULL ||
		!request->authority->validate_frame(request->authority->context, request,
			request->frame) ||
		(request->mechanism != NULL &&
			(!SG_TacticMechanismRequestValid(request->mechanism) ||
			 request->authority->validate_mechanism == NULL ||
			 !request->authority->validate_mechanism(request->authority->context,
				request, request->mechanism))) ||
		(request->temporary_block != NULL &&
			(request->mechanism == NULL ||
			 !SG_TacticTemporaryBlockEvidenceValid(request->temporary_block) ||
			 request->authority->validate_temporary_block == NULL ||
			 request->temporary_block->observed_at_ms != request->live.now_ms ||
			 request->temporary_block->mechanism_revision !=
				request->mechanism->mechanism_revision ||
			 request->temporary_block->handoff_id != request->mechanism->handoff_id ||
			 request->temporary_block->portal.value != request->mechanism->portal.value ||
			 request->temporary_block->entry_cell.value !=
				request->mechanism->entry_cell.value ||
			 request->temporary_block->exit_cell.value !=
				request->mechanism->exit_cell.value ||
			 !request->authority->validate_temporary_block(
				request->authority->context, request, request->temporary_block))))
		return 0;
	for (index = 0U; index < request->modifier_count; index++)
	{
		size_t other;

		if (!SG_TacticModifierValid(&request->modifiers[index]) ||
			request->authority->validate_modifier == NULL ||
			!request->authority->validate_modifier(request->authority->context,
				request, &request->modifiers[index]) ||
			(request->modifiers[index].active != 0U &&
			 request->modifiers[index].expires_at_ms <= request->live.now_ms))
			return 0;
		for (other = index + 1U; other < request->modifier_count; other++)
			if (request->modifiers[index].kind == request->modifiers[other].kind &&
				request->modifiers[index].source_id ==
				request->modifiers[other].source_id)
				return 0;
	}
	return 1;
}

static inline int SG_TacticResultValid(const sg_tactic_result_t *result)
{
	if (result == NULL || result->status < SG_TACTIC_RESULT_PROGRESS ||
		result->status > SG_TACTIC_RESULT_FAILURE ||
		result->failure < SG_TACTIC_FAILURE_NONE ||
		result->failure > SG_TACTIC_FAILURE_TRACE_REQUIRED ||
		!SG_TacticCapabilityValid(result->capability) ||
		result->progress < 0.0f || result->progress > 1.0f ||
		!SG_TacticFloatValid(result->progress) ||
		result->exact_live_validation_required > 1U ||
		result->mechanism_handoff_valid > 1U)
		return 0;
	switch (result->status)
	{
	case SG_TACTIC_RESULT_PROGRESS:
		if (result->failure != SG_TACTIC_FAILURE_NONE ||
			!SG_TacticSuccessorStateValid(&result->successor) ||
			!SG_TacticPhaseValid(result->target_phase) ||
			result->nominal_cost.units == SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE ||
			result->progress <= 0.0f)
			return 0;
		break;
	case SG_TACTIC_RESULT_HOLD:
		if (result->failure != SG_TACTIC_FAILURE_NONE ||
			result->capability != SG_TACTIC_CAPABILITY_WAIT ||
			!SG_TacticSuccessorStateValid(&result->successor) ||
			!SG_TacticPhaseValid(result->target_phase) ||
			result->nominal_cost.units == SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE ||
			result->progress != 0.0f || result->mechanism_handoff_valid != 0U)
			return 0;
		break;
	case SG_TACTIC_RESULT_RETRY:
	case SG_TACTIC_RESULT_FAILURE:
		if (result->failure == SG_TACTIC_FAILURE_NONE ||
			result->successor.cell.value != SG_RUNE_COMPACT_INDEX_NONE ||
			result->successor.stance != SG_RUNE_COMPACT_FIELD_STANCE_COUNT ||
			result->successor.hook_phase !=
				(sg_host_hook_phase_t)(SG_HOST_HOOK_COAST + 1) ||
			result->target_phase != SG_TACTIC_PHASE_COUNT ||
			result->nominal_cost.units != 0U || result->progress != 0.0f ||
			result->exact_live_validation_required != 0U ||
			result->mechanism_handoff_valid != 0U)
			return 0;
		break;
	default:
		return 0;
	}
	if (result->mechanism_handoff_valid != 0U &&
		(result->capability != SG_TACTIC_CAPABILITY_MECHANISM ||
		 result->exact_live_validation_required != 1U ||
		 !SG_TacticMechanismRequestValid(&result->mechanism_handoff) ||
		 result->successor.cell.value != result->mechanism_handoff.exit_cell.value))
		return 0;
	return 1;
}

/* The controller enumerates legal, frame-authenticated live descriptors.
 * Strategy handles terminal field results before creating this request. */
int SG_TacticSelectCapability(const sg_tactic_request_t *request,
	const sg_tactic_capability_descriptor_t *descriptors,
	uint32_t descriptor_count, sg_tactic_result_t *out);

#endif /* SG_TACTIC_CONTRACT_H */
