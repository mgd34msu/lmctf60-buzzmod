#include "sg_strategy_runtime_bridge.h"

#include <string.h>

static sg_strategy_runtime_target_locator_fn sg_strategy_runtime_locator;
static void *sg_strategy_runtime_locator_context;
static sg_strategy_runtime_target_authority_fn sg_strategy_runtime_authority;
static void *sg_strategy_runtime_authority_context;

static int RuntimeAuthorityValid(
	const sg_strategy_caller_authority_t *authority)
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

static int RuntimeAuthorityEqual(
	const sg_strategy_caller_authority_t *left,
	const sg_strategy_caller_authority_t *right)
{
	return left && right && left->rank == right->rank &&
		left->principal_kind == right->principal_kind &&
		left->principal_id == right->principal_id;
}

static int RuntimeDestinationEqual(const sg_destination_ref_t *left,
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

static int RuntimeFieldHandleEqual(const sg_field_handle_t *left,
	const sg_field_handle_t *right)
{
	return left && right && left->service_identity == right->service_identity &&
		left->service_generation == right->service_generation &&
		left->rune_identity == right->rune_identity &&
		left->topology_revision == right->topology_revision &&
		left->terminal_generation == right->terminal_generation &&
		left->field_generation == right->field_generation;
}

/* The authority, not the locator, establishes this relation.  In particular,
 * `accepted_view` is an owner-issued opaque capability for the returned raw
 * execution pointer and canonical field objects.  Requiring it to be the
 * exact view just accepted prevents a caller/provider echo from substituting
 * another same-kind field after the authority check. */
static int RuntimeBindingAccepted(
	const sg_strategy_runtime_target_request_t *target,
	const sg_strategy_runtime_target_view_t *view,
	const sg_strategy_caller_target_binding_t *binding)
{
	if (!target || !target->localized_player || !view || !view->opaque ||
	    !binding ||
	    binding->commitment_id != target->commitment_id ||
	    !RuntimeAuthorityEqual(&binding->authority, &target->authority) ||
	    binding->goal_id != target->goal_id ||
	    binding->target_id != target->target_id ||
	    !RuntimeDestinationEqual(&binding->destination,
		&target->destination) ||
	    binding->role != target->role || !binding->execution_field ||
	    binding->accepted_view != view->opaque || !binding->snapshot ||
	    !binding->terminal || !binding->field_handle || !binding->guidance ||
	    !binding->localized || !SG_RuneRuntimeSnapshotValid(binding->snapshot) ||
	    !SG_DestinationTerminalValid(binding->terminal) ||
	    !RuntimeDestinationEqual(&binding->terminal->destination,
		&target->destination) ||
	    !SG_FieldHandleValid(binding->field_handle) ||
	    binding->field_handle->rune_identity != binding->snapshot->identity ||
	    binding->field_handle->topology_revision !=
		binding->snapshot->topology_revision ||
	    binding->field_handle->terminal_generation !=
		binding->terminal->generation ||
	    !SG_FieldGuidanceValid(binding->guidance) ||
	    !RuntimeFieldHandleEqual(binding->field_handle,
		&binding->guidance->field) ||
	    !SG_LocalizedFieldStateValid(binding->localized) ||
	    binding->localized->rune_identity != binding->snapshot->identity ||
	    binding->localized->topology_revision !=
		binding->snapshot->topology_revision ||
	    target->localized_player->rune_identity != binding->snapshot->identity ||
	    target->localized_player->topology_revision !=
		binding->snapshot->topology_revision ||
	    !SG_PhaseCoordinateValid(binding->snapshot,
		&target->localized_player->field_pose.phase) ||
	    binding->guidance->pose_revision != binding->localized->pose_revision ||
	    binding->guidance->sampled_at_ms != binding->localized->sampled_at_ms ||
	    !SG_DestinationHandleValid(&binding->resolved_destination) ||
	    binding->resolved_destination.kind != target->destination.kind ||
	    !SG_PhaseCoordinateValid(binding->snapshot,
		&binding->resolved_destination.pose.phase) ||
	    binding->resolved_destination.pose.region_id >=
		binding->snapshot->region_count)
		return 0;
	return 1;
}

static int RuntimeExecutionFor(const sg_strategy_runtime_plan_request_t *request,
	sg_strategy_goal_id_t goal_id, sg_strategy_target_id_t target_id,
	const sg_strategy_runtime_execution_t **execution_out)
{
	const sg_strategy_runtime_execution_t *found = NULL;
	uint16_t index;

	if (!request || !execution_out)
		return 0;
	for (index = 0U; index < request->execution_count; index++)
	{
		const sg_strategy_runtime_execution_t *execution =
			&request->executions[index];

		if (execution->goal_id != goal_id || execution->target_id != target_id)
			continue;
		if (found)
			return 0;
		found = execution;
	}
	if (!found)
		return 0;
	*execution_out = found;
	return 1;
}

static int RuntimeRequestCompile(const sg_strategy_runtime_plan_request_t *request,
	sg_strategy_plan_t *compiled)
{
	sg_strategy_compile_error_t error;
	sg_strategy_plan_spec_t spec;
	uint16_t target_count = 0U;
	uint16_t goal_index;

	if (!request || !compiled || request->commitment_id == 0U ||
	    !request->localized_player ||
	    request->localized_player->subject.reserved != 0U ||
	    request->localized_player->subject.client_id == UINT32_MAX ||
	    request->localized_player->subject.spawn_generation == 0U ||
	    request->localized_player->rune_identity == 0U ||
	    request->localized_player->topology_revision == 0U ||
	    request->localized_player->frame_sequence == 0U ||
	    !SG_DestinationPoseValid(&request->localized_player->field_pose) ||
	    !RuntimeAuthorityValid(&request->authority) ||
	    request->spec.plan_id != 0U || request->spec.goal_count == 0U ||
	    request->spec.goal_count > SG_STRATEGY_MAX_GOALS ||
	    request->execution_count == 0U ||
	    request->execution_count > SG_STRATEGY_CALLER_MAX_BINDINGS)
		return 0;
	for (goal_index = 0U; goal_index < request->spec.goal_count; goal_index++)
	{
		const sg_strategy_goal_spec_t *goal = &request->spec.goals[goal_index];

		if (goal->choice_count > SG_STRATEGY_MAX_CHOICES ||
		    target_count > UINT16_MAX - goal->choice_count)
			return 0;
		target_count = (uint16_t)(target_count + goal->choice_count);
	}
	if (target_count != request->execution_count)
		return 0;
	memset(&spec, 0, sizeof(spec));
	spec = request->spec;
	spec.plan_id = 1U;
	if (!SG_StrategyPlanCompile(&spec, compiled, &error))
		return 0;
	for (goal_index = 0U; goal_index < compiled->goal_count; goal_index++)
	{
		const sg_strategy_goal_t *goal = &compiled->goals[goal_index];
		uint8_t choice_index;

		for (choice_index = 0U; choice_index < goal->choice_count;
		     choice_index++)
		{
			const sg_strategy_runtime_execution_t *execution;

			if (!RuntimeExecutionFor(request, goal->id,
				goal->choices[choice_index].id, &execution))
				return 0;
		}
	}
	return 1;
}

void SG_StrategyRuntimeTargetProviderSet(
	sg_strategy_runtime_target_locator_fn locator, void *locator_context,
	sg_strategy_runtime_target_authority_fn authority, void *authority_context)
{
	sg_strategy_runtime_locator = locator;
	sg_strategy_runtime_locator_context = locator_context;
	sg_strategy_runtime_authority = authority;
	sg_strategy_runtime_authority_context = authority_context;
}

int SG_StrategyRuntimeTargetProviderAvailable(void)
{
	return sg_strategy_runtime_locator != NULL &&
		sg_strategy_runtime_authority != NULL;
}

int SG_StrategyRuntimePlanResolve(
	const sg_strategy_runtime_plan_request_t *request,
	sg_strategy_caller_plan_t *plan_out)
{
	sg_strategy_caller_plan_t candidate;
	sg_strategy_plan_t compiled;
	uint16_t goal_index;
	uint16_t binding_index = 0U;

	if (!request || !plan_out || !SG_StrategyRuntimeTargetProviderAvailable() ||
	    !RuntimeRequestCompile(request, &compiled))
		return 0;
	memset(&candidate, 0, sizeof(candidate));
	candidate.commitment_id = request->commitment_id;
	candidate.authority = request->authority;
	candidate.spec = request->spec;
	candidate.binding_count = request->execution_count;
	for (goal_index = 0U; goal_index < compiled.goal_count; goal_index++)
	{
		const sg_strategy_goal_t *goal = &compiled.goals[goal_index];
		uint8_t choice_index;

		for (choice_index = 0U; choice_index < goal->choice_count;
		     choice_index++)
		{
			const sg_strategy_runtime_execution_t *execution;
			sg_strategy_runtime_target_request_t target;
			sg_strategy_runtime_target_view_t view;
			sg_strategy_caller_target_binding_t binding;

			if (binding_index >= candidate.binding_count ||
			    !RuntimeExecutionFor(request, goal->id,
				goal->choices[choice_index].id, &execution))
				return 0;
			memset(&target, 0, sizeof(target));
			target.commitment_id = request->commitment_id;
			target.localized_player = request->localized_player;
			target.authority = request->authority;
			target.goal_id = goal->id;
			target.target_id = goal->choices[choice_index].id;
			target.destination = goal->choices[choice_index].destination;
			target.role = execution->role;
			memset(&view, 0, sizeof(view));
			memset(&binding, 0, sizeof(binding));
			if (!sg_strategy_runtime_locator(
				sg_strategy_runtime_locator_context, &target, &view) ||
			    !view.opaque ||
			    !sg_strategy_runtime_authority(
				sg_strategy_runtime_authority_context, &target, &view,
				&binding) ||
			    !RuntimeBindingAccepted(&target, &view, &binding))
				return 0;
			candidate.bindings[binding_index] = binding;
			binding_index++;
		}
	}
	if (binding_index != candidate.binding_count)
		return 0;
	*plan_out = candidate;
	return 1;
}
