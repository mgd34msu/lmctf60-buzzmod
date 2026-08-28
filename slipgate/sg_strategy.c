#include "sg_strategy_contract.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

typedef enum strategy_condition_result_e
{
	STRATEGY_CONDITION_WAIT = 0,
	STRATEGY_CONDITION_TRUE,
	STRATEGY_CONDITION_FALSE,
	STRATEGY_CONDITION_EXPIRED
} strategy_condition_result_t;

static int StrategyFactKeyValid(const sg_strategy_fact_key_t *key)
{
	if (!key || key->kind < SG_STRATEGY_FACT_ALIVE ||
	    key->kind >= SG_STRATEGY_FACT_KIND_COUNT || key->team > 2U ||
	    key->reserved[0] != 0U || key->reserved[1] != 0U ||
	    key->reserved[2] != 0U)
		return 0;
	if (key->kind == SG_STRATEGY_FACT_CUSTOM && key->subject_id == 0U)
		return 0;
	return 1;
}

static int StrategyFactKeyEqual(const sg_strategy_fact_key_t *left,
	const sg_strategy_fact_key_t *right)
{
	return left && right && left->kind == right->kind &&
		left->subject_id == right->subject_id && left->team == right->team;
}

static int StrategyActivationEqual(const sg_strategy_activation_t *left,
	const sg_strategy_activation_t *right)
{
	return left && right && left->plan_id == right->plan_id &&
		left->activation_id == right->activation_id &&
		left->goal_id == right->goal_id;
}

static int StrategyActivationEmpty(const sg_strategy_activation_t *activation)
{
	return activation && activation->plan_id == 0U &&
		activation->activation_id == 0U && activation->goal_id == 0U;
}

static int StrategyConditionValid(const sg_strategy_condition_t *condition)
{
	if (!condition || condition->kind < SG_STRATEGY_CONDITION_FACT_EQUALS ||
	    condition->kind >= SG_STRATEGY_CONDITION_KIND_COUNT ||
	    condition->scope < SG_STRATEGY_CONDITION_START_ONLY ||
	    condition->scope >= SG_STRATEGY_CONDITION_SCOPE_COUNT)
		return 0;
	if (condition->kind == SG_STRATEGY_CONDITION_FACT_EQUALS)
		return StrategyFactKeyValid(&condition->value.fact.key);
	return condition->value.time.not_after_ms == 0U ||
		condition->value.time.not_after_ms >=
		condition->value.time.not_before_ms;
}

static int StrategyFailureRuleValid(const sg_strategy_failure_rule_t *rule)
{
	if (!rule || rule->try_alternatives > 1U ||
	    rule->max_attempts_per_choice == 0U ||
	    rule->retry_wake.kind < SG_STRATEGY_RETRY_NONE ||
	    rule->retry_wake.kind >= SG_STRATEGY_RETRY_WAKE_COUNT ||
	    rule->exhausted < SG_STRATEGY_FAILURE_SKIP_GOAL ||
	    rule->exhausted >= SG_STRATEGY_FAILURE_TERMINAL_COUNT)
		return 0;
	if (rule->max_attempts_per_choice > 1U &&
	    rule->retry_wake.kind == SG_STRATEGY_RETRY_NONE)
		return 0;
	if (rule->retry_wake.kind == SG_STRATEGY_RETRY_FACT_REVISION &&
	    !StrategyFactKeyValid(&rule->retry_wake.fact))
		return 0;
	if (rule->retry_wake.kind == SG_STRATEGY_RETRY_NOT_BEFORE)
		return rule->retry_wake.delay_ms != 0U;
	if (rule->retry_wake.delay_ms != 0U)
		return 0;
	return 1;
}

static int StrategyGoalDestinationKindValid(sg_strategy_goal_kind_t goal_kind,
	sg_destination_kind_t destination_kind)
{
	switch (goal_kind)
	{
	case SG_STRATEGY_GOAL_DESTINATION:
		return SG_DestinationKindValid(destination_kind);
	case SG_STRATEGY_GOAL_CAPTURE_FLAG:
	case SG_STRATEGY_GOAL_CARRY_FLAG:
	case SG_STRATEGY_GOAL_RECOVER_FLAG:
		return destination_kind == SG_DESTINATION_FLAG;
	case SG_STRATEGY_GOAL_COLLECT_ITEM:
		return destination_kind == SG_DESTINATION_ITEM ||
			destination_kind == SG_DESTINATION_WEAPON ||
			destination_kind == SG_DESTINATION_ARMOR ||
			destination_kind == SG_DESTINATION_POWERUP;
	case SG_STRATEGY_GOAL_ESCORT_CARRIER:
		return destination_kind == SG_DESTINATION_ESCORT;
	case SG_STRATEGY_GOAL_INTERCEPT_CARRIER:
		return destination_kind == SG_DESTINATION_INTERCEPT;
	case SG_STRATEGY_GOAL_DEFEND_POST:
		return destination_kind == SG_DESTINATION_DEFENSIVE_POST;
	case SG_STRATEGY_GOAL_WAIT:
	case SG_STRATEGY_GOAL_KIND_COUNT:
	default:
		return 0;
	}
}

static int StrategyGoalSpecValid(const sg_strategy_goal_spec_t *goal)
{
	uint8_t index;

	if (!goal || goal->id == 0U || goal->kind < SG_STRATEGY_GOAL_DESTINATION ||
	    goal->kind >= SG_STRATEGY_GOAL_KIND_COUNT ||
	    goal->dependency_count > SG_STRATEGY_MAX_DEPENDENCIES ||
	    goal->condition_count > SG_STRATEGY_MAX_CONDITIONS ||
	    goal->choice_count > SG_STRATEGY_MAX_CHOICES ||
	    goal->unavailable < SG_STRATEGY_UNAVAILABLE_WAIT ||
	    goal->unavailable >= SG_STRATEGY_UNAVAILABLE_ACTION_COUNT ||
	    !StrategyFailureRuleValid(&goal->failure))
		return 0;
	if ((goal->kind == SG_STRATEGY_GOAL_WAIT && goal->choice_count != 0U) ||
	    (goal->kind != SG_STRATEGY_GOAL_WAIT && goal->choice_count == 0U))
		return 0;
	for (index = 0U; index < goal->dependency_count; index++)
		if (goal->dependencies[index].goal_id == 0U ||
		    goal->dependencies[index].accept <
			SG_STRATEGY_DEPENDENCY_SUCCESS ||
		    goal->dependencies[index].accept >=
			SG_STRATEGY_DEPENDENCY_ACCEPT_COUNT)
			return 0;
	for (index = 0U; index < goal->condition_count; index++)
		if (!StrategyConditionValid(&goal->conditions[index]))
			return 0;
	for (index = 0U; index < goal->choice_count; index++)
		if (goal->choices[index].id == 0U ||
		    !SG_DestinationRefValid(&goal->choices[index].destination) ||
		    !StrategyGoalDestinationKindValid(goal->kind,
			goal->choices[index].destination.kind))
			return 0;
	return 1;
}

static void StrategyCompileError(sg_strategy_compile_error_t *error,
	sg_strategy_compile_error_code_t code, uint16_t goal_index,
	uint16_t dependency_index)
{
	if (!error)
		return;
	error->code = code;
	error->goal_index = goal_index;
	error->dependency_index = dependency_index;
}

static int StrategyFindSpecGoal(const sg_strategy_plan_spec_t *spec,
	sg_strategy_goal_id_t id)
{
	uint16_t index;

	for (index = 0U; index < spec->goal_count; index++)
		if (spec->goals[index].id == id)
			return (int)index;
	return -1;
}

static void StrategyCopyDestinationRef(sg_destination_ref_t *out,
	const sg_destination_ref_t *source)
{
	memset(out, 0, sizeof(*out));
	out->kind = source->kind;
	switch (source->kind)
	{
	case SG_DESTINATION_FLAG:
		out->value.flag.team = source->value.flag.team;
		out->value.flag.location = source->value.flag.location;
		break;
	case SG_DESTINATION_ITEM:
	case SG_DESTINATION_WEAPON:
	case SG_DESTINATION_ARMOR:
	case SG_DESTINATION_POWERUP:
		out->value.item.item_id = source->value.item.item_id;
		break;
	case SG_DESTINATION_CARRIER:
	case SG_DESTINATION_ESCORT:
	case SG_DESTINATION_INTERCEPT:
		out->value.carrier.client_id = source->value.carrier.client_id;
		out->value.carrier.team = source->value.carrier.team;
		out->value.carrier.selector = source->value.carrier.selector;
		break;
	case SG_DESTINATION_DEFENSIVE_POST:
		out->value.post.region_id = source->value.post.region_id;
		break;
	case SG_DESTINATION_LEARNED_POINT:
	case SG_DESTINATION_WAYPOINT:
		out->value.point.point_id = source->value.point.point_id;
		break;
	case SG_DESTINATION_KIND_COUNT:
	default:
		break;
	}
}

static void StrategyCopyCondition(sg_strategy_condition_t *out,
	const sg_strategy_condition_t *source)
{
	memset(out, 0, sizeof(*out));
	out->kind = source->kind;
	out->scope = source->scope;
	if (source->kind == SG_STRATEGY_CONDITION_FACT_EQUALS)
	{
		out->value.fact.key.kind = source->value.fact.key.kind;
		out->value.fact.key.subject_id = source->value.fact.key.subject_id;
		out->value.fact.key.team = source->value.fact.key.team;
		out->value.fact.expected_value = source->value.fact.expected_value;
	}
	else
	{
		out->value.time.not_before_ms = source->value.time.not_before_ms;
		out->value.time.not_after_ms = source->value.time.not_after_ms;
	}
}

static void StrategyCopyFailureRule(sg_strategy_failure_rule_t *out,
	const sg_strategy_failure_rule_t *source)
{
	memset(out, 0, sizeof(*out));
	out->try_alternatives = source->try_alternatives;
	out->max_attempts_per_choice = source->max_attempts_per_choice;
	out->retry_wake.kind = source->retry_wake.kind;
	out->retry_wake.fact.kind = source->retry_wake.fact.kind;
	out->retry_wake.fact.subject_id = source->retry_wake.fact.subject_id;
	out->retry_wake.fact.team = source->retry_wake.fact.team;
	out->retry_wake.delay_ms = source->retry_wake.delay_ms;
	out->exhausted = source->exhausted;
}

