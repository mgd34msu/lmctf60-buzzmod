/* Typed strategy ownership and parameter-learning contracts. */
#ifndef SG_STRATEGY_CONTRACT_H
#define SG_STRATEGY_CONTRACT_H

#include <stdint.h>

#include "sg_destination_field.h"

#define SG_STRATEGY_MAX_PREREQUISITES 8U
#define SG_STRATEGY_MAX_ALTERNATIVES 4U
#define SG_STRATEGY_MAX_GOAL_KINDS 16U
#define SG_STRATEGY_MAX_GOALS 64U
#define SG_LEARNING_MAX_CAPABILITIES 32U
#define SG_LEARNING_MAX_TACTICS 32U
#define SG_LEARNING_MAX_REGIONS 128U
#define SG_STRATEGY_MAX_CLIENTS 256U

typedef enum sg_strategy_goal_kind_e
{
	SG_STRATEGY_GOAL_DESTINATION = 0,
	SG_STRATEGY_GOAL_CAPTURE_FLAG = 1,
	SG_STRATEGY_GOAL_CARRY_FLAG = 2,
	SG_STRATEGY_GOAL_RECOVER_FLAG = 3,
	SG_STRATEGY_GOAL_COLLECT_ITEM = 4,
	SG_STRATEGY_GOAL_ESCORT_CARRIER = 5,
	SG_STRATEGY_GOAL_INTERCEPT_CARRIER = 6,
	SG_STRATEGY_GOAL_DEFEND_POST = 7,
	SG_STRATEGY_GOAL_WAIT_WINDOW = 8,
	SG_STRATEGY_GOAL_ARBITRARY_WAYPOINT = 9,
	SG_STRATEGY_GOAL_KIND_COUNT = 10
} sg_strategy_goal_kind_t;

typedef enum sg_strategy_prerequisite_kind_e
{
	SG_STRATEGY_PREREQUISITE_NONE = 0,
	SG_STRATEGY_PREREQUISITE_GOAL_COMPLETE = 1,
	SG_STRATEGY_PREREQUISITE_DESTINATION_AVAILABLE = 2,
	SG_STRATEGY_PREREQUISITE_FLAG_HOME = 3,
	SG_STRATEGY_PREREQUISITE_CARRIER_PRESENT = 4,
	SG_STRATEGY_PREREQUISITE_TIME_WINDOW = 5,
	SG_STRATEGY_PREREQUISITE_KIND_COUNT = 6
} sg_strategy_prerequisite_kind_t;

typedef enum sg_strategy_failure_policy_e
{
	SG_STRATEGY_FAILURE_ABORT = 0,
	SG_STRATEGY_FAILURE_TRY_ALTERNATIVES = 1,
	SG_STRATEGY_FAILURE_SKIP = 2,
	SG_STRATEGY_FAILURE_SUSPEND = 3,
	SG_STRATEGY_FAILURE_POLICY_COUNT = 4
} sg_strategy_failure_policy_t;

typedef enum sg_strategy_authority_e
{
	SG_STRATEGY_AUTHORITY_AUTONOMOUS = 1,
	SG_STRATEGY_AUTHORITY_TEAM_ORDER = 2,
	SG_STRATEGY_AUTHORITY_HUMAN_ORDER = 3,
	SG_STRATEGY_AUTHORITY_EMERGENCY = 4
} sg_strategy_authority_t;

typedef enum sg_strategy_actor_e
{
	SG_STRATEGY_ACTOR_STRATEGY = 0,
	SG_STRATEGY_ACTOR_CONTROLLER = 1,
	SG_STRATEGY_ACTOR_TACTICS = 2,
	SG_STRATEGY_ACTOR_HUMAN = 3,
	SG_STRATEGY_ACTOR_HOST = 4,
	SG_STRATEGY_ACTOR_COUNT = 5
} sg_strategy_actor_t;

typedef enum sg_strategy_lifecycle_e
{
	SG_STRATEGY_EMPTY = 0,
	SG_STRATEGY_READY = 1,
	SG_STRATEGY_RUNNING = 2,
	SG_STRATEGY_SUSPENDED = 3,
	SG_STRATEGY_COMPLETED = 4,
	SG_STRATEGY_FAILED = 5,
	SG_STRATEGY_CANCELLED = 6
} sg_strategy_lifecycle_t;

