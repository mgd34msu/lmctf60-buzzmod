#include "sg_strategy_caller.h"

#include <limits.h>
#include <string.h>

static int CallerAuthorityValid(const sg_strategy_caller_authority_t *authority)
{
	if (!authority || authority->principal_id == 0U)
		return 0;
	switch (authority->rank)
	{
	case SG_STRATEGY_AUTHORITY_AUTONOMOUS:
		return authority->principal_kind == SG_STRATEGY_PRINCIPAL_AUTONOMOUS;
	case SG_STRATEGY_AUTHORITY_TEAM:
		return authority->principal_kind == SG_STRATEGY_PRINCIPAL_TEAM;
	case SG_STRATEGY_AUTHORITY_HUMAN:
		return authority->principal_kind == SG_STRATEGY_PRINCIPAL_HUMAN;
	case SG_STRATEGY_AUTHORITY_EMERGENCY:
		return authority->principal_kind == SG_STRATEGY_PRINCIPAL_EMERGENCY;
	default:
		return 0;
	}
}

static int CallerAuthorityEqual(const sg_strategy_caller_authority_t *left,
	const sg_strategy_caller_authority_t *right)
{
	return left && right && left->rank == right->rank &&
		left->principal_kind == right->principal_kind &&
		left->principal_id == right->principal_id;
}

static int CallerAuthorityMatchesState(
	const sg_strategy_caller_authority_t *authority,
	const sg_strategy_state_t *state)
{
	return authority && state && authority->rank == state->authority.rank &&
		authority->principal_kind == state->authority.principal.kind &&
		authority->principal_id == state->authority.principal.id;
}

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

static int CallerFieldHandleEqual(const sg_field_handle_t *left,
	const sg_field_handle_t *right)
{
	return left && right && left->service_identity == right->service_identity &&
		left->service_generation == right->service_generation &&
		left->rune_identity == right->rune_identity &&
		left->topology_revision == right->topology_revision &&
		left->terminal_generation == right->terminal_generation &&
		left->field_generation == right->field_generation;
}

static int CallerGuidanceObservation(const sg_field_guidance_t *guidance,
	sg_strategy_destination_status_t *status_out, uint32_t *cost_out)
{
	uint64_t cost_ms;
	uint64_t upper_us;

	if (!guidance || !status_out || !cost_out ||
	    !SG_FieldGuidanceValid(guidance))
		return 0;
	switch (guidance->kind)
	{
	case SG_FIELD_GUIDANCE_TERMINAL:
		*status_out = SG_STRATEGY_DESTINATION_REACHABLE;
		*cost_out = 0U;
		return 1;
	case SG_FIELD_GUIDANCE_DESCENT:
		/* Preserve the field service's upper bound when projecting it into the
		 * reducer's millisecond domain.  A non-representable cost is rejected,
		 * never silently capped or made reachable by a legacy estimate. */
		upper_us = guidance->value.descent.arrival_cost.upper_us;
		cost_ms = upper_us / UINT64_C(1000);
		if (upper_us % UINT64_C(1000) != 0U)
			cost_ms++;
		if (cost_ms >= (uint64_t)SG_DESTINATION_COST_INFINITE)
			return 0;
		*status_out = SG_STRATEGY_DESTINATION_REACHABLE;
		*cost_out = (uint32_t)cost_ms;
		return 1;
	case SG_FIELD_GUIDANCE_UNREACHABLE:
		*status_out = SG_STRATEGY_DESTINATION_UNREACHABLE;
		*cost_out = SG_DESTINATION_COST_INFINITE;
		return 1;
	case SG_FIELD_GUIDANCE_KIND_COUNT:
	default:
		return 0;
	}
}

static int CallerNext(uint64_t *value)
{
	if (!value || *value == UINT64_MAX)
		return 0;
	(*value)++;
	return *value != 0U;
}