int SG_StrategyPlanCompile(const sg_strategy_plan_spec_t *spec,
	sg_strategy_plan_t *out, sg_strategy_compile_error_t *error)
{
	sg_strategy_plan_t candidate_plan;
	sg_strategy_plan_t *compiled = &candidate_plan;
	uint16_t indegree[SG_STRATEGY_MAX_GOALS] = { 0U };
	uint8_t emitted[SG_STRATEGY_MAX_GOALS] = { 0U };
	uint16_t index;
	uint16_t position;

	StrategyCompileError(error, SG_STRATEGY_COMPILE_OK,
		SG_STRATEGY_NO_INDEX, SG_STRATEGY_NO_INDEX);
	if (!spec || !out || spec->plan_id == 0U || spec->goal_count == 0U)
	{
		StrategyCompileError(error, SG_STRATEGY_COMPILE_INVALID_ARGUMENT,
			SG_STRATEGY_NO_INDEX, SG_STRATEGY_NO_INDEX);
		return 0;
	}
	if (spec->goal_count > SG_STRATEGY_MAX_GOALS)
	{
		StrategyCompileError(error, SG_STRATEGY_COMPILE_CAPACITY,
			SG_STRATEGY_NO_INDEX, SG_STRATEGY_NO_INDEX);
		return 0;
	}
	for (index = 0U; index < spec->goal_count; index++)
	{
		uint16_t other;
		uint8_t choice;

		if (!StrategyGoalSpecValid(&spec->goals[index]))
		{
			StrategyCompileError(error, SG_STRATEGY_COMPILE_INVALID_GOAL,
				index, SG_STRATEGY_NO_INDEX);
			return 0;
		}
		for (other = 0U; other < index; other++)
			if (spec->goals[other].id == spec->goals[index].id)
			{
				StrategyCompileError(error,
					SG_STRATEGY_COMPILE_DUPLICATE_GOAL_ID, index,
					SG_STRATEGY_NO_INDEX);
				return 0;
			}
		for (choice = 0U; choice < spec->goals[index].choice_count;
		     choice++)
		{
			uint16_t prior_goal;
			for (prior_goal = 0U; prior_goal <= index; prior_goal++)
			{
				uint8_t limit = prior_goal == index ? choice :
					spec->goals[prior_goal].choice_count;
				uint8_t prior_choice;
				for (prior_choice = 0U; prior_choice < limit; prior_choice++)
					if (spec->goals[prior_goal].choices[prior_choice].id ==
					    spec->goals[index].choices[choice].id)
					{
						StrategyCompileError(error,
							SG_STRATEGY_COMPILE_DUPLICATE_TARGET_ID,
							index, choice);
						return 0;
					}
			}
		}
	}

	memset(compiled, 0, sizeof(*compiled));
	compiled->plan_id = spec->plan_id;
	compiled->compiled_tag = SG_STRATEGY_PLAN_COMPILED_TAG;
	compiled->goal_count = spec->goal_count;
	for (index = 0U; index < spec->goal_count; index++)
	{
		const sg_strategy_goal_spec_t *source = &spec->goals[index];
		sg_strategy_goal_t *goal = &compiled->goals[index];
		uint8_t dependency;
		uint8_t condition;
		uint8_t choice;

		goal->id = source->id;
		goal->kind = source->kind;
		goal->priority = source->priority;
		goal->queue_order = index;
		goal->dependency_count = source->dependency_count;
		goal->condition_count = source->condition_count;
		goal->choice_count = source->choice_count;
		goal->unavailable = source->unavailable;
		StrategyCopyFailureRule(&goal->failure, &source->failure);
		for (dependency = 0U; dependency < source->dependency_count;
		     dependency++)
		{
			int found = StrategyFindSpecGoal(spec,
				source->dependencies[dependency].goal_id);
			if (found < 0)
			{
				StrategyCompileError(error,
					SG_STRATEGY_COMPILE_MISSING_DEPENDENCY, index,
					dependency);
				return 0;
			}
			if ((uint16_t)found == index)
			{
				StrategyCompileError(error,
					SG_STRATEGY_COMPILE_SELF_DEPENDENCY, index,
					dependency);
				return 0;
			}
			goal->dependencies[dependency].goal_index = (uint16_t)found;
			goal->dependencies[dependency].accept =
				source->dependencies[dependency].accept;
			indegree[index]++;
		}
		for (condition = 0U; condition < source->condition_count;
		     condition++)
			StrategyCopyCondition(&goal->conditions[condition],
				&source->conditions[condition]);
		for (choice = 0U; choice < source->choice_count; choice++)
		{
			goal->choices[choice].id = source->choices[choice].id;
			StrategyCopyDestinationRef(&goal->choices[choice].destination,
				&source->choices[choice].destination);
		}
	}

	for (position = 0U; position < spec->goal_count; position++)
	{
		uint16_t selected = SG_STRATEGY_NO_INDEX;
		uint16_t candidate;

		for (candidate = 0U; candidate < spec->goal_count; candidate++)
			if (!emitted[candidate] && indegree[candidate] == 0U)
			{
				selected = candidate;
				break;
			}
		if (selected == SG_STRATEGY_NO_INDEX)
		{
			StrategyCompileError(error, SG_STRATEGY_COMPILE_CYCLE,
				SG_STRATEGY_NO_INDEX, SG_STRATEGY_NO_INDEX);
			return 0;
		}
		emitted[selected] = 1U;
		compiled->topological_order[position] = selected;
		for (candidate = 0U; candidate < spec->goal_count; candidate++)
		{
			uint8_t dependency;
			if (emitted[candidate])
				continue;
			for (dependency = 0U;
			     dependency < compiled->goals[candidate].dependency_count;
			     dependency++)
				if (compiled->goals[candidate].dependencies[dependency].goal_index ==
				    selected)
					indegree[candidate]--;
		}
	}
	*out = candidate_plan;
	return 1;
}

static int StrategyPlanValid(const sg_strategy_plan_t *plan)
{
	uint8_t emitted[SG_STRATEGY_MAX_GOALS] = { 0U };
	uint16_t indegree[SG_STRATEGY_MAX_GOALS] = { 0U };
	uint16_t index;

	if (!plan || plan->plan_id == 0U ||
	    plan->compiled_tag != SG_STRATEGY_PLAN_COMPILED_TAG ||
	    plan->goal_count == 0U || plan->goal_count > SG_STRATEGY_MAX_GOALS)
		return 0;
	for (index = 0U; index < plan->goal_count; index++)
	{
		const sg_strategy_goal_t *goal = &plan->goals[index];
		uint8_t dependency;
		uint8_t condition;
		uint8_t choice;
		uint16_t other;

		if (goal->id == 0U || goal->kind < SG_STRATEGY_GOAL_DESTINATION ||
		    goal->kind >= SG_STRATEGY_GOAL_KIND_COUNT ||
		    goal->queue_order != index ||
		    goal->dependency_count > SG_STRATEGY_MAX_DEPENDENCIES ||
		    goal->condition_count > SG_STRATEGY_MAX_CONDITIONS ||
		    goal->choice_count > SG_STRATEGY_MAX_CHOICES ||
		    (goal->kind == SG_STRATEGY_GOAL_WAIT && goal->choice_count != 0U) ||
		    (goal->kind != SG_STRATEGY_GOAL_WAIT && goal->choice_count == 0U) ||
		    goal->unavailable < SG_STRATEGY_UNAVAILABLE_WAIT ||
		    goal->unavailable >= SG_STRATEGY_UNAVAILABLE_ACTION_COUNT ||
		    !StrategyFailureRuleValid(&goal->failure))
			return 0;
		for (other = 0U; other < index; other++)
			if (plan->goals[other].id == goal->id)
				return 0;
		for (dependency = 0U; dependency < goal->dependency_count;
		     dependency++)
		{
			if (goal->dependencies[dependency].goal_index >= plan->goal_count ||
			    goal->dependencies[dependency].goal_index == index ||
			    goal->dependencies[dependency].accept <
				SG_STRATEGY_DEPENDENCY_SUCCESS ||
			    goal->dependencies[dependency].accept >=
				SG_STRATEGY_DEPENDENCY_ACCEPT_COUNT)
				return 0;
			indegree[index]++;
		}
		for (condition = 0U; condition < goal->condition_count; condition++)
			if (!StrategyConditionValid(&goal->conditions[condition]))
				return 0;
		for (choice = 0U; choice < goal->choice_count; choice++)
		{
			uint16_t prior_goal;
			if (goal->choices[choice].id == 0U ||
			    !SG_DestinationRefValid(&goal->choices[choice].destination) ||
			    !StrategyGoalDestinationKindValid(goal->kind,
				goal->choices[choice].destination.kind))
				return 0;
			for (prior_goal = 0U; prior_goal <= index; prior_goal++)
			{
				uint8_t limit = prior_goal == index ? choice :
					plan->goals[prior_goal].choice_count;
				uint8_t prior_choice;
				for (prior_choice = 0U; prior_choice < limit; prior_choice++)
					if (plan->goals[prior_goal].choices[prior_choice].id ==
					    goal->choices[choice].id)
						return 0;
			}
		}
	}
	for (index = 0U; index < plan->goal_count; index++)
	{
		uint16_t selected = SG_STRATEGY_NO_INDEX;
		uint16_t candidate;
		for (candidate = 0U; candidate < plan->goal_count; candidate++)
			if (!emitted[candidate] && indegree[candidate] == 0U)
			{
				selected = candidate;
				break;
			}
		if (selected == SG_STRATEGY_NO_INDEX ||
		    plan->topological_order[index] != selected)
			return 0;
		emitted[selected] = 1U;
		for (candidate = 0U; candidate < plan->goal_count; candidate++)
		{
			uint8_t dependency;
			if (emitted[candidate])
				continue;
			for (dependency = 0U;
			     dependency < plan->goals[candidate].dependency_count;
			     dependency++)
				if (plan->goals[candidate].dependencies[dependency].goal_index ==
				    selected)
					indegree[candidate]--;
		}
	}
	return 1;
}