typedef enum sg_strategy_event_kind_e
{
	SG_STRATEGY_EVENT_START = 0,
	SG_STRATEGY_EVENT_SUSPEND = 1,
	SG_STRATEGY_EVENT_RESUME = 2,
	SG_STRATEGY_EVENT_CANCELLED = 3,
	SG_STRATEGY_EVENT_REPLACED = 4,
	SG_STRATEGY_EVENT_COMPLETED = 5,
	SG_STRATEGY_EVENT_FAILED = 6,
	SG_STRATEGY_EVENT_ALTERNATIVE_SELECTED = 7,
	SG_STRATEGY_EVENT_KIND_COUNT = 8
} sg_strategy_event_kind_t;

typedef enum sg_strategy_failure_reason_e
{
	SG_STRATEGY_FAILURE_UNKNOWN = 0,
	SG_STRATEGY_FAILURE_UNAVAILABLE = 1,
	SG_STRATEGY_FAILURE_OBSTRUCTED = 2,
	SG_STRATEGY_FAILURE_TIMEOUT = 3,
	SG_STRATEGY_FAILURE_DEAD = 4,
	SG_STRATEGY_FAILURE_AUTHORITY = 5
} sg_strategy_failure_reason_t;

typedef struct sg_strategy_prerequisite_s
{
	sg_strategy_prerequisite_kind_t kind;
	uint8_t required;
	uint8_t reserved[3];
	union
	{
		uint32_t goal_id;
		sg_destination_handle_t destination;
		struct
		{
			uint64_t not_before_ms;
			uint64_t not_after_ms;
		} window;
	} value;
} sg_strategy_prerequisite_t;

typedef struct sg_strategy_alternative_s
{
	sg_destination_handle_t destination;
	uint32_t priority;
	uint16_t flags;
	uint16_t reserved;
} sg_strategy_alternative_t;

typedef struct sg_strategy_flag_target_s
{
	uint8_t team;
	uint8_t require_home;
	uint16_t reserved;
} sg_strategy_flag_target_t;

typedef struct sg_strategy_item_target_s
{
	uint64_t item_id;
} sg_strategy_item_target_t;

typedef struct sg_strategy_carrier_target_s
{
	uint16_t client_id;
	uint8_t team;
	uint8_t reserved;
} sg_strategy_carrier_target_t;

typedef struct sg_strategy_post_target_s
{
	uint32_t region_id;
	float heading;
} sg_strategy_post_target_t;

typedef struct sg_strategy_window_target_s
{
	uint64_t not_before_ms;
	uint64_t not_after_ms;
} sg_strategy_window_target_t;

/* Goal kind selects target. The destination handle remains a point-in-space
 * commitment when this goal has one, while the union carries semantic state
 * that a field query must not infer from a route link. */
typedef struct sg_strategy_goal_s
{
	uint32_t id;
	sg_strategy_goal_kind_t kind;
	sg_strategy_failure_policy_t failure_policy;
	uint8_t priority;
	uint8_t alternative_count;
	/* Zero selects the primary destination. A fallback is one-based. */
	uint8_t selected_alternative;
	uint8_t prerequisite_count;
	uint16_t flags;
	uint16_t reserved;
	uint64_t authority_generation;
	sg_destination_handle_t destination;
	sg_strategy_prerequisite_t prerequisites[SG_STRATEGY_MAX_PREREQUISITES];
	sg_strategy_alternative_t alternatives[SG_STRATEGY_MAX_ALTERNATIVES];
	union
	{
		sg_strategy_flag_target_t flag;
		sg_strategy_item_target_t item;
		sg_strategy_carrier_target_t carrier;
		sg_strategy_post_target_t post;
		sg_strategy_window_target_t window;
	} target;
} sg_strategy_goal_t;

typedef struct sg_strategy_queue_s
{
	sg_strategy_goal_t *items;
	uint32_t count;
	uint32_t capacity;
	uint64_t plan_id;
	uint64_t generation;
} sg_strategy_queue_t;

