#include "sg_strategy_runtime_bridge.h"

#include <string.h>

static sg_strategy_runtime_target_provider_fn sg_strategy_runtime_provider;
static void *sg_strategy_runtime_provider_context;

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
	if (!found || !found->execution_field)
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
	sg_strategy_runtime_target_provider_fn provider, void *context)
{
	sg_strategy_runtime_provider = provider;
	sg_strategy_runtime_provider_context = context;
}

int SG_StrategyRuntimePlanResolve(
	const sg_strategy_runtime_plan_request_t *request,
	sg_strategy_caller_plan_t *plan_out)
{
	sg_strategy_caller_plan_t candidate;
	sg_strategy_plan_t compiled;
	uint16_t goal_index;
	uint16_t binding_index = 0U;

	if (!request || !plan_out || !sg_strategy_runtime_provider ||
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
			sg_strategy_caller_target_binding_t binding;

			if (binding_index >= candidate.binding_count ||
			    !RuntimeExecutionFor(request, goal->id,
				goal->choices[choice_index].id, &execution))
				return 0;
			memset(&target, 0, sizeof(target));
			target.commitment_id = request->commitment_id;
			target.authority = request->authority;
			target.goal_id = goal->id;
			target.target_id = goal->choices[choice_index].id;
			target.destination = goal->choices[choice_index].destination;
			target.role = execution->role;
			target.execution_field = execution->execution_field;
			memset(&binding, 0, sizeof(binding));
			if (!sg_strategy_runtime_provider(
				sg_strategy_runtime_provider_context, &target, &binding) ||
			    binding.goal_id != target.goal_id ||
			    binding.target_id != target.target_id ||
			    binding.role != target.role ||
			    binding.execution_field != target.execution_field)
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
