/* Live tactical capability selection over a phase-space destination field. */
#ifndef SG_TACTIC_CONTRACT_H
#define SG_TACTIC_CONTRACT_H

#include <stddef.h>
#include <stdint.h>

#include "sg_destination_field.h"

#define SG_TACTIC_CAPABILITY_BIT(capability) (UINT32_C(1) << (capability))
#define SG_TACTIC_CAPABILITY_MASK \
	((UINT32_C(1) << SG_TACTIC_CAPABILITY_COUNT) - UINT32_C(1))

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

typedef enum sg_tactic_localization_status_e
{
	SG_TACTIC_LOCALIZATION_UNAVAILABLE = 0,
	SG_TACTIC_LOCALIZATION_EXACT,
	SG_TACTIC_LOCALIZATION_RECOVERED,
	SG_TACTIC_LOCALIZATION_OUT_OF_FIELD
} sg_tactic_localization_status_t;

typedef enum sg_tactic_modifier_kind_e
{
	SG_TACTIC_MODIFIER_THREAT = 0,
	SG_TACTIC_MODIFIER_COVER,
	SG_TACTIC_MODIFIER_WEAPON_OPPORTUNITY,
	SG_TACTIC_MODIFIER_OBSTRUCTION,
	SG_TACTIC_MODIFIER_TEAMMATE,
	SG_TACTIC_MODIFIER_KIND_COUNT
} sg_tactic_modifier_kind_t;

typedef enum sg_tactic_capability_flag_e
{
	SG_TACTIC_CAPABILITY_REQUIRES_SUPPORT = 1,
	SG_TACTIC_CAPABILITY_REQUIRES_WATER = 2,
	SG_TACTIC_CAPABILITY_REQUIRES_AIR = 4,
	SG_TACTIC_CAPABILITY_REQUIRES_LIVE_TRACE = 8,
	SG_TACTIC_CAPABILITY_MECHANISM_BOUNDARY = 16
} sg_tactic_capability_flag_t;

typedef struct sg_tactic_live_phase_s
{
	uint64_t rune_identity;
	uint64_t pose_revision;
	uint64_t now_ms;
	sg_phase_coordinate_t phase_coordinate;
	sg_tactic_phase_t phase;
	uint8_t supported;
	uint8_t waterlevel;
	uint8_t stance;
	uint8_t reserved;
	float origin[3];
	float velocity[3];
} sg_tactic_live_phase_t;

/* A gradient is usable only for the exact live pose, phase, and sample time.
 * A zero-cost terminal field sample has no RUNE edge provenance: strategy
 * completes that goal before constructing a tactical gradient. */
typedef struct sg_tactic_gradient_s
{
	uint64_t rune_identity;
	uint64_t field_generation;
	uint64_t pose_revision;
	uint64_t sampled_at_ms;
	sg_phase_coordinate_t phase_coordinate;
	sg_phase_coordinate_t next_phase_coordinate;
	sg_tactic_phase_t phase;
	uint32_t cost_ms;
	/* The adapter retains RUNE provenance while choosing a separate tactical
	 * capability_mask from exact live state. */
	sg_field_capability_family_mask_t field_capability_families;
	sg_rune_phase_transition_kind_t field_transition_kind;
	uint32_t capability_mask;
	float direction[3];
	float velocity_direction[3];
	uint8_t finite;
	uint8_t reserved[3];
} sg_tactic_gradient_t;

typedef struct sg_tactic_modifier_s
{
	sg_tactic_modifier_kind_t kind;
	uint32_t source_id;
	int32_t cost_delta_ms;
	uint64_t expires_at_ms;
	uint8_t active;
	uint8_t reserved[3];
} sg_tactic_modifier_t;

typedef struct sg_tactic_localization_s
{
	uint64_t rune_identity;
	uint64_t pose_revision;
	sg_phase_coordinate_t phase_coordinate;
	sg_tactic_phase_t phase;
	float distance_error;
	uint8_t deterministic;
	uint8_t reserved[3];
	sg_tactic_localization_status_t status;
} sg_tactic_localization_t;