typedef struct sg_strategy_readiness_s
{
	const uint32_t *completed_goal_ids;
	uint32_t completed_goal_count;
	uint64_t now_ms;
	uint8_t destination_available;
	uint8_t flag_home;
	uint8_t carrier_present;
	uint8_t reserved;
} sg_strategy_readiness_t;

typedef struct sg_strategy_queue_replacement_s
{
	const sg_strategy_queue_t *queue;
} sg_strategy_queue_replacement_t;

typedef struct sg_strategy_event_s
{
	sg_strategy_event_kind_t kind;
	sg_strategy_actor_t actor;
	sg_strategy_authority_t authority;
	uint32_t goal_id;
	uint64_t expected_revision;
	uint64_t expected_authority_generation;
	uint64_t at_ms;
	uint64_t valid_until_ms;
	union
	{
		sg_strategy_queue_replacement_t replacement;
		sg_strategy_failure_reason_t failure;
		uint8_t alternative_index;
		uint32_t reason_code;
	} detail;
} sg_strategy_event_t;

typedef struct sg_strategy_state_s
{
	sg_strategy_queue_t queue;
	/* State owns its plan. Queue builders and replacement events may disappear
	 * or mutate immediately after the state accepts them. */
	sg_strategy_goal_t owned_items[SG_STRATEGY_MAX_GOALS];
	sg_strategy_lifecycle_t lifecycle;
	sg_strategy_event_kind_t last_event;
	sg_strategy_failure_reason_t last_failure;
	sg_strategy_authority_t authority;
	uint32_t active_index;
	uint64_t revision;
	uint64_t authority_generation;
	uint64_t last_event_at_ms;
} sg_strategy_state_t;

static inline int SG_StrategyGoalKindValid(sg_strategy_goal_kind_t kind)
{
	return kind >= SG_STRATEGY_GOAL_DESTINATION &&
	       kind < SG_STRATEGY_GOAL_KIND_COUNT;
}

static inline int SG_StrategyPrerequisiteKindValid(
	sg_strategy_prerequisite_kind_t kind)
{
	return kind >= SG_STRATEGY_PREREQUISITE_NONE &&
	       kind < SG_STRATEGY_PREREQUISITE_KIND_COUNT;
}

static inline int SG_StrategyFailureReasonValid(
	sg_strategy_failure_reason_t reason)
{
	return reason >= SG_STRATEGY_FAILURE_UNKNOWN &&
	       reason <= SG_STRATEGY_FAILURE_AUTHORITY;
}

static inline int SG_StrategyActorValid(sg_strategy_actor_t actor)
{
	return actor >= SG_STRATEGY_ACTOR_STRATEGY &&
	       actor < SG_STRATEGY_ACTOR_COUNT;
}

static inline int SG_StrategyTeamValid(uint8_t team)
{
	return team == 1U || team == 2U;
}

static inline int SG_StrategyFlagTargetValid(
	const sg_strategy_flag_target_t *target)
{
	return target && SG_StrategyTeamValid(target->team) &&
	       target->require_home <= 1U;
}

static inline int SG_StrategyCarrierTargetValid(
	const sg_strategy_carrier_target_t *target)
{
	return target && SG_StrategyTeamValid(target->team) &&
	       target->client_id < SG_STRATEGY_MAX_CLIENTS;
}

static inline int SG_StrategyWindowValid(uint64_t not_before_ms,
	uint64_t not_after_ms)
{
	return not_after_ms == 0U || not_after_ms >= not_before_ms;
}

static inline int SG_StrategyPrerequisiteValid(
	const sg_strategy_prerequisite_t *prerequisite)
{
	if (!prerequisite || !SG_StrategyPrerequisiteKindValid(prerequisite->kind) ||
	    prerequisite->required > 1U)
		return 0;
	switch (prerequisite->kind)
	{
	case SG_STRATEGY_PREREQUISITE_NONE:
		return prerequisite->required == 0U;
	case SG_STRATEGY_PREREQUISITE_GOAL_COMPLETE:
		return prerequisite->value.goal_id != 0U;
	case SG_STRATEGY_PREREQUISITE_DESTINATION_AVAILABLE:
		return SG_DestinationHandleValid(&prerequisite->value.destination);
	case SG_STRATEGY_PREREQUISITE_FLAG_HOME:
	case SG_STRATEGY_PREREQUISITE_CARRIER_PRESENT:
		return 1;
	case SG_STRATEGY_PREREQUISITE_TIME_WINDOW:
		return SG_StrategyWindowValid(prerequisite->value.window.not_before_ms,
			prerequisite->value.window.not_after_ms);
	default:
		return 0;
	}
}

