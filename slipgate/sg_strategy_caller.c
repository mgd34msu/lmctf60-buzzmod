#include "sg_strategy_caller.h"
#include "sg_strategy_caller_private.h"

#include "sg_authority_entropy.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#define CALLER_OUTPUT_AUTHORITY_NONE UINT8_C(0)
#define CALLER_OUTPUT_AUTHORITY_PROOF UINT8_C(1)
#define CALLER_OUTPUT_AUTHORITY_RECEIPT UINT8_C(2)
#define CALLER_OUTPUT_PROOF_KIND UINT64_C(0)
#define CALLER_OUTPUT_RECEIPT_KIND UINT64_C(1)
#define CALLER_OUTPUT_MAX_ISSUANCE (UINT64_MAX >> 1U)

_Static_assert(sizeof(uint64_t) + sizeof(uint64_t) + 16U <=
	SG_STRATEGY_CALLER_OUTPUT_AUTHORITY_BYTES,
	"strategy output authority storage is too small");

static uint64_t sg_strategy_caller_next_owner_id = UINT64_C(1);

static void CallerOutputAuthorityClear(sg_strategy_caller_t *caller)
{
	if (caller == NULL)
		return;
	memset(caller->output_authority_token, 0,
		sizeof(caller->output_authority_token));
	caller->output_authority_phase = CALLER_OUTPUT_AUTHORITY_NONE;
}

static int CallerBytesNonzero(const uint8_t *bytes, size_t size)
{
	uint8_t combined = 0U;

	if (bytes == NULL)
		return 0;
	for (size_t index = 0U; index < size; index++)
		combined = (uint8_t)(combined | bytes[index]);
	return combined != 0U;
}

static int CallerBytesEqual(const uint8_t *left, const uint8_t *right,
	size_t size)
{
	uint8_t difference = 0U;

	if (left == NULL || right == NULL)
		return 0;
	for (size_t index = 0U; index < size; index++)
		difference = (uint8_t)(difference | (uint8_t)(left[index] ^
			right[index]));
	return difference == 0U;
}

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

static void CallerRetiredPlanRelease(
	const sg_strategy_caller_plan_t *retired)
{
	uint16_t index;

	if (!retired || !retired->release_view)
		return;
	for (index = 0U; index < retired->binding_count &&
	     index < SG_STRATEGY_CALLER_MAX_BINDINGS; index++)
		if (retired->bindings[index].accepted_view)
			retired->release_view(retired->release_context,
				retired->bindings[index].accepted_view);
}

void SG_StrategyCallerPlanDiscard(sg_strategy_caller_plan_t *plan)
{
	sg_strategy_caller_plan_t retired;

	if (!plan)
		return;
	retired = *plan;
	memset(plan, 0, sizeof(*plan));
	CallerRetiredPlanRelease(&retired);
}

void SG_StrategyCallerDestroy(sg_strategy_caller_t *caller)
{
	sg_strategy_caller_plan_t retired;
	int owns_plan;

	if (!caller)
		return;
	memset(&retired, 0, sizeof(retired));
	owns_plan = caller->initialized && caller->has_plan;
	if (owns_plan)
		retired = caller->plan;
	memset(caller, 0, sizeof(*caller));
	if (owns_plan)
		CallerRetiredPlanRelease(&retired);
}