static int CallerLifeFrame(const sg_strategy_caller_t *caller, uint8_t alive,
	sg_strategy_life_snapshot_t *life)
{
	uint64_t revision;
	uint64_t life_id;

	if (!caller || alive > 1U || !life)
		return 0;
	revision = caller->life_revision;
	life_id = caller->life_id;
	if (!caller->life_known || caller->life_alive != alive)
	{
		if (!CallerNext(&revision))
			return 0;
		if ((!caller->life_known || (!caller->life_alive && alive)) &&
		    !CallerNext(&life_id))
			return 0;
	}
	memset(life, 0, sizeof(*life));
	life->present = 1U;
	life->alive = alive;
	life->observation_revision = revision;
	life->life_id = life_id;
	return 1;
}

static void CallerLifeCommit(sg_strategy_caller_t *caller,
	const sg_strategy_life_snapshot_t *life)
{
	caller->life_known = 1U;
	caller->life_alive = life->alive;
	caller->life_revision = life->observation_revision;
	caller->life_id = life->life_id;
}

static int CallerBindingFor(const sg_strategy_caller_plan_t *plan,
	sg_strategy_goal_id_t goal_id, sg_strategy_target_id_t target_id,
	const sg_strategy_caller_target_binding_t **binding_out)
{
	const sg_strategy_caller_target_binding_t *found = NULL;
	uint16_t index;

	if (!plan || !binding_out)
		return 0;
	for (index = 0U; index < plan->binding_count; index++)
	{
		const sg_strategy_caller_target_binding_t *binding =
			&plan->bindings[index];

		if (binding->goal_id != goal_id || binding->target_id != target_id)
			continue;
		if (found)
			return 0;
		found = binding;
	}
	if (!found)
		return 0;
	*binding_out = found;
	return 1;
}

static int CallerBindingAuthenticated(const sg_strategy_caller_plan_t *plan,
	const sg_strategy_caller_target_binding_t *binding,
	const sg_strategy_target_choice_t *choice, uint64_t at_ms)
{
	/* The runtime bridge has already asked the field-service/localization owner
	 * to accept an opaque view for this exact semantic target.  This boundary
	 * verifies the emitted target and the complete authenticated chain:
	 * target -> terminal -> field handle -> guidance/localization.  The raw
	 * execution pointer is usable only alongside that owner-issued capability;
	 * matching a destination kind or echoed IDs is never sufficient. */
	if (!plan || !binding || !choice ||
	    binding->commitment_id != plan->commitment_id ||
	    !CallerAuthorityEqual(&binding->authority, &plan->authority) ||
	    !CallerDestinationEqual(&binding->destination, &choice->destination) ||
	    !binding->execution_field || !binding->accepted_view ||
	    !binding->snapshot || !binding->terminal || !binding->field_handle ||
	    !binding->guidance || !binding->localized ||
	    binding->observation_revision == 0U || binding->pose_revision == 0U ||
	    binding->valid_until_ms < at_ms ||
	    !SG_RuneRuntimeSnapshotValid(binding->snapshot) ||
	    !SG_DestinationTerminalValid(binding->terminal) ||
	    !CallerDestinationEqual(&binding->terminal->destination,
		&binding->destination) ||
	    !SG_FieldHandleValid(binding->field_handle) ||
	    binding->field_handle->rune_identity != binding->snapshot->identity ||
	    binding->field_handle->topology_revision !=
		binding->snapshot->topology_revision ||
	    binding->field_handle->terminal_generation !=
		binding->terminal->generation ||
	    !SG_FieldGuidanceValid(binding->guidance) ||
	    !CallerFieldHandleEqual(binding->field_handle,
		&binding->guidance->field) ||
	    binding->guidance->pose_revision != binding->pose_revision ||
	    binding->guidance->sampled_at_ms == 0U ||
	    binding->guidance->sampled_at_ms > at_ms ||
	    !SG_LocalizedFieldStateValid(binding->localized) ||
	    binding->localized->rune_identity != binding->snapshot->identity ||
	    binding->localized->topology_revision !=
		binding->snapshot->topology_revision ||
	    binding->localized->pose_revision != binding->pose_revision ||
	    binding->localized->sampled_at_ms != binding->guidance->sampled_at_ms ||
	    binding->localized->sampled_at_ms > at_ms ||
	    !SG_DestinationHandleValid(&binding->resolved_destination) ||
	    binding->resolved_destination.kind != binding->destination.kind ||
	    !SG_PhaseCoordinateValid(binding->snapshot,
		&binding->resolved_destination.pose.phase) ||
	    binding->resolved_destination.pose.region_id >=
		binding->snapshot->region_count)
		return 0;
	return 1;
}

