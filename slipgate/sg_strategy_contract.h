/* Immutable strategy plans and their transactional execution reducer. */
#ifndef SG_STRATEGY_CONTRACT_H
#define SG_STRATEGY_CONTRACT_H

#include <stdint.h>

#include "sg_destination_field.h"

#define SG_STRATEGY_MAX_GOALS 64U
#define SG_STRATEGY_MAX_DEPENDENCIES 8U
#define SG_STRATEGY_MAX_CONDITIONS 8U
#define SG_STRATEGY_MAX_CHOICES 5U
#define SG_STRATEGY_MAX_FACTS 64U
/* A frame can emit one directive effect, one effect per goal while settling
 * or cancelling, and one plan-completed effect. */
#define SG_STRATEGY_MAX_EFFECTS (SG_STRATEGY_MAX_GOALS + 2U)
#define SG_STRATEGY_NO_INDEX UINT16_MAX
#define SG_STRATEGY_NO_CHOICE UINT8_MAX
#define SG_STRATEGY_PLAN_COMPILED_TAG UINT32_C(0x53504731)

typedef uint32_t sg_strategy_goal_id_t;
typedef uint32_t sg_strategy_target_id_t;

typedef enum sg_strategy_goal_kind_e
{
	SG_STRATEGY_GOAL_DESTINATION = 0,
	SG_STRATEGY_GOAL_CAPTURE_FLAG,
	SG_STRATEGY_GOAL_CARRY_FLAG,
	SG_STRATEGY_GOAL_RECOVER_FLAG,
	SG_STRATEGY_GOAL_COLLECT_ITEM,
	SG_STRATEGY_GOAL_ESCORT_CARRIER,
	SG_STRATEGY_GOAL_INTERCEPT_CARRIER,
	SG_STRATEGY_GOAL_DEFEND_POST,
	SG_STRATEGY_GOAL_WAIT,
	SG_STRATEGY_GOAL_KIND_COUNT
} sg_strategy_goal_kind_t;

typedef enum sg_strategy_dependency_accept_e
{
	SG_STRATEGY_DEPENDENCY_SUCCESS = 0,
	SG_STRATEGY_DEPENDENCY_SETTLED,
	SG_STRATEGY_DEPENDENCY_ACCEPT_COUNT
} sg_strategy_dependency_accept_t;

typedef enum sg_strategy_condition_kind_e
{
	SG_STRATEGY_CONDITION_FACT_EQUALS = 0,
	SG_STRATEGY_CONDITION_TIME_WINDOW,
	SG_STRATEGY_CONDITION_KIND_COUNT
} sg_strategy_condition_kind_t;

typedef enum sg_strategy_condition_scope_e
{
	SG_STRATEGY_CONDITION_START_ONLY = 0,
	SG_STRATEGY_CONDITION_WHILE_ACTIVE,
	SG_STRATEGY_CONDITION_SCOPE_COUNT
} sg_strategy_condition_scope_t;

typedef enum sg_strategy_fact_kind_e
{
	SG_STRATEGY_FACT_ALIVE = 0,
	SG_STRATEGY_FACT_ROLE,
	SG_STRATEGY_FACT_CARRYING_FLAG,
	SG_STRATEGY_FACT_FLAG_HOME,
	SG_STRATEGY_FACT_CARRIER_PRESENT,
	SG_STRATEGY_FACT_COMBAT_ACTIVE,
	SG_STRATEGY_FACT_ITEM_OWNED,
	SG_STRATEGY_FACT_CUSTOM,
	SG_STRATEGY_FACT_KIND_COUNT
} sg_strategy_fact_kind_t;

typedef struct sg_strategy_fact_key_s
{
	sg_strategy_fact_kind_t kind;
	uint32_t subject_id;
	uint8_t team;
	uint8_t reserved[3];
} sg_strategy_fact_key_t;

typedef struct sg_strategy_fact_predicate_s
{
	sg_strategy_fact_key_t key;
	int64_t expected_value;
} sg_strategy_fact_predicate_t;