static inline int SG_StrategyAuthorityValid(sg_strategy_authority_t authority)
{
	return authority >= SG_STRATEGY_AUTHORITY_AUTONOMOUS &&
	       authority <= SG_STRATEGY_AUTHORITY_EMERGENCY;
}

static inline int SG_StrategyGoalValid(const sg_strategy_goal_t *goal)
{
	uint32_t index;

	if (!goal || goal->id == 0U || !SG_StrategyGoalKindValid(goal->kind) ||
	    goal->failure_policy < SG_STRATEGY_FAILURE_ABORT ||
	    goal->failure_policy >= SG_STRATEGY_FAILURE_POLICY_COUNT ||
	    goal->alternative_count > SG_STRATEGY_MAX_ALTERNATIVES ||
	    goal->prerequisite_count > SG_STRATEGY_MAX_PREREQUISITES ||
	    goal->selected_alternative > goal->alternative_count ||
	    goal->authority_generation == 0U)
		return 0;
	if (goal->kind == SG_STRATEGY_GOAL_DESTINATION ||
	    goal->kind == SG_STRATEGY_GOAL_COLLECT_ITEM ||
	    goal->kind == SG_STRATEGY_GOAL_DEFEND_POST ||
	    goal->kind == SG_STRATEGY_GOAL_ARBITRARY_WAYPOINT)
		if (!SG_DestinationHandleValid(&goal->destination))
			return 0;
	for (index = 0U; index < goal->prerequisite_count; index++)
		if (!SG_StrategyPrerequisiteValid(&goal->prerequisites[index]))
			return 0;
	for (index = 0U; index < goal->alternative_count; index++)
		if (!SG_DestinationHandleValid(&goal->alternatives[index].destination))
			return 0;
	if (goal->selected_alternative != 0U &&
	    !SG_DestinationSameTarget(&goal->destination,
			&goal->alternatives[goal->selected_alternative - 1U].destination))
		return 0;
	switch (goal->kind)
	{
	case SG_STRATEGY_GOAL_CAPTURE_FLAG:
	case SG_STRATEGY_GOAL_CARRY_FLAG:
	case SG_STRATEGY_GOAL_RECOVER_FLAG:
		if (!SG_StrategyFlagTargetValid(&goal->target.flag))
			return 0;
		break;
	case SG_STRATEGY_GOAL_COLLECT_ITEM:
		if (goal->target.item.item_id == 0U)
			return 0;
		break;
	case SG_STRATEGY_GOAL_ESCORT_CARRIER:
	case SG_STRATEGY_GOAL_INTERCEPT_CARRIER:
		if (!SG_StrategyCarrierTargetValid(&goal->target.carrier))
			return 0;
		break;
	case SG_STRATEGY_GOAL_DEFEND_POST:
		if (!SG_DestinationFloatValid(goal->target.post.heading))
			return 0;
		break;
	case SG_STRATEGY_GOAL_WAIT_WINDOW:
		if (!SG_StrategyWindowValid(goal->target.window.not_before_ms,
			goal->target.window.not_after_ms))
			return 0;
		break;
	case SG_STRATEGY_GOAL_DESTINATION:
	case SG_STRATEGY_GOAL_ARBITRARY_WAYPOINT:
	case SG_STRATEGY_GOAL_KIND_COUNT:
		break;
	default:
		return 0;
	}
	return 1;
}