static void StrategyCopyPlan(sg_strategy_plan_t *out,
	const sg_strategy_plan_t *source)
{
	uint16_t index;

	memset(out, 0, sizeof(*out));
	out->plan_id = source->plan_id;
	out->compiled_tag = source->compiled_tag;
	out->goal_count = source->goal_count;
	for (index = 0U; index < source->goal_count; index++)
	{
		const sg_strategy_goal_t *source_goal = &source->goals[index];
		sg_strategy_goal_t *goal = &out->goals[index];
		uint8_t dependency;
		uint8_t condition;
		uint8_t choice;

		out->topological_order[index] = source->topological_order[index];
		goal->id = source_goal->id;
		goal->kind = source_goal->kind;
		goal->priority = source_goal->priority;
		goal->queue_order = source_goal->queue_order;
		goal->dependency_count = source_goal->dependency_count;
		goal->condition_count = source_goal->condition_count;
		goal->choice_count = source_goal->choice_count;
		goal->unavailable = source_goal->unavailable;
		StrategyCopyFailureRule(&goal->failure, &source_goal->failure);
		for (dependency = 0U; dependency < goal->dependency_count; dependency++)
		{
			goal->dependencies[dependency].goal_index =
				source_goal->dependencies[dependency].goal_index;
			goal->dependencies[dependency].accept =
				source_goal->dependencies[dependency].accept;
		}
		for (condition = 0U; condition < goal->condition_count; condition++)
			StrategyCopyCondition(&goal->conditions[condition],
				&source_goal->conditions[condition]);
		for (choice = 0U; choice < goal->choice_count; choice++)
		{
			goal->choices[choice].id = source_goal->choices[choice].id;
			StrategyCopyDestinationRef(&goal->choices[choice].destination,
				&source_goal->choices[choice].destination);
		}
	}
}

int SG_StrategyStateInit(sg_strategy_state_t *state)
{
	if (!state)
		return 0;
	memset(state, 0, sizeof(*state));
	state->revision = 1U;
	state->authority.rank = SG_STRATEGY_AUTHORITY_AUTONOMOUS;
	state->authority.principal.kind = SG_STRATEGY_PRINCIPAL_NONE;
	state->next_activation_id = 1U;
	state->current_instruction.kind = SG_STRATEGY_INSTRUCTION_EMPTY;
	return 1;
}

static int StrategyPrincipalValid(const sg_strategy_authority_stamp_t *stamp)
{
	if (!stamp || stamp->rank < SG_STRATEGY_AUTHORITY_AUTONOMOUS ||
	    stamp->rank > SG_STRATEGY_AUTHORITY_EMERGENCY || stamp->epoch == 0U ||
	    stamp->principal.kind <= SG_STRATEGY_PRINCIPAL_NONE ||
	    stamp->principal.kind >= SG_STRATEGY_PRINCIPAL_KIND_COUNT)
		return 0;
	switch (stamp->rank)
	{
	case SG_STRATEGY_AUTHORITY_AUTONOMOUS:
		return stamp->principal.kind == SG_STRATEGY_PRINCIPAL_AUTONOMOUS;
	case SG_STRATEGY_AUTHORITY_TEAM:
		return stamp->principal.kind == SG_STRATEGY_PRINCIPAL_TEAM;
	case SG_STRATEGY_AUTHORITY_HUMAN:
		return stamp->principal.kind == SG_STRATEGY_PRINCIPAL_HUMAN;
	case SG_STRATEGY_AUTHORITY_EMERGENCY:
		return stamp->principal.kind == SG_STRATEGY_PRINCIPAL_EMERGENCY;
	default:
		return 0;
	}
}

static int StrategyAuthorityDominates(
	const sg_strategy_authority_stamp_t *offered,
	const sg_strategy_authority_stamp_t *current)
{
	return StrategyPrincipalValid(offered) && current &&
		offered->epoch > current->epoch && offered->rank >= current->rank;
}

static int StrategyReleaseAuthorized(
	const sg_strategy_authority_stamp_t *offered,
	const sg_strategy_authority_stamp_t *current)
{
	return StrategyPrincipalValid(offered) && current &&
		offered->epoch > current->epoch && offered->rank == current->rank &&
		offered->principal.kind == current->principal.kind &&
		offered->principal.id == current->principal.id;
}

static int StrategyFindPlanGoal(const sg_strategy_plan_t *plan,
	sg_strategy_goal_id_t id)
{
	uint16_t index;

	for (index = 0U; index < plan->goal_count; index++)
		if (plan->goals[index].id == id)
			return (int)index;
	return -1;
}

static int StrategyFindChoice(const sg_strategy_goal_t *goal,
	sg_strategy_target_id_t id)
{
	uint8_t index;

	for (index = 0U; index < goal->choice_count; index++)
		if (goal->choices[index].id == id)
			return (int)index;
	return -1;
}

static int StrategyHandleMatchesRef(const sg_destination_handle_t *handle,
	const sg_destination_ref_t *ref)
{
	return SG_DestinationHandleValid(handle) && ref &&
		handle->kind == ref->kind;
}

static int StrategyFactObservationEqual(
	const sg_strategy_fact_observation_t *left,
	const sg_strategy_fact_observation_t *right)
{
	return StrategyFactKeyEqual(&left->key, &right->key) &&
		left->value == right->value &&
		left->observation_revision == right->observation_revision &&
		left->observed_at_ms == right->observed_at_ms &&
		left->valid_until_ms == right->valid_until_ms;
}

static int StrategyDestinationHandleEqual(const sg_destination_handle_t *left,
	const sg_destination_handle_t *right)
{
	uint8_t index;

	if (!left || !right || left->id != right->id ||
	    left->generation != right->generation || left->kind != right->kind ||
	    left->motion != right->motion || left->valid != right->valid ||
	    left->pose.phase.phase_id != right->pose.phase.phase_id ||
	    left->pose.phase.cell_id != right->pose.phase.cell_id ||
	    left->pose.sample_time_ms != right->pose.sample_time_ms ||
	    left->pose.region_id != right->pose.region_id)
		return 0;
	for (index = 0U; index < 3U; index++)
		if (left->pose.position[index] != right->pose.position[index] ||
		    left->pose.velocity[index] != right->pose.velocity[index])
			return 0;
	return 1;
}

static void StrategyCopyDestinationHandle(sg_destination_handle_t *out,
	const sg_destination_handle_t *source)
{
	uint8_t index;

	memset(out, 0, sizeof(*out));
	out->id = source->id;
	out->generation = source->generation;
	out->kind = source->kind;
	out->motion = source->motion;
	out->valid = source->valid;
	out->pose.phase.phase_id = source->pose.phase.phase_id;
	out->pose.phase.cell_id = source->pose.phase.cell_id;
	for (index = 0U; index < 3U; index++)
	{
		out->pose.position[index] = source->pose.position[index];
		out->pose.velocity[index] = source->pose.velocity[index];
	}
	out->pose.sample_time_ms = source->pose.sample_time_ms;
	out->pose.region_id = source->pose.region_id;
}

static int StrategyDestinationObservationEqual(
	const sg_strategy_destination_observation_t *left,
	const sg_strategy_destination_observation_t *right)
{
	int common = left->plan_id == right->plan_id &&
		left->goal_id == right->goal_id &&
		left->target_id == right->target_id &&
		left->observation_revision == right->observation_revision &&
		left->pose_revision == right->pose_revision &&
		left->observed_at_ms == right->observed_at_ms &&
		left->valid_until_ms == right->valid_until_ms &&
		left->status == right->status && left->cost_ms == right->cost_ms;
	return common && (left->status == SG_STRATEGY_DESTINATION_UNOBSERVED ||
		StrategyDestinationHandleEqual(&left->handle, &right->handle));
}

static int StrategyFindFactRecord(const sg_strategy_state_t *state,
	const sg_strategy_fact_key_t *key)
{
	uint16_t index;

	for (index = 0U; index < state->fact_count; index++)
		if (state->facts[index].occupied &&
		    StrategyFactKeyEqual(&state->facts[index].observation.key, key))
			return (int)index;
	return -1;
}

static int StrategyFactObservationValid(
	const sg_strategy_fact_observation_t *observation, uint64_t at_ms)
{
	return observation && StrategyFactKeyValid(&observation->key) &&
		observation->observation_revision != 0U &&
		observation->observed_at_ms != 0U &&
		observation->observed_at_ms <= at_ms &&
		observation->valid_until_ms >= at_ms;
}

static int StrategyDestinationObservationValid(
	const sg_strategy_destination_observation_t *observation,
	const sg_strategy_plan_t *plan, uint64_t at_ms)
{
	int goal_index;
	int choice_index;
	const sg_strategy_target_choice_t *choice;

	if (!observation || !plan || observation->plan_id != plan->plan_id ||
	    observation->observation_revision == 0U ||
	    observation->observed_at_ms == 0U ||
	    observation->observed_at_ms > at_ms ||
	    observation->valid_until_ms < at_ms ||
	    observation->status < SG_STRATEGY_DESTINATION_UNOBSERVED ||
	    observation->status >= SG_STRATEGY_DESTINATION_STATUS_COUNT)
		return 0;
	goal_index = StrategyFindPlanGoal(plan, observation->goal_id);
	if (goal_index < 0)
		return 0;
	choice_index = StrategyFindChoice(&plan->goals[(uint16_t)goal_index],
		observation->target_id);
	if (choice_index < 0)
		return 0;
	choice = &plan->goals[(uint16_t)goal_index].choices[(uint8_t)choice_index];
	if (observation->status == SG_STRATEGY_DESTINATION_UNOBSERVED)
		return observation->cost_ms == SG_DESTINATION_FIELD_INF &&
			observation->pose_revision == 0U && observation->handle.valid == 0U;
	if (observation->cost_ms != SG_DESTINATION_FIELD_INF &&
	    observation->status == SG_STRATEGY_DESTINATION_UNREACHABLE)
		return 0;
	if (observation->status == SG_STRATEGY_DESTINATION_REACHABLE &&
	    observation->cost_ms >= SG_DESTINATION_FIELD_INF)
		return 0;
	return observation->pose_revision != 0U &&
		StrategyHandleMatchesRef(&observation->handle, &choice->destination);
}

