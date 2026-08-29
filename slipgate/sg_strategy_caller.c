#include "sg_strategy_caller.h"

#include <limits.h>
#include <string.h>

#define SG_STRATEGY_CALLER_GOAL_ID UINT32_C(1)
#define SG_STRATEGY_CALLER_TARGET_ID UINT32_C(1)

static int CallerDestinationEqual(const sg_destination_ref_t *left,
	const sg_destination_ref_t *right)
{
	if (!left || !right || left->kind != right->kind)
		return 0;
	switch (left->kind)
	{
	case SG_DESTINATION_FLAG:
		return left->value.flag.team == right->value.flag.team &&
			left->value.flag.location == right->value.flag.location;
	case SG_DESTINATION_ITEM:
	case SG_DESTINATION_WEAPON:
	case SG_DESTINATION_ARMOR:
	case SG_DESTINATION_POWERUP:
		return left->value.item.item_id == right->value.item.item_id;
	case SG_DESTINATION_CARRIER:
	case SG_DESTINATION_ESCORT:
	case SG_DESTINATION_INTERCEPT:
		return left->value.carrier.client_id ==
				right->value.carrier.client_id &&
			left->value.carrier.team == right->value.carrier.team &&
			left->value.carrier.selector == right->value.carrier.selector;
	case SG_DESTINATION_DEFENSIVE_POST:
		return left->value.post.region_id == right->value.post.region_id;
	case SG_DESTINATION_LEARNED_POINT:
	case SG_DESTINATION_WAYPOINT:
		return left->value.point.point_id == right->value.point.point_id;
	case SG_DESTINATION_KIND_COUNT:
	default:
		return 0;
	}
}

static int CallerAuthorityValid(const sg_strategy_proposal_t *proposal)
{
	if (!proposal)
		return 0;
	switch (proposal->authority_rank)
	{
	case SG_STRATEGY_AUTHORITY_AUTONOMOUS:
		return proposal->principal_kind ==
			SG_STRATEGY_PRINCIPAL_AUTONOMOUS;
	case SG_STRATEGY_AUTHORITY_TEAM:
		return proposal->principal_kind == SG_STRATEGY_PRINCIPAL_TEAM;
	case SG_STRATEGY_AUTHORITY_HUMAN:
		return proposal->principal_kind == SG_STRATEGY_PRINCIPAL_HUMAN;
	case SG_STRATEGY_AUTHORITY_EMERGENCY:
		return proposal->principal_kind == SG_STRATEGY_PRINCIPAL_EMERGENCY;
	default:
		return 0;
	}
}

static int CallerProposalValid(const sg_strategy_proposal_t *proposal)
{
	if (!proposal || proposal->commitment_id == 0U ||
	    proposal->goal_kind < SG_STRATEGY_GOAL_DESTINATION ||
	    proposal->goal_kind >= SG_STRATEGY_GOAL_KIND_COUNT ||
	    proposal->goal_kind == SG_STRATEGY_GOAL_WAIT ||
	    !SG_DestinationRefValid(&proposal->destination) ||
	    proposal->destination_status < SG_STRATEGY_DESTINATION_UNOBSERVED ||
	    proposal->destination_status >= SG_STRATEGY_DESTINATION_STATUS_COUNT ||
	    !CallerAuthorityValid(proposal) || !proposal->goal_field)
		return 0;
	if (proposal->destination_status == SG_STRATEGY_DESTINATION_REACHABLE)
		return proposal->cost_ms < SG_DESTINATION_FIELD_INF &&
			SG_DestinationHandleValid(&proposal->handle) &&
			proposal->handle.kind == proposal->destination.kind;
	if (proposal->destination_status == SG_STRATEGY_DESTINATION_UNREACHABLE)
		return proposal->cost_ms == SG_DESTINATION_FIELD_INF &&
			SG_DestinationHandleValid(&proposal->handle) &&
			proposal->handle.kind == proposal->destination.kind;
	return proposal->cost_ms == SG_DESTINATION_FIELD_INF &&
		proposal->handle.valid == 0U;
}

static int CallerBindingMatches(const sg_strategy_caller_binding_t *binding,
	const sg_strategy_proposal_t *proposal)
{
	return binding && proposal && binding->plan_id != 0U &&
		binding->commitment_id == proposal->commitment_id &&
		binding->goal_kind == proposal->goal_kind &&
		binding->role == proposal->role &&
		binding->authority_rank == proposal->authority_rank &&
		binding->principal_kind == proposal->principal_kind &&
		binding->principal_id == proposal->principal_id &&
		CallerDestinationEqual(&binding->destination,
			&proposal->destination);
}

