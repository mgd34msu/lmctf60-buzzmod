/* Consumer seam between live policy and authenticated compact RUNE data. */
#ifndef SG_STRATEGY_RUNTIME_BRIDGE_H
#define SG_STRATEGY_RUNTIME_BRIDGE_H

#include "sg_compact_localization.h"
#include "sg_strategy_caller.h"

struct sg_strategy_runtime_bot_observation_owner_s;
struct sg_strategy_runtime_bot_observation_s;

/* The planner names a semantic target.  A moving target carries the exact
 * owner observation used while resolving this request; static destinations
 * leave live_pose zeroed.  No execution field, seed, link, or legacy
 * dynamics object crosses this boundary. */
typedef struct sg_strategy_runtime_execution_s
{
	sg_strategy_goal_id_t goal_id;
	sg_strategy_target_id_t target_id;
	int role;
	sg_rune_compact_field_service_live_pose_t live_pose;
} sg_strategy_runtime_execution_t;

typedef struct sg_strategy_runtime_plan_request_s
{
	uint64_t commitment_id;
	const sg_compact_localized_state_t *localized_player;
	/* A host owner may publish one immutable mechanism snapshot for this
	 * frame.  A resolved plan borrows it only for that exact frame. */
	const sg_rune_compact_field_mechanism_snapshot_t *mechanisms;
	/* Discrete portal-root authority for the same exact localized frame. */
	const sg_rune_compact_field_portal_root_snapshot_t *portal_roots;
	/* Opaque, one-use capability issued by the installed bot-only host owner. */
	const struct sg_strategy_runtime_bot_observation_s *bot_observation;
	sg_strategy_caller_authority_t authority;
	sg_strategy_plan_spec_t spec;
	uint16_t execution_count;
	uint16_t reserved;
	sg_strategy_runtime_execution_t
		executions[SG_STRATEGY_CALLER_MAX_BINDINGS];
} sg_strategy_runtime_plan_request_t;

/* The planner supplies immutable semantic identity and the owner-authenticated
 * local observation.  The compact field service resolves the semantic target
 * to a compact destination and mints the lease; callers cannot nominate an
 * execution field or manufacture a raw route pointer. */
typedef struct sg_strategy_runtime_target_request_s
{
	uint64_t commitment_id;
	const sg_compact_localized_state_t *localized_player;
	const sg_rune_compact_field_mechanism_snapshot_t *mechanisms;
	const sg_rune_compact_field_portal_root_snapshot_t *portal_roots;
	sg_strategy_caller_authority_t authority;
	sg_strategy_goal_id_t goal_id;
	sg_strategy_target_id_t target_id;
	sg_destination_ref_t destination;
	int role;
	sg_rune_compact_field_service_live_pose_t live_pose;
} sg_strategy_runtime_target_request_t;

/* Install the production semantic adapter for one accepted compact field
 * service.  The service borrows an immutable accepted model; installation
 * fails closed when that model owner is absent or invalid.  Clear must run
 * before the owner destroys the service. */
int SG_StrategyRuntimeCompactProviderInstall(
	sg_rune_compact_field_service_t *service,
	const struct sg_strategy_runtime_bot_observation_owner_s *bot_observation);
int SG_StrategyRuntimeCompactProviderAvailable(void);
int SG_StrategyRuntimeCompactProviderInstalledFor(
	const sg_rune_compact_field_service_t *service);
void SG_StrategyRuntimeCompactProviderClear(
	sg_rune_compact_field_service_t *service);

/* Resolution compiles the typed semantic queue and leases one compact field
 * for every target.  `plan_out` must be zeroed or moved from; failure releases
 * every lease accepted during this attempt. */
int SG_StrategyRuntimePlanResolve(
	const sg_strategy_runtime_plan_request_t *request,
	sg_strategy_caller_plan_t *plan_out);

#endif /* SG_STRATEGY_RUNTIME_BRIDGE_H */