typedef struct sg_strategy_time_window_s
{
	uint64_t not_before_ms;
	uint64_t not_after_ms;
} sg_strategy_time_window_t;

typedef struct sg_strategy_condition_s
{
	sg_strategy_condition_kind_t kind;
	sg_strategy_condition_scope_t scope;
	union
	{
		sg_strategy_fact_predicate_t fact;
		sg_strategy_time_window_t time;
	} value;
} sg_strategy_condition_t;

typedef enum sg_strategy_unavailable_action_e
{
	SG_STRATEGY_UNAVAILABLE_WAIT = 0,
	SG_STRATEGY_UNAVAILABLE_APPLY_FAILURE,
	SG_STRATEGY_UNAVAILABLE_ACTION_COUNT
} sg_strategy_unavailable_action_t;

typedef enum sg_strategy_retry_wake_kind_e
{
	SG_STRATEGY_RETRY_NONE = 0,
	SG_STRATEGY_RETRY_NEXT_FRAME,
	SG_STRATEGY_RETRY_TARGET_REVISION,
	SG_STRATEGY_RETRY_FACT_REVISION,
	SG_STRATEGY_RETRY_NOT_BEFORE,
	SG_STRATEGY_RETRY_WAKE_COUNT
} sg_strategy_retry_wake_kind_t;

typedef struct sg_strategy_retry_wake_s
{
	sg_strategy_retry_wake_kind_t kind;
	sg_strategy_fact_key_t fact;
	uint64_t delay_ms;
} sg_strategy_retry_wake_t;

typedef enum sg_strategy_failure_terminal_e
{
	SG_STRATEGY_FAILURE_SKIP_GOAL = 0,
	SG_STRATEGY_FAILURE_FAIL_PLAN,
	SG_STRATEGY_FAILURE_TERMINAL_COUNT
} sg_strategy_failure_terminal_t;

typedef struct sg_strategy_failure_rule_s
{
	uint8_t try_alternatives;
	uint8_t max_attempts_per_choice;
	uint16_t reserved;
	sg_strategy_retry_wake_t retry_wake;
	sg_strategy_failure_terminal_t exhausted;
} sg_strategy_failure_rule_t;

typedef struct sg_strategy_dependency_spec_s
{
	sg_strategy_goal_id_t goal_id;
	sg_strategy_dependency_accept_t accept;
} sg_strategy_dependency_spec_t;

typedef struct sg_strategy_dependency_s
{
	uint16_t goal_index;
	sg_strategy_dependency_accept_t accept;
} sg_strategy_dependency_t;

typedef struct sg_strategy_target_choice_s
{
	sg_strategy_target_id_t id;
	sg_destination_ref_t destination;
} sg_strategy_target_choice_t;

typedef struct sg_strategy_goal_spec_s
{
	sg_strategy_goal_id_t id;
	sg_strategy_goal_kind_t kind;
	int16_t priority;
	uint8_t dependency_count;
	uint8_t condition_count;
	uint8_t choice_count;
	uint8_t reserved;
	sg_strategy_unavailable_action_t unavailable;
	sg_strategy_dependency_spec_t dependencies[SG_STRATEGY_MAX_DEPENDENCIES];
	sg_strategy_condition_t conditions[SG_STRATEGY_MAX_CONDITIONS];
	sg_strategy_target_choice_t choices[SG_STRATEGY_MAX_CHOICES];
	sg_strategy_failure_rule_t failure;
} sg_strategy_goal_spec_t;

typedef struct sg_strategy_goal_s
{
	sg_strategy_goal_id_t id;
	sg_strategy_goal_kind_t kind;
	int16_t priority;
	uint16_t queue_order;
	uint8_t dependency_count;
	uint8_t condition_count;
	uint8_t choice_count;
	uint8_t reserved;
	sg_strategy_unavailable_action_t unavailable;
	sg_strategy_dependency_t dependencies[SG_STRATEGY_MAX_DEPENDENCIES];
	sg_strategy_condition_t conditions[SG_STRATEGY_MAX_CONDITIONS];
	sg_strategy_target_choice_t choices[SG_STRATEGY_MAX_CHOICES];
	sg_strategy_failure_rule_t failure;
} sg_strategy_goal_t;