static sg_strategy_reduce_result_t StrategyPreflight(
	const sg_strategy_state_t *state, const sg_strategy_frame_t *frame)
{
	const sg_strategy_plan_t *plan;
	uint16_t index;

	if (!state || !frame || state->revision == 0U || frame->sequence == 0U ||
	    frame->expected_revision == 0U || frame->at_ms == 0U)
		return SG_STRATEGY_REDUCE_REJECTED_INVALID;
	plan = state->has_plan ? &state->plan : NULL;
	if (frame->sequence < state->last_frame_sequence ||
	    frame->expected_revision != state->revision ||
	    frame->at_ms < state->last_frame_at_ms)
		return SG_STRATEGY_REDUCE_REJECTED_STALE;
	if (frame->fact_count > SG_STRATEGY_MAX_FACTS ||
	    (frame->fact_count != 0U && !frame->facts) ||
	    frame->destination_count >
		SG_STRATEGY_MAX_GOALS * SG_STRATEGY_MAX_CHOICES ||
	    (frame->destination_count != 0U && !frame->destinations) ||
	    frame->directive.kind < SG_STRATEGY_DIRECTIVE_NONE ||
	    frame->directive.kind >= SG_STRATEGY_DIRECTIVE_KIND_COUNT)
		return SG_STRATEGY_REDUCE_REJECTED_INVALID;
	if (frame->directive.kind == SG_STRATEGY_DIRECTIVE_REPLACE)
	{
		if (!frame->directive.replacement ||
		    !StrategyPlanValid(frame->directive.replacement))
			return SG_STRATEGY_REDUCE_REJECTED_INVALID;
		if (!StrategyAuthorityDominates(&frame->directive.stamp,
		    &state->authority))
			return SG_STRATEGY_REDUCE_REJECTED_AUTHORITY;
		plan = frame->directive.replacement;
	}
	else if (frame->directive.kind == SG_STRATEGY_DIRECTIVE_CANCEL)
	{
		if (frame->directive.replacement || !state->has_plan)
			return SG_STRATEGY_REDUCE_REJECTED_INVALID;
		if (!StrategyAuthorityDominates(&frame->directive.stamp,
		    &state->authority))
			return SG_STRATEGY_REDUCE_REJECTED_AUTHORITY;
	}
	else if (frame->directive.kind == SG_STRATEGY_DIRECTIVE_RELEASE)
	{
		if (frame->directive.replacement)
			return SG_STRATEGY_REDUCE_REJECTED_INVALID;
		if (!StrategyReleaseAuthorized(&frame->directive.stamp,
		    &state->authority))
			return SG_STRATEGY_REDUCE_REJECTED_AUTHORITY;
	}
	else if (frame->directive.replacement)
		return SG_STRATEGY_REDUCE_REJECTED_INVALID;
	if (frame->directive.kind != SG_STRATEGY_DIRECTIVE_NONE &&
	    (frame->goal_outcome.present || frame->tactical.present))
		return SG_STRATEGY_REDUCE_REJECTED_INVALID;

	if (frame->life.present)
	{
		if (frame->life.alive > 1U ||
		    frame->life.observation_revision == 0U || frame->life.life_id == 0U)
			return SG_STRATEGY_REDUCE_REJECTED_INVALID;
		if (state->life_known && frame->life.observation_revision <
		    state->life_observation_revision)
			return SG_STRATEGY_REDUCE_REJECTED_STALE;
		if (state->life_known && frame->life.observation_revision ==
		    state->life_observation_revision &&
		    (frame->life.alive != state->life_alive ||
		     frame->life.life_id != state->life_id))
			return SG_STRATEGY_REDUCE_REJECTED_INVALID;
	}
	if (frame->life.present && !frame->life.alive &&
	    frame->goal_outcome.present)
		return SG_STRATEGY_REDUCE_REJECTED_INVALID;

	for (index = 0U; index < frame->fact_count; index++)
	{
		uint16_t other;
		int existing;
		if (!StrategyFactObservationValid(&frame->facts[index], frame->at_ms))
			return SG_STRATEGY_REDUCE_REJECTED_INVALID;
		for (other = 0U; other < index; other++)
			if (StrategyFactKeyEqual(&frame->facts[index].key,
			    &frame->facts[other].key))
				return SG_STRATEGY_REDUCE_REJECTED_INVALID;
		existing = StrategyFindFactRecord(state, &frame->facts[index].key);
		if (existing >= 0)
		{
			const sg_strategy_fact_observation_t *prior =
				&state->facts[(uint16_t)existing].observation;
			if (frame->facts[index].observation_revision <
			    prior->observation_revision)
				return SG_STRATEGY_REDUCE_REJECTED_STALE;
			if (frame->facts[index].observation_revision ==
			    prior->observation_revision &&
			    !StrategyFactObservationEqual(&frame->facts[index], prior))
				return SG_STRATEGY_REDUCE_REJECTED_INVALID;
		}
	}
	if (frame->destination_count != 0U && !plan)
		return SG_STRATEGY_REDUCE_REJECTED_INVALID;
	for (index = 0U; index < frame->destination_count; index++)
	{
		uint16_t other;
		int goal_index;
		int choice_index;
		if (!StrategyDestinationObservationValid(&frame->destinations[index],
		    plan, frame->at_ms))
			return SG_STRATEGY_REDUCE_REJECTED_INVALID;
		for (other = 0U; other < index; other++)
			if (frame->destinations[index].goal_id ==
			    frame->destinations[other].goal_id &&
			    frame->destinations[index].target_id ==
			    frame->destinations[other].target_id)
				return SG_STRATEGY_REDUCE_REJECTED_INVALID;
		if (frame->directive.kind == SG_STRATEGY_DIRECTIVE_REPLACE)
			continue;
		goal_index = StrategyFindPlanGoal(&state->plan,
			frame->destinations[index].goal_id);
		choice_index = StrategyFindChoice(&state->plan.goals[(uint16_t)goal_index],
			frame->destinations[index].target_id);
		if (state->goals[(uint16_t)goal_index]
		    .choices[(uint8_t)choice_index].observed)
		{
			const sg_strategy_choice_runtime_t *prior =
				&state->goals[(uint16_t)goal_index]
				 .choices[(uint8_t)choice_index];
			if (frame->destinations[index].observation_revision <
			    prior->observation_revision)
				return SG_STRATEGY_REDUCE_REJECTED_STALE;
			if (frame->destinations[index].observation_revision ==
			    prior->observation_revision)
			{
				sg_strategy_destination_observation_t saved;
				memset(&saved, 0, sizeof(saved));
				saved.plan_id = state->plan.plan_id;
				saved.goal_id = state->plan.goals[(uint16_t)goal_index].id;
				saved.target_id = state->plan.goals[(uint16_t)goal_index]
					.choices[(uint8_t)choice_index].id;
				saved.observation_revision = prior->observation_revision;
				saved.pose_revision = prior->pose_revision;
				saved.observed_at_ms = prior->observed_at_ms;
				saved.valid_until_ms = prior->valid_until_ms;
				saved.status = prior->status;
				saved.cost_ms = prior->cost_ms;
				saved.handle = prior->handle;
				if (!StrategyDestinationObservationEqual(
				    &frame->destinations[index], &saved))
					return SG_STRATEGY_REDUCE_REJECTED_INVALID;
			}
		}
	}

	if (frame->goal_outcome.present)
	{
		if (!state->has_plan || StrategyActivationEmpty(&state->activation) ||
		    !StrategyActivationEqual(&frame->goal_outcome.activation,
			&state->activation) ||
		    frame->goal_outcome.kind <= SG_STRATEGY_OUTCOME_NONE ||
		    frame->goal_outcome.kind >= SG_STRATEGY_OUTCOME_KIND_COUNT ||
		    (frame->goal_outcome.kind == SG_STRATEGY_OUTCOME_COMPLETED &&
		     frame->goal_outcome.failure != SG_STRATEGY_FAILURE_NONE) ||
		    (frame->goal_outcome.kind == SG_STRATEGY_OUTCOME_FAILED &&
		     (frame->goal_outcome.failure <= SG_STRATEGY_FAILURE_NONE ||
		      frame->goal_outcome.failure >= SG_STRATEGY_FAILURE_REASON_COUNT)))
			return SG_STRATEGY_REDUCE_REJECTED_INVALID;
		if (frame->tactical.present && frame->tactical.blocked)
			return SG_STRATEGY_REDUCE_REJECTED_INVALID;
	}
	if (!StrategyActivationEmpty(&state->activation) &&
	    frame->directive.kind == SG_STRATEGY_DIRECTIVE_NONE)
	{
		if (!frame->tactical.present ||
		    !StrategyActivationEqual(&frame->tactical.activation,
			&state->activation))
			return SG_STRATEGY_REDUCE_REJECTED_INVALID;
	}
	if (frame->tactical.present)
	{
		if (StrategyActivationEmpty(&state->activation) ||
		    frame->tactical.blocked > 1U ||
		    frame->tactical.observation_revision == 0U ||
		    frame->tactical.reason < SG_STRATEGY_BLOCK_NONE ||
		    frame->tactical.reason >= SG_STRATEGY_BLOCK_REASON_COUNT ||
		    (frame->tactical.blocked &&
		     frame->tactical.reason == SG_STRATEGY_BLOCK_NONE) ||
		    (!frame->tactical.blocked &&
		     frame->tactical.reason != SG_STRATEGY_BLOCK_NONE))
			return SG_STRATEGY_REDUCE_REJECTED_INVALID;
		if (!StrategyActivationEmpty(&state->activation) &&
		    frame->tactical.observation_revision <
		    state->suspension.observation_revision)
			return SG_STRATEGY_REDUCE_REJECTED_STALE;
		if (!StrategyActivationEmpty(&state->activation) &&
		    frame->tactical.observation_revision ==
		    state->suspension.observation_revision &&
		    ((state->suspension.active &&
		      (!frame->tactical.blocked || frame->tactical.reason !=
		       state->suspension.reason)) ||
		     (!state->suspension.active && frame->tactical.blocked)))
			return SG_STRATEGY_REDUCE_REJECTED_INVALID;
	}
	return SG_STRATEGY_REDUCE_APPLIED;
}

static int StrategyEffect(sg_strategy_state_t *state,
	sg_strategy_reduction_t *out, uint64_t at_ms,
	sg_strategy_effect_kind_t kind, sg_strategy_goal_id_t goal_id,
	uint8_t choice_index, sg_strategy_failure_reason_t failure)
{
	sg_strategy_history_effect_t *effect;

	if (out->effect_count >= SG_STRATEGY_MAX_EFFECTS ||
	    state->history_sequence == UINT64_MAX)
		return 0;
	effect = &out->effects[out->effect_count++];
	memset(effect, 0, sizeof(*effect));
	state->history_sequence++;
	effect->sequence = state->history_sequence;
	effect->at_ms = at_ms;
	effect->kind = kind;
	effect->goal_id = goal_id;
	effect->choice_index = choice_index;
	effect->failure = failure;
	return 1;
}