static void CallerCopyBinding(sg_strategy_caller_binding_t *binding,
	const sg_strategy_proposal_t *proposal, uint64_t plan_id)
{
	memset(binding, 0, sizeof(*binding));
	binding->plan_id = plan_id;
	binding->commitment_id = proposal->commitment_id;
	binding->goal_kind = proposal->goal_kind;
	binding->destination = proposal->destination;
	binding->handle = proposal->handle;
	binding->destination_status = proposal->destination_status;
	binding->cost_ms = proposal->cost_ms;
	binding->authority_rank = proposal->authority_rank;
	binding->principal_kind = proposal->principal_kind;
	binding->principal_id = proposal->principal_id;
	binding->role = proposal->role;
	binding->goal_field = proposal->goal_field;
}

static void CallerRefreshBinding(sg_strategy_caller_binding_t *binding,
	const sg_strategy_proposal_t *proposal)
{
	binding->handle = proposal->handle;
	binding->destination_status = proposal->destination_status;
	binding->cost_ms = proposal->cost_ms;
	binding->role = proposal->role;
	binding->goal_field = proposal->goal_field;
}

static int CallerNext(uint64_t *value)
{
	if (!value || *value == UINT64_MAX)
		return 0;
	(*value)++;
	return *value != 0U;
}

static int CallerLife(sg_strategy_caller_t *caller, uint8_t alive,
	sg_strategy_life_snapshot_t *life)
{
	uint64_t revision = caller->life_revision;
	uint64_t life_id = caller->life_id;

	if (alive > 1U || !life)
		return 0;
	if (!caller->life_known || caller->life_alive != alive)
	{
		if (!CallerNext(&revision))
			return 0;
		if (!caller->life_known || (!caller->life_alive && alive))
			if (!CallerNext(&life_id))
				return 0;
	}
	memset(life, 0, sizeof(*life));
	life->present = 1U;
	life->alive = alive;
	life->observation_revision = revision;
	life->life_id = life_id;
	caller->life_known = 1U;
	caller->life_alive = alive;
	caller->life_revision = revision;
	caller->life_id = life_id;
	return 1;
}

static int CallerDestinationObservation(sg_strategy_caller_t *caller,
	uint64_t at_ms, sg_strategy_destination_observation_t *observation)
{
	const sg_strategy_caller_binding_t *binding = &caller->binding;

	if (!binding->plan_id || !observation ||
	    !CallerNext(&caller->destination_revision))
		return 0;
	memset(observation, 0, sizeof(*observation));
	observation->plan_id = binding->plan_id;
	observation->goal_id = SG_STRATEGY_CALLER_GOAL_ID;
	observation->target_id = SG_STRATEGY_CALLER_TARGET_ID;
	observation->observation_revision = caller->destination_revision;
	observation->observed_at_ms = at_ms;
	observation->valid_until_ms = UINT64_MAX;
	observation->status = binding->destination_status;
	observation->cost_ms = binding->cost_ms;
	if (binding->destination_status != SG_STRATEGY_DESTINATION_UNOBSERVED)
	{
		observation->pose_revision = caller->destination_revision;
		observation->handle = binding->handle;
	}
	return 1;
}

static void CallerOutput(const sg_strategy_caller_t *caller,
	sg_strategy_caller_output_t *out)
{
	memset(out, 0, sizeof(*out));
	out->instruction = caller->reducer.current_instruction;
	out->plan_id = out->instruction.plan_id;
	out->activation_id = out->instruction.activation.activation_id;
	if (caller->binding.plan_id != out->instruction.plan_id)
		return;
	if (out->instruction.kind != SG_STRATEGY_INSTRUCTION_EXECUTE &&
	    out->instruction.kind != SG_STRATEGY_INSTRUCTION_SUSPENDED)
		return;
	out->role = caller->binding.role;
	out->goal_field = caller->binding.goal_field;
}

static int CallerReduce(sg_strategy_caller_t *caller,
	sg_strategy_frame_t *frame)
{
	sg_strategy_reduction_t reduction;
	sg_strategy_reduce_result_t result;

	if (!CallerNext(&caller->next_sequence))
		return 0;
	frame->sequence = caller->next_sequence;
	frame->expected_revision = caller->reducer.revision;
	result = SG_StrategyReduce(&caller->reducer, frame, &reduction);
	return result == SG_STRATEGY_REDUCE_APPLIED ||
		result == SG_STRATEGY_REDUCE_DUPLICATE;
}