typedef struct sg_strategy_plan_spec_s
{
	uint64_t plan_id;
	uint16_t goal_count;
	uint16_t reserved;
	sg_strategy_goal_spec_t goals[SG_STRATEGY_MAX_GOALS];
} sg_strategy_plan_spec_t;

typedef struct sg_strategy_plan_s
{
	uint64_t plan_id;
	uint32_t compiled_tag;
	uint16_t goal_count;
	uint16_t topological_order[SG_STRATEGY_MAX_GOALS];
	sg_strategy_goal_t goals[SG_STRATEGY_MAX_GOALS];
} sg_strategy_plan_t;

typedef enum sg_strategy_compile_error_code_e
{
	SG_STRATEGY_COMPILE_OK = 0,
	SG_STRATEGY_COMPILE_INVALID_ARGUMENT,
	SG_STRATEGY_COMPILE_CAPACITY,
	SG_STRATEGY_COMPILE_INVALID_GOAL,
	SG_STRATEGY_COMPILE_DUPLICATE_GOAL_ID,
	SG_STRATEGY_COMPILE_DUPLICATE_TARGET_ID,
	SG_STRATEGY_COMPILE_MISSING_DEPENDENCY,
	SG_STRATEGY_COMPILE_SELF_DEPENDENCY,
	SG_STRATEGY_COMPILE_CYCLE
} sg_strategy_compile_error_code_t;

typedef struct sg_strategy_compile_error_s
{
	sg_strategy_compile_error_code_t code;
	uint16_t goal_index;
	uint16_t dependency_index;
} sg_strategy_compile_error_t;

typedef enum sg_strategy_authority_rank_e
{
	SG_STRATEGY_AUTHORITY_AUTONOMOUS = 1,
	SG_STRATEGY_AUTHORITY_TEAM = 2,
	SG_STRATEGY_AUTHORITY_HUMAN = 3,
	SG_STRATEGY_AUTHORITY_EMERGENCY = 4
} sg_strategy_authority_rank_t;

typedef enum sg_strategy_principal_kind_e
{
	SG_STRATEGY_PRINCIPAL_NONE = 0,
	SG_STRATEGY_PRINCIPAL_AUTONOMOUS,
	SG_STRATEGY_PRINCIPAL_TEAM,
	SG_STRATEGY_PRINCIPAL_HUMAN,
	SG_STRATEGY_PRINCIPAL_EMERGENCY,
	SG_STRATEGY_PRINCIPAL_KIND_COUNT
} sg_strategy_principal_kind_t;

typedef struct sg_strategy_principal_s
{
	sg_strategy_principal_kind_t kind;
	uint32_t id;
} sg_strategy_principal_t;

typedef struct sg_strategy_authority_stamp_s
{
	sg_strategy_authority_rank_t rank;
	sg_strategy_principal_t principal;
	uint64_t epoch;
} sg_strategy_authority_stamp_t;

typedef struct sg_strategy_activation_s
{
	uint64_t plan_id;
	uint64_t activation_id;
	sg_strategy_goal_id_t goal_id;
} sg_strategy_activation_t;

typedef enum sg_strategy_destination_status_e
{
	SG_STRATEGY_DESTINATION_UNOBSERVED = 0,
	SG_STRATEGY_DESTINATION_UNREACHABLE,
	SG_STRATEGY_DESTINATION_REACHABLE,
	SG_STRATEGY_DESTINATION_STATUS_COUNT
} sg_strategy_destination_status_t;

typedef struct sg_strategy_destination_observation_s
{
	uint64_t plan_id;
	sg_strategy_goal_id_t goal_id;
	sg_strategy_target_id_t target_id;
	uint64_t observation_revision;
	uint64_t pose_revision;
	uint64_t observed_at_ms;
	uint64_t valid_until_ms;
	sg_strategy_destination_status_t status;
	uint32_t cost_ms;
	sg_destination_handle_t handle;
} sg_strategy_destination_observation_t;