static int CallerPlanCountsSafe(const sg_strategy_caller_plan_t *plan,
	uint16_t *target_count_out)
{
	uint16_t goal_index;
	uint16_t target_count = 0U;

	if (!plan || !target_count_out || plan->spec.goal_count == 0U ||
	    plan->spec.goal_count > SG_STRATEGY_MAX_GOALS ||
	    plan->binding_count == 0U ||
	    plan->binding_count > SG_STRATEGY_CALLER_MAX_BINDINGS)
		return 0;
	for (goal_index = 0U; goal_index < plan->spec.goal_count; goal_index++)
	{
		const sg_strategy_goal_spec_t *goal = &plan->spec.goals[goal_index];

		if (goal->choice_count > SG_STRATEGY_MAX_CHOICES ||
		    target_count > UINT16_MAX - goal->choice_count)
			return 0;
		target_count = (uint16_t)(target_count + goal->choice_count);
	}
	if (target_count != plan->binding_count ||
	    target_count > SG_STRATEGY_CALLER_MAX_BINDINGS)
		return 0;
	*target_count_out = target_count;
	return 1;
}

static int CallerPlanCompile(const sg_strategy_caller_plan_t *plan,
	uint64_t plan_id, uint64_t at_ms, sg_strategy_plan_t *compiled)
{
	sg_strategy_plan_spec_t spec;
	sg_strategy_compile_error_t error;
	uint16_t target_count;
	uint16_t goal_index;

	if (!plan || !compiled || plan_id == 0U || at_ms == 0U ||
	    plan->commitment_id == 0U || plan->spec.plan_id != 0U ||
	    !CallerAuthorityValid(&plan->authority) ||
	    !CallerPlanCountsSafe(plan, &target_count))
		return 0;
	memset(&spec, 0, sizeof(spec));
	spec = plan->spec;
	spec.plan_id = plan_id;
	if (!SG_StrategyPlanCompile(&spec, compiled, &error))
		return 0;
	if (target_count != plan->binding_count)
		return 0;
	for (goal_index = 0U; goal_index < compiled->goal_count; goal_index++)
	{
		const sg_strategy_goal_t *goal = &compiled->goals[goal_index];
		uint8_t choice_index;

		for (choice_index = 0U; choice_index < goal->choice_count;
		     choice_index++)
		{
			const sg_strategy_caller_target_binding_t *binding;

			if (!CallerBindingFor(plan, goal->id,
				goal->choices[choice_index].id, &binding) ||
			    !CallerBindingAuthenticated(plan, binding,
				&goal->choices[choice_index], at_ms))
				return 0;
		}
	}
	return 1;
}

static int CallerPlansEquivalent(const sg_strategy_caller_plan_t *left,
	const sg_strategy_caller_plan_t *right)
{
	sg_strategy_plan_t left_compiled;
	sg_strategy_plan_t right_compiled;
	sg_strategy_compile_error_t error;
	sg_strategy_plan_spec_t left_spec;
	sg_strategy_plan_spec_t right_spec;

	if (!left || !right || left->commitment_id != right->commitment_id ||
	    !CallerAuthorityEqual(&left->authority, &right->authority))
		return 0;
	memset(&left_spec, 0, sizeof(left_spec));
	memset(&right_spec, 0, sizeof(right_spec));
	left_spec = left->spec;
	right_spec = right->spec;
	left_spec.plan_id = 1U;
	right_spec.plan_id = 1U;
	if (!SG_StrategyPlanCompile(&left_spec, &left_compiled, &error) ||
	    !SG_StrategyPlanCompile(&right_spec, &right_compiled, &error))
		return 0;
	return memcmp(&left_compiled, &right_compiled,
		sizeof(left_compiled)) == 0;
}

