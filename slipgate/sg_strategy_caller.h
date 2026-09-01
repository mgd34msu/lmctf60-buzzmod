/* Authenticated production caller boundary for the typed strategy reducer. */
#ifndef SG_STRATEGY_CALLER_H
#define SG_STRATEGY_CALLER_H

#include <stdint.h>

#include "sg_rune_compact_field_service.h"
#include "sg_strategy_contract.h"

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

/* The runtime bridge snapshots this identity from authenticated compact
 * localization.  Spawn generation is reducer life identity; client id pins
 * a caller so another subject can never reuse its retained commitment. */
typedef struct sg_strategy_caller_life_identity_s
{
	uint32_t client_id;
	uint32_t reserved;
	uint64_t spawn_generation;
} sg_strategy_caller_life_identity_t;

typedef enum sg_strategy_caller_field_observation_kind_e
{
	SG_STRATEGY_CALLER_FIELD_DISCONNECTED = 0,
	SG_STRATEGY_CALLER_FIELD_LOCAL_DESTINATION,
	SG_STRATEGY_CALLER_FIELD_CELL_DESTINATION,
	SG_STRATEGY_CALLER_FIELD_MECHANISMS_REQUIRED,
	SG_STRATEGY_CALLER_FIELD_BLOCKED_NOW,
	SG_STRATEGY_CALLER_FIELD_STEP,
	SG_STRATEGY_CALLER_FIELD_OBSERVATION_KIND_COUNT
} sg_strategy_caller_field_observation_kind_t;

/* Scalar projection returned by one current frame query. STEP carries the
 * exact total Q52.12 analytic cost, never descent geometry or a movement-
 * family identity. Temporary states have unavailable cost. */
typedef struct sg_strategy_caller_field_observation_s
{
	sg_strategy_caller_field_observation_kind_t kind;
	sg_rune_compact_field_cost_t cost_to_go;
} sg_strategy_caller_field_observation_t;

typedef struct sg_strategy_caller_target_observation_s
{
	sg_strategy_caller_field_observation_t field;
	uint64_t target_revision;
	uint64_t observation_revision;
	uint64_t observed_at_ms;
} sg_strategy_caller_target_observation_t;

/* Convert and validate one field query before its frame-local result expires.
 * The output never borrows the result's portal or mechanism storage. */
int SG_StrategyCallerFieldObservationFromResult(
	const sg_rune_compact_field_result_t *result,
	sg_strategy_caller_field_observation_t *observation_out);
int SG_StrategyCallerFieldObservationValid(
	const sg_strategy_caller_field_observation_t *observation);

/* A binding is compact execution data supplied only by the registered field
 * service/localization authority. The plan owns a lease on the exact compact
 * target field; callers query it through field_service and field_handle and
 * never reconstruct seed/link arrays or legacy dynamics objects. Tactical
 * code queries the handle with its current local context for frame-local
 * descent geometry. */
typedef struct sg_strategy_caller_target_binding_s
{
	uint64_t commitment_id;
	sg_strategy_caller_authority_t authority;
	sg_strategy_goal_id_t goal_id;
	sg_strategy_target_id_t target_id;
	sg_destination_ref_t destination;
	int role;
	const void *accepted_view;
	sg_rune_compact_field_service_t *field_service;
	sg_rune_compact_field_target_t compact_target;
	sg_rune_compact_field_handle_t field_handle;
} sg_strategy_caller_target_binding_t;

/* By-value projection accepted from the level-installed bot observation
 * owner.  Strategy code may copy it but cannot create a current observation:
 * the runtime bridge revalidates it through that owner on every plan use. */
typedef struct sg_strategy_caller_bot_observation_s
{
	sg_strategy_caller_life_identity_t life_identity;
	uint64_t host_authority_epoch;
	uint64_t frame_sequence;
	uint64_t observed_at_ms;
	sg_host_hook_phase_t hook_phase;
	float hook_length;
	float target_radius;
} sg_strategy_caller_bot_observation_t;

struct sg_strategy_caller_plan_s;

/* The runtime owner revalidates this plan's provider generation and exact
 * frame capability before every operation, then queries one binding against
 * that current local context. An accepted plan is usable only at its first
 * operation time; the next host frame must resolve a new capability. */
typedef int (*sg_strategy_caller_plan_current_fn)(
	const struct sg_strategy_caller_plan_s *plan);