typedef struct sg_strategy_fact_observation_s
{
	sg_strategy_fact_key_t key;
	int64_t value;
	uint64_t observation_revision;
	uint64_t observed_at_ms;
	uint64_t valid_until_ms;
} sg_strategy_fact_observation_t;

typedef enum sg_strategy_goal_phase_e
{
	SG_STRATEGY_GOAL_PENDING = 0,
	SG_STRATEGY_GOAL_ACTIVE,
	SG_STRATEGY_GOAL_RETRY_WAIT,
	SG_STRATEGY_GOAL_SUCCEEDED,
	SG_STRATEGY_GOAL_SKIPPED,
	SG_STRATEGY_GOAL_FAILED,
	SG_STRATEGY_GOAL_CANCELLED,
	SG_STRATEGY_GOAL_PHASE_COUNT
} sg_strategy_goal_phase_t;

typedef enum sg_strategy_goal_outcome_kind_e
{
	SG_STRATEGY_OUTCOME_NONE = 0,
	SG_STRATEGY_OUTCOME_COMPLETED,
	SG_STRATEGY_OUTCOME_FAILED,
	SG_STRATEGY_OUTCOME_KIND_COUNT
} sg_strategy_goal_outcome_kind_t;

typedef enum sg_strategy_failure_reason_e
{
	SG_STRATEGY_FAILURE_NONE = 0,
	SG_STRATEGY_FAILURE_UNAVAILABLE,
	SG_STRATEGY_FAILURE_OBSTRUCTED,
	SG_STRATEGY_FAILURE_CONDITION_LOST,
	SG_STRATEGY_FAILURE_DEPENDENCY,
	SG_STRATEGY_FAILURE_REASON_COUNT
} sg_strategy_failure_reason_t;

typedef struct sg_strategy_goal_outcome_observation_s
{
	uint8_t present;
	uint8_t reserved[3];
	sg_strategy_activation_t activation;
	sg_strategy_goal_outcome_kind_t kind;
	sg_strategy_failure_reason_t failure;
} sg_strategy_goal_outcome_observation_t;

typedef enum sg_strategy_tactical_block_reason_e
{
	SG_STRATEGY_BLOCK_NONE = 0,
	SG_STRATEGY_BLOCK_COMBAT,
	SG_STRATEGY_BLOCK_OBSTRUCTION,
	SG_STRATEGY_BLOCK_HOOK_OPPORTUNITY,
	SG_STRATEGY_BLOCK_CONTROLLER,
	SG_STRATEGY_BLOCK_REASON_COUNT
} sg_strategy_tactical_block_reason_t;

typedef struct sg_strategy_tactical_snapshot_s
{
	uint8_t present;
	uint8_t blocked;
	uint16_t reserved;
	uint64_t observation_revision;
	sg_strategy_activation_t activation;
	sg_strategy_tactical_block_reason_t reason;
} sg_strategy_tactical_snapshot_t;

typedef struct sg_strategy_life_snapshot_s
{
	uint8_t present;
	uint8_t alive;
	uint16_t reserved;
	uint64_t observation_revision;
	uint64_t life_id;
} sg_strategy_life_snapshot_t;

typedef enum sg_strategy_directive_kind_e
{
	SG_STRATEGY_DIRECTIVE_NONE = 0,
	SG_STRATEGY_DIRECTIVE_REPLACE,
	SG_STRATEGY_DIRECTIVE_CANCEL,
	SG_STRATEGY_DIRECTIVE_RELEASE,
	SG_STRATEGY_DIRECTIVE_KIND_COUNT
} sg_strategy_directive_kind_t;

typedef struct sg_strategy_directive_s
{
	sg_strategy_directive_kind_t kind;
	sg_strategy_authority_stamp_t stamp;
	const sg_strategy_plan_t *replacement;
} sg_strategy_directive_t;

