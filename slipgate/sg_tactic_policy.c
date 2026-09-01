#include "sg_tactic_policy.h"

#include <string.h>

typedef struct sg_tactic_wide_sum_s
{
	uint64_t high;
	uint64_t low;
} sg_tactic_wide_sum_t;

typedef struct sg_tactic_ranked_candidate_s
{
	sg_tactic_candidate_t candidate;
	uint64_t nominal_cost;
	uint64_t ranking_cost;
	uint32_t descriptor_index;
	uint16_t priority;
	uint8_t exact_live_validation_required;
	uint8_t mechanism_handoff_valid;
} sg_tactic_ranked_candidate_t;

static void TacticSetFailure(sg_tactic_result_t *out,
	sg_tactic_result_status_t status, sg_tactic_failure_reason_t failure)
{
	memset(out, 0, sizeof(*out));
	out->status = status;
	out->failure = failure;
	out->capability = SG_TACTIC_CAPABILITY_WALK;
	out->successor.cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	out->successor.stance = SG_RUNE_COMPACT_FIELD_STANCE_COUNT;
	out->successor.hook_phase =
		(sg_host_hook_phase_t)(SG_HOST_HOOK_COAST + 1);
	out->target_phase = SG_TACTIC_PHASE_COUNT;
}

static int TacticDescriptorPolicyValid(
	const sg_tactic_capability_descriptor_t *descriptor)
{
	const int mechanism_capability =
		descriptor != NULL &&
		descriptor->capability == SG_TACTIC_CAPABILITY_MECHANISM;
	const int mechanism_boundary = descriptor != NULL &&
		(descriptor->flags & SG_TACTIC_CAPABILITY_MECHANISM_BOUNDARY) != 0U;

	if (!SG_TacticDescriptorValid(descriptor))
		return 0;
	if (mechanism_capability != mechanism_boundary)
		return 0;
	if ((descriptor->flags & SG_TACTIC_CAPABILITY_REQUIRES_SUPPORT) != 0U &&
		(descriptor->flags & SG_TACTIC_CAPABILITY_REQUIRES_AIR) != 0U)
		return 0;
	return 1;
}

static int TacticTransitionSuccessor(const sg_tactic_request_t *request,
	sg_tactic_successor_state_t *successor_out)
{
	return request != NULL && SG_TacticTransitionSuccessor(
		request->gradient.current_cell, &request->gradient.transition,
		successor_out);
}

static int TacticMechanismMatchesTransition(const sg_tactic_request_t *request)
{
	sg_tactic_successor_state_t successor;

	return request != NULL && request->mechanism != NULL &&
		request->gradient.transition.v12.kind ==
			SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL &&
		TacticTransitionSuccessor(request, &successor) &&
		request->mechanism->portal.value ==
			request->gradient.transition.v12.value.portal.next_portal.value &&
		request->mechanism->entry_cell.value == request->live.cell.value &&
		request->mechanism->exit_cell.value == successor.cell.value;
}

static int TacticTemporaryBlockMatchesTransition(
	const sg_tactic_request_t *request)
{
	return request != NULL && request->temporary_block != NULL &&
		TacticMechanismMatchesTransition(request) &&
		request->temporary_block->portal.value ==
			request->gradient.transition.v12.value.portal.next_portal.value &&
		request->temporary_block->entry_cell.value == request->live.cell.value &&
		request->temporary_block->exit_cell.value ==
			request->mechanism->exit_cell.value;
}

static int TacticDescriptorEligible(const sg_tactic_request_t *request,
	const sg_tactic_capability_descriptor_t *descriptor)
{
	const uint32_t capability_bit =
		SG_TACTIC_CAPABILITY_BIT(descriptor->capability);
	const uint32_t phase_bit = SG_TACTIC_PHASE_BIT(request->live.phase);

	if ((request->legal_capability_mask & capability_bit) == 0U ||
		(descriptor->phase_mask & phase_bit) == 0U)
		return 0;
	if ((descriptor->flags & SG_TACTIC_CAPABILITY_REQUIRES_SUPPORT) != 0U &&
		request->live.supported == 0U)
		return 0;
	if ((descriptor->flags & SG_TACTIC_CAPABILITY_REQUIRES_WATER) != 0U &&
		request->live.waterlevel < 2U)
		return 0;
	if ((descriptor->flags & SG_TACTIC_CAPABILITY_REQUIRES_AIR) != 0U &&
		(request->live.supported != 0U || request->live.waterlevel >= 2U))
		return 0;
	if ((descriptor->flags & SG_TACTIC_CAPABILITY_MECHANISM_BOUNDARY) != 0U &&
		!TacticMechanismMatchesTransition(request))
		return 0;
	return 1;
}

