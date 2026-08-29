/* Authenticated production caller boundary for the typed strategy reducer. */
#ifndef SG_STRATEGY_CALLER_H
#define SG_STRATEGY_CALLER_H

#include <stdint.h>

#include "sg_strategy_contract.h"

typedef struct sg_localized_player_state_s sg_localized_player_state_t;

/* The only caller-owned capacity is the reducer contract's exact plan
 * capacity: every non-WAIT target has one authenticated runtime binding. */
#define SG_STRATEGY_CALLER_MAX_BINDINGS \
	(SG_STRATEGY_MAX_GOALS * SG_STRATEGY_MAX_CHOICES)

typedef struct sg_strategy_caller_authority_s
{
	sg_strategy_authority_rank_t rank;
	sg_strategy_principal_kind_t principal_kind;
	uint32_t principal_id;
} sg_strategy_caller_authority_t;

/* `execution_field` is execution data supplied only by the registered
 * destination-field authority.  It never supplies destination identity,
 * reachability, pose, phase, or authority on its own.  The runtime authority
 * owns all pointed-to data until the caller replaces/releases it.
 * `observation_revision` and `pose_revision` are authority-monotonic whenever
 * their respective authenticated inputs change. */
typedef struct sg_strategy_caller_target_binding_s
{
	/* These semantic fields are emitted by the destination-field authority.
	 * The caller rejects a binding unless they exactly match the immutable
	 * target request; matching a destination kind alone is never sufficient. */
	uint64_t commitment_id;
	sg_strategy_caller_authority_t authority;
	sg_strategy_goal_id_t goal_id;
	sg_strategy_target_id_t target_id;
	sg_destination_ref_t destination;
	int role;
	const int *execution_field;
	const sg_rune_runtime_snapshot_t *snapshot;
	const sg_destination_field_t *field;
	const sg_localized_player_state_t *localized;
	uint64_t observation_revision;
	uint64_t pose_revision;
	uint64_t valid_until_ms;
} sg_strategy_caller_target_binding_t;

/* Callers submit an actual immutable, potentially queued plan.  plan_id is
 * assigned by the caller, so providers must leave spec.plan_id as zero. */
typedef struct sg_strategy_caller_plan_s
{
	uint64_t commitment_id;
	sg_strategy_caller_authority_t authority;
	sg_strategy_plan_spec_t spec;
	uint16_t binding_count;
	uint16_t reserved;
	sg_strategy_caller_target_binding_t
		bindings[SG_STRATEGY_CALLER_MAX_BINDINGS];
} sg_strategy_caller_plan_t;

typedef struct sg_strategy_caller_s
{
	sg_strategy_state_t reducer;
	sg_strategy_caller_plan_t plan;
	uint64_t next_sequence;
	uint64_t next_plan_id;
	uint64_t next_authority_epoch;
	uint64_t tactical_revision;
	uint64_t life_revision;
	uint64_t life_id;
	uint8_t initialized;
	uint8_t has_plan;
	uint8_t life_known;
	uint8_t life_alive;
} sg_strategy_caller_t;

typedef struct sg_strategy_caller_output_s
{
	sg_strategy_instruction_t instruction;
	uint64_t plan_id;
	uint64_t activation_id;
	int role;
	const int *execution_field;
	const sg_rune_runtime_snapshot_t *snapshot;
	const sg_destination_field_t *field;
	const sg_localized_player_state_t *localized;
} sg_strategy_caller_output_t;

int SG_StrategyCallerInit(sg_strategy_caller_t *caller);

/* A lower authority proposal only refreshes/pulses the retained plan.  It
 * cannot issue cancellation or release on behalf of the current principal. */
int SG_StrategyCallerSubmit(sg_strategy_caller_t *caller,
	const sg_strategy_caller_plan_t *plan, uint8_t alive, uint64_t at_ms,
	sg_strategy_tactical_block_reason_t block_reason,
	sg_strategy_caller_output_t *out);

int SG_StrategyCallerPulse(sg_strategy_caller_t *caller, uint8_t alive,
	uint64_t at_ms, sg_strategy_tactical_block_reason_t block_reason,
	sg_strategy_caller_output_t *out);

/* Advance is the production terminal-event entrypoint.  Settle is retained
 * as an explicit spelling for host integrations that already name it. */
int SG_StrategyCallerAdvance(sg_strategy_caller_t *caller, uint8_t alive,
	sg_strategy_goal_outcome_kind_t outcome,
	sg_strategy_failure_reason_t failure, uint64_t at_ms,
	sg_strategy_caller_output_t *out);
int SG_StrategyCallerSettle(sg_strategy_caller_t *caller, uint8_t alive,
	sg_strategy_goal_outcome_kind_t outcome,
	sg_strategy_failure_reason_t failure, uint64_t at_ms,
	sg_strategy_caller_output_t *out);

/* Cancellation is an explicit owner action.  It records the cancelled
 * terminal state; callers that want to yield the authority afterwards must
 * issue the separate exact-owner Release action. */
int SG_StrategyCallerCancel(sg_strategy_caller_t *caller,
	const sg_strategy_caller_authority_t *authority, uint8_t alive,
	uint64_t at_ms, sg_strategy_caller_output_t *out);

/* Release is intentionally separate from Submit.  The owning authenticated
 * authority must identify itself exactly; a lower-ranked proposal can never
 * synthesize a release stamp for the retained authority. */
int SG_StrategyCallerRelease(sg_strategy_caller_t *caller,
	const sg_strategy_caller_authority_t *authority, uint8_t alive,
	uint64_t at_ms, sg_strategy_caller_output_t *out);

#endif /* SG_STRATEGY_CALLER_H */