typedef struct sg_tactic_mechanism_request_s
{
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t mechanism_revision;
	uint64_t handoff_id;
	uint32_t mechanism_id;
	uint32_t controller_id;
	uint32_t entry_cell_id;
	uint32_t exit_cell_id;
	uint32_t trigger_id;
	uint32_t dwell_ms;
	uint32_t recovery_timeout_ms;
	uint8_t authenticated;
	uint8_t requires_live_trace;
	uint8_t reserved[2];
} sg_tactic_mechanism_request_t;

typedef struct sg_tactic_request_s
{
	sg_tactic_live_phase_t live;
	sg_tactic_gradient_t gradient;
	uint32_t legal_capability_mask;
	const sg_tactic_modifier_t *modifiers;
	size_t modifier_count;
	const sg_tactic_mechanism_request_t *mechanism;
} sg_tactic_request_t;

typedef struct sg_tactic_capability_descriptor_s
{
	sg_tactic_capability_t capability;
	uint32_t phase_mask;
	uint32_t flags;
	uint32_t base_cost_ms;
	uint16_t priority;
	uint16_t reserved;
} sg_tactic_capability_descriptor_t;

typedef struct sg_tactic_result_s
{
	sg_tactic_result_status_t status;
	sg_tactic_failure_reason_t failure;
	sg_tactic_capability_t capability;
	sg_phase_coordinate_t target_phase;
	uint32_t expected_cost_ms;
	sg_tactic_mechanism_request_t mechanism_handoff;
	float progress;
	uint8_t exact_live_validation_required;
	uint8_t mechanism_handoff_valid;
	uint8_t reserved;
} sg_tactic_result_t;

static inline int SG_TacticPhaseValid(sg_tactic_phase_t phase)
{
	return phase >= SG_TACTIC_PHASE_GROUND && phase < SG_TACTIC_PHASE_COUNT;
}

static inline int SG_TacticCapabilityValid(sg_tactic_capability_t capability)
{
	return capability >= SG_TACTIC_CAPABILITY_WALK &&
	       capability < SG_TACTIC_CAPABILITY_COUNT;
}

static inline int SG_TacticLivePhaseValid(const sg_tactic_live_phase_t *live)
{
	uint32_t axis;

	if (!live || live->rune_identity == 0U || live->pose_revision == 0U ||
	    live->now_ms == 0U ||
	    live->phase_coordinate.phase_id == SG_DESTINATION_FIELD_NO_PHASE ||
	    live->phase_coordinate.cell_id == SG_DESTINATION_FIELD_NO_CELL ||
	    !SG_TacticPhaseValid(live->phase) || live->supported > 1U ||
	    live->waterlevel > 3U)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!SG_DestinationFloatValid(live->origin[axis]) ||
		    !SG_DestinationFloatValid(live->velocity[axis]))
			return 0;
	return 1;
}

static inline int SG_TacticGradientValid(
	const sg_tactic_gradient_t *gradient,
	const sg_tactic_live_phase_t *live)
{
	uint32_t axis;

	if (!gradient || !SG_TacticLivePhaseValid(live) ||
	    gradient->rune_identity != live->rune_identity ||
	    gradient->field_generation == 0U ||
	    gradient->pose_revision != live->pose_revision ||
	    gradient->sampled_at_ms != live->now_ms ||
	    gradient->phase != live->phase ||
	    gradient->phase_coordinate.phase_id !=
		live->phase_coordinate.phase_id ||
	    gradient->phase_coordinate.cell_id !=
		live->phase_coordinate.cell_id ||
	    gradient->next_phase_coordinate.phase_id ==
		SG_DESTINATION_FIELD_NO_PHASE ||
	    gradient->next_phase_coordinate.cell_id == SG_DESTINATION_FIELD_NO_CELL ||
	    (gradient->field_capability_families.bits &
	     ~SG_FIELD_CAPABILITY_FAMILY_MASK) != 0U ||
	    (gradient->field_capability_families.bits == 0U &&
	     gradient->field_transition_kind == SG_RUNE_PHASE_TRANSITION_NONE) ||
	    gradient->field_transition_kind < SG_RUNE_PHASE_TRANSITION_NONE ||
	    gradient->field_transition_kind >=
		SG_RUNE_PHASE_TRANSITION_KIND_COUNT ||
	    (gradient->capability_mask & ~SG_TACTIC_CAPABILITY_MASK) != 0U ||
	    gradient->capability_mask == 0U || gradient->finite != 1U ||
	    gradient->cost_ms >= SG_DESTINATION_FIELD_INF)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!SG_DestinationFloatValid(gradient->direction[axis]) ||
		    !SG_DestinationFloatValid(gradient->velocity_direction[axis]))
			return 0;
	return 1;
}

