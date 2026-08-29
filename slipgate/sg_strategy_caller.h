/* Production caller boundary for the typed strategy reducer. */
#ifndef SG_STRATEGY_CALLER_H
#define SG_STRATEGY_CALLER_H

#include <stdint.h>

#include "sg_strategy_contract.h"

typedef struct sg_strategy_proposal_s
{
	uint64_t commitment_id;
	sg_strategy_goal_kind_t goal_kind;
	sg_destination_ref_t destination;
	sg_destination_handle_t handle;
	sg_strategy_destination_status_t destination_status;
	uint32_t cost_ms;
	sg_strategy_authority_rank_t authority_rank;
	sg_strategy_principal_kind_t principal_kind;
	uint32_t principal_id;
	int role;
	const int *goal_field;
} sg_strategy_proposal_t;

typedef struct sg_strategy_caller_binding_s
{
	uint64_t plan_id;
	uint64_t commitment_id;
	sg_strategy_goal_kind_t goal_kind;
	sg_destination_ref_t destination;
	sg_destination_handle_t handle;
	sg_strategy_destination_status_t destination_status;
	uint32_t cost_ms;
	sg_strategy_authority_rank_t authority_rank;
	sg_strategy_principal_kind_t principal_kind;
	uint32_t principal_id;
	int role;
	const int *goal_field;
} sg_strategy_caller_binding_t;

typedef struct sg_strategy_caller_s
{
	sg_strategy_state_t reducer;
	sg_strategy_caller_binding_t binding;
	uint64_t next_sequence;
	uint64_t next_plan_id;
	uint64_t next_authority_epoch;
	uint64_t destination_revision;
	uint64_t tactical_revision;
	uint64_t life_revision;
	uint64_t life_id;
	uint8_t initialized;
	uint8_t life_known;
	uint8_t life_alive;
	uint8_t reserved;
} sg_strategy_caller_t;

typedef struct sg_strategy_caller_output_s
{
	sg_strategy_instruction_t instruction;
	uint64_t plan_id;
	uint64_t activation_id;
	int role;
	const int *goal_field;
} sg_strategy_caller_output_t;

int SG_StrategyCallerInit(sg_strategy_caller_t *caller);
int SG_StrategyCallerStep(sg_strategy_caller_t *caller,
	const sg_strategy_proposal_t *proposal, uint8_t alive, uint64_t at_ms,
	sg_strategy_tactical_block_reason_t block_reason,
	sg_strategy_caller_output_t *out);
int SG_StrategyCallerPulse(sg_strategy_caller_t *caller, uint8_t alive,
	uint64_t at_ms, sg_strategy_tactical_block_reason_t block_reason,
	sg_strategy_caller_output_t *out);
int SG_StrategyCallerSettle(sg_strategy_caller_t *caller,
	sg_strategy_goal_outcome_kind_t outcome,
	sg_strategy_failure_reason_t failure, uint64_t at_ms,
	sg_strategy_caller_output_t *out);

#endif /* SG_STRATEGY_CALLER_H */