static inline int SG_StrategyQueueValid(const sg_strategy_queue_t *queue)
{
	uint32_t index;
	uint32_t other;

	if (!queue || queue->capacity == 0U || queue->count > queue->capacity ||
	    queue->capacity > SG_STRATEGY_MAX_GOALS ||
	    queue->plan_id == 0U || queue->generation == 0U ||
	    !queue->items)
		return 0;
	for (index = 0U; index < queue->count; index++)
		if (!SG_StrategyGoalValid(&queue->items[index]))
			return 0;
	for (index = 0U; index < queue->count; index++)
		for (other = index + 1U; other < queue->count; other++)
			if (queue->items[index].id == queue->items[other].id)
				return 0;
	return 1;
}

static inline int SG_StrategyReadinessValid(
	const sg_strategy_readiness_t *ready)
{
	return ready &&
	       (ready->completed_goal_count == 0U ||
		ready->completed_goal_ids != NULL) &&
	       ready->destination_available <= 1U && ready->flag_home <= 1U &&
	       ready->carrier_present <= 1U;
}

static inline int SG_StrategyStateValid(const sg_strategy_state_t *state)
{
	uint32_t index;

	if (!state || !SG_StrategyQueueValid(&state->queue) ||
	    state->queue.items != state->owned_items ||
	    state->lifecycle < SG_STRATEGY_READY ||
	    state->lifecycle > SG_STRATEGY_CANCELLED ||
	    state->last_event < SG_STRATEGY_EVENT_START ||
	    state->last_event >= SG_STRATEGY_EVENT_KIND_COUNT ||
	    !SG_StrategyFailureReasonValid(state->last_failure) ||
	    !SG_StrategyAuthorityValid(state->authority) || state->revision == 0U ||
	    state->authority_generation == 0U || state->last_event_at_ms == 0U ||
	    state->active_index > state->queue.count)
		return 0;
	for (index = 0U; index < state->queue.count; index++)
		if (state->queue.items[index].authority_generation !=
		    state->authority_generation)
			return 0;
	return 1;
}

static inline int SG_StrategyEventValid(const sg_strategy_event_t *event)
{
	return event && event->kind >= SG_STRATEGY_EVENT_START &&
	       event->kind < SG_STRATEGY_EVENT_KIND_COUNT &&
	       SG_StrategyActorValid(event->actor) &&
	       SG_StrategyAuthorityValid(event->authority) &&
	       event->expected_revision != 0U &&
	       event->expected_authority_generation != 0U && event->at_ms != 0U &&
	       event->valid_until_ms >= event->at_ms &&
	       ((event->kind != SG_STRATEGY_EVENT_FAILED) ||
		SG_StrategyFailureReasonValid(event->detail.failure));
}

static inline int SG_StrategyEventChangesAuthority(
	const sg_strategy_event_t *event)
{
	if (!SG_StrategyEventValid(event))
		return 0;
	return event->kind == SG_STRATEGY_EVENT_CANCELLED ||
	       event->kind == SG_STRATEGY_EVENT_REPLACED;
}

static inline int SG_StrategyQueueAuthorityValid(
	const sg_strategy_queue_t *queue, uint64_t authority_generation)
{
	uint32_t index;

	if (!SG_StrategyQueueValid(queue) || authority_generation == 0U)
		return 0;
	for (index = 0U; index < queue->count; index++)
		if (queue->items[index].authority_generation != authority_generation)
			return 0;
	return 1;
}

static inline int SG_StrategyEventBoundToState(
	const sg_strategy_state_t *state, const sg_strategy_event_t *event,
	uint64_t now_ms)
{
	if (!SG_StrategyStateValid(state) || !SG_StrategyEventValid(event) ||
	    event->expected_revision != state->revision ||
	    event->expected_authority_generation != state->authority_generation ||
	    event->at_ms < state->last_event_at_ms || now_ms < event->at_ms ||
	    now_ms > event->valid_until_ms || event->actor == SG_STRATEGY_ACTOR_TACTICS)
		return 0;
	if (SG_StrategyEventChangesAuthority(event))
	{
		if (event->actor == SG_STRATEGY_ACTOR_CONTROLLER ||
		    event->authority < state->authority ||
		    state->authority_generation == UINT64_MAX)
			return 0;
		if (event->kind == SG_STRATEGY_EVENT_REPLACED)
			return event->detail.replacement.queue &&
			       event->detail.replacement.queue->count != 0U &&
			       SG_StrategyQueueAuthorityValid(
				event->detail.replacement.queue,
				state->authority_generation + 1U);
		return 1;
	}
	return event->authority == state->authority;
}