void SG_StrategyCallerOwnerLost(sg_strategy_caller_t *caller)
{
	if (caller)
		memset(caller, 0, sizeof(*caller));
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

static int CallerActivationEqual(const sg_strategy_activation_t *left,
	const sg_strategy_activation_t *right)
{
	return left != NULL && right != NULL &&
		left->plan_id == right->plan_id &&
		left->activation_id == right->activation_id &&
		left->goal_id == right->goal_id;
}

static int CallerInstructionEqual(const sg_strategy_instruction_t *left,
	const sg_strategy_instruction_t *right)
{
	return left != NULL && right != NULL && left->kind == right->kind &&
		left->plan_id == right->plan_id &&
		left->goal_id == right->goal_id &&
		left->choice_index == right->choice_index &&
		CallerActivationEqual(&left->activation, &right->activation) &&
		CallerDestinationEqual(&left->destination, &right->destination) &&
		left->target_id == right->target_id &&
		left->target_generation == right->target_generation &&
		left->field_state == right->field_state &&
		left->cost_to_go.units == right->cost_to_go.units &&
		left->block_reason == right->block_reason &&
		left->destination_wait_reason == right->destination_wait_reason;
}

static int CallerCompactDestinationEqual(
	const sg_rune_compact_destination_t *left,
	const sg_rune_compact_destination_t *right)
{
	uint32_t axis;

	if (left == NULL || right == NULL || left->kind != right->kind)
		return 0;
	switch (left->kind)
	{
	case SG_RUNE_COMPACT_DESTINATION_POINT:
		for (axis = 0U; axis < 3U; axis++)
			if (left->value.point.value[axis] !=
				right->value.point.value[axis])
				return 0;
		return 1;
	case SG_RUNE_COMPACT_DESTINATION_CELL:
		return left->value.cell.value == right->value.cell.value;
	case SG_RUNE_COMPACT_DESTINATION_SURFACE:
		return left->value.surface.value == right->value.surface.value;
	case SG_RUNE_COMPACT_DESTINATION_ITEM:
		return left->value.item.value == right->value.item.value;
	case SG_RUNE_COMPACT_DESTINATION_KIND_COUNT:
	default:
		return 0;
	}
}

static int CallerCompactTargetEqual(
	const sg_rune_compact_field_target_t *left,
	const sg_rune_compact_field_target_t *right)
{
	return left != NULL && right != NULL &&
		left->target_id == right->target_id &&
		left->target_generation == right->target_generation &&
		left->motion == right->motion &&
		CallerDestinationEqual(&left->semantic_destination,
			&right->semantic_destination) &&
		CallerCompactDestinationEqual(&left->destination,
			&right->destination);
}

static int CallerFieldHandleEqual(
	const sg_rune_compact_field_handle_t *left,
	const sg_rune_compact_field_handle_t *right)
{
	return left != NULL && right != NULL &&
		left->service_identity == right->service_identity &&
		left->service_generation == right->service_generation &&
		left->rune_identity == right->rune_identity &&
		left->topology_revision == right->topology_revision &&
		left->target_id == right->target_id &&
		left->target_generation == right->target_generation &&
		left->field_generation == right->field_generation;
}

static int CallerOutputEqual(const sg_strategy_caller_output_t *left,
	const sg_strategy_caller_output_t *right)
{
	return left != NULL && right != NULL &&
		CallerInstructionEqual(&left->instruction, &right->instruction) &&
		left->commitment_id == right->commitment_id &&
		left->plan_id == right->plan_id &&
		left->activation_id == right->activation_id &&
		left->frame_sequence == right->frame_sequence &&
		left->observed_at_ms == right->observed_at_ms &&
		left->life_identity.client_id == right->life_identity.client_id &&
		left->life_identity.reserved == right->life_identity.reserved &&
		left->life_identity.spawn_generation ==
			right->life_identity.spawn_generation &&
		left->role == right->role &&
		left->field_service == right->field_service &&
		CallerCompactTargetEqual(&left->compact_target,
			&right->compact_target) &&
		CallerFieldHandleEqual(&left->field_handle, &right->field_handle);
}

int SG_StrategyCallerFieldObservationFromResult(
	const sg_rune_compact_field_result_t *result,
	sg_strategy_caller_field_observation_t *observation_out)
{
	sg_strategy_caller_field_observation_t observation;

	if (observation_out != NULL)
		memset(observation_out, 0, sizeof(*observation_out));
	if (!result || !observation_out ||
	    result->kind >= SG_RUNE_COMPACT_FIELD_RESULT_KIND_COUNT ||
	    result->current_cell.value == SG_RUNE_COMPACT_INDEX_NONE)
		return 0;
	memset(&observation, 0, sizeof(observation));
	switch (result->kind)
	{
	case SG_RUNE_COMPACT_FIELD_DISCONNECTED:
		observation.kind = SG_STRATEGY_CALLER_FIELD_DISCONNECTED;
		observation.cost_to_go.units =
			SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE;
		break;
	case SG_RUNE_COMPACT_FIELD_LOCAL_DESTINATION:
		observation.kind = SG_STRATEGY_CALLER_FIELD_LOCAL_DESTINATION;
		if (result->value.destination.kind >=
			SG_RUNE_COMPACT_DESTINATION_KIND_COUNT ||
		    result->value.destination.kind ==
			SG_RUNE_COMPACT_DESTINATION_CELL)
			return 0;
		break;
	case SG_RUNE_COMPACT_FIELD_CELL_DESTINATION:
		observation.kind = SG_STRATEGY_CALLER_FIELD_CELL_DESTINATION;
		if (result->value.destination.kind !=
			SG_RUNE_COMPACT_DESTINATION_CELL ||
		    result->value.destination.value.cell.value ==
			SG_RUNE_COMPACT_INDEX_NONE)
			return 0;
		break;
	case SG_RUNE_COMPACT_FIELD_MECHANISMS_REQUIRED:
		observation.kind = SG_STRATEGY_CALLER_FIELD_MECHANISMS_REQUIRED;
		if (result->value.requirements.portal.value ==
			SG_RUNE_COMPACT_INDEX_NONE ||
		    result->value.requirements.mechanisms == NULL ||
		    result->value.requirements.mechanism_count == 0U ||
		    result->value.requirements.state >=
			SG_RUNE_COMPACT_FIELD_MECHANISM_REQUIREMENTS_STATE_COUNT)
			return 0;
		observation.cost_to_go.units =
			SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE;
		break;
	case SG_RUNE_COMPACT_FIELD_BLOCKED_NOW:
		observation.kind = SG_STRATEGY_CALLER_FIELD_BLOCKED_NOW;
		if (result->value.step.kind !=
			SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL ||
		    result->value.step.target_stance >=
			SG_RUNE_COMPACT_FIELD_STANCE_COUNT ||
		    result->value.step.cost_to_go.units !=
			SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE ||
		    result->value.step.next_cost_to_go.units !=
			SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE ||
		    !isinf(result->value.step.value.portal.local_cost) ||
		    result->value.step.value.portal.local_cost < 0.0f ||
		    result->value.step.value.portal.next_cell.value !=
			SG_RUNE_COMPACT_INDEX_NONE ||
		    result->value.step.value.portal.next_portal.value !=
			SG_RUNE_COMPACT_INDEX_NONE)
			return 0;
		observation.cost_to_go.units =
			SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE;
		break;
	case SG_RUNE_COMPACT_FIELD_STEP:
		observation.kind = SG_STRATEGY_CALLER_FIELD_STEP;
		if (result->value.step.kind >=
			SG_RUNE_COMPACT_FIELD_TRANSITION_KIND_COUNT ||
		    result->value.step.target_stance >=
			SG_RUNE_COMPACT_FIELD_STANCE_COUNT ||
		    result->value.step.cost_to_go.units ==
			SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE ||
		    result->value.step.next_cost_to_go.units ==
			SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE ||
		    result->value.step.next_cost_to_go.units >=
			result->value.step.cost_to_go.units)
			return 0;
		if (result->value.step.kind ==
			SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL &&
		    (!isfinite(result->value.step.value.portal.local_cost) ||
		     result->value.step.value.portal.local_cost < 0.0f ||
		     result->value.step.value.portal.next_cell.value ==
			SG_RUNE_COMPACT_INDEX_NONE ||
			     result->value.step.value.portal.next_portal.value ==
				SG_RUNE_COMPACT_INDEX_NONE))
			return 0;
		if (result->value.step.kind ==
			SG_RUNE_COMPACT_FIELD_TRANSITION_DIRECT &&
			(!isfinite(result->value.step.value.direct.local_cost) ||
			 result->value.step.value.direct.local_cost < 0.0f ||
			 result->value.step.value.direct.next_cell.value ==
				SG_RUNE_COMPACT_INDEX_NONE))
			return 0;
		observation.cost_to_go = result->value.step.cost_to_go;
		break;
	case SG_RUNE_COMPACT_FIELD_RESULT_KIND_COUNT:
	default:
		return 0;
	}
	*observation_out = observation;
	return 1;
}

int SG_StrategyCallerFieldObservationValid(
	const sg_strategy_caller_field_observation_t *observation)
{
	if (!observation || observation->kind >=
		SG_STRATEGY_CALLER_FIELD_OBSERVATION_KIND_COUNT)
		return 0;
	switch (observation->kind)
	{
	case SG_STRATEGY_CALLER_FIELD_LOCAL_DESTINATION:
	case SG_STRATEGY_CALLER_FIELD_CELL_DESTINATION:
		return observation->cost_to_go.units == 0U;
	case SG_STRATEGY_CALLER_FIELD_STEP:
		return observation->cost_to_go.units != 0U &&
			observation->cost_to_go.units !=
				SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE;
	case SG_STRATEGY_CALLER_FIELD_DISCONNECTED:
	case SG_STRATEGY_CALLER_FIELD_MECHANISMS_REQUIRED:
	case SG_STRATEGY_CALLER_FIELD_BLOCKED_NOW:
		return observation->cost_to_go.units ==
			SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE;
	case SG_STRATEGY_CALLER_FIELD_OBSERVATION_KIND_COUNT:
	default:
		return 0;
	}
}

static int CallerTargetObservationValid(
	const sg_strategy_caller_target_observation_t *observation)
{
	return observation != NULL &&
		SG_StrategyCallerFieldObservationValid(&observation->field) &&
		observation->target_revision != 0U &&
		observation->observation_revision != 0U &&
		observation->observed_at_ms != 0U;
}

static int CallerLifeIdentityValid(
	const sg_strategy_caller_life_identity_t *identity)
{
	return identity != NULL && identity->client_id != UINT32_MAX &&
		identity->reserved == 0U && identity->spawn_generation != 0U;
}

static int CallerPlanCurrent(const sg_strategy_caller_plan_t *plan)
{
	return plan != NULL && plan->provider_generation != 0U &&
		plan->frame_sequence != 0U && plan->observed_at_ms != 0U &&
		CallerLifeIdentityValid(&plan->life_identity) &&
		plan->frame_capability != NULL && plan->plan_current != NULL &&
		plan->observe_target != NULL && plan->plan_current(plan);
}

static int CallerNext(uint64_t *value)
{
	if (!value || *value == UINT64_MAX)
		return 0;
	(*value)++;
	return *value != 0U;
}

static int CallerLifeFrame(const sg_strategy_caller_t *caller,
	const sg_strategy_caller_life_identity_t *identity, uint8_t alive,
	sg_strategy_life_snapshot_t *life)
{
	uint64_t revision;

	if (!caller || !CallerLifeIdentityValid(identity) || alive > 1U || !life ||
		(caller->subject_known &&
		 caller->subject_client_id != identity->client_id))
		return 0;
	revision = caller->life_revision;
	if (!caller->life_known || caller->life_alive != alive ||
		caller->life_id != identity->spawn_generation)
	{
		if (!CallerNext(&revision))
			return 0;
	}
	memset(life, 0, sizeof(*life));
	life->present = 1U;
	life->alive = alive;
	life->observation_revision = revision;
	life->life_id = identity->spawn_generation;
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
	const sg_strategy_target_choice_t *choice,
	uint64_t at_ms,
	sg_strategy_caller_target_observation_t *observation_out)
{
	sg_strategy_caller_target_observation_t observation;

	/* The runtime bridge has already asked the compact field owner to lease an
	 * opaque view for this exact semantic target. This boundary verifies the
	 * emitted target and the complete compact capability chain. No seed/link
	 * array or legacy dynamics object can satisfy this check. */
	if (observation_out != NULL)
		memset(observation_out, 0, sizeof(*observation_out));
	if (!CallerPlanCurrent(plan) || !binding || !choice || !observation_out ||
	    at_ms == 0U || plan->observed_at_ms != at_ms ||
	    (plan->frame_use_at_ms != 0U &&
	    plan->frame_use_at_ms != at_ms) ||
	    binding->commitment_id != plan->commitment_id ||
	    !CallerAuthorityEqual(&binding->authority, &plan->authority) ||
	    !CallerDestinationEqual(&binding->destination, &choice->destination) ||
	    !binding->accepted_view || !binding->field_service ||
	    binding->compact_target.target_id != binding->target_id ||
	    binding->compact_target.target_generation == 0U ||
	    binding->compact_target.motion >=
		SG_RUNE_COMPACT_FIELD_TARGET_MOTION_COUNT ||
	    binding->field_handle.target_id != binding->target_id ||
	    binding->field_handle.target_generation !=
		binding->compact_target.target_generation ||
	    !plan->observe_target(plan, binding, &observation) ||
	    !CallerTargetObservationValid(&observation) ||
	    observation.target_revision !=
		binding->compact_target.target_generation ||
	    observation.observation_revision != plan->frame_sequence ||
	    observation.observed_at_ms != plan->observed_at_ms)
		return 0;
	*observation_out = observation;
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
			sg_strategy_caller_target_observation_t observation;

			if (!CallerBindingFor(plan, goal->id,
				goal->choices[choice_index].id, &binding) ||
			    !CallerBindingAuthenticated(plan, binding,
				&goal->choices[choice_index], at_ms, &observation))
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
	if (left_compiled.plan_id != right_compiled.plan_id ||
	    left_compiled.compiled_tag != right_compiled.compiled_tag ||
	    left_compiled.goal_count != right_compiled.goal_count)
		return 0;
	for (uint16_t goal_index = 0U;
	     goal_index < left_compiled.goal_count; goal_index++)
	{
		const sg_strategy_goal_t *left_goal =
			&left_compiled.goals[goal_index];
		const sg_strategy_goal_t *right_goal =
			&right_compiled.goals[goal_index];

		if (left_compiled.topological_order[goal_index] !=
				right_compiled.topological_order[goal_index] ||
		    left_goal->id != right_goal->id ||
		    left_goal->kind != right_goal->kind ||
		    left_goal->priority != right_goal->priority ||
		    left_goal->queue_order != right_goal->queue_order ||
		    left_goal->dependency_count != right_goal->dependency_count ||
		    left_goal->condition_count != right_goal->condition_count ||
		    left_goal->choice_count != right_goal->choice_count ||
		    left_goal->unavailable != right_goal->unavailable ||
		    left_goal->failure.try_alternatives !=
				right_goal->failure.try_alternatives ||
		    left_goal->failure.max_attempts_per_choice !=
				right_goal->failure.max_attempts_per_choice ||
		    left_goal->failure.exhausted != right_goal->failure.exhausted ||
		    left_goal->failure.retry_wake.kind !=
				right_goal->failure.retry_wake.kind)
			return 0;
		switch (left_goal->failure.retry_wake.kind)
		{
		case SG_STRATEGY_RETRY_FACT_REVISION:
			if (left_goal->failure.retry_wake.fact.kind !=
					right_goal->failure.retry_wake.fact.kind ||
			    left_goal->failure.retry_wake.fact.subject_id !=
					right_goal->failure.retry_wake.fact.subject_id ||
			    left_goal->failure.retry_wake.fact.team !=
					right_goal->failure.retry_wake.fact.team)
				return 0;
			break;
		case SG_STRATEGY_RETRY_NOT_BEFORE:
			if (left_goal->failure.retry_wake.delay_ms !=
				right_goal->failure.retry_wake.delay_ms)
				return 0;
			break;
		case SG_STRATEGY_RETRY_NONE:
		case SG_STRATEGY_RETRY_NEXT_FRAME:
		case SG_STRATEGY_RETRY_TARGET_REVISION:
			break;
		case SG_STRATEGY_RETRY_WAKE_COUNT:
		default:
			return 0;
		}
		for (uint8_t dependency_index = 0U;
		     dependency_index < left_goal->dependency_count;
		     dependency_index++)
			if (left_goal->dependencies[dependency_index].goal_index !=
					right_goal->dependencies[dependency_index].goal_index ||
			    left_goal->dependencies[dependency_index].accept !=
					right_goal->dependencies[dependency_index].accept)
				return 0;
		for (uint8_t condition_index = 0U;
		     condition_index < left_goal->condition_count; condition_index++)
		{
			const sg_strategy_condition_t *left_condition =
				&left_goal->conditions[condition_index];
			const sg_strategy_condition_t *right_condition =
				&right_goal->conditions[condition_index];

			if (left_condition->kind != right_condition->kind ||
			    left_condition->scope != right_condition->scope)
				return 0;
			switch (left_condition->kind)
			{
			case SG_STRATEGY_CONDITION_FACT_EQUALS:
				if (left_condition->value.fact.key.kind !=
						right_condition->value.fact.key.kind ||
				    left_condition->value.fact.key.subject_id !=
						right_condition->value.fact.key.subject_id ||
				    left_condition->value.fact.key.team !=
						right_condition->value.fact.key.team ||
				    left_condition->value.fact.expected_value !=
						right_condition->value.fact.expected_value)
					return 0;
				break;
			case SG_STRATEGY_CONDITION_TIME_WINDOW:
				if (left_condition->value.time.not_before_ms !=
						right_condition->value.time.not_before_ms ||
				    left_condition->value.time.not_after_ms !=
						right_condition->value.time.not_after_ms)
					return 0;
				break;
			case SG_STRATEGY_CONDITION_KIND_COUNT:
			default:
				return 0;
			}
		}
		for (uint8_t choice_index = 0U;
		     choice_index < left_goal->choice_count; choice_index++)
			if (left_goal->choices[choice_index].id !=
					right_goal->choices[choice_index].id ||
			    !CallerDestinationEqual(
					&left_goal->choices[choice_index].destination,
					&right_goal->choices[choice_index].destination))
				return 0;
	}
	return 1;
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
			sg_strategy_caller_target_observation_t target_observation;
			sg_strategy_destination_observation_t *observation;

			if (count >= SG_STRATEGY_CALLER_MAX_BINDINGS ||
			    !CallerBindingFor(plan, goal->id,
				goal->choices[choice_index].id, &binding) ||
			    !CallerBindingAuthenticated(plan, binding,
				&goal->choices[choice_index], at_ms,
				&target_observation))
				return 0;
			observation = &observations[count];
			memset(observation, 0, sizeof(*observation));
			observation->plan_id = compiled->plan_id;
			observation->goal_id = goal->id;
			observation->target_id = goal->choices[choice_index].id;
			observation->observation_revision =
				target_observation.observation_revision;
			observation->target_generation =
				target_observation.target_revision;
			/* Each query is evidence for this reducer operation only.  A later
			 * operation must pass the owner frame capability and query again. */
			observation->observed_at_ms = at_ms;
			observation->valid_until_ms = at_ms;
			observation->cost_to_go =
				target_observation.field.cost_to_go;
			switch (target_observation.field.kind)
			{
			case SG_STRATEGY_CALLER_FIELD_DISCONNECTED:
				observation->field_state = SG_STRATEGY_FIELD_DISCONNECTED;
				break;
			case SG_STRATEGY_CALLER_FIELD_LOCAL_DESTINATION:
				observation->field_state =
					SG_STRATEGY_FIELD_LOCAL_DESTINATION;
				break;
			case SG_STRATEGY_CALLER_FIELD_CELL_DESTINATION:
				observation->field_state =
					SG_STRATEGY_FIELD_CELL_DESTINATION;
				break;
			case SG_STRATEGY_CALLER_FIELD_MECHANISMS_REQUIRED:
				observation->field_state =
					SG_STRATEGY_FIELD_MECHANISMS_REQUIRED;
				break;
			case SG_STRATEGY_CALLER_FIELD_BLOCKED_NOW:
				observation->field_state = SG_STRATEGY_FIELD_BLOCKED_NOW;
				break;
			case SG_STRATEGY_CALLER_FIELD_STEP:
				observation->field_state = SG_STRATEGY_FIELD_STEP;
				break;
			case SG_STRATEGY_CALLER_FIELD_OBSERVATION_KIND_COUNT:
			default:
				return 0;
			}
			count++;
		}
	}
	*count_out = count;
	return count == plan->binding_count;
}