static int CallerReleaseAuthority(sg_strategy_caller_t *caller,
	uint64_t at_ms, uint8_t alive)
{
	sg_strategy_frame_t frame;

	if (caller->reducer.authority.principal.kind ==
	    SG_STRATEGY_PRINCIPAL_NONE)
		return 1;
	memset(&frame, 0, sizeof(frame));
	frame.at_ms = at_ms;
	frame.directive.kind = SG_STRATEGY_DIRECTIVE_RELEASE;
	frame.directive.stamp.rank = caller->reducer.authority.rank;
	frame.directive.stamp.principal = caller->reducer.authority.principal;
	if (!CallerNext(&caller->next_authority_epoch))
		return 0;
	frame.directive.stamp.epoch = caller->next_authority_epoch;
	if (!CallerLife(caller, alive, &frame.life))
		return 0;
	return CallerReduce(caller, &frame);
}

static int CallerCancelCurrent(sg_strategy_caller_t *caller,
	const sg_strategy_proposal_t *proposal, uint64_t at_ms, uint8_t alive)
{
	sg_strategy_frame_t frame;

	if (!caller->reducer.has_plan || caller->reducer.cancelled)
		return 1;
	memset(&frame, 0, sizeof(frame));
	frame.at_ms = at_ms;
	frame.directive.kind = SG_STRATEGY_DIRECTIVE_CANCEL;
	frame.directive.stamp.rank = proposal->authority_rank;
	frame.directive.stamp.principal.kind = proposal->principal_kind;
	frame.directive.stamp.principal.id = proposal->principal_id;
	if (!CallerNext(&caller->next_authority_epoch))
		return 0;
	frame.directive.stamp.epoch = caller->next_authority_epoch;
	if (!CallerLife(caller, alive, &frame.life))
		return 0;
	return CallerReduce(caller, &frame);
}

static int CallerReplace(sg_strategy_caller_t *caller,
	const sg_strategy_proposal_t *proposal, uint8_t alive, uint64_t at_ms)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_plan_t plan;
	sg_strategy_compile_error_t error;
	sg_strategy_destination_observation_t observation;
	sg_strategy_frame_t frame;
	uint64_t plan_id = caller->next_plan_id;

	if (!CallerNext(&plan_id))
		return 0;
	memset(&spec, 0, sizeof(spec));
	spec.plan_id = plan_id;
	spec.goal_count = 1U;
	spec.goals[0].id = SG_STRATEGY_CALLER_GOAL_ID;
	spec.goals[0].kind = proposal->goal_kind;
	spec.goals[0].choice_count = 1U;
	spec.goals[0].unavailable = SG_STRATEGY_UNAVAILABLE_WAIT;
	spec.goals[0].choices[0].id = SG_STRATEGY_CALLER_TARGET_ID;
	spec.goals[0].choices[0].destination = proposal->destination;
	spec.goals[0].failure.max_attempts_per_choice = 1U;
	spec.goals[0].failure.exhausted = SG_STRATEGY_FAILURE_FAIL_PLAN;
	if (!SG_StrategyPlanCompile(&spec, &plan, &error))
		return 0;
	if (proposal->authority_rank < caller->reducer.authority.rank)
	{
		if (!CallerReleaseAuthority(caller, at_ms, alive))
			return 0;
	}
	if (!CallerCancelCurrent(caller, proposal, at_ms, alive))
		return 0;
	CallerCopyBinding(&caller->binding, proposal, plan_id);
	if (!CallerDestinationObservation(caller, at_ms, &observation))
		return 0;
	memset(&frame, 0, sizeof(frame));
	frame.at_ms = at_ms;
	frame.directive.kind = SG_STRATEGY_DIRECTIVE_REPLACE;
	frame.directive.replacement = &plan;
	frame.directive.stamp.rank = proposal->authority_rank;
	frame.directive.stamp.principal.kind = proposal->principal_kind;
	frame.directive.stamp.principal.id = proposal->principal_id;
	if (!CallerNext(&caller->next_authority_epoch))
		return 0;
	frame.directive.stamp.epoch = caller->next_authority_epoch;
	if (!CallerLife(caller, alive, &frame.life))
		return 0;
	frame.destinations = &observation;
	frame.destination_count = 1U;
	if (!CallerReduce(caller, &frame))
		return 0;
	caller->next_plan_id = plan_id;
	return 1;
}