static int TacticCandidateSuccessorMatchesTransition(
	const sg_tactic_request_t *request, const sg_tactic_candidate_t *candidate)
{
	sg_tactic_successor_state_t successor;

	return TacticTransitionSuccessor(request, &successor) &&
		SG_TacticSuccessorStateEqual(&candidate->successor, &successor);
}

static int TacticCandidateSuccessorMatchesLive(
	const sg_tactic_request_t *request, const sg_tactic_candidate_t *candidate)
{
	const sg_tactic_successor_state_t live = {
		.cell = request->live.cell,
		.stance = SG_TacticFieldStanceFromLive(request->live.stance),
		.hook_phase = request->live.hook_phase
	};

	return SG_TacticSuccessorStateEqual(&candidate->successor, &live);
}

static void TacticWideAdd(sg_tactic_wide_sum_t *sum, uint64_t value)
{
	const uint64_t previous = sum->low;

	sum->low += value;
	if (sum->low < previous)
		sum->high++;
}

static int TacticWideCompare(const sg_tactic_wide_sum_t *left,
	const sg_tactic_wide_sum_t *right)
{
	if (left->high != right->high)
		return left->high < right->high ? -1 : 1;
	if (left->low != right->low)
		return left->low < right->low ? -1 : 1;
	return 0;
}

static sg_tactic_wide_sum_t TacticWideSubtract(
	const sg_tactic_wide_sum_t *larger,
	const sg_tactic_wide_sum_t *smaller)
{
	sg_tactic_wide_sum_t difference;

	difference.low = larger->low - smaller->low;
	difference.high = larger->high - smaller->high -
		(larger->low < smaller->low ? UINT64_C(1) : UINT64_C(0));
	return difference;
}

/* A deformation only ranks candidates that have already passed every static
 * and authenticated dynamic check. Saturation makes it incapable of removing
 * an admissible candidate through arithmetic failure. */
static uint64_t TacticRankingCost(const sg_tactic_request_t *request,
	const sg_tactic_candidate_t *candidate, uint64_t nominal_cost)
{
	sg_tactic_wide_sum_t positive = { 0U, 0U };
	sg_tactic_wide_sum_t negative = { 0U, 0U };
	sg_tactic_wide_sum_t magnitude;
	int adjustment_sign;
	size_t index;

	for (index = 0U; index < request->modifier_count; index++)
	{
		const sg_tactic_modifier_t *modifier = &request->modifiers[index];

		if (!SG_TacticModifierAppliesToCandidate(request, modifier, candidate))
			continue;
		if (modifier->cost_delta_units >= 0)
			TacticWideAdd(&positive, (uint64_t)modifier->cost_delta_units);
		else
			TacticWideAdd(&negative, (uint64_t)-modifier->cost_delta_units);
	}
	adjustment_sign = TacticWideCompare(&positive, &negative);
	if (adjustment_sign == 0)
		return nominal_cost;
	magnitude = adjustment_sign > 0 ?
		TacticWideSubtract(&positive, &negative) :
		TacticWideSubtract(&negative, &positive);
	if (adjustment_sign > 0)
	{
		if (magnitude.high != 0U || magnitude.low >=
			SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE - nominal_cost)
			return SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE - UINT64_C(1);
		return nominal_cost + magnitude.low;
	}
	if (magnitude.high != 0U || magnitude.low >= nominal_cost)
		return 0U;
	return nominal_cost - magnitude.low;
}

static int TacticNominalCost(const sg_tactic_request_t *request,
	const sg_tactic_candidate_t *candidate,
	sg_rune_compact_field_cost_t *nominal_out)
{
	return request != NULL && candidate != NULL &&
		SG_TacticLiveDescentValid(&request->gradient.transition,
			candidate->local_cost, nominal_out);
}