static void StrategyClearActivation(sg_strategy_state_t *state)
{
	memset(&state->activation, 0, sizeof(state->activation));
	memset(&state->suspension, 0, sizeof(state->suspension));
}

static int StrategyApplyDirective(sg_strategy_state_t *state,
	const sg_strategy_frame_t *frame, sg_strategy_reduction_t *out)
{
	uint16_t index;

	switch (frame->directive.kind)
	{
	case SG_STRATEGY_DIRECTIVE_NONE:
		return 1;
	case SG_STRATEGY_DIRECTIVE_REPLACE:
		StrategyCopyPlan(&state->plan, frame->directive.replacement);
		state->has_plan = 1U;
		state->cancelled = 0U;
		memset(&state->authority, 0, sizeof(state->authority));
		state->authority.rank = frame->directive.stamp.rank;
		state->authority.principal.kind = frame->directive.stamp.principal.kind;
		state->authority.principal.id = frame->directive.stamp.principal.id;
		state->authority.epoch = frame->directive.stamp.epoch;
		memset(state->goals, 0, sizeof(state->goals));
		StrategyClearActivation(state);
		memset(&state->current_instruction, 0,
			sizeof(state->current_instruction));
		state->current_instruction.kind = SG_STRATEGY_INSTRUCTION_EMPTY;
		for (index = 0U; index < state->plan.goal_count; index++)
		{
			state->goals[index].phase = SG_STRATEGY_GOAL_PENDING;
			state->goals[index].selected_choice = SG_STRATEGY_NO_CHOICE;
		}
		return StrategyEffect(state, out, frame->at_ms,
			SG_STRATEGY_EFFECT_PLAN_REPLACED, 0U,
			SG_STRATEGY_NO_CHOICE, SG_STRATEGY_FAILURE_NONE);
	case SG_STRATEGY_DIRECTIVE_CANCEL:
		memset(&state->authority, 0, sizeof(state->authority));
		state->authority.rank = frame->directive.stamp.rank;
		state->authority.principal.kind = frame->directive.stamp.principal.kind;
		state->authority.principal.id = frame->directive.stamp.principal.id;
		state->authority.epoch = frame->directive.stamp.epoch;
		state->cancelled = 1U;
		StrategyClearActivation(state);
		if (!StrategyEffect(state, out, frame->at_ms,
			SG_STRATEGY_EFFECT_PLAN_CANCELLED, 0U,
			SG_STRATEGY_NO_CHOICE, SG_STRATEGY_FAILURE_NONE))
			return 0;
		for (index = 0U; index < state->plan.goal_count; index++)
		{
			sg_strategy_goal_runtime_t *runtime = &state->goals[index];
			uint8_t selected = runtime->selected_choice;

			if (runtime->phase != SG_STRATEGY_GOAL_PENDING &&
			    runtime->phase != SG_STRATEGY_GOAL_ACTIVE &&
			    runtime->phase != SG_STRATEGY_GOAL_RETRY_WAIT)
				continue;
			runtime->phase = SG_STRATEGY_GOAL_CANCELLED;
			runtime->selected_choice = SG_STRATEGY_NO_CHOICE;
			runtime->resume_after_life = 0U;
			runtime->last_transition_at_ms = frame->at_ms;
			memset(&runtime->retry, 0, sizeof(runtime->retry));
			if (!StrategyEffect(state, out, frame->at_ms,
			    SG_STRATEGY_EFFECT_GOAL_CANCELLED,
			    state->plan.goals[index].id, selected,
			    SG_STRATEGY_FAILURE_NONE))
				return 0;
		}
		return 1;
	case SG_STRATEGY_DIRECTIVE_RELEASE:
		state->authority.rank = SG_STRATEGY_AUTHORITY_AUTONOMOUS;
		state->authority.principal.kind = SG_STRATEGY_PRINCIPAL_NONE;
		state->authority.principal.id = 0U;
		state->authority.epoch = frame->directive.stamp.epoch;
		return StrategyEffect(state, out, frame->at_ms,
			SG_STRATEGY_EFFECT_AUTHORITY_RELEASED, 0U,
			SG_STRATEGY_NO_CHOICE, SG_STRATEGY_FAILURE_NONE);
	case SG_STRATEGY_DIRECTIVE_KIND_COUNT:
	default:
		return 0;
	}
}

static int StrategyApplyFacts(sg_strategy_state_t *state,
	const sg_strategy_frame_t *frame)
{
	uint16_t index;

	for (index = 0U; index < frame->fact_count; index++)
	{
		int found = StrategyFindFactRecord(state, &frame->facts[index].key);
		uint16_t slot;
		if (found >= 0)
		{
			slot = (uint16_t)found;
			if (state->facts[slot].observation.observation_revision ==
			    frame->facts[index].observation_revision)
				continue;
		}
		else
		{
			if (state->fact_count >= SG_STRATEGY_MAX_FACTS)
				return 0;
			slot = state->fact_count++;
			state->facts[slot].occupied = 1U;
		}
		memset(&state->facts[slot].observation, 0,
			sizeof(state->facts[slot].observation));
		state->facts[slot].observation.key.kind = frame->facts[index].key.kind;
		state->facts[slot].observation.key.subject_id =
			frame->facts[index].key.subject_id;
		state->facts[slot].observation.key.team = frame->facts[index].key.team;
		state->facts[slot].observation.value = frame->facts[index].value;
		state->facts[slot].observation.observation_revision =
			frame->facts[index].observation_revision;
		state->facts[slot].observation.observed_at_ms =
			frame->facts[index].observed_at_ms;
		state->facts[slot].observation.valid_until_ms =
			frame->facts[index].valid_until_ms;
	}
	return 1;
}

static int StrategyApplyDestinations(sg_strategy_state_t *state,
	const sg_strategy_frame_t *frame)
{
	uint16_t index;

	for (index = 0U; index < frame->destination_count; index++)
	{
		const sg_strategy_destination_observation_t *observation =
			&frame->destinations[index];
		int goal_found = StrategyFindPlanGoal(&state->plan, observation->goal_id);
		int choice_found = StrategyFindChoice(
			&state->plan.goals[(uint16_t)goal_found], observation->target_id);
		sg_strategy_choice_runtime_t *choice =
			&state->goals[(uint16_t)goal_found]
			 .choices[(uint8_t)choice_found];

		if (choice->observed && choice->observation_revision ==
		    observation->observation_revision)
			continue;
		choice->observed = 1U;
		choice->observation_revision = observation->observation_revision;
		choice->pose_revision = observation->pose_revision;
		choice->observed_at_ms = observation->observed_at_ms;
		choice->valid_until_ms = observation->valid_until_ms;
		choice->status = observation->status;
		choice->cost_ms = observation->cost_ms;
		memset(&choice->handle, 0, sizeof(choice->handle));
		if (observation->status != SG_STRATEGY_DESTINATION_UNOBSERVED)
			StrategyCopyDestinationHandle(&choice->handle,
				&observation->handle);
	}
	return 1;
}

static strategy_condition_result_t StrategyEvaluateCondition(
	const sg_strategy_state_t *state, const sg_strategy_condition_t *condition,
	uint64_t at_ms)
{
	if (condition->kind == SG_STRATEGY_CONDITION_TIME_WINDOW)
	{
		if (at_ms < condition->value.time.not_before_ms)
			return STRATEGY_CONDITION_WAIT;
		if (condition->value.time.not_after_ms != 0U &&
		    at_ms > condition->value.time.not_after_ms)
			return STRATEGY_CONDITION_EXPIRED;
		return STRATEGY_CONDITION_TRUE;
	}
	{
		int found = StrategyFindFactRecord(state, &condition->value.fact.key);
		const sg_strategy_fact_observation_t *fact;
		if (found < 0)
			return STRATEGY_CONDITION_WAIT;
		fact = &state->facts[(uint16_t)found].observation;
		if (fact->observed_at_ms > at_ms || fact->valid_until_ms < at_ms)
			return STRATEGY_CONDITION_WAIT;
		return fact->value == condition->value.fact.expected_value ?
			STRATEGY_CONDITION_TRUE : STRATEGY_CONDITION_FALSE;
	}
}

static strategy_condition_result_t StrategyEvaluateConditions(
	const sg_strategy_state_t *state, const sg_strategy_goal_t *goal,
	sg_strategy_condition_scope_t scope, uint64_t at_ms)
{
	uint8_t index;
	strategy_condition_result_t aggregate = STRATEGY_CONDITION_TRUE;

	for (index = 0U; index < goal->condition_count; index++)
	{
		strategy_condition_result_t result;
		if (goal->conditions[index].scope != scope)
			continue;
		result = StrategyEvaluateCondition(state, &goal->conditions[index], at_ms);
		if (result == STRATEGY_CONDITION_EXPIRED ||
		    result == STRATEGY_CONDITION_FALSE)
			return result;
		if (result == STRATEGY_CONDITION_WAIT)
			aggregate = result;
	}
	return aggregate;
}

static int StrategyGoalSettled(sg_strategy_goal_phase_t phase)
{
	return phase == SG_STRATEGY_GOAL_SUCCEEDED ||
		phase == SG_STRATEGY_GOAL_SKIPPED ||
		phase == SG_STRATEGY_GOAL_FAILED ||
		phase == SG_STRATEGY_GOAL_CANCELLED;
}

static int StrategyDependenciesReady(const sg_strategy_state_t *state,
	uint16_t goal_index, int *impossible)
{
	const sg_strategy_goal_t *goal = &state->plan.goals[goal_index];
	uint8_t index;

	*impossible = 0;
	for (index = 0U; index < goal->dependency_count; index++)
	{
		const sg_strategy_dependency_t *dependency = &goal->dependencies[index];
		sg_strategy_goal_phase_t phase =
			state->goals[dependency->goal_index].phase;
		if (dependency->accept == SG_STRATEGY_DEPENDENCY_SUCCESS)
		{
			if (phase == SG_STRATEGY_GOAL_SUCCEEDED)
				continue;
			if (StrategyGoalSettled(phase))
				*impossible = 1;
			return 0;
		}
		if (!StrategyGoalSettled(phase))
			return 0;
	}
	return 1;
}