static int CallerPulse(sg_strategy_caller_t *caller, uint8_t alive,
	uint64_t at_ms, sg_strategy_tactical_block_reason_t block_reason)
{
	sg_strategy_destination_observation_t observation;
	sg_strategy_frame_t frame;

	if (block_reason < SG_STRATEGY_BLOCK_NONE ||
	    block_reason >= SG_STRATEGY_BLOCK_REASON_COUNT)
		return 0;
	memset(&frame, 0, sizeof(frame));
	frame.at_ms = at_ms;
	if (!CallerLife(caller, alive, &frame.life))
		return 0;
	if (caller->binding.plan_id)
	{
		if (!CallerDestinationObservation(caller, at_ms, &observation))
			return 0;
		frame.destinations = &observation;
		frame.destination_count = 1U;
	}
	if (caller->reducer.activation.activation_id != 0U)
	{
		if (!CallerNext(&caller->tactical_revision))
			return 0;
		frame.tactical.present = 1U;
		frame.tactical.blocked = block_reason != SG_STRATEGY_BLOCK_NONE;
		frame.tactical.observation_revision = caller->tactical_revision;
		frame.tactical.activation = caller->reducer.activation;
		frame.tactical.reason = block_reason;
	}
	return CallerReduce(caller, &frame);
}

int SG_StrategyCallerInit(sg_strategy_caller_t *caller)
{
	if (!caller)
		return 0;
	memset(caller, 0, sizeof(*caller));
	if (!SG_StrategyStateInit(&caller->reducer))
		return 0;
	caller->initialized = 1U;
	return 1;
}

int SG_StrategyCallerStep(sg_strategy_caller_t *caller,
	const sg_strategy_proposal_t *proposal, uint8_t alive, uint64_t at_ms,
	sg_strategy_tactical_block_reason_t block_reason,
	sg_strategy_caller_output_t *out)
{
	if (!caller || !caller->initialized || !CallerProposalValid(proposal) ||
	    at_ms == 0U || !out)
		return 0;
	if (!CallerBindingMatches(&caller->binding, proposal))
	{
		if (!CallerReplace(caller, proposal, alive, at_ms))
			return 0;
	}
	else
		CallerRefreshBinding(&caller->binding, proposal);
	if (!CallerPulse(caller, alive, at_ms, block_reason))
		return 0;
	CallerOutput(caller, out);
	return 1;
}

int SG_StrategyCallerPulse(sg_strategy_caller_t *caller, uint8_t alive,
	uint64_t at_ms, sg_strategy_tactical_block_reason_t block_reason,
	sg_strategy_caller_output_t *out)
{
	if (!caller || !caller->initialized || at_ms == 0U || !out ||
	    !CallerPulse(caller, alive, at_ms, block_reason))
		return 0;
	CallerOutput(caller, out);
	return 1;
}

int SG_StrategyCallerSettle(sg_strategy_caller_t *caller,
	sg_strategy_goal_outcome_kind_t outcome,
	sg_strategy_failure_reason_t failure, uint64_t at_ms,
	sg_strategy_caller_output_t *out)
{
	sg_strategy_frame_t frame;

	if (!caller || !caller->initialized || !out || at_ms == 0U ||
	    caller->reducer.activation.activation_id == 0U ||
	    outcome <= SG_STRATEGY_OUTCOME_NONE ||
	    outcome >= SG_STRATEGY_OUTCOME_KIND_COUNT ||
	    (outcome == SG_STRATEGY_OUTCOME_COMPLETED &&
	     failure != SG_STRATEGY_FAILURE_NONE) ||
	    (outcome == SG_STRATEGY_OUTCOME_FAILED &&
	     (failure <= SG_STRATEGY_FAILURE_NONE ||
	      failure >= SG_STRATEGY_FAILURE_REASON_COUNT)))
		return 0;
	memset(&frame, 0, sizeof(frame));
	frame.at_ms = at_ms;
	if (!CallerLife(caller, 1U, &frame.life))
		return 0;
	frame.goal_outcome.present = 1U;
	frame.goal_outcome.activation = caller->reducer.activation;
	frame.goal_outcome.kind = outcome;
	frame.goal_outcome.failure = failure;
	if (!CallerReduce(caller, &frame))
		return 0;
	CallerOutput(caller, out);
	return 1;
}