typedef int (*sg_strategy_caller_target_observe_fn)(
	const struct sg_strategy_caller_plan_s *plan,
	const sg_strategy_caller_target_binding_t *binding,
	sg_strategy_caller_target_observation_t *observation_out);

/* The runtime owner leases each accepted opaque view to a resolved plan.
 * Release is expressed in terms of that capability: the strategy caller never
 * dereferences the opaque view or reconstructs a field-service handle from it. */
typedef void (*sg_strategy_caller_view_release_fn)(void *context,
	const void *accepted_view);

/* Callers submit an actual immutable, potentially queued plan.  plan_id is
 * assigned by the caller, so providers must leave spec.plan_id as zero. */
typedef struct sg_strategy_caller_plan_s
{
	uint64_t commitment_id;
	uint64_t provider_generation;
	uint64_t frame_sequence;
	uint64_t observed_at_ms;
	uint64_t frame_use_at_ms;
	sg_strategy_caller_life_identity_t life_identity;
	sg_strategy_caller_bot_observation_t bot_observation;
	sg_strategy_caller_authority_t authority;
	sg_strategy_plan_spec_t spec;
	uint16_t binding_count;
	uint16_t reserved;
	const void *frame_capability;
	const sg_rune_compact_field_mechanism_snapshot_t *mechanisms;
	const sg_rune_compact_field_portal_root_snapshot_t *portal_roots;
	sg_strategy_caller_plan_current_fn plan_current;
	sg_strategy_caller_target_observe_fn observe_target;
	sg_strategy_caller_view_release_fn release_view;
	void *release_context;
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
	uint64_t output_authority_owner_id;
	uint64_t output_authority_issuance;
	/* Owner-private output authority state.  The bytes are never accepted as
	 * strategy data and are meaningful only through the private proof API. */
	uint8_t output_authority_token[16];
	uint32_t subject_client_id;
	uint8_t initialized;
	uint8_t has_plan;
	uint8_t life_known;
	uint8_t life_alive;
	uint8_t subject_known;
	uint8_t output_authority_phase;
} sg_strategy_caller_t;

typedef struct sg_strategy_caller_output_s
{
	sg_strategy_instruction_t instruction;
	uint64_t commitment_id;
	uint64_t plan_id;
	uint64_t activation_id;
	uint64_t frame_sequence;
	uint64_t observed_at_ms;
	sg_strategy_caller_life_identity_t life_identity;
	int role;
	sg_rune_compact_field_service_t *field_service;
	sg_rune_compact_field_target_t compact_target;
	sg_rune_compact_field_handle_t field_handle;
} sg_strategy_caller_output_t;

int SG_StrategyCallerInit(sg_strategy_caller_t *caller);

/* Discard an unsubmitted resolved plan or retire a caller at an owner
 * boundary.  Both operations first detach and clear every borrowed pointer,
 * then release the retirement-local accepted views.  Owner callbacks may
 * re-enter or mutate the cleared source without releasing a view twice.
 * Destroy is safe on zero-initialized caller storage. */
void SG_StrategyCallerPlanDiscard(sg_strategy_caller_plan_t *plan);
void SG_StrategyCallerDestroy(sg_strategy_caller_t *caller);
/* Emergency recovery for a host that reports a level change only after the
 * registered owner storage is already gone.  It clears dangling leases
 * without invoking their now-invalid callback. */
void SG_StrategyCallerOwnerLost(sg_strategy_caller_t *caller);

/* Submit consumes and clears the input plan on success.  It either retains the
 * leases or releases them when a lower authority proposal only pulses the
 * existing plan.  Failure leaves the input untouched for the submitter to
 * discard.  The input must not alias the caller's retained plan. */
int SG_StrategyCallerSubmit(sg_strategy_caller_t *caller,
	sg_strategy_caller_plan_t *plan, uint8_t alive, uint64_t at_ms,
	sg_strategy_tactical_block_reason_t block_reason,
	sg_strategy_caller_output_t *out);

int SG_StrategyCallerPulse(sg_strategy_caller_t *caller, uint8_t alive,
	uint64_t at_ms, sg_strategy_tactical_block_reason_t block_reason,
	sg_strategy_caller_output_t *out);

/* Retire the last authenticated life even after its frame-local field lease
 * expires.  The caller accepts no replacement identity on this path. */
int SG_StrategyCallerRetireCurrentLife(sg_strategy_caller_t *caller,
	uint64_t at_ms, sg_strategy_caller_output_t *out);

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