static int StrategyRetryReady(const sg_strategy_state_t *state,
	uint16_t goal_index, const sg_strategy_frame_t *frame)
{
	const sg_strategy_goal_t *goal = &state->plan.goals[goal_index];
	const sg_strategy_goal_runtime_t *runtime = &state->goals[goal_index];
	const sg_strategy_retry_record_t *retry = &runtime->retry;

	switch (retry->wake.kind)
	{
	case SG_STRATEGY_RETRY_NEXT_FRAME:
		return frame->sequence > retry->after_sequence;
	case SG_STRATEGY_RETRY_TARGET_REVISION:
	{
		uint8_t index;
		uint8_t limit = goal->failure.try_alternatives ?
			goal->choice_count : 1U;

		for (index = 0U; index < limit; index++)
			if (runtime->choices[index].attempts <
			    goal->failure.max_attempts_per_choice &&
			    runtime->choices[index].observation_revision >
			    retry->target_baseline_revisions[index])
				return 1;
		return 0;
	}
	case SG_STRATEGY_RETRY_FACT_REVISION:
	{
		int found = StrategyFindFactRecord(state, &retry->wake.fact);
		return found >= 0 && state->facts[(uint16_t)found]
			.observation.observation_revision >
			retry->fact_baseline_revision;
	}
	case SG_STRATEGY_RETRY_NOT_BEFORE:
		return frame->at_ms >= retry->not_before_ms;
	case SG_STRATEGY_RETRY_NONE:
	case SG_STRATEGY_RETRY_WAKE_COUNT:
	default:
		return 0;
	}
}

static int StrategyAnyChoiceCapacity(const sg_strategy_goal_t *goal,
	const sg_strategy_goal_runtime_t *runtime)
{
	uint8_t index;
	uint8_t limit = goal->failure.try_alternatives ? goal->choice_count : 1U;

	for (index = 0U; index < limit; index++)
		if (runtime->choices[index].attempts <
		    goal->failure.max_attempts_per_choice)
			return 1;
	return 0;
}

static int StrategyChoiceUsable(const sg_strategy_choice_runtime_t *choice,
	uint64_t at_ms)
{
	return choice->observed &&
		choice->status == SG_STRATEGY_DESTINATION_REACHABLE &&
		choice->observed_at_ms <= at_ms && choice->valid_until_ms >= at_ms;
}

static int StrategyHasFreshAlternative(const sg_strategy_goal_t *goal,
	const sg_strategy_goal_runtime_t *runtime, uint8_t failed_choice,
	uint64_t at_ms)
{
	uint8_t index;
	if (!goal->failure.try_alternatives)
		return 0;
	for (index = 0U; index < goal->choice_count; index++)
		if (index != failed_choice && runtime->choices[index].attempts <
		    goal->failure.max_attempts_per_choice &&
		    StrategyChoiceUsable(&runtime->choices[index], at_ms) &&
		    (failed_choice >= goal->choice_count ||
		     runtime->choices[index].attempts <
		     runtime->choices[failed_choice].attempts))
			return 1;
	return 0;
}

static int StrategyFinishFailure(sg_strategy_state_t *state,
	uint16_t goal_index, sg_strategy_failure_reason_t reason,
	const sg_strategy_frame_t *frame, sg_strategy_reduction_t *out)
{
	const sg_strategy_goal_t *goal = &state->plan.goals[goal_index];
	sg_strategy_goal_runtime_t *runtime = &state->goals[goal_index];
	uint8_t failed_choice = runtime->selected_choice;

	StrategyClearActivation(state);
	runtime->last_outcome = SG_STRATEGY_OUTCOME_FAILED;
	runtime->last_failure = reason;
	runtime->last_transition_at_ms = frame->at_ms;
	if (StrategyHasFreshAlternative(goal, runtime, failed_choice,
	    frame->at_ms))
	{
		runtime->phase = SG_STRATEGY_GOAL_PENDING;
		runtime->selected_choice = SG_STRATEGY_NO_CHOICE;
		return 1;
	}
	if (StrategyAnyChoiceCapacity(goal, runtime) &&
	    goal->failure.retry_wake.kind != SG_STRATEGY_RETRY_NONE)
	{
		int fact;
		runtime->phase = SG_STRATEGY_GOAL_RETRY_WAIT;
		runtime->selected_choice = SG_STRATEGY_NO_CHOICE;
		runtime->retry_count++;
		memset(&runtime->retry, 0, sizeof(runtime->retry));
		runtime->retry.wake = goal->failure.retry_wake;
		runtime->retry.after_sequence = frame->sequence;
		if (goal->failure.retry_wake.kind ==
		    SG_STRATEGY_RETRY_TARGET_REVISION)
		{
			uint8_t index;

			for (index = 0U; index < goal->choice_count; index++)
				runtime->retry.target_baseline_revisions[index] =
					runtime->choices[index].observation_revision;
		}
		else if (goal->failure.retry_wake.kind ==
		    SG_STRATEGY_RETRY_FACT_REVISION)
		{
			fact = StrategyFindFactRecord(state,
				&goal->failure.retry_wake.fact);
			if (fact >= 0)
				runtime->retry.fact_baseline_revision =
					state->facts[(uint16_t)fact]
					.observation.observation_revision;
		}
		else if (goal->failure.retry_wake.kind ==
		    SG_STRATEGY_RETRY_NOT_BEFORE)
		{
			if (UINT64_MAX - frame->at_ms <
			    goal->failure.retry_wake.delay_ms)
				return 0;
			runtime->retry.not_before_ms = frame->at_ms +
				goal->failure.retry_wake.delay_ms;
		}
		return StrategyEffect(state, out, frame->at_ms,
			SG_STRATEGY_EFFECT_GOAL_RETRY_WAIT, goal->id, failed_choice,
			reason);
	}
	runtime->selected_choice = SG_STRATEGY_NO_CHOICE;
	if (goal->failure.exhausted == SG_STRATEGY_FAILURE_SKIP_GOAL)
	{
		runtime->phase = SG_STRATEGY_GOAL_SKIPPED;
		return StrategyEffect(state, out, frame->at_ms,
			SG_STRATEGY_EFFECT_GOAL_SKIPPED, goal->id, failed_choice,
			reason);
	}
	runtime->phase = SG_STRATEGY_GOAL_FAILED;
	return StrategyEffect(state, out, frame->at_ms,
		SG_STRATEGY_EFFECT_GOAL_FAILED, goal->id, failed_choice, reason);
}

static int StrategyFinishTerminalFailure(sg_strategy_state_t *state,
	uint16_t goal_index, sg_strategy_failure_reason_t reason,
	const sg_strategy_frame_t *frame, sg_strategy_reduction_t *out)
{
	const sg_strategy_goal_t *goal = &state->plan.goals[goal_index];
	sg_strategy_goal_runtime_t *runtime = &state->goals[goal_index];
	uint8_t selected = runtime->selected_choice;

	StrategyClearActivation(state);
	runtime->selected_choice = SG_STRATEGY_NO_CHOICE;
	runtime->last_outcome = SG_STRATEGY_OUTCOME_FAILED;
	runtime->last_failure = reason;
	runtime->last_transition_at_ms = frame->at_ms;
	if (goal->failure.exhausted == SG_STRATEGY_FAILURE_SKIP_GOAL)
	{
		runtime->phase = SG_STRATEGY_GOAL_SKIPPED;
		return StrategyEffect(state, out, frame->at_ms,
			SG_STRATEGY_EFFECT_GOAL_SKIPPED, goal->id, selected, reason);
	}
	runtime->phase = SG_STRATEGY_GOAL_FAILED;
	return StrategyEffect(state, out, frame->at_ms,
		SG_STRATEGY_EFFECT_GOAL_FAILED, goal->id, selected, reason);
}

static int StrategyApplyTactical(sg_strategy_state_t *state,
	const sg_strategy_frame_t *frame, sg_strategy_reduction_t *out)
{
	int goal_found;
	sg_strategy_goal_runtime_t *runtime;

	if (!frame->tactical.present)
		return 1;
	if (frame->tactical.observation_revision ==
	    state->suspension.observation_revision)
		return 1;
	goal_found = StrategyFindPlanGoal(&state->plan, state->activation.goal_id);
	if (goal_found < 0)
		return 0;
	runtime = &state->goals[(uint16_t)goal_found];
	state->suspension.observation_revision =
		frame->tactical.observation_revision;
	if (frame->tactical.blocked)
	{
		if (!state->suspension.active)
		{
			state->suspension.active = 1U;
			state->suspension.activation = state->activation;
			state->suspension.reason = frame->tactical.reason;
			return StrategyEffect(state, out, frame->at_ms,
				SG_STRATEGY_EFFECT_TACTICAL_SUSPENDED,
				state->activation.goal_id, runtime->selected_choice,
				SG_STRATEGY_FAILURE_NONE);
		}
		state->suspension.reason = frame->tactical.reason;
		return 1;
	}
	if (state->suspension.active)
	{
		state->suspension.active = 0U;
		memset(&state->suspension.activation, 0,
			sizeof(state->suspension.activation));
		state->suspension.reason = SG_STRATEGY_BLOCK_NONE;
		return StrategyEffect(state, out, frame->at_ms,
			SG_STRATEGY_EFFECT_TACTICAL_RESUMED,
			state->activation.goal_id, runtime->selected_choice,
			SG_STRATEGY_FAILURE_NONE);
	}
	return 1;
}

static int StrategyApplyOutcome(sg_strategy_state_t *state,
	const sg_strategy_frame_t *frame, sg_strategy_reduction_t *out)
{
	int found;
	sg_strategy_goal_runtime_t *runtime;

	if (!frame->goal_outcome.present)
		return 1;
	found = StrategyFindPlanGoal(&state->plan,
		frame->goal_outcome.activation.goal_id);
	if (found < 0)
		return 0;
	runtime = &state->goals[(uint16_t)found];
	if (frame->goal_outcome.kind == SG_STRATEGY_OUTCOME_COMPLETED)
	{
		uint8_t selected = runtime->selected_choice;
		runtime->phase = SG_STRATEGY_GOAL_SUCCEEDED;
		runtime->last_outcome = SG_STRATEGY_OUTCOME_COMPLETED;
		runtime->last_failure = SG_STRATEGY_FAILURE_NONE;
		runtime->completed_at_ms = frame->at_ms;
		runtime->last_transition_at_ms = frame->at_ms;
		StrategyClearActivation(state);
		return StrategyEffect(state, out, frame->at_ms,
			SG_STRATEGY_EFFECT_GOAL_COMPLETED,
			state->plan.goals[(uint16_t)found].id, selected,
			SG_STRATEGY_FAILURE_NONE);
	}
	return StrategyFinishFailure(state, (uint16_t)found,
		frame->goal_outcome.failure, frame, out);
}

