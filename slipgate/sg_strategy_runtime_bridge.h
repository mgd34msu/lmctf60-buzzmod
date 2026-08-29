/* Consumer seam between live policy and authenticated RUNE runtime data. */
#ifndef SG_STRATEGY_RUNTIME_BRIDGE_H
#define SG_STRATEGY_RUNTIME_BRIDGE_H

#include "sg_strategy_caller.h"

typedef struct sg_strategy_runtime_execution_s
{
	sg_strategy_goal_id_t goal_id;
	sg_strategy_target_id_t target_id;
	int role;
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

/* The planner supplies an immutable semantic target and role only.  In
 * particular, it cannot nominate an execution field: that field belongs to
 * the destination-field authority which owns the exact target binding. */
typedef struct sg_strategy_runtime_target_request_s
{
	uint64_t commitment_id;
	sg_strategy_caller_authority_t authority;
	sg_strategy_goal_id_t goal_id;
	sg_strategy_target_id_t target_id;
	sg_destination_ref_t destination;
	int role;
} sg_strategy_runtime_target_request_t;

/* A locator is not an authority.  It can only nominate an opaque borrowed
 * view issued by the destination-field owner; it cannot manufacture a
 * binding, snapshot, field, or execution pointer.  The bridge immediately
 * gives the view back to the registered authority for validation. */
typedef struct sg_strategy_runtime_target_view_s
{
	const void *opaque;
} sg_strategy_runtime_target_view_t;

typedef int (*sg_strategy_runtime_target_locator_fn)(void *context,
	const sg_strategy_runtime_target_request_t *request,
	sg_strategy_runtime_target_view_t *view_out);

/* This callback is implemented by the destination-field/localization owner.
 * It must accept a view only when that exact view owns `request`'s complete
 * semantic destination and then return the matching field, snapshot, and
 * localization binding.  A same-kind FLAG/CURRENT versus FLAG/HOME swap must
 * fail here even if a locator could otherwise echo every request field. */
typedef int (*sg_strategy_runtime_target_authority_fn)(void *context,
	const sg_strategy_runtime_target_request_t *request,
	const sg_strategy_runtime_target_view_t *view,
	sg_strategy_caller_target_binding_t *binding_out);

void SG_StrategyRuntimeTargetProviderSet(
	sg_strategy_runtime_target_locator_fn locator, void *locator_context,
	sg_strategy_runtime_target_authority_fn authority, void *authority_context);

/* Map teardown clears this locator/authority registration before either
 * borrowed view, snapshot, field, or localization lifetime ends. */
int SG_StrategyRuntimeTargetProviderAvailable(void);

/* A typed plan is available only while both an untrusted locator and the
 * destination-field authority are registered.  No registration means no
 * typed strategy plan: the production caller never derives destination
 * handles from legacy seeds or route fields. */
int SG_StrategyRuntimePlanResolve(
	const sg_strategy_runtime_plan_request_t *request,
	sg_strategy_caller_plan_t *plan_out);

#endif /* SG_STRATEGY_RUNTIME_BRIDGE_H */
