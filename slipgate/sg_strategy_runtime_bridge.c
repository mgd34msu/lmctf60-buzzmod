#include "sg_strategy_runtime_bridge.h"

#include <stdint.h>
#include <string.h>

/* Registration is one capability.  A resolver snapshots the entire object
 * before it calls untrusted code, then compares its non-wrapping identity
 * after every callback.  Replacing just one callback/context pair must never
 * make an old borrowed view cross into the new authority or release owner. */
typedef struct sg_strategy_runtime_provider_registration_s
{
	sg_strategy_runtime_target_locator_fn locator;
	void *locator_context;
	sg_strategy_runtime_target_authority_fn authority;
	void *authority_context;
	sg_strategy_runtime_target_release_fn release_view;
	void *release_context;
	uint64_t identity;
} sg_strategy_runtime_provider_registration_t;

static sg_strategy_runtime_provider_registration_t sg_strategy_runtime_provider;
static uint64_t sg_strategy_runtime_provider_next_identity = 1U;

static int RuntimeProviderRegistrationAvailable(
	const sg_strategy_runtime_provider_registration_t *registration)
{
	return registration && registration->identity != 0U &&
		registration->locator != NULL && registration->authority != NULL &&
		registration->release_view != NULL;
}

static int RuntimeProviderRegistrationCurrent(
	const sg_strategy_runtime_provider_registration_t *registration)
{
	return registration && registration->identity != 0U &&
		registration->identity == sg_strategy_runtime_provider.identity;
}

static void RuntimeProviderRegistrationReplace(
	sg_strategy_runtime_target_locator_fn locator, void *locator_context,
	sg_strategy_runtime_target_authority_fn authority, void *authority_context,
	sg_strategy_runtime_target_release_fn release_view, void *release_context)
{
	sg_strategy_runtime_provider_registration_t registration;

	/* Do not wrap an identity and accidentally authenticate an ancient
	 * resolver snapshot.  Once the finite identity space is spent, every
	 * provider operation remains unavailable until process restart. */
	if (sg_strategy_runtime_provider_next_identity == UINT64_MAX)
	{
		memset(&sg_strategy_runtime_provider, 0,
			sizeof(sg_strategy_runtime_provider));
		return;
	}
	memset(&registration, 0, sizeof(registration));
	registration.identity = sg_strategy_runtime_provider_next_identity;
	sg_strategy_runtime_provider_next_identity++;
	if (locator && authority && release_view)
	{
		registration.locator = locator;
		registration.locator_context = locator_context;
		registration.authority = authority;
		registration.authority_context = authority_context;
		registration.release_view = release_view;
		registration.release_context = release_context;
	}
	sg_strategy_runtime_provider = registration;
}

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

static int RuntimeLocalizedPlayerValid(
	const sg_compact_localized_state_t *player)
{
	return player && player->valid == 1U &&
		player->subject.reserved == 0U &&
		player->subject.client_id != UINT32_MAX &&
		player->subject.spawn_generation != 0U &&
		player->rune_identity != 0U && player->topology_revision != 0U &&
		player->frame_sequence != 0U && player->localized_at_ms != 0U &&
		player->location.cell.value != SG_RUNE_COMPACT_INDEX_NONE;
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
	if (!target || !RuntimeLocalizedPlayerValid(target->localized_player) ||
		!view || !view->opaque ||
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
	    !RuntimeLocalizedPlayerValid(request->localized_player) ||
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
	sg_strategy_runtime_target_authority_fn authority, void *authority_context,
	sg_strategy_runtime_target_release_fn release_view,
	void *release_context)
{
	RuntimeProviderRegistrationReplace(locator, locator_context, authority,
		authority_context, release_view, release_context);
}

int SG_StrategyRuntimeTargetProviderAvailable(void)
{
	return RuntimeProviderRegistrationAvailable(&sg_strategy_runtime_provider);
}

int SG_StrategyRuntimePlanResolve(
	const sg_strategy_runtime_plan_request_t *request,
	sg_strategy_caller_plan_t *plan_out)
{
	sg_strategy_caller_plan_t candidate;
	sg_strategy_plan_t compiled;
	sg_strategy_runtime_provider_registration_t registration;
	uint16_t goal_index;
	uint16_t binding_index = 0U;

	if (!plan_out || plan_out->binding_count != 0U || plan_out->release_view ||
	    plan_out->release_context)
		return 0;
	registration = sg_strategy_runtime_provider;
	if (!request || !RuntimeProviderRegistrationAvailable(&registration) ||
	    !RuntimeRequestCompile(request, &compiled))
		return 0;
	memset(&candidate, 0, sizeof(candidate));
	candidate.commitment_id = request->commitment_id;
	candidate.authority = request->authority;
	candidate.spec = request->spec;
	candidate.binding_count = request->execution_count;
	candidate.release_view = registration.release_view;
	candidate.release_context = registration.release_context;
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
			int located;
			int authority_accepted;

			if (binding_index >= candidate.binding_count ||
			    !RuntimeExecutionFor(request, goal->id,
				goal->choices[choice_index].id, &execution))
				goto reject;
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
			located = registration.locator(registration.locator_context, &target,
				&view);
			/* A locator only lends a view.  It transfers no lease until this
			 * exact registration's authority accepts that same opaque object. */
			if (!RuntimeProviderRegistrationCurrent(&registration) || !located ||
			    !view.opaque)
				goto reject;
			authority_accepted = registration.authority(
				registration.authority_context, &target, &view, &binding);
			/* An accepted view belongs to this snapshot's release owner even if
			 * authority changes the live registration before returning. */
			if (!RuntimeProviderRegistrationCurrent(&registration))
			{
				if (authority_accepted)
					registration.release_view(registration.release_context,
						view.opaque);
				goto reject;
			}
			if (!authority_accepted)
				goto reject;
			if (!RuntimeBindingAccepted(&target, &view, &binding))
			{
				/* The authority accepted this lease even though its emitted
				 * binding failed the caller contract.  Attach only the opaque
				 * lease to the rollback plan so every accepted view retires
				 * from one detached callback/context snapshot. */
				candidate.bindings[binding_index].accepted_view = view.opaque;
				binding_index++;
				goto reject;
			}
			candidate.bindings[binding_index] = binding;
			binding_index++;
		}
	}
	if (binding_index != candidate.binding_count ||
	    !RuntimeProviderRegistrationCurrent(&registration))
		goto reject;
	*plan_out = candidate;
	return 1;

reject:
	candidate.binding_count = binding_index;
	SG_StrategyCallerPlanDiscard(&candidate);
	return 0;
}