static int CallerDestinationObservations(const sg_strategy_caller_plan_t *plan,
	const sg_strategy_plan_t *compiled, uint64_t at_ms,
	sg_strategy_destination_observation_t *observations,
	uint16_t *count_out)
{
	uint16_t goal_index;
	uint16_t count = 0U;

	if (!plan || !compiled || !observations || !count_out || at_ms == 0U)
		return 0;
	for (goal_index = 0U; goal_index < compiled->goal_count; goal_index++)
	{
		const sg_strategy_goal_t *goal = &compiled->goals[goal_index];
		uint8_t choice_index;

		for (choice_index = 0U; choice_index < goal->choice_count;
		     choice_index++)
		{
			const sg_strategy_caller_target_binding_t *binding;
			sg_strategy_destination_observation_t *observation;

			if (count >= SG_STRATEGY_CALLER_MAX_BINDINGS ||
			    !CallerBindingFor(plan, goal->id,
				goal->choices[choice_index].id, &binding) ||
			    !CallerBindingAuthenticated(plan, binding,
				&goal->choices[choice_index], at_ms))
				return 0;
			observation = &observations[count];
			memset(observation, 0, sizeof(*observation));
			observation->plan_id = compiled->plan_id;
			observation->goal_id = goal->id;
			observation->target_id = goal->choices[choice_index].id;
			observation->observation_revision =
				binding->observation_revision;
			observation->pose_revision = binding->pose_revision;
			observation->observed_at_ms =
				binding->guidance->sampled_at_ms;
			observation->valid_until_ms = binding->valid_until_ms;
			if (!CallerGuidanceObservation(binding->guidance,
				&observation->status, &observation->cost_ms))
				return 0;
			observation->handle = binding->resolved_destination;
			count++;
		}
	}
	*count_out = count;
	return count == plan->binding_count;
}

static int CallerReduce(sg_strategy_caller_t *caller,
	sg_strategy_frame_t *frame)
{
	sg_strategy_reduction_t reduction;
	sg_strategy_reduce_result_t result;
	uint64_t sequence;

	if (!caller || !frame)
		return 0;
	sequence = caller->next_sequence;
	if (!CallerNext(&sequence))
		return 0;
	frame->sequence = sequence;
	frame->expected_revision = caller->reducer.revision;
	result = SG_StrategyReduce(&caller->reducer, frame, &reduction);
	if (result != SG_STRATEGY_REDUCE_APPLIED &&
	    result != SG_STRATEGY_REDUCE_DUPLICATE)
		return 0;
	caller->next_sequence = sequence;
	return 1;
}

static int CallerPulsePlan(sg_strategy_caller_t *caller,
	const sg_strategy_caller_plan_t *plan, uint8_t alive, uint64_t at_ms,
	sg_strategy_tactical_block_reason_t block_reason)
{
	sg_strategy_destination_observation_t
		observations[SG_STRATEGY_CALLER_MAX_BINDINGS];
	sg_strategy_life_snapshot_t life;
	sg_strategy_frame_t frame;
	uint16_t observation_count;
	uint64_t tactical_revision;

	if (!caller || !plan || at_ms == 0U ||
	    block_reason < SG_STRATEGY_BLOCK_NONE ||
	    block_reason >= SG_STRATEGY_BLOCK_REASON_COUNT ||
	    !CallerLifeFrame(caller, alive, &life) ||
	    !CallerDestinationObservations(plan, &caller->reducer.plan, at_ms,
		observations, &observation_count))
		return 0;
	memset(&frame, 0, sizeof(frame));
	frame.at_ms = at_ms;
	frame.life = life;
	frame.destinations = observations;
	frame.destination_count = observation_count;
	tactical_revision = caller->tactical_revision;
	if (caller->reducer.activation.activation_id != 0U)
	{
		if (!CallerNext(&tactical_revision))
			return 0;
		frame.tactical.present = 1U;
		frame.tactical.blocked = block_reason != SG_STRATEGY_BLOCK_NONE;
		frame.tactical.observation_revision = tactical_revision;
		frame.tactical.activation = caller->reducer.activation;
		frame.tactical.reason = block_reason;
	}
	if (!CallerReduce(caller, &frame))
		return 0;
	CallerLifeCommit(caller, &life);
	caller->tactical_revision = tactical_revision;
	return 1;
}