typedef struct sg_strategy_frame_s
{
	uint64_t sequence;
	uint64_t expected_revision;
	uint64_t at_ms;
	sg_strategy_directive_t directive;
	sg_strategy_life_snapshot_t life;
	sg_strategy_goal_outcome_observation_t goal_outcome;
	sg_strategy_tactical_snapshot_t tactical;
	const sg_strategy_fact_observation_t *facts;
	uint16_t fact_count;
	const sg_strategy_destination_observation_t *destinations;
	uint16_t destination_count;
} sg_strategy_frame_t;

typedef struct sg_strategy_retry_record_s
{
	uint64_t after_sequence;
	uint64_t baseline_revision;
	uint64_t not_before_ms;
	sg_strategy_retry_wake_t wake;
} sg_strategy_retry_record_t;

typedef struct sg_strategy_choice_runtime_s
{
	uint8_t observed;
	uint8_t attempts;
	uint16_t reserved;
	uint64_t observation_revision;
	uint64_t pose_revision;
	uint64_t observed_at_ms;
	uint64_t valid_until_ms;
	sg_strategy_destination_status_t status;
	uint32_t cost_ms;
	sg_destination_handle_t handle;
} sg_strategy_choice_runtime_t;

typedef struct sg_strategy_goal_runtime_s
{
	sg_strategy_goal_phase_t phase;
	uint8_t selected_choice;
	uint8_t resume_after_life;
	uint16_t attempt_count;
	uint16_t retry_count;
	sg_strategy_goal_outcome_kind_t last_outcome;
	sg_strategy_failure_reason_t last_failure;
	uint64_t activated_at_ms;
	uint64_t completed_at_ms;
	uint64_t last_transition_at_ms;
	sg_strategy_retry_record_t retry;
	sg_strategy_choice_runtime_t choices[SG_STRATEGY_MAX_CHOICES];
} sg_strategy_goal_runtime_t;

typedef struct sg_strategy_fact_record_s
{
	uint8_t occupied;
	uint8_t reserved[7];
	sg_strategy_fact_observation_t observation;
} sg_strategy_fact_record_t;

typedef struct sg_strategy_suspend_record_s
{
	uint8_t active;
	uint8_t reserved[7];
	uint64_t observation_revision;
	sg_strategy_activation_t activation;
	sg_strategy_tactical_block_reason_t reason;
} sg_strategy_suspend_record_t;

typedef enum sg_strategy_instruction_kind_e
{
	SG_STRATEGY_INSTRUCTION_EMPTY = 0,
	SG_STRATEGY_INSTRUCTION_WAIT_LIFE,
	SG_STRATEGY_INSTRUCTION_WAIT_CONDITION,
	SG_STRATEGY_INSTRUCTION_WAIT_DESTINATION,
	SG_STRATEGY_INSTRUCTION_EXECUTE,
	SG_STRATEGY_INSTRUCTION_SUSPENDED,
	SG_STRATEGY_INSTRUCTION_COMPLETED,
	SG_STRATEGY_INSTRUCTION_FAILED,
	SG_STRATEGY_INSTRUCTION_CANCELLED,
	SG_STRATEGY_INSTRUCTION_KIND_COUNT
} sg_strategy_instruction_kind_t;

typedef enum sg_strategy_destination_wait_reason_e
{
	SG_STRATEGY_DESTINATION_WAIT_NONE = 0,
	SG_STRATEGY_DESTINATION_WAIT_UNOBSERVED,
	SG_STRATEGY_DESTINATION_WAIT_UNREACHABLE,
	SG_STRATEGY_DESTINATION_WAIT_STALE,
	SG_STRATEGY_DESTINATION_WAIT_REASON_COUNT
} sg_strategy_destination_wait_reason_t;