static int StrategyApplyLife(sg_strategy_state_t *state,
	const sg_strategy_frame_t *frame, sg_strategy_reduction_t *out)
{
	uint8_t was_alive;
	int found;

	if (!frame->life.present ||
	    (state->life_known && frame->life.observation_revision ==
	     state->life_observation_revision))
		return 1;
	was_alive = state->life_alive;
	state->life_known = 1U;
	state->life_alive = frame->life.alive;
	state->life_observation_revision = frame->life.observation_revision;
	state->life_id = frame->life.life_id;
	if (!frame->life.alive && was_alive &&
	    !StrategyActivationEmpty(&state->activation))
	{
		found = StrategyFindPlanGoal(&state->plan, state->activation.goal_id);
		if (found < 0)
			return 0;
		state->goals[(uint16_t)found].phase = SG_STRATEGY_GOAL_PENDING;
		state->goals[(uint16_t)found].resume_after_life = 1U;
		state->goals[(uint16_t)found].last_transition_at_ms = frame->at_ms;
		StrategyClearActivation(state);
		return StrategyEffect(state, out, frame->at_ms,
			SG_STRATEGY_EFFECT_LIFE_RETIRED,
			state->plan.goals[(uint16_t)found].id,
			state->goals[(uint16_t)found].selected_choice,
			SG_STRATEGY_FAILURE_NONE);
	}
	return 1;
}

static int StrategySelectReachable(const sg_strategy_goal_t *goal,
	const sg_strategy_goal_runtime_t *runtime, uint64_t at_ms)
{
	uint8_t index;
	uint8_t limit = goal->failure.try_alternatives ? goal->choice_count : 1U;
	int selected = -1;
	uint8_t fewest_attempts = UINT8_MAX;
	uint32_t best_cost = SG_DESTINATION_FIELD_INF;

	for (index = 0U; index < limit; index++)
		if (runtime->choices[index].attempts <
		    goal->failure.max_attempts_per_choice &&
		    StrategyChoiceUsable(&runtime->choices[index], at_ms) &&
		    (selected < 0 || runtime->choices[index].attempts < fewest_attempts ||
		     (runtime->choices[index].attempts == fewest_attempts &&
		      runtime->choices[index].cost_ms < best_cost)))
		{
			selected = (int)index;
			fewest_attempts = runtime->choices[index].attempts;
			best_cost = runtime->choices[index].cost_ms;
		}
	return selected;
}

static int StrategyAllEligibleObserved(const sg_strategy_goal_t *goal,
	const sg_strategy_goal_runtime_t *runtime, uint64_t at_ms)
{
	uint8_t index;
	uint8_t limit = goal->failure.try_alternatives ? goal->choice_count : 1U;
	int has_capacity = 0;

	for (index = 0U; index < limit; index++)
	{
		const sg_strategy_choice_runtime_t *choice = &runtime->choices[index];
		if (choice->attempts >= goal->failure.max_attempts_per_choice)
			continue;
		has_capacity = 1;
		if (!choice->observed || choice->observed_at_ms > at_ms ||
		    choice->valid_until_ms < at_ms ||
		    choice->status == SG_STRATEGY_DESTINATION_UNOBSERVED)
			return 0;
	}
	return has_capacity;
}

static int StrategyFirstChoiceCapacity(const sg_strategy_goal_t *goal,
	const sg_strategy_goal_runtime_t *runtime)
{
	uint8_t index;
	uint8_t limit = goal->failure.try_alternatives ? goal->choice_count : 1U;
	int selected = -1;
	uint8_t fewest_attempts = UINT8_MAX;
	for (index = 0U; index < limit; index++)
		if (runtime->choices[index].attempts <
		    goal->failure.max_attempts_per_choice &&
		    runtime->choices[index].attempts < fewest_attempts)
		{
			selected = (int)index;
			fewest_attempts = runtime->choices[index].attempts;
		}
	return selected;
}

static int StrategyActivate(sg_strategy_state_t *state, uint16_t goal_index,
	uint8_t choice_index, uint64_t at_ms, sg_strategy_reduction_t *out)
{
	sg_strategy_goal_runtime_t *runtime = &state->goals[goal_index];
	const sg_strategy_goal_t *goal = &state->plan.goals[goal_index];

	if (state->next_activation_id == 0U ||
	    state->next_activation_id == UINT64_MAX)
		return 0;
	runtime->phase = SG_STRATEGY_GOAL_ACTIVE;
	runtime->selected_choice = choice_index;
	runtime->activated_at_ms = at_ms;
	runtime->last_transition_at_ms = at_ms;
	if (!runtime->resume_after_life)
	{
		if (runtime->attempt_count == UINT16_MAX ||
		    runtime->choices[choice_index].attempts == UINT8_MAX)
			return 0;
		runtime->attempt_count++;
		runtime->choices[choice_index].attempts++;
	}
	runtime->resume_after_life = 0U;
	state->activation.plan_id = state->plan.plan_id;
	state->activation.goal_id = goal->id;
	state->activation.activation_id = state->next_activation_id++;
	return StrategyEffect(state, out, at_ms,
		SG_STRATEGY_EFFECT_GOAL_ACTIVATED, goal->id, choice_index,
		SG_STRATEGY_FAILURE_NONE);
}

static void StrategyInstructionBase(sg_strategy_instruction_t *instruction,
	sg_strategy_instruction_kind_t kind, const sg_strategy_state_t *state)
{
	memset(instruction, 0, sizeof(*instruction));
	instruction->kind = kind;
	instruction->plan_id = state->has_plan ? state->plan.plan_id : 0U;
	instruction->choice_index = SG_STRATEGY_NO_CHOICE;
}

static sg_strategy_destination_wait_reason_t StrategyChoiceWaitReason(
	const sg_strategy_choice_runtime_t *choice, uint64_t at_ms)
{
	if (!choice->observed ||
	    choice->status == SG_STRATEGY_DESTINATION_UNOBSERVED)
		return SG_STRATEGY_DESTINATION_WAIT_UNOBSERVED;
	if (choice->observed_at_ms > at_ms || choice->valid_until_ms < at_ms)
		return SG_STRATEGY_DESTINATION_WAIT_STALE;
	if (choice->status == SG_STRATEGY_DESTINATION_UNREACHABLE)
		return SG_STRATEGY_DESTINATION_WAIT_UNREACHABLE;
	return SG_STRATEGY_DESTINATION_WAIT_NONE;
}

static sg_strategy_destination_wait_reason_t StrategyGoalWaitReason(
	const sg_strategy_goal_t *goal,
	const sg_strategy_goal_runtime_t *runtime, uint64_t at_ms)
{
	uint8_t index;
	uint8_t limit = goal->failure.try_alternatives ? goal->choice_count : 1U;
	sg_strategy_destination_wait_reason_t result =
		SG_STRATEGY_DESTINATION_WAIT_UNREACHABLE;

	for (index = 0U; index < limit; index++)
	{
		sg_strategy_destination_wait_reason_t reason;
		if (runtime->choices[index].attempts >=
		    goal->failure.max_attempts_per_choice)
			continue;
		reason = StrategyChoiceWaitReason(&runtime->choices[index], at_ms);
		if (reason == SG_STRATEGY_DESTINATION_WAIT_STALE)
			return reason;
		if (reason == SG_STRATEGY_DESTINATION_WAIT_UNOBSERVED)
			result = reason;
	}
	return result;
}

static void StrategyInstructionForActive(sg_strategy_state_t *state,
	sg_strategy_instruction_kind_t kind, uint64_t at_ms)
{
	int found = StrategyFindPlanGoal(&state->plan, state->activation.goal_id);
	sg_strategy_goal_runtime_t *runtime;
	const sg_strategy_goal_t *goal;
	uint8_t choice;

	StrategyInstructionBase(&state->current_instruction, kind, state);
	if (found < 0)
		return;
	runtime = &state->goals[(uint16_t)found];
	goal = &state->plan.goals[(uint16_t)found];
	choice = runtime->selected_choice;
	state->current_instruction.goal_id = goal->id;
	state->current_instruction.choice_index = choice;
	state->current_instruction.activation = state->activation;
	if (choice < goal->choice_count)
	{
		state->current_instruction.destination = goal->choices[choice].destination;
		if (kind == SG_STRATEGY_INSTRUCTION_WAIT_DESTINATION)
		{
			state->current_instruction.cost_ms = SG_DESTINATION_FIELD_INF;
			state->current_instruction.destination_wait_reason =
				StrategyChoiceWaitReason(&runtime->choices[choice],
					at_ms);
		}
		else
		{
			state->current_instruction.handle = runtime->choices[choice].handle;
			state->current_instruction.cost_ms = runtime->choices[choice].cost_ms;
		}
	}
	if (kind == SG_STRATEGY_INSTRUCTION_SUSPENDED)
		state->current_instruction.block_reason = state->suspension.reason;
}