static int TacticScoreCandidate(const sg_tactic_request_t *request,
	const sg_tactic_capability_descriptor_t *descriptor,
	uint32_t descriptor_index, const sg_tactic_candidate_t *candidate,
	sg_tactic_ranked_candidate_t *ranked_out)
{
	sg_rune_compact_field_cost_t nominal;

	if (!TacticCandidateSuccessorMatchesTransition(request, candidate) ||
		!TacticNominalCost(request, candidate, &nominal) ||
		!request->authority->validate_probe(request->authority->context, request,
			descriptor, candidate, nominal))
		return 0;
	*ranked_out = (sg_tactic_ranked_candidate_t){
		.candidate = *candidate,
		.nominal_cost = nominal.units,
		.ranking_cost = TacticRankingCost(request, candidate, nominal.units),
		.descriptor_index = descriptor_index,
		.priority = descriptor->priority,
		.exact_live_validation_required =
			(candidate->exact_live_validation_required != 0U ||
			 (descriptor->flags &
			  SG_TACTIC_CAPABILITY_REQUIRES_LIVE_TRACE) != 0U ||
			 ((descriptor->flags &
			   SG_TACTIC_CAPABILITY_MECHANISM_BOUNDARY) != 0U &&
			  request->mechanism->requires_live_trace != 0U)) ? 1U : 0U,
		.mechanism_handoff_valid =
			(descriptor->flags &
			 SG_TACTIC_CAPABILITY_MECHANISM_BOUNDARY) != 0U ? 1U : 0U
	};
	return 1;
}

static int TacticCandidateBefore(
	const sg_tactic_ranked_candidate_t *candidate,
	const sg_tactic_ranked_candidate_t *incumbent)
{
	if (candidate->ranking_cost != incumbent->ranking_cost)
		return candidate->ranking_cost < incumbent->ranking_cost;
	if (candidate->nominal_cost != incumbent->nominal_cost)
		return candidate->nominal_cost < incumbent->nominal_cost;
	if (candidate->priority != incumbent->priority)
		return candidate->priority > incumbent->priority;
	if (candidate->candidate.capability != incumbent->candidate.capability)
		return candidate->candidate.capability < incumbent->candidate.capability;
	if (candidate->candidate.predicted_phase !=
		incumbent->candidate.predicted_phase)
		return candidate->candidate.predicted_phase <
			incumbent->candidate.predicted_phase;
	return candidate->descriptor_index < incumbent->descriptor_index;
}

static void TacticSetSelection(const sg_tactic_request_t *request,
	const sg_tactic_ranked_candidate_t *winner, sg_tactic_result_t *out)
{
	const uint64_t current = request->gradient.transition.v12.cost_to_go.units;
	const uint64_t improvement = current - winner->nominal_cost;

	memset(out, 0, sizeof(*out));
	out->status = SG_TACTIC_RESULT_PROGRESS;
	out->capability = winner->candidate.capability;
	out->successor = winner->candidate.successor;
	out->target_phase = winner->candidate.predicted_phase;
	out->nominal_cost.units = winner->nominal_cost;
	out->progress = (float)((double)improvement / (double)current);
	out->exact_live_validation_required = winner->exact_live_validation_required;
	if (winner->mechanism_handoff_valid != 0U)
	{
		out->mechanism_handoff = *request->mechanism;
		out->mechanism_handoff_valid = 1U;
		out->exact_live_validation_required = 1U;
	}
}

static int TacticWaitCandidate(const sg_tactic_request_t *request,
	const sg_tactic_capability_descriptor_t *descriptor,
	uint32_t descriptor_index, const sg_tactic_candidate_t *candidate,
	sg_tactic_ranked_candidate_t *ranked_out)
{
	const sg_rune_compact_field_cost_t nominal =
		request->gradient.transition.v12.cost_to_go;

	if (candidate->capability != SG_TACTIC_CAPABILITY_WAIT ||
		candidate->local_cost.units != 0U ||
		!TacticTemporaryBlockMatchesTransition(request) ||
		!TacticCandidateSuccessorMatchesLive(request, candidate) ||
		!request->authority->validate_probe(request->authority->context, request,
			descriptor, candidate, nominal))
		return 0;
	*ranked_out = (sg_tactic_ranked_candidate_t){
		.candidate = *candidate,
		.nominal_cost = nominal.units,
		.ranking_cost = nominal.units,
		.descriptor_index = descriptor_index,
		.priority = descriptor->priority,
		.exact_live_validation_required =
			(candidate->exact_live_validation_required != 0U ||
			 (descriptor->flags &
			  SG_TACTIC_CAPABILITY_REQUIRES_LIVE_TRACE) != 0U ||
			 request->mechanism->requires_live_trace != 0U) ? 1U : 0U
	};
	return 1;
}