/* This checks field provenance only. Tactical action selection remains a
 * later live-state decision and is intentionally absent from this adapter. */
static inline int SG_TacticGradientMatchesFieldQuery(
	const sg_tactic_gradient_t *gradient,
	const sg_field_query_result_t *query)
{
	uint32_t axis;

	if (!gradient || !query || gradient->finite != query->sample.finite ||
	    gradient->phase_coordinate.phase_id != query->sample.phase.phase_id ||
	    gradient->phase_coordinate.cell_id != query->sample.phase.cell_id ||
	    gradient->next_phase_coordinate.phase_id !=
		query->sample.next_phase.phase_id ||
	    gradient->next_phase_coordinate.cell_id !=
		query->sample.next_phase.cell_id ||
	    gradient->cost_ms != query->sample.cost_ms ||
	    gradient->field_capability_families.bits !=
		query->sample.capability_families.bits ||
	    gradient->field_transition_kind !=
		query->sample.phase_transition_kind)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (gradient->direction[axis] != query->sample.direction[axis] ||
		    gradient->velocity_direction[axis] !=
			query->sample.velocity_direction[axis])
			return 0;
	return 1;
}

static inline int SG_TacticMechanismRequestValid(
	const sg_tactic_mechanism_request_t *mechanism)
{
	return mechanism && mechanism->rune_identity != 0U &&
	       mechanism->topology_revision != 0U &&
	       mechanism->mechanism_revision != 0U && mechanism->handoff_id != 0U &&
	       mechanism->mechanism_id != 0U && mechanism->controller_id != 0U &&
	       mechanism->entry_cell_id != SG_DESTINATION_FIELD_NO_CELL &&
	       mechanism->exit_cell_id != SG_DESTINATION_FIELD_NO_CELL &&
	       mechanism->trigger_id != 0U && mechanism->authenticated == 1U &&
	       mechanism->requires_live_trace <= 1U;
}

static inline int SG_TacticModifierValid(const sg_tactic_modifier_t *modifier)
{
	return modifier && modifier->kind >= SG_TACTIC_MODIFIER_THREAT &&
	       modifier->kind < SG_TACTIC_MODIFIER_KIND_COUNT &&
	       modifier->active <= 1U && modifier->source_id != 0U &&
	       modifier->expires_at_ms != 0U;
}

static inline int SG_TacticRequestValid(const sg_tactic_request_t *request)
{
	size_t index;

	if (!request || !SG_TacticLivePhaseValid(&request->live) ||
	    !SG_TacticGradientValid(&request->gradient, &request->live) ||
	    request->legal_capability_mask == 0U ||
	    (request->legal_capability_mask & ~SG_TACTIC_CAPABILITY_MASK) != 0U ||
	    (request->modifier_count != 0U && !request->modifiers) ||
	    (request->mechanism &&
	     (!SG_TacticMechanismRequestValid(request->mechanism) ||
	      request->mechanism->rune_identity != request->live.rune_identity)))
		return 0;
	for (index = 0U; index < request->modifier_count; index++)
	{
		size_t other;

		if (!SG_TacticModifierValid(&request->modifiers[index]))
			return 0;
		for (other = index + 1U;
		     other < request->modifier_count; other++)
			if (request->modifiers[index].kind ==
			    request->modifiers[other].kind &&
			    request->modifiers[index].source_id ==
			    request->modifiers[other].source_id)
				return 0;
	}
	return 1;
}