static int CallerReplace(sg_strategy_caller_t *caller,
	const sg_strategy_caller_plan_t *plan, uint8_t alive, uint64_t at_ms)
{
	sg_strategy_destination_observation_t
		observations[SG_STRATEGY_CALLER_MAX_BINDINGS];
	sg_strategy_plan_t compiled;
	sg_strategy_life_snapshot_t life;
	sg_strategy_frame_t frame;
	uint16_t observation_count;
	uint64_t plan_id;
	uint64_t authority_epoch;

	if (!caller || !plan || !CallerLifeFrame(caller, alive, &life))
		return 0;
	plan_id = caller->next_plan_id;
	authority_epoch = caller->next_authority_epoch;
	if (!CallerNext(&plan_id) || !CallerNext(&authority_epoch) ||
	    !CallerPlanCompile(plan, plan_id, at_ms, &compiled) ||
	    !CallerDestinationObservations(plan, &compiled, at_ms, observations,
		&observation_count))
		return 0;
	memset(&frame, 0, sizeof(frame));
	frame.at_ms = at_ms;
	frame.life = life;
	frame.directive.kind = SG_STRATEGY_DIRECTIVE_REPLACE;
	frame.directive.replacement = &compiled;
	frame.directive.stamp.rank = plan->authority.rank;
	frame.directive.stamp.principal.kind = plan->authority.principal_kind;
	frame.directive.stamp.principal.id = plan->authority.principal_id;
	frame.directive.stamp.epoch = authority_epoch;
	frame.destinations = observations;
	frame.destination_count = observation_count;
	if (!CallerReduce(caller, &frame))
		return 0;
	caller->plan = *plan;
	caller->has_plan = 1U;
	caller->next_plan_id = plan_id;
	caller->next_authority_epoch = authority_epoch;
	CallerLifeCommit(caller, &life);
	return 1;
}

static const sg_strategy_caller_target_binding_t *CallerActiveBinding(
	const sg_strategy_caller_t *caller)
{
	const sg_strategy_goal_t *goal;
	int goal_index;
	uint8_t choice_index;

	if (!caller || !caller->has_plan || !caller->reducer.has_plan ||
	    caller->reducer.activation.activation_id == 0U)
		return NULL;
	goal_index = -1;
	for (uint16_t index = 0U; index < caller->reducer.plan.goal_count;
	     index++)
		if (caller->reducer.plan.goals[index].id ==
		    caller->reducer.activation.goal_id)
		{
			goal_index = (int)index;
			break;
		}
	if (goal_index < 0)
		return NULL;
	goal = &caller->reducer.plan.goals[(uint16_t)goal_index];
	choice_index = caller->reducer.goals[(uint16_t)goal_index].selected_choice;
	if (choice_index == SG_STRATEGY_NO_CHOICE ||
	    choice_index >= goal->choice_count)
		return NULL;
	{
		const sg_strategy_caller_target_binding_t *binding;

		if (!CallerBindingFor(&caller->plan, goal->id,
			goal->choices[choice_index].id, &binding))
			return NULL;
		return binding;
	}
}