/* Downstream strategy nodes own queue mutation and lifecycle reduction. */
int SG_StrategyQueueInit(sg_strategy_queue_t *queue,
	sg_strategy_goal_t *storage, uint32_t capacity, uint64_t plan_id);
int SG_StrategyQueueAppend(sg_strategy_queue_t *queue,
	const sg_strategy_goal_t *goal);
int SG_StrategyGoalReady(const sg_strategy_goal_t *goal,
	const sg_strategy_readiness_t *ready);
int SG_StrategyStateInit(sg_strategy_state_t *state,
	const sg_strategy_queue_t *queue, sg_strategy_authority_t authority,
	uint64_t initialized_at_ms);
int SG_StrategyAdvance(sg_strategy_state_t *state, uint64_t at_ms);
int SG_StrategyApplyEvent(sg_strategy_state_t *state,
	const sg_strategy_event_t *event, uint64_t now_ms);

typedef enum sg_learning_update_kind_e
{
	SG_LEARNING_UPDATE_COST = 0,
	SG_LEARNING_UPDATE_TACTIC_PRIOR = 1,
	SG_LEARNING_UPDATE_LANDING_PREFERENCE = 2,
	SG_LEARNING_UPDATE_STRATEGY = 3,
	SG_LEARNING_UPDATE_KIND_COUNT = 4
} sg_learning_update_kind_t;

typedef enum sg_learning_transaction_state_e
{
	SG_LEARNING_TRANSACTION_EMPTY = 0,
	SG_LEARNING_TRANSACTION_PREPARED = 1,
	SG_LEARNING_TRANSACTION_APPLIED = 2,
	SG_LEARNING_TRANSACTION_COMMITTED = 3,
	SG_LEARNING_TRANSACTION_ROLLED_BACK = 4
} sg_learning_transaction_state_t;

typedef struct sg_learning_evidence_s
{
	uint64_t evidence_id;
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t bsp_identity;
	uint64_t physics_identity;
	uint64_t trace_identity;
	uint64_t captured_at_ms;
	uint64_t authenticated_at_ms;
	uint8_t authenticated;
	uint8_t exact_bound;
	uint8_t host_verified;
	uint8_t post_match;
} sg_learning_evidence_t;

typedef struct sg_learning_update_s
{
	sg_learning_evidence_t evidence;
	sg_learning_update_kind_t kind;
	union
	{
		struct
		{
			uint32_t capability_id;
			int32_t delta_ms;
		} cost;
		struct
		{
			uint32_t tactic_id;
			float prior;
		} tactic;
		struct
		{
			uint32_t region_id;
			float preference;
		} landing;
		struct
		{
			uint32_t goal_kind;
			int16_t priority_delta;
		} strategy;
	} value;
} sg_learning_update_t;

typedef struct sg_learning_parameters_s
{
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t bsp_identity;
	uint64_t physics_identity;
	uint64_t generation;
	int32_t cost_delta_ms[SG_LEARNING_MAX_CAPABILITIES];
	float tactic_prior[SG_LEARNING_MAX_TACTICS];
	float landing_preference[SG_LEARNING_MAX_REGIONS];
	int16_t strategy_priority_delta[SG_STRATEGY_MAX_GOAL_KINDS];
} sg_learning_parameters_t;

typedef struct sg_learning_transaction_s
{
	uint64_t transaction_id;
	uint64_t expected_generation;
	uint64_t applied_generation;
	uint64_t evidence_id;
	sg_learning_transaction_state_t state;
	sg_learning_parameters_t before;
	sg_learning_update_t authorized_update;
} sg_learning_transaction_t;

static inline int SG_LearningParametersValid(
	const sg_learning_parameters_t *parameters)
{
	return parameters && parameters->rune_identity != 0U &&
	       parameters->topology_revision != 0U &&
	       parameters->bsp_identity != 0U && parameters->physics_identity != 0U &&
	       parameters->generation != 0U;
}