static int StrategyFixedPoint(sg_strategy_state_t *state,
	const sg_strategy_frame_t *frame, sg_strategy_reduction_t *out)
{
	for (;;)
	{
		uint16_t index;
		int selected = -1;
		int16_t selected_priority = INT16_MIN;
		int any_wait = 0;
		int all_settled = 1;

		if (!state->has_plan)
		{
			StrategyInstructionBase(&state->current_instruction,
				SG_STRATEGY_INSTRUCTION_EMPTY, state);
			return 1;
		}
		if (state->cancelled)
		{
			StrategyInstructionBase(&state->current_instruction,
				SG_STRATEGY_INSTRUCTION_CANCELLED, state);
			return 1;
		}
		for (index = 0U; index < state->plan.goal_count; index++)
		{
			if (state->goals[index].phase == SG_STRATEGY_GOAL_CANCELLED)
			{
				StrategyInstructionBase(&state->current_instruction,
					SG_STRATEGY_INSTRUCTION_CANCELLED, state);
				return 1;
			}
			if (state->goals[index].phase == SG_STRATEGY_GOAL_FAILED)
			{
				StrategyInstructionBase(&state->current_instruction,
					SG_STRATEGY_INSTRUCTION_FAILED, state);
				return 1;
			}
			if (!StrategyGoalSettled(state->goals[index].phase))
				all_settled = 0;
		}
		if (all_settled)
		{
			int already = state->current_instruction.kind ==
				SG_STRATEGY_INSTRUCTION_COMPLETED;
			StrategyInstructionBase(&state->current_instruction,
				SG_STRATEGY_INSTRUCTION_COMPLETED, state);
			if (!already && !StrategyEffect(state, out, frame->at_ms,
			    SG_STRATEGY_EFFECT_PLAN_COMPLETED, 0U,
			    SG_STRATEGY_NO_CHOICE, SG_STRATEGY_FAILURE_NONE))
				return 0;
			return 1;
		}
		if (!state->life_known || !state->life_alive)
		{
			StrategyInstructionBase(&state->current_instruction,
				SG_STRATEGY_INSTRUCTION_WAIT_LIFE, state);
			return 1;
		}
		if (!StrategyActivationEmpty(&state->activation))
		{
			int active = StrategyFindPlanGoal(&state->plan,
				state->activation.goal_id);
			const sg_strategy_goal_t *goal;
			sg_strategy_goal_runtime_t *runtime;
			strategy_condition_result_t condition;
			if (active < 0)
				return 0;
			goal = &state->plan.goals[(uint16_t)active];
			runtime = &state->goals[(uint16_t)active];
			if (state->suspension.active)
			{
				StrategyInstructionForActive(state,
					SG_STRATEGY_INSTRUCTION_SUSPENDED, frame->at_ms);
				return 1;
			}
			condition = StrategyEvaluateConditions(state, goal,
				SG_STRATEGY_CONDITION_WHILE_ACTIVE, frame->at_ms);
			if (condition == STRATEGY_CONDITION_FALSE ||
			    condition == STRATEGY_CONDITION_EXPIRED)
			{
				if (!StrategyFinishTerminalFailure(state, (uint16_t)active,
				    SG_STRATEGY_FAILURE_CONDITION_LOST, frame, out))
					return 0;
				continue;
			}
			if (condition == STRATEGY_CONDITION_WAIT)
			{
				StrategyInstructionForActive(state,
					SG_STRATEGY_INSTRUCTION_WAIT_CONDITION,
					frame->at_ms);
				return 1;
			}
			if (runtime->selected_choice >= goal->choice_count)
				return 0;
			if (StrategyChoiceUsable(
			    &runtime->choices[runtime->selected_choice], frame->at_ms))
			{
				StrategyInstructionForActive(state,
					SG_STRATEGY_INSTRUCTION_EXECUTE, frame->at_ms);
				return 1;
			}
			if (goal->unavailable == SG_STRATEGY_UNAVAILABLE_WAIT)
			{
				StrategyInstructionForActive(state,
					SG_STRATEGY_INSTRUCTION_WAIT_DESTINATION,
					frame->at_ms);
				return 1;
			}
			if (!StrategyFinishFailure(state, (uint16_t)active,
			    SG_STRATEGY_FAILURE_UNAVAILABLE, frame, out))
				return 0;
			continue;
		}

		for (index = 0U; index < state->plan.goal_count; index++)
			if (state->goals[index].phase == SG_STRATEGY_GOAL_RETRY_WAIT &&
			    StrategyRetryReady(state, index, frame))
			{
				state->goals[index].phase = SG_STRATEGY_GOAL_PENDING;
				state->goals[index].last_transition_at_ms = frame->at_ms;
				memset(&state->goals[index].retry, 0,
					sizeof(state->goals[index].retry));
			}

		for (index = 0U; index < state->plan.goal_count; index++)
		{
			const sg_strategy_goal_t *goal = &state->plan.goals[index];
			sg_strategy_goal_runtime_t *runtime = &state->goals[index];
			strategy_condition_result_t condition;
			int impossible;
			if (runtime->phase != SG_STRATEGY_GOAL_PENDING)
			{
				if (runtime->phase == SG_STRATEGY_GOAL_RETRY_WAIT)
					any_wait = 1;
				continue;
			}
			if (!StrategyDependenciesReady(state, index, &impossible))
			{
				if (impossible)
				{
					runtime->selected_choice = SG_STRATEGY_NO_CHOICE;
					if (!StrategyFinishTerminalFailure(state, index,
					    SG_STRATEGY_FAILURE_DEPENDENCY, frame, out))
						return 0;
					break;
				}
				any_wait = 1;
				continue;
			}
			condition = StrategyEvaluateConditions(state, goal,
				SG_STRATEGY_CONDITION_START_ONLY, frame->at_ms);
			if (condition == STRATEGY_CONDITION_FALSE ||
			    condition == STRATEGY_CONDITION_EXPIRED)
			{
				runtime->selected_choice = SG_STRATEGY_NO_CHOICE;
				if (!StrategyFinishTerminalFailure(state, index,
				    SG_STRATEGY_FAILURE_CONDITION_LOST, frame, out))
					return 0;
				break;
			}
			if (condition == STRATEGY_CONDITION_WAIT)
			{
				any_wait = 1;
				continue;
			}
			if (goal->kind == SG_STRATEGY_GOAL_WAIT)
			{
				runtime->phase = SG_STRATEGY_GOAL_SUCCEEDED;
				runtime->last_outcome = SG_STRATEGY_OUTCOME_COMPLETED;
				runtime->completed_at_ms = frame->at_ms;
				runtime->last_transition_at_ms = frame->at_ms;
				if (!StrategyEffect(state, out, frame->at_ms,
				    SG_STRATEGY_EFFECT_GOAL_COMPLETED, goal->id,
				    SG_STRATEGY_NO_CHOICE, SG_STRATEGY_FAILURE_NONE))
					return 0;
				break;
			}
			if (selected < 0 || goal->priority > selected_priority)
			{
				selected = (int)index;
				selected_priority = goal->priority;
			}
		}
		if (index < state->plan.goal_count)
			continue;
		if (selected >= 0)
		{
			uint16_t goal_index = (uint16_t)selected;
			const sg_strategy_goal_t *goal = &state->plan.goals[goal_index];
			sg_strategy_goal_runtime_t *runtime = &state->goals[goal_index];
			int choice;
			if (runtime->resume_after_life &&
			    runtime->selected_choice < goal->choice_count)
				choice = (int)runtime->selected_choice;
			else
				choice = StrategySelectReachable(goal, runtime, frame->at_ms);
			if (choice >= 0)
			{
				if (!StrategyActivate(state, goal_index, (uint8_t)choice,
				    frame->at_ms, out))
					return 0;
				continue;
			}
			if (goal->unavailable == SG_STRATEGY_UNAVAILABLE_APPLY_FAILURE &&
			    StrategyAllEligibleObserved(goal, runtime, frame->at_ms))
			{
				choice = StrategyFirstChoiceCapacity(goal, runtime);
				if (choice < 0 || runtime->attempt_count == UINT16_MAX ||
				    runtime->choices[(uint8_t)choice].attempts == UINT8_MAX)
					return 0;
				runtime->selected_choice = (uint8_t)choice;
				runtime->attempt_count++;
				runtime->choices[(uint8_t)choice].attempts++;
				if (!StrategyFinishFailure(state, goal_index,
				    SG_STRATEGY_FAILURE_UNAVAILABLE, frame, out))
					return 0;
				continue;
			}
			StrategyInstructionBase(&state->current_instruction,
				SG_STRATEGY_INSTRUCTION_WAIT_DESTINATION, state);
			state->current_instruction.goal_id = goal->id;
			state->current_instruction.destination_wait_reason =
				StrategyGoalWaitReason(goal, runtime, frame->at_ms);
			return 1;
		}
		StrategyInstructionBase(&state->current_instruction,
			any_wait ? SG_STRATEGY_INSTRUCTION_WAIT_CONDITION :
			SG_STRATEGY_INSTRUCTION_FAILED, state);
		return 1;
	}
}

sg_strategy_reduce_result_t SG_StrategyReduce(sg_strategy_state_t *state,
	const sg_strategy_frame_t *frame, sg_strategy_reduction_t *out)
{
	sg_strategy_state_t candidate;
	sg_strategy_reduction_t reduction;
	sg_strategy_reduce_result_t preflight;

	if (!state || !frame || !out)
		return SG_STRATEGY_REDUCE_REJECTED_INVALID;
	memset(&reduction, 0, sizeof(reduction));
	reduction.committed_revision = state->revision;
	reduction.instruction = state->current_instruction;
	if (state->last_frame_sequence != 0U &&
	    frame->sequence == state->last_frame_sequence)
	{
		reduction.result = SG_STRATEGY_REDUCE_DUPLICATE;
		*out = reduction;
		return reduction.result;
	}
	preflight = StrategyPreflight(state, frame);
	if (preflight != SG_STRATEGY_REDUCE_APPLIED)
	{
		reduction.result = preflight;
		*out = reduction;
		return reduction.result;
	}
	candidate = *state;
	if (!StrategyApplyDirective(&candidate, frame, &reduction) ||
	    !StrategyApplyFacts(&candidate, frame) ||
	    !StrategyApplyDestinations(&candidate, frame) ||
	    !StrategyApplyTactical(&candidate, frame, &reduction) ||
	    !StrategyApplyOutcome(&candidate, frame, &reduction) ||
	    !StrategyApplyLife(&candidate, frame, &reduction) ||
	    !StrategyFixedPoint(&candidate, frame, &reduction) ||
	    candidate.revision == UINT64_MAX)
	{
		memset(&reduction, 0, sizeof(reduction));
		reduction.result = SG_STRATEGY_REDUCE_INTERNAL_CAPACITY;
		reduction.committed_revision = state->revision;
		reduction.instruction = state->current_instruction;
		*out = reduction;
		return reduction.result;
	}
	candidate.revision++;
	candidate.last_frame_sequence = frame->sequence;
	candidate.last_frame_at_ms = frame->at_ms;
	reduction.result = SG_STRATEGY_REDUCE_APPLIED;
	reduction.committed_revision = candidate.revision;
	reduction.instruction = candidate.current_instruction;
	*state = candidate;
	*out = reduction;
	return reduction.result;
}