typedef struct sg_strategy_instruction_s
{
	sg_strategy_instruction_kind_t kind;
	uint64_t plan_id;
	sg_strategy_goal_id_t goal_id;
	uint8_t choice_index;
	uint8_t reserved[3];
	sg_strategy_activation_t activation;
	sg_destination_ref_t destination;
	sg_destination_handle_t handle;
	uint32_t cost_ms;
	sg_strategy_tactical_block_reason_t block_reason;
	sg_strategy_destination_wait_reason_t destination_wait_reason;
} sg_strategy_instruction_t;

typedef enum sg_strategy_effect_kind_e
{
	SG_STRATEGY_EFFECT_PLAN_REPLACED = 0,
	SG_STRATEGY_EFFECT_PLAN_CANCELLED,
	SG_STRATEGY_EFFECT_AUTHORITY_RELEASED,
	SG_STRATEGY_EFFECT_GOAL_ACTIVATED,
	SG_STRATEGY_EFFECT_GOAL_COMPLETED,
	SG_STRATEGY_EFFECT_GOAL_RETRY_WAIT,
	SG_STRATEGY_EFFECT_GOAL_SKIPPED,
	SG_STRATEGY_EFFECT_GOAL_FAILED,
	SG_STRATEGY_EFFECT_GOAL_CANCELLED,
	SG_STRATEGY_EFFECT_TACTICAL_SUSPENDED,
	SG_STRATEGY_EFFECT_TACTICAL_RESUMED,
	SG_STRATEGY_EFFECT_LIFE_RETIRED,
	SG_STRATEGY_EFFECT_PLAN_COMPLETED
} sg_strategy_effect_kind_t;

typedef struct sg_strategy_history_effect_s
{
	uint64_t sequence;
	uint64_t at_ms;
	sg_strategy_effect_kind_t kind;
	sg_strategy_goal_id_t goal_id;
	uint8_t choice_index;
	uint8_t reserved[3];
	sg_strategy_failure_reason_t failure;
} sg_strategy_history_effect_t;

typedef struct sg_strategy_state_s
{
	uint64_t revision;
	uint64_t last_frame_sequence;
	uint64_t last_frame_at_ms;
	uint64_t history_sequence;
	uint8_t has_plan;
	uint8_t cancelled;
	uint8_t life_known;
	uint8_t life_alive;
	uint64_t life_observation_revision;
	uint64_t life_id;
	sg_strategy_plan_t plan;
	sg_strategy_authority_stamp_t authority;
	sg_strategy_goal_runtime_t goals[SG_STRATEGY_MAX_GOALS];
	uint16_t fact_count;
	sg_strategy_fact_record_t facts[SG_STRATEGY_MAX_FACTS];
	uint64_t next_activation_id;
	sg_strategy_activation_t activation;
	sg_strategy_suspend_record_t suspension;
	sg_strategy_instruction_t current_instruction;
} sg_strategy_state_t;

typedef enum sg_strategy_reduce_result_e
{
	SG_STRATEGY_REDUCE_APPLIED = 0,
	SG_STRATEGY_REDUCE_DUPLICATE,
	SG_STRATEGY_REDUCE_REJECTED_INVALID,
	SG_STRATEGY_REDUCE_REJECTED_STALE,
	SG_STRATEGY_REDUCE_REJECTED_AUTHORITY,
	SG_STRATEGY_REDUCE_INTERNAL_CAPACITY
} sg_strategy_reduce_result_t;

typedef struct sg_strategy_reduction_s
{
	sg_strategy_reduce_result_t result;
	uint64_t committed_revision;
	uint16_t effect_count;
	sg_strategy_history_effect_t effects[SG_STRATEGY_MAX_EFFECTS];
	sg_strategy_instruction_t instruction;
} sg_strategy_reduction_t;

int SG_StrategyPlanCompile(const sg_strategy_plan_spec_t *spec,
	sg_strategy_plan_t *out, sg_strategy_compile_error_t *error);
int SG_StrategyStateInit(sg_strategy_state_t *state);
sg_strategy_reduce_result_t SG_StrategyReduce(sg_strategy_state_t *state,
	const sg_strategy_frame_t *frame, sg_strategy_reduction_t *out);

#endif /* SG_STRATEGY_CONTRACT_H */