static void CallerOutput(const sg_strategy_caller_t *caller,
	sg_strategy_caller_output_t *out)
{
	const sg_strategy_caller_target_binding_t *binding;

	memset(out, 0, sizeof(*out));
	out->instruction = caller->reducer.current_instruction;
	out->plan_id = out->instruction.plan_id;
	out->activation_id = out->instruction.activation.activation_id;
	if (out->instruction.kind != SG_STRATEGY_INSTRUCTION_EXECUTE &&
	    out->instruction.kind != SG_STRATEGY_INSTRUCTION_SUSPENDED)
		return;
	binding = CallerActiveBinding(caller);
	if (!binding)
		return;
	out->role = binding->role;
	out->execution_field = binding->execution_field;
	out->snapshot = binding->snapshot;
	out->terminal = binding->terminal;
	out->field_handle = binding->field_handle;
	out->guidance = binding->guidance;
	out->localized = binding->localized;
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

int SG_StrategyCallerSubmit(sg_strategy_caller_t *caller,
	const sg_strategy_caller_plan_t *plan, uint8_t alive, uint64_t at_ms,
	sg_strategy_tactical_block_reason_t block_reason,
	sg_strategy_caller_output_t *out)
{
	sg_strategy_plan_t compiled;
	uint64_t validation_plan_id;

	if (!caller || !caller->initialized || !plan || !out || at_ms == 0U ||
	    !CallerAuthorityValid(&plan->authority))
		return 0;
	if (caller->reducer.has_plan &&
	    plan->authority.rank < caller->reducer.authority.rank)
	{
		if (!caller->has_plan || !CallerPulsePlan(caller, &caller->plan,
			alive, at_ms, block_reason))
			return 0;
		CallerOutput(caller, out);
		return 1;
	}
	validation_plan_id = caller->reducer.has_plan
		? caller->reducer.plan.plan_id : 1U;
	if (!CallerPlanCompile(plan, validation_plan_id, at_ms, &compiled))
		return 0;
	if (caller->has_plan && CallerPlansEquivalent(&caller->plan, plan))
	{
		if (!CallerPulsePlan(caller, plan, alive, at_ms, block_reason))
			return 0;
		caller->plan = *plan;
		CallerOutput(caller, out);
		return 1;
	}
	if (!CallerReplace(caller, plan, alive, at_ms) ||
	    !CallerPulsePlan(caller, &caller->plan, alive, at_ms, block_reason))
		return 0;
	CallerOutput(caller, out);
	return 1;
}

int SG_StrategyCallerPulse(sg_strategy_caller_t *caller, uint8_t alive,
	uint64_t at_ms, sg_strategy_tactical_block_reason_t block_reason,
	sg_strategy_caller_output_t *out)
{
	if (!caller || !caller->initialized || !out || at_ms == 0U ||
	    !caller->has_plan || !CallerPulsePlan(caller, &caller->plan, alive,
		at_ms, block_reason))
		return 0;
	CallerOutput(caller, out);
	return 1;
}

int SG_StrategyCallerAdvance(sg_strategy_caller_t *caller, uint8_t alive,
	sg_strategy_goal_outcome_kind_t outcome,
	sg_strategy_failure_reason_t failure, uint64_t at_ms,
	sg_strategy_caller_output_t *out)
{
	sg_strategy_destination_observation_t
		observations[SG_STRATEGY_CALLER_MAX_BINDINGS];
	sg_strategy_life_snapshot_t life;
	sg_strategy_frame_t frame;
	uint16_t observation_count;

	if (!caller || !caller->initialized || !caller->has_plan || !out ||
	    at_ms == 0U || caller->reducer.activation.activation_id == 0U ||
	    outcome <= SG_STRATEGY_OUTCOME_NONE ||
	    outcome >= SG_STRATEGY_OUTCOME_KIND_COUNT ||
	    (outcome == SG_STRATEGY_OUTCOME_COMPLETED &&
	     failure != SG_STRATEGY_FAILURE_NONE) ||
	    (outcome == SG_STRATEGY_OUTCOME_FAILED &&
	     (failure <= SG_STRATEGY_FAILURE_NONE ||
	      failure >= SG_STRATEGY_FAILURE_REASON_COUNT)) ||
	    !CallerLifeFrame(caller, alive, &life) ||
	    !CallerDestinationObservations(&caller->plan, &caller->reducer.plan,
		at_ms, observations, &observation_count))
		return 0;
	memset(&frame, 0, sizeof(frame));
	frame.at_ms = at_ms;
	frame.life = life;
	frame.destinations = observations;
	frame.destination_count = observation_count;
	frame.goal_outcome.present = 1U;
	frame.goal_outcome.activation = caller->reducer.activation;
	frame.goal_outcome.kind = outcome;
	frame.goal_outcome.failure = failure;
	if (!CallerReduce(caller, &frame))
		return 0;
	CallerLifeCommit(caller, &life);
	CallerOutput(caller, out);
	return 1;
}

int SG_StrategyCallerSettle(sg_strategy_caller_t *caller, uint8_t alive,
	sg_strategy_goal_outcome_kind_t outcome,
	sg_strategy_failure_reason_t failure, uint64_t at_ms,
	sg_strategy_caller_output_t *out)
{
	return SG_StrategyCallerAdvance(caller, alive, outcome, failure, at_ms,
		out);
}

int SG_StrategyCallerCancel(sg_strategy_caller_t *caller,
	const sg_strategy_caller_authority_t *authority, uint8_t alive,
	uint64_t at_ms, sg_strategy_caller_output_t *out)
{
	sg_strategy_life_snapshot_t life;
	sg_strategy_frame_t frame;
	uint64_t authority_epoch;

	if (!caller || !caller->initialized || !authority || !out || at_ms == 0U ||
	    !CallerAuthorityValid(authority) || !caller->reducer.has_plan ||
	    !CallerAuthorityMatchesState(authority, &caller->reducer) ||
	    !CallerLifeFrame(caller, alive, &life))
		return 0;
	authority_epoch = caller->next_authority_epoch;
	if (!CallerNext(&authority_epoch))
		return 0;
	memset(&frame, 0, sizeof(frame));
	frame.at_ms = at_ms;
	frame.life = life;
	frame.directive.kind = SG_STRATEGY_DIRECTIVE_CANCEL;
	frame.directive.stamp.rank = authority->rank;
	frame.directive.stamp.principal.kind = authority->principal_kind;
	frame.directive.stamp.principal.id = authority->principal_id;
	frame.directive.stamp.epoch = authority_epoch;
	if (!CallerReduce(caller, &frame))
		return 0;
	caller->next_authority_epoch = authority_epoch;
	CallerLifeCommit(caller, &life);
	CallerOutput(caller, out);
	return 1;
}

int SG_StrategyCallerRelease(sg_strategy_caller_t *caller,
	const sg_strategy_caller_authority_t *authority, uint8_t alive,
	uint64_t at_ms, sg_strategy_caller_output_t *out)
{
	sg_strategy_life_snapshot_t life;
	sg_strategy_frame_t frame;
	uint64_t authority_epoch;

	if (!caller || !caller->initialized || !authority || !out || at_ms == 0U ||
	    !CallerAuthorityValid(authority) || !caller->reducer.has_plan ||
	    !CallerAuthorityMatchesState(authority, &caller->reducer) ||
	    !CallerLifeFrame(caller, alive, &life))
		return 0;
	authority_epoch = caller->next_authority_epoch;
	if (!CallerNext(&authority_epoch))
		return 0;
	memset(&frame, 0, sizeof(frame));
	frame.at_ms = at_ms;
	frame.life = life;
	frame.directive.kind = SG_STRATEGY_DIRECTIVE_RELEASE;
	frame.directive.stamp.rank = authority->rank;
	frame.directive.stamp.principal.kind = authority->principal_kind;
	frame.directive.stamp.principal.id = authority->principal_id;
	frame.directive.stamp.epoch = authority_epoch;
	if (!CallerReduce(caller, &frame))
		return 0;
	caller->next_authority_epoch = authority_epoch;
	CallerLifeCommit(caller, &life);
	memset(&caller->plan, 0, sizeof(caller->plan));
	caller->has_plan = 0U;
	CallerOutput(caller, out);
	return 1;
}