static inline int SG_LearningEvidenceValid(
	const sg_learning_evidence_t *evidence)
{
	return evidence && evidence->evidence_id != 0U &&
	       evidence->rune_identity != 0U && evidence->topology_revision != 0U &&
	       evidence->bsp_identity != 0U && evidence->physics_identity != 0U &&
	       evidence->trace_identity != 0U && evidence->captured_at_ms != 0U &&
	       evidence->authenticated_at_ms >= evidence->captured_at_ms &&
	       evidence->authenticated == 1U && evidence->exact_bound == 1U &&
	       evidence->host_verified == 1U && evidence->post_match == 1U;
}

static inline int SG_LearningEvidenceMatches(
	const sg_learning_parameters_t *parameters,
	const sg_learning_evidence_t *evidence)
{
	return SG_LearningParametersValid(parameters) &&
	       SG_LearningEvidenceValid(evidence) &&
	       parameters->rune_identity == evidence->rune_identity &&
	       parameters->topology_revision == evidence->topology_revision &&
	       parameters->bsp_identity == evidence->bsp_identity &&
	       parameters->physics_identity == evidence->physics_identity;
}

static inline int SG_LearningBoundedFloat(float value)
{
	return value == value && value >= 0.0f && value <= 1.0f;
}

static inline int SG_LearningUpdateValid(const sg_learning_update_t *update)
{
	if (!update || !SG_LearningEvidenceValid(&update->evidence) ||
	    update->kind < SG_LEARNING_UPDATE_COST ||
	    update->kind >= SG_LEARNING_UPDATE_KIND_COUNT)
		return 0;
	switch (update->kind)
	{
	case SG_LEARNING_UPDATE_COST:
		return update->value.cost.capability_id < SG_LEARNING_MAX_CAPABILITIES &&
		       update->value.cost.delta_ms >= -30000 &&
		       update->value.cost.delta_ms <= 30000;
	case SG_LEARNING_UPDATE_TACTIC_PRIOR:
		return update->value.tactic.tactic_id < SG_LEARNING_MAX_TACTICS &&
		       SG_LearningBoundedFloat(update->value.tactic.prior);
	case SG_LEARNING_UPDATE_LANDING_PREFERENCE:
		return update->value.landing.region_id < SG_LEARNING_MAX_REGIONS &&
		       SG_LearningBoundedFloat(update->value.landing.preference);
	case SG_LEARNING_UPDATE_STRATEGY:
		return update->value.strategy.goal_kind < SG_STRATEGY_MAX_GOAL_KINDS;
	default:
		return 0;
	}
}

static inline int SG_LearningUpdateTouchesGeometry(
	const sg_learning_update_t *update)
{
	(void)update;
	return 0;
}

static inline int SG_LearningUpdateSame(const sg_learning_update_t *left,
	const sg_learning_update_t *right)
{
	if (!SG_LearningUpdateValid(left) || !SG_LearningUpdateValid(right) ||
	    left->kind != right->kind ||
	    left->evidence.evidence_id != right->evidence.evidence_id ||
	    left->evidence.rune_identity != right->evidence.rune_identity ||
	    left->evidence.topology_revision != right->evidence.topology_revision ||
	    left->evidence.bsp_identity != right->evidence.bsp_identity ||
	    left->evidence.physics_identity != right->evidence.physics_identity ||
	    left->evidence.trace_identity != right->evidence.trace_identity ||
	    left->evidence.captured_at_ms != right->evidence.captured_at_ms ||
	    left->evidence.authenticated_at_ms !=
		right->evidence.authenticated_at_ms)
		return 0;
	switch (left->kind)
	{
	case SG_LEARNING_UPDATE_COST:
		return left->value.cost.capability_id ==
			right->value.cost.capability_id &&
		       left->value.cost.delta_ms == right->value.cost.delta_ms;
	case SG_LEARNING_UPDATE_TACTIC_PRIOR:
		return left->value.tactic.tactic_id == right->value.tactic.tactic_id &&
		       left->value.tactic.prior == right->value.tactic.prior;
	case SG_LEARNING_UPDATE_LANDING_PREFERENCE:
		return left->value.landing.region_id == right->value.landing.region_id &&
		       left->value.landing.preference == right->value.landing.preference;
	case SG_LEARNING_UPDATE_STRATEGY:
		return left->value.strategy.goal_kind == right->value.strategy.goal_kind &&
		       left->value.strategy.priority_delta ==
			right->value.strategy.priority_delta;
	default:
		return 0;
	}
}