static void TacticSetHold(const sg_tactic_ranked_candidate_t *winner,
	sg_tactic_result_t *out)
{
	memset(out, 0, sizeof(*out));
	out->status = SG_TACTIC_RESULT_HOLD;
	out->capability = winner->candidate.capability;
	out->successor = winner->candidate.successor;
	out->target_phase = winner->candidate.predicted_phase;
	out->nominal_cost.units = winner->nominal_cost;
	out->exact_live_validation_required = winner->exact_live_validation_required;
}

int SG_TacticSelectCapability(const sg_tactic_request_t *request,
	const sg_tactic_capability_descriptor_t *descriptors,
	uint32_t descriptor_count, sg_tactic_result_t *out)
{
	sg_tactic_ranked_candidate_t winner;
	sg_tactic_ranked_candidate_t hold;
	int have_winner = 0;
	int have_hold = 0;
	int catalog_failure = 0;
	int probe_failure = 0;
	uint32_t index;

	if (out == NULL)
		return 0;
	TacticSetFailure(out, SG_TACTIC_RESULT_FAILURE,
		SG_TACTIC_FAILURE_LIVE_STATE);
	if (request == NULL || !SG_TacticLiveStateValid(&request->live))
		return 0;
	if (!SG_TacticGradientValid(&request->gradient, &request->live))
	{
		TacticSetFailure(out, SG_TACTIC_RESULT_RETRY,
			SG_TACTIC_FAILURE_NO_GRADIENT);
		return 0;
	}
	if (!SG_TacticRequestValid(request) ||
		(descriptor_count != 0U && descriptors == NULL))
		return 0;
	for (index = 0U; index < descriptor_count; index++)
	{
		const sg_tactic_capability_descriptor_t *descriptor = &descriptors[index];
		sg_tactic_candidate_t candidate;
		sg_tactic_ranked_candidate_t ranked;

		if (!TacticDescriptorPolicyValid(descriptor))
		{
			catalog_failure = 1;
			continue;
		}
		if (!TacticDescriptorEligible(request, descriptor))
			continue;
		memset(&candidate, 0, sizeof(candidate));
		if (!descriptor->probe(descriptor->context, request, &candidate))
			continue;
		if (!SG_TacticCandidateValid(&candidate) ||
			candidate.capability != descriptor->capability)
		{
			probe_failure = 1;
			continue;
		}
		if (candidate.capability == SG_TACTIC_CAPABILITY_WAIT)
		{
			if (!TacticWaitCandidate(request, descriptor, index, &candidate,
				&ranked))
				probe_failure = 1;
			else if (!have_hold || TacticCandidateBefore(&ranked, &hold))
			{
				hold = ranked;
				have_hold = 1;
			}
			continue;
		}
		if (!TacticScoreCandidate(request, descriptor, index, &candidate,
			&ranked))
		{
			probe_failure = 1;
			continue;
		}
		if (!have_winner || TacticCandidateBefore(&ranked, &winner))
		{
			winner = ranked;
			have_winner = 1;
		}
	}
	if (catalog_failure || probe_failure)
		return 0;
	if (have_winner)
		TacticSetSelection(request, &winner, out);
	else if (have_hold)
		TacticSetHold(&hold, out);
	else
	{
		TacticSetFailure(out, SG_TACTIC_RESULT_RETRY,
			SG_TACTIC_FAILURE_NO_LEGAL_CAPABILITY);
		return 0;
	}
	if (!SG_TacticResultValid(out))
	{
		TacticSetFailure(out, SG_TACTIC_RESULT_FAILURE,
			SG_TACTIC_FAILURE_LIVE_STATE);
		return 0;
	}
	return 1;
}
