/* Consumer seam between live policy and authenticated RUNE runtime data. */
#ifndef SG_STRATEGY_RUNTIME_BRIDGE_H
#define SG_STRATEGY_RUNTIME_BRIDGE_H

#include "sg_strategy_caller.h"

typedef struct sg_strategy_runtime_execution_s
{
	sg_strategy_goal_id_t goal_id;
	sg_strategy_target_id_t target_id;
	int role;
	/* Legacy execution data only.  The resolver must bind authenticated
	 * snapshot, field, and localization sources before the caller accepts it. */
	const int *execution_field;
} sg_strategy_runtime_execution_t;

typedef struct sg_strategy_runtime_plan_request_s
{
	uint64_t commitment_id;
	sg_strategy_caller_authority_t authority;
	sg_strategy_plan_spec_t spec;
	uint16_t execution_count;
	uint16_t reserved;
	sg_strategy_runtime_execution_t
		executions[SG_STRATEGY_CALLER_MAX_BINDINGS];
} sg_strategy_runtime_plan_request_t;

/* The runtime provider receives the immutable semantic target selected by the
 * caller and may only supply its authenticated dynamic binding.  Its output
 * must echo commitment, authority, goal, target, destination, role, and
 * execution field exactly; the bridge rejects a partial or same-kind-only
 * binding. */
typedef struct sg_strategy_runtime_target_request_s
{
	uint64_t commitment_id;
	sg_strategy_caller_authority_t authority;
	sg_strategy_goal_id_t goal_id;
	sg_strategy_target_id_t target_id;
	sg_destination_ref_t destination;
	int role;
	const int *execution_field;
} sg_strategy_runtime_target_request_t;

/* The destination-field/localization integration owns this provider.  For
 * each target it must return the exact snapshot, destination field, localized
 * player state, and monotonic revisions.  It cannot alter the policy plan. */
typedef int (*sg_strategy_runtime_target_provider_fn)(void *context,
	const sg_strategy_runtime_target_request_t *request,
	sg_strategy_caller_target_binding_t *binding_out);

void SG_StrategyRuntimeTargetProviderSet(
	sg_strategy_runtime_target_provider_fn provider, void *context);

/* Map teardown clears this registration before the provider's borrowed
 * snapshot, field, or localization lifetime ends. */
int SG_StrategyRuntimeTargetProviderAvailable(void);

/* No provider means no typed strategy plan: the production caller never
 * derives destination handles from legacy seeds or route fields. */
int SG_StrategyRuntimePlanResolve(
	const sg_strategy_runtime_plan_request_t *request,
	sg_strategy_caller_plan_t *plan_out);

#endif /* SG_STRATEGY_RUNTIME_BRIDGE_H */