static inline int SG_TacticLocalizationValid(
	const sg_tactic_localization_t *localization)
{
	return localization && localization->rune_identity != 0U &&
	       localization->pose_revision != 0U &&
	       localization->phase_coordinate.phase_id !=
		SG_DESTINATION_FIELD_NO_PHASE &&
	       localization->phase_coordinate.cell_id != SG_DESTINATION_FIELD_NO_CELL &&
	       SG_TacticPhaseValid(localization->phase) &&
	       localization->status >= SG_TACTIC_LOCALIZATION_UNAVAILABLE &&
	       localization->status <= SG_TACTIC_LOCALIZATION_OUT_OF_FIELD &&
	       localization->deterministic <= 1U &&
	       localization->distance_error >= 0.0f &&
	       SG_DestinationFloatValid(localization->distance_error);
}

static inline int SG_TacticDescriptorValid(
	const sg_tactic_capability_descriptor_t *descriptor)
{
	const uint32_t flag_mask = SG_TACTIC_CAPABILITY_REQUIRES_SUPPORT |
		SG_TACTIC_CAPABILITY_REQUIRES_WATER |
		SG_TACTIC_CAPABILITY_REQUIRES_AIR |
		SG_TACTIC_CAPABILITY_REQUIRES_LIVE_TRACE |
		SG_TACTIC_CAPABILITY_MECHANISM_BOUNDARY;

	return descriptor && SG_TacticCapabilityValid(descriptor->capability) &&
	       descriptor->phase_mask != 0U &&
	       (descriptor->phase_mask &
	        ~((UINT32_C(1) << SG_TACTIC_PHASE_COUNT) - 1U)) == 0U &&
	       (descriptor->flags & ~flag_mask) == 0U;
}

static inline int SG_TacticResultValid(const sg_tactic_result_t *result)
{
	if (!result || result->status < SG_TACTIC_RESULT_PROGRESS ||
	    result->status > SG_TACTIC_RESULT_FAILURE ||
	    result->failure < SG_TACTIC_FAILURE_NONE ||
	    result->failure > SG_TACTIC_FAILURE_TRACE_REQUIRED ||
	    !SG_TacticCapabilityValid(result->capability) ||
	    result->progress < 0.0f || result->progress > 1.0f ||
	    !SG_DestinationFloatValid(result->progress) ||
	    result->exact_live_validation_required > 1U ||
	    result->mechanism_handoff_valid > 1U)
		return 0;
	switch (result->status)
	{
	case SG_TACTIC_RESULT_PROGRESS:
		if (result->failure != SG_TACTIC_FAILURE_NONE ||
		    result->target_phase.phase_id == SG_DESTINATION_FIELD_NO_PHASE ||
		    result->target_phase.cell_id == SG_DESTINATION_FIELD_NO_CELL ||
		    result->expected_cost_ms >= SG_DESTINATION_FIELD_INF ||
		    result->progress <= 0.0f)
			return 0;
		break;
	case SG_TACTIC_RESULT_HOLD:
		if (result->failure != SG_TACTIC_FAILURE_NONE ||
		    result->target_phase.phase_id == SG_DESTINATION_FIELD_NO_PHASE ||
		    result->target_phase.cell_id == SG_DESTINATION_FIELD_NO_CELL ||
		    result->expected_cost_ms >= SG_DESTINATION_FIELD_INF ||
		    result->progress != 0.0f || result->mechanism_handoff_valid != 0U)
			return 0;
		break;
	case SG_TACTIC_RESULT_RETRY:
	case SG_TACTIC_RESULT_FAILURE:
		if (result->failure == SG_TACTIC_FAILURE_NONE ||
		    result->expected_cost_ms != SG_DESTINATION_FIELD_INF ||
		    result->progress != 0.0f || result->mechanism_handoff_valid != 0U)
			return 0;
		break;
	default:
		return 0;
	}
	if (result->mechanism_handoff_valid != 0U &&
	    (result->capability != SG_TACTIC_CAPABILITY_MECHANISM ||
	     result->exact_live_validation_required != 1U ||
	     !SG_TacticMechanismRequestValid(&result->mechanism_handoff) ||
	     result->target_phase.cell_id !=
		result->mechanism_handoff.exit_cell_id))
		return 0;
	return 1;
}

/* Downstream tactical nodes own admission, costing, and selection. */
int SG_TacticSelectCapability(const sg_tactic_request_t *request,
	const sg_tactic_capability_descriptor_t *descriptors,
	uint32_t descriptor_count, sg_tactic_result_t *out);

#endif /* SG_TACTIC_CONTRACT_H */