static const sg_strategy_caller_target_binding_t *CallerActiveBindingInPlan(
	const sg_strategy_caller_t *caller,
	const sg_strategy_caller_plan_t *plan);

static sg_strategy_tactical_block_reason_t CallerFieldBlockReason(
	const sg_strategy_caller_t *caller,
	const sg_strategy_destination_observation_t *observations,
	uint16_t observation_count)
{
	const sg_strategy_caller_target_binding_t *binding =
		CallerActiveBindingInPlan(caller, &caller->plan);
	uint16_t index;

	if (!binding)
		return SG_STRATEGY_BLOCK_NONE;
	for (index = 0U; index < observation_count; index++)
		if (observations[index].goal_id == binding->goal_id &&
			observations[index].target_id == binding->target_id)
		{
			if (observations[index].field_state ==
				SG_STRATEGY_FIELD_MECHANISMS_REQUIRED)
				return SG_STRATEGY_BLOCK_CONTROLLER;
			if (observations[index].field_state ==
				SG_STRATEGY_FIELD_BLOCKED_NOW)
				return SG_STRATEGY_BLOCK_OBSTRUCTION;
			return SG_STRATEGY_BLOCK_NONE;
		}
	return SG_STRATEGY_BLOCK_NONE;
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
	sg_strategy_tactical_block_reason_t effective_block_reason;

	if (!caller || !plan || at_ms == 0U ||
	    block_reason < SG_STRATEGY_BLOCK_NONE ||
	    block_reason >= SG_STRATEGY_BLOCK_REASON_COUNT ||
	    !CallerLifeFrame(caller, &plan->life_identity, alive, &life) ||
	    !CallerDestinationObservations(plan, &caller->reducer.plan, at_ms,
		observations, &observation_count))
		return 0;
	memset(&frame, 0, sizeof(frame));
	frame.at_ms = at_ms;
	frame.life = life;
	frame.destinations = observations;
	frame.destination_count = observation_count;
	effective_block_reason = block_reason;
	if (effective_block_reason == SG_STRATEGY_BLOCK_NONE)
		effective_block_reason = CallerFieldBlockReason(caller, observations,
			observation_count);
	tactical_revision = caller->tactical_revision;
	if (caller->reducer.activation.activation_id != 0U)
	{
		if (!CallerNext(&tactical_revision))
			return 0;
		frame.tactical.present = 1U;
		frame.tactical.blocked =
			effective_block_reason != SG_STRATEGY_BLOCK_NONE;
		frame.tactical.observation_revision = tactical_revision;
		frame.tactical.activation = caller->reducer.activation;
		frame.tactical.reason = effective_block_reason;
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

	if (!caller || !plan ||
	    !CallerLifeFrame(caller, &plan->life_identity, alive, &life))
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
	caller->plan.frame_use_at_ms = at_ms;
	caller->has_plan = 1U;
	caller->next_plan_id = plan_id;
	caller->next_authority_epoch = authority_epoch;
	caller->subject_client_id = plan->life_identity.client_id;
	caller->subject_known = 1U;
	CallerLifeCommit(caller, &life);
	return 1;
}

static const sg_strategy_caller_target_binding_t *CallerActiveBindingInPlan(
	const sg_strategy_caller_t *caller,
	const sg_strategy_caller_plan_t *plan)
{
	const sg_strategy_goal_t *goal;
	int goal_index;
	uint8_t choice_index;

	if (!caller || !plan || !caller->reducer.has_plan ||
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

		if (!CallerBindingFor(plan, goal->id,
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
	    out->instruction.kind != SG_STRATEGY_INSTRUCTION_SUSPENDED &&
	    out->instruction.kind != SG_STRATEGY_INSTRUCTION_WAIT_DESTINATION)
		return;
	if (!CallerPlanCurrent(&caller->plan))
		return;
	if (!CallerBindingFor(&caller->plan, out->instruction.goal_id,
		out->instruction.target_id, &binding))
		return;
	if (!binding)
		return;
	out->commitment_id = caller->plan.commitment_id;
	out->frame_sequence = caller->plan.frame_sequence;
	out->observed_at_ms = caller->plan.observed_at_ms;
	out->life_identity = caller->plan.life_identity;
	out->role = binding->role;
	out->field_service = binding->field_service;
	out->compact_target = binding->compact_target;
	out->field_handle = binding->field_handle;
}

static void CallerOutputAuthorityEncode(uint8_t *bytes,
	const sg_strategy_caller_t *caller, uint64_t kind,
	const uint8_t token[16])
{
	uint64_t issuance_kind = (caller->output_authority_issuance << 1U) | kind;

	memset(bytes, 0, SG_STRATEGY_CALLER_OUTPUT_AUTHORITY_BYTES);
	memcpy(bytes, &caller->output_authority_owner_id,
		sizeof(caller->output_authority_owner_id));
	memcpy(bytes + sizeof(caller->output_authority_owner_id), &issuance_kind,
		sizeof(issuance_kind));
	memcpy(bytes + sizeof(caller->output_authority_owner_id) +
		sizeof(issuance_kind), token, 16U);
}

static int CallerOutputAuthorityMatches(const uint8_t *bytes,
	const sg_strategy_caller_t *caller, uint64_t expected_kind)
{
	uint64_t encoded_owner_id = 0U;
	uint64_t encoded_issuance_kind = 0U;
	uint8_t encoded_token[16];
	uint64_t expected_issuance_kind;

	if (bytes == NULL || caller == NULL)
		return 0;
	expected_issuance_kind = (caller->output_authority_issuance << 1U) |
		expected_kind;
	memcpy(&encoded_owner_id, bytes, sizeof(encoded_owner_id));
	memcpy(&encoded_issuance_kind, bytes + sizeof(encoded_owner_id),
		sizeof(encoded_issuance_kind));
	memcpy(encoded_token, bytes + sizeof(encoded_owner_id) +
		sizeof(encoded_issuance_kind), sizeof(encoded_token));
	return encoded_owner_id == caller->output_authority_owner_id &&
		encoded_issuance_kind == expected_issuance_kind &&
		CallerBytesNonzero(caller->output_authority_token,
			sizeof(caller->output_authority_token)) &&
		CallerBytesEqual(encoded_token, caller->output_authority_token,
			sizeof(encoded_token));
}

int SG_StrategyCallerOutputCurrent(const sg_strategy_caller_t *caller,
	const sg_strategy_caller_output_t *output)
{
	sg_strategy_caller_output_t expected;

	if (caller == NULL || output == NULL || !caller->initialized ||
		!caller->has_plan || !caller->subject_known || !caller->life_known ||
		!caller->life_alive || !caller->reducer.has_plan ||
		caller->plan.frame_use_at_ms != caller->plan.observed_at_ms ||
		caller->life_id != caller->plan.life_identity.spawn_generation ||
		caller->subject_client_id != caller->plan.life_identity.client_id ||
		caller->reducer.life_id !=
			caller->plan.life_identity.spawn_generation ||
		caller->reducer.current_instruction.kind !=
			SG_STRATEGY_INSTRUCTION_EXECUTE ||
		caller->reducer.plan.plan_id == 0U ||
		caller->reducer.activation.activation_id == 0U ||
		!CallerActivationEqual(&caller->reducer.activation,
			&caller->reducer.current_instruction.activation) ||
		!CallerPlanCurrent(&caller->plan))
		return 0;
	CallerOutput(caller, &expected);
	return expected.instruction.kind == SG_STRATEGY_INSTRUCTION_EXECUTE &&
		expected.commitment_id != 0U && expected.field_service != NULL &&
		CallerOutputEqual(&expected, output);
}

int SG_StrategyCallerOutputProofIssue(sg_strategy_caller_t *caller,
	const sg_strategy_caller_output_t *output,
	sg_strategy_caller_output_proof_t *proof_out)
{
	uint8_t token[16];

	if (proof_out != NULL)
		memset(proof_out, 0, sizeof(*proof_out));
	if (caller == NULL || output == NULL || proof_out == NULL)
		return 0;
	CallerOutputAuthorityClear(caller);
	memset(token, 0, sizeof(token));
	if (caller->output_authority_owner_id == 0U ||
		caller->output_authority_issuance == CALLER_OUTPUT_MAX_ISSUANCE ||
		!SG_StrategyCallerOutputCurrent(caller, output) ||
		!SG_AuthorityEntropyFill(token, sizeof(token)) ||
		!CallerBytesNonzero(token, sizeof(token)))
		return 0;
	caller->output_authority_issuance++;
	memcpy(caller->output_authority_token, token, sizeof(token));
	caller->output_authority_phase = CALLER_OUTPUT_AUTHORITY_PROOF;
	CallerOutputAuthorityEncode(proof_out->opaque, caller,
		CALLER_OUTPUT_PROOF_KIND, token);
	return 1;
}

int SG_StrategyCallerOutputProofCurrent(const sg_strategy_caller_t *caller,
	const sg_strategy_caller_output_t *output,
	const sg_strategy_caller_output_proof_t *proof)
{
	return caller != NULL && output != NULL && proof != NULL &&
		caller->output_authority_phase == CALLER_OUTPUT_AUTHORITY_PROOF &&
		CallerOutputAuthorityMatches(proof->opaque, caller,
			CALLER_OUTPUT_PROOF_KIND) &&
		SG_StrategyCallerOutputCurrent(caller, output);
}

int SG_StrategyCallerOutputProofConsume(sg_strategy_caller_t *caller,
	const sg_strategy_caller_output_t *output,
	const sg_strategy_caller_output_proof_t *proof,
	sg_strategy_caller_output_receipt_t *receipt_out)
{
	if (receipt_out != NULL)
		memset(receipt_out, 0, sizeof(*receipt_out));
	if (caller == NULL || output == NULL || proof == NULL ||
		receipt_out == NULL ||
		!SG_StrategyCallerOutputProofCurrent(caller, output, proof))
		return 0;
	caller->output_authority_phase = CALLER_OUTPUT_AUTHORITY_RECEIPT;
	CallerOutputAuthorityEncode(receipt_out->opaque, caller,
		CALLER_OUTPUT_RECEIPT_KIND, caller->output_authority_token);
	return 1;
}

int SG_StrategyCallerOutputReceiptCurrent(const sg_strategy_caller_t *caller,
	const sg_strategy_caller_output_t *output,
	const sg_strategy_caller_output_receipt_t *receipt)
{
	return caller != NULL && output != NULL && receipt != NULL &&
		caller->output_authority_phase == CALLER_OUTPUT_AUTHORITY_RECEIPT &&
		CallerOutputAuthorityMatches(receipt->opaque, caller,
			CALLER_OUTPUT_RECEIPT_KIND) &&
		SG_StrategyCallerOutputCurrent(caller, output);
}

int SG_StrategyCallerOutputReceiptMatchesProof(
	const sg_strategy_caller_t *caller,
	const sg_strategy_caller_output_t *output,
	const sg_strategy_caller_output_proof_t *proof,
	const sg_strategy_caller_output_receipt_t *receipt)
{
	return caller != NULL && output != NULL && proof != NULL &&
		receipt != NULL &&
		SG_StrategyCallerOutputReceiptCurrent(caller, output, receipt) &&
		CallerOutputAuthorityMatches(proof->opaque, caller,
			CALLER_OUTPUT_PROOF_KIND);
}

int SG_StrategyCallerOutputReceiptLineageMatches(
	const sg_strategy_caller_output_proof_t *proof,
	const sg_strategy_caller_output_receipt_t *receipt)
{
	uint64_t proof_owner = 0U;
	uint64_t receipt_owner = 0U;
	uint64_t proof_issuance_kind = 0U;
	uint64_t receipt_issuance_kind = 0U;
	const size_t issuance_offset = sizeof(uint64_t);
	const size_t token_offset = sizeof(uint64_t) + sizeof(uint64_t);

	if (proof == NULL || receipt == NULL)
		return 0;
	memcpy(&proof_owner, proof->opaque, sizeof(proof_owner));
	memcpy(&receipt_owner, receipt->opaque, sizeof(receipt_owner));
	memcpy(&proof_issuance_kind, &proof->opaque[issuance_offset],
		sizeof(proof_issuance_kind));
	memcpy(&receipt_issuance_kind, &receipt->opaque[issuance_offset],
		sizeof(receipt_issuance_kind));
	return proof_owner != 0U && proof_owner == receipt_owner &&
		(proof_issuance_kind & UINT64_C(1)) == CALLER_OUTPUT_PROOF_KIND &&
		(receipt_issuance_kind & UINT64_C(1)) ==
			CALLER_OUTPUT_RECEIPT_KIND &&
		(proof_issuance_kind >> 1U) == (receipt_issuance_kind >> 1U) &&
		CallerBytesNonzero(&proof->opaque[token_offset], 16U) &&
		CallerBytesEqual(&proof->opaque[token_offset],
			&receipt->opaque[token_offset], 16U);
}

int SG_StrategyCallerInit(sg_strategy_caller_t *caller)
{
	uint64_t owner_id;

	if (!caller || sg_strategy_caller_next_owner_id == UINT64_MAX)
		return 0;
	owner_id = sg_strategy_caller_next_owner_id;
	sg_strategy_caller_next_owner_id++;
	memset(caller, 0, sizeof(*caller));
	if (!SG_StrategyStateInit(&caller->reducer))
		return 0;
	caller->output_authority_owner_id = owner_id;
	caller->initialized = 1U;
	return 1;
}

int SG_StrategyCallerSubmit(sg_strategy_caller_t *caller,
	sg_strategy_caller_plan_t *plan, uint8_t alive, uint64_t at_ms,
	sg_strategy_tactical_block_reason_t block_reason,
	sg_strategy_caller_output_t *out)
{
	sg_strategy_plan_t compiled;
	sg_strategy_caller_t candidate;
	sg_strategy_caller_plan_t retired;
	uint64_t validation_plan_id;

	CallerOutputAuthorityClear(caller);
	if (!caller || !caller->initialized || !plan || plan == &caller->plan ||
	    !out || at_ms == 0U ||
	    plan->frame_use_at_ms != 0U ||
	    !CallerAuthorityValid(&plan->authority) || !CallerPlanCurrent(plan) ||
	    (caller->subject_known &&
	     caller->subject_client_id != plan->life_identity.client_id))
		return 0;
	if (caller->reducer.has_plan &&
	    plan->authority.rank < caller->reducer.authority.rank)
	{
		if (!caller->has_plan || !CallerPulsePlan(caller, &caller->plan,
			alive, at_ms, block_reason))
			return 0;
		SG_StrategyCallerPlanDiscard(plan);
		CallerOutput(caller, out);
		return 1;
	}
	validation_plan_id = caller->reducer.has_plan
		? caller->reducer.plan.plan_id : 1U;
	if (!CallerPlanCompile(plan, validation_plan_id, at_ms, &compiled))
		return 0;
	if (caller->has_plan && CallerPlansEquivalent(&caller->plan, plan))
	{
		candidate = *caller;
		if (!CallerPulsePlan(&candidate, plan, alive, at_ms, block_reason))
			return 0;
		retired = caller->plan;
		candidate.plan = *plan;
		candidate.plan.frame_use_at_ms = at_ms;
		*caller = candidate;
		memset(plan, 0, sizeof(*plan));
		CallerRetiredPlanRelease(&retired);
		CallerOutput(caller, out);
		return 1;
	}
	memset(&retired, 0, sizeof(retired));
	if (caller->has_plan)
		retired = caller->plan;
	candidate = *caller;
	if (!CallerReplace(&candidate, plan, alive, at_ms) ||
	    !CallerPulsePlan(&candidate, &candidate.plan, alive, at_ms,
		block_reason))
		return 0;
	*caller = candidate;
	memset(plan, 0, sizeof(*plan));
	CallerRetiredPlanRelease(&retired);
	CallerOutput(caller, out);
	return 1;
}

int SG_StrategyCallerPulse(sg_strategy_caller_t *caller, uint8_t alive,
	uint64_t at_ms, sg_strategy_tactical_block_reason_t block_reason,
	sg_strategy_caller_output_t *out)
{
	CallerOutputAuthorityClear(caller);
	if (!caller || !caller->initialized || !out || at_ms == 0U ||
	    !caller->has_plan || !CallerPulsePlan(caller, &caller->plan, alive,
		at_ms, block_reason))
		return 0;
	CallerOutput(caller, out);
	return 1;
}

int SG_StrategyCallerRetireCurrentLife(sg_strategy_caller_t *caller,
	uint64_t at_ms, sg_strategy_caller_output_t *out)
{
	sg_strategy_life_snapshot_t life;
	sg_strategy_frame_t frame;

	CallerOutputAuthorityClear(caller);
	if (!caller || !caller->initialized || !caller->has_plan || !out ||
		at_ms == 0U || !CallerLifeFrame(caller,
			&caller->plan.life_identity, 0U, &life))
		return 0;
	memset(&frame, 0, sizeof(frame));
	frame.at_ms = at_ms;
	frame.life = life;
	if (!CallerReduce(caller, &frame))
		return 0;
	CallerLifeCommit(caller, &life);
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

	CallerOutputAuthorityClear(caller);
	if (!caller || !caller->initialized || !caller->has_plan || !out ||
	    at_ms == 0U || caller->reducer.activation.activation_id == 0U ||
	    outcome <= SG_STRATEGY_OUTCOME_NONE ||
	    outcome >= SG_STRATEGY_OUTCOME_KIND_COUNT ||
	    (outcome == SG_STRATEGY_OUTCOME_COMPLETED &&
	     failure != SG_STRATEGY_FAILURE_NONE) ||
	    (outcome == SG_STRATEGY_OUTCOME_FAILED &&
	     (failure <= SG_STRATEGY_FAILURE_NONE ||
	      failure >= SG_STRATEGY_FAILURE_REASON_COUNT)) ||
	    !CallerLifeFrame(caller, &caller->plan.life_identity, alive, &life) ||
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

	CallerOutputAuthorityClear(caller);
	if (!caller || !caller->initialized || !authority || !out || at_ms == 0U ||
	    !CallerAuthorityValid(authority) || !caller->reducer.has_plan ||
	    !caller->has_plan || !CallerPlanCurrent(&caller->plan) ||
	    caller->plan.frame_use_at_ms != at_ms ||
	    !CallerAuthorityMatchesState(authority, &caller->reducer) ||
	    !CallerLifeFrame(caller, &caller->plan.life_identity, alive, &life))
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
	sg_strategy_caller_plan_t retired;
	sg_strategy_life_snapshot_t life;
	sg_strategy_frame_t frame;
	uint64_t authority_epoch;

	CallerOutputAuthorityClear(caller);
	if (!caller || !caller->initialized || !authority || !out || at_ms == 0U ||
	    !CallerAuthorityValid(authority) || !caller->reducer.has_plan ||
	    !CallerAuthorityMatchesState(authority, &caller->reducer) ||
	    !CallerLifeFrame(caller, &caller->plan.life_identity, alive, &life))
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
	retired = caller->plan;
	memset(&caller->plan, 0, sizeof(caller->plan));
	caller->has_plan = 0U;
	CallerOutput(caller, out);
	CallerRetiredPlanRelease(&retired);
	return 1;
}