static inline int SG_LearningParametersSameIdentity(
	const sg_learning_parameters_t *left,
	const sg_learning_parameters_t *right)
{
	return SG_LearningParametersValid(left) &&
	       SG_LearningParametersValid(right) &&
	       left->rune_identity == right->rune_identity &&
	       left->topology_revision == right->topology_revision &&
	       left->bsp_identity == right->bsp_identity &&
	       left->physics_identity == right->physics_identity;
}

static inline int SG_LearningTransactionValid(
	const sg_learning_transaction_t *transaction)
{
	if (!transaction || transaction->transaction_id == 0U ||
	    transaction->evidence_id == 0U ||
	    transaction->state < SG_LEARNING_TRANSACTION_PREPARED ||
	    transaction->state > SG_LEARNING_TRANSACTION_ROLLED_BACK ||
	    !SG_LearningParametersValid(&transaction->before) ||
	    !SG_LearningUpdateValid(&transaction->authorized_update) ||
	    !SG_LearningEvidenceMatches(&transaction->before,
		&transaction->authorized_update.evidence) ||
	    transaction->expected_generation != transaction->before.generation ||
	    transaction->evidence_id !=
		transaction->authorized_update.evidence.evidence_id)
		return 0;
	if (transaction->state == SG_LEARNING_TRANSACTION_PREPARED)
		return transaction->applied_generation == 0U;
	return transaction->expected_generation != UINT64_MAX &&
	       transaction->applied_generation ==
		transaction->expected_generation + 1U;
}

static inline int SG_LearningTransactionBoundToParameters(
	const sg_learning_parameters_t *parameters,
	const sg_learning_transaction_t *transaction)
{
	if (!SG_LearningParametersValid(parameters) ||
	    !SG_LearningTransactionValid(transaction) ||
	    !SG_LearningParametersSameIdentity(parameters, &transaction->before))
		return 0;
	if (transaction->state == SG_LEARNING_TRANSACTION_PREPARED ||
	    transaction->state == SG_LEARNING_TRANSACTION_ROLLED_BACK)
		return parameters->generation == transaction->expected_generation;
	return parameters->generation == transaction->applied_generation;
}

static inline int SG_LearningTransactionMayCommit(
	const sg_learning_parameters_t *parameters,
	const sg_learning_transaction_t *transaction)
{
	return transaction && transaction->state == SG_LEARNING_TRANSACTION_APPLIED &&
	       SG_LearningTransactionBoundToParameters(parameters, transaction);
}

static inline int SG_LearningTransactionMayRollback(
	const sg_learning_parameters_t *parameters,
	const sg_learning_transaction_t *transaction)
{
	return transaction && transaction->state == SG_LEARNING_TRANSACTION_APPLIED &&
	       SG_LearningTransactionBoundToParameters(parameters, transaction);
}

/* Downstream learning nodes own all parameter and transaction mutation. */
int SG_LearningParametersInit(sg_learning_parameters_t *parameters,
	uint64_t rune_identity, uint64_t topology_revision,
	uint64_t bsp_identity, uint64_t physics_identity);
int SG_LearningTransactionBegin(const sg_learning_parameters_t *parameters,
	const sg_learning_update_t *update, uint64_t transaction_id,
	sg_learning_transaction_t *transaction);
int SG_LearningApplyUpdate(sg_learning_parameters_t *parameters,
	const sg_learning_update_t *update,
	sg_learning_transaction_t *transaction);
int SG_LearningTransactionCommit(const sg_learning_parameters_t *parameters,
	sg_learning_transaction_t *transaction);
int SG_LearningTransactionRollback(sg_learning_parameters_t *parameters,
	sg_learning_transaction_t *transaction);

#endif /* SG_STRATEGY_CONTRACT_H */
