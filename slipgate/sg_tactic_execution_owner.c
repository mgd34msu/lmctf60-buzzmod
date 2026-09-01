#include "sg_tactic_execution_owner_private.h"

#include <stdlib.h>
#include <string.h>

#include "sg_authority_entropy.h"
#include "sg_host_law_owner.h"
#include "sg_tactic_runtime_private.h"

typedef enum sg_tactic_execution_action_kind_e
{
	SG_TACTIC_EXECUTION_ACTION_NONE = 0,
	SG_TACTIC_EXECUTION_ACTION_STANCE,
	SG_TACTIC_EXECUTION_ACTION_PMOVE,
	SG_TACTIC_EXECUTION_ACTION_HOOK,
	SG_TACTIC_EXECUTION_ACTION_MECHANISM,
	SG_TACTIC_EXECUTION_ACTION_ANGULAR_MOVER,
	SG_TACTIC_EXECUTION_ACTION_ROCKET_JUMP,
#ifdef SG_TACTIC_EXECUTION_OWNER_TESTING
	SG_TACTIC_EXECUTION_ACTION_TEST,
#endif
	SG_TACTIC_EXECUTION_ACTION_KIND_COUNT
} sg_tactic_execution_action_kind_t;

typedef struct sg_tactic_execution_entity_identity_s
{
	uint32_t key;
	uint32_t source_ordinal;
	uint64_t generation;
	uint32_t link_count;
	uint32_t kind;
} sg_tactic_execution_entity_identity_t;

/* Complete scalar live facts required by future family sealers. Presence bits
 * prevent a partial observation from becoming an executable action. */
typedef struct sg_tactic_execution_body_snapshot_s
{
	pmove_state_t current_state;
	pmove_state_t previous_state;
	uint32_t origin_bits[3];
	uint32_t velocity_bits[3];
	uint32_t mins_bits[3];
	uint32_t maxs_bits[3];
	uint32_t view_height_bits;
	uint32_t view_angle_bits[3];
	int16_t delta_angles[3];
	int32_t entity_number;
	int32_t model_index;
	int32_t move_type;
	int32_t dead_flag;
	int32_t health;
	int32_t water_level;
	int32_t water_type;
	int32_t hand;
	sg_tactic_execution_entity_identity_t ground;
	uint8_t in_use;
	uint8_t connected;
	uint8_t player;
	uint8_t bot_owned;
} sg_tactic_execution_body_snapshot_t;

typedef struct sg_tactic_execution_hook_snapshot_s
{
	sg_tactic_execution_entity_identity_t hook;
	sg_tactic_execution_entity_identity_t target;
	sg_host_hook_phase_t state;
	uint32_t length;
	uint32_t origin_bits[3];
	uint32_t offset_bits[3];
	uint64_t last_frame;
	uint8_t owner_backpointer_current;
	uint8_t client_backpointer_current;
	uint8_t reserved[6];
} sg_tactic_execution_hook_snapshot_t;

typedef struct sg_tactic_execution_mechanism_snapshot_s
{
	sg_tactic_execution_entity_identity_t entity;
	uint32_t transition;
	uint32_t controller;
	uint32_t controller_target;
	uint32_t mechanism_kind;
	uint32_t motion_state;
	uint32_t phase;
	uint8_t phase_known;
	uint8_t reserved[7];
} sg_tactic_execution_mechanism_snapshot_t;

typedef struct sg_tactic_execution_weapon_snapshot_s
{
	sg_tactic_execution_entity_identity_t launcher;
	int32_t weapon_state;
	int32_t inventory_index;
	int32_t inventory_count;
	int32_t ammo_index;
	int32_t ammo_count;
	int32_t health;
	uint32_t quad_frame;
} sg_tactic_execution_weapon_snapshot_t;

typedef struct sg_tactic_execution_external_force_snapshot_s
{
	sg_tactic_execution_entity_identity_t source;
	uint64_t frame_sequence;
	uint64_t observed_at_ms;
	uint32_t kind;
	uint8_t active;
	uint8_t reserved[3];
} sg_tactic_execution_external_force_snapshot_t;

typedef struct sg_tactic_execution_live_snapshot_s
{
	sg_host_law_runtime_authority_t host_authority;
	sg_host_law_subject_t subject;
	sg_tactic_execution_body_snapshot_t body;
	sg_tactic_execution_hook_snapshot_t hook;
	sg_tactic_execution_mechanism_snapshot_t mechanism;
	sg_tactic_execution_weapon_snapshot_t weapon;
	sg_tactic_execution_external_force_snapshot_t external_force;
	uint64_t present_mask;
} sg_tactic_execution_live_snapshot_t;

typedef struct sg_tactic_execution_pmove_action_s
{
	sg_host_pmove_request_t request;
	sg_host_pmove_result_t expected;
} sg_tactic_execution_pmove_action_t;

typedef struct sg_tactic_execution_hook_action_s
{
	uint32_t movement_kind;
	uint32_t hook_target;
	sg_tactic_execution_entity_identity_t expected_hook;
	uint64_t expected_event;
} sg_tactic_execution_hook_action_t;

typedef struct sg_tactic_execution_mechanism_action_s
{
	sg_tactic_execution_mechanism_snapshot_t expected;
	uint64_t expected_event;
} sg_tactic_execution_mechanism_action_t;

typedef struct sg_tactic_execution_rocket_jump_action_s
{
	sg_tactic_execution_entity_identity_t projectile;
	uint64_t expected_launch;
	uint64_t expected_impact;
} sg_tactic_execution_rocket_jump_action_t;

typedef struct sg_tactic_execution_sealed_action_s
{
	sg_tactic_execution_action_kind_t kind;
	union
	{
		sg_tactic_execution_pmove_action_t stance;
		sg_tactic_execution_pmove_action_t pmove;
		sg_tactic_execution_hook_action_t hook;
		sg_tactic_execution_mechanism_action_t mechanism;
		sg_tactic_execution_mechanism_action_t angular_mover;
		sg_tactic_execution_rocket_jump_action_t rocket_jump;
#ifdef SG_TACTIC_EXECUTION_OWNER_TESTING
		struct
		{
			uint64_t sealed_epoch;
			uint64_t expected_result;
		} test;
#endif
	} value;
} sg_tactic_execution_sealed_action_t;

typedef struct sg_tactic_execution_pending_s
{
	sg_tactic_execution_token_t token;
	uint64_t owner_epoch;
	uint64_t sequence;
	uint64_t diagnostic_digest;
	sg_tactic_runtime_prepared_step_t prepared;
	sg_tactic_execution_live_snapshot_t live;
	sg_tactic_execution_sealed_action_t action;
	uint8_t occupied;
	uint8_t reserved[7];
} sg_tactic_execution_pending_t;

struct sg_tactic_execution_owner_s
{
	uint64_t owner_epoch;
	uint64_t next_sequence;
	sg_tactic_execution_pending_t pending;
	uint8_t lost;
	uint8_t reserved[7];
#ifdef SG_TACTIC_EXECUTION_OWNER_TESTING
	sg_tactic_execution_owner_test_receipt_t last_receipt;
#endif
};

#ifdef SG_TACTIC_EXECUTION_OWNER_TESTING
static uint64_t test_seal_epoch;
static uint64_t test_current_epoch;
static sg_tactic_execution_owner_status_t test_action_outcome =
	SG_TACTIC_EXECUTION_OWNER_OK;
#endif

static void SecureClear(void *memory, size_t size)
{
	volatile uint8_t *bytes = memory;

	while (size > 0U)
	{
		*bytes++ = 0U;
		--size;
	}
}

static void PendingTake(sg_tactic_execution_owner_t *owner,
	sg_tactic_execution_pending_t *pending_out)
{
	*pending_out = owner->pending;
	SecureClear(&owner->pending, sizeof(owner->pending));
}

static void PendingReleaseAndClear(sg_tactic_execution_pending_t *pending)
{
	if (pending->occupied)
		(void)SG_TacticRuntimePreparedStepRelease(&pending->prepared);
	SecureClear(pending, sizeof(*pending));
}

static void OwnerPendingRelease(sg_tactic_execution_owner_t *owner)
{
	sg_tactic_execution_pending_t pending;

	if (!owner || !owner->pending.occupied)
		return;
	PendingTake(owner, &pending);
	PendingReleaseAndClear(&pending);
}

static void DiagnosticClear(sg_tactic_execution_diagnostic_t *diagnostic)
{
	if (diagnostic)
		memset(diagnostic, 0, sizeof(*diagnostic));
}

static sg_tactic_execution_owner_status_t DiagnosticSet(
	sg_tactic_execution_diagnostic_t *diagnostic,
	sg_tactic_execution_owner_status_t status,
	sg_tactic_execution_diagnostic_family_t family,
	sg_tactic_execution_diagnostic_reason_t reason, uint64_t digest)
{
	if (diagnostic)
	{
		diagnostic->family = family;
		diagnostic->reason = reason;
		diagnostic->digest = digest;
	}
	return status;
}

static int TokenIsZero(const sg_tactic_execution_token_t *token)
{
	uint8_t value = 0U;
	size_t i;

	for (i = 0U; i < sizeof(token->opaque); ++i)
		value = (uint8_t)(value | token->opaque[i]);
	return value == 0U;
}

static int TokenEqual(const sg_tactic_execution_token_t *left,
	const sg_tactic_execution_token_t *right)
{
	uint8_t difference = 0U;
	size_t i;

	for (i = 0U; i < sizeof(left->opaque); ++i)
		difference = (uint8_t)(difference |
			(uint8_t)(left->opaque[i] ^ right->opaque[i]));
	return difference == 0U;
}

static uint64_t HashWord(uint64_t hash, uint64_t value)
{
	uint32_t shift;

	for (shift = 0U; shift < 64U; shift += 8U)
	{
		hash ^= (value >> shift) & UINT64_C(0xff);
		hash *= UINT64_C(1099511628211);
	}
	return hash;
}

static uint64_t PendingDiagnosticDigest(
	const sg_tactic_execution_pending_t *pending)
{
	uint64_t hash = UINT64_C(1469598103934665603);

	hash = HashWord(hash, UINT64_C(0x5441435449434f57));
	hash = HashWord(hash, pending->owner_epoch);
	hash = HashWord(hash, pending->sequence);
	hash = HashWord(hash, pending->prepared.frame.subject.client_id);
	hash = HashWord(hash,
		pending->prepared.frame.subject.spawn_generation);
	hash = HashWord(hash, pending->prepared.frame.frame_sequence);
	hash = HashWord(hash, pending->prepared.frame.observed_at_ms);
	hash = HashWord(hash, pending->prepared.provider.rune_identity);
	hash = HashWord(hash, pending->prepared.provider.topology_revision);
	hash = HashWord(hash,
		(uint64_t)pending->prepared.exact_probe.provenance.kind);
	return hash == 0U ? UINT64_C(1) : hash;
}

static sg_tactic_execution_diagnostic_family_t PreparedFamily(
	const sg_tactic_runtime_prepared_step_t *prepared)
{
	switch (prepared->exact_probe.provenance.kind)
	{
	case SG_RUNE_COMPACT_FIELD_PROBE_INTRINSIC_STANCE:
		return SG_TACTIC_EXECUTION_DIAGNOSTIC_STANCE;
	case SG_RUNE_COMPACT_FIELD_PROBE_PMOVE:
		return SG_TACTIC_EXECUTION_DIAGNOSTIC_PMOVE;
	case SG_RUNE_COMPACT_FIELD_PROBE_HOOK:
		return SG_TACTIC_EXECUTION_DIAGNOSTIC_HOOK;
	case SG_RUNE_COMPACT_FIELD_PROBE_MECHANISM_TRANSITION:
		return SG_TACTIC_EXECUTION_DIAGNOSTIC_MECHANISM;
	case SG_RUNE_COMPACT_FIELD_PROBE_ANGULAR_MOVER:
		return SG_TACTIC_EXECUTION_DIAGNOSTIC_ANGULAR_MOVER;
	case SG_RUNE_COMPACT_FIELD_PROBE_PROVENANCE_KIND_COUNT:
	default:
		return SG_TACTIC_EXECUTION_DIAGNOSTIC_RUNTIME;
	}
}

static sg_tactic_execution_owner_status_t SealAction(
	const sg_tactic_runtime_prepared_step_t *prepared,
	sg_tactic_execution_live_snapshot_t *live_out,
	sg_tactic_execution_sealed_action_t *action_out)
{
	memset(live_out, 0, sizeof(*live_out));
	memset(action_out, 0, sizeof(*action_out));
	if ((uint32_t)prepared->exact_probe.provenance.kind >=
		(uint32_t)SG_RUNE_COMPACT_FIELD_PROBE_PROVENANCE_KIND_COUNT)
		return SG_TACTIC_EXECUTION_OWNER_PREPARE_REJECTED;
#ifdef SG_TACTIC_EXECUTION_OWNER_TESTING
	if (test_seal_epoch == 0U)
		return SG_TACTIC_EXECUTION_OWNER_WITNESS_INCOMPLETE;
	action_out->kind = SG_TACTIC_EXECUTION_ACTION_TEST;
	action_out->value.test.sealed_epoch = test_seal_epoch;
	action_out->value.test.expected_result = UINT64_C(1);
	live_out->subject = prepared->frame.subject;
	live_out->present_mask = UINT64_MAX;
	return SG_TACTIC_EXECUTION_OWNER_OK;
#else
	(void)prepared;
	return SG_TACTIC_EXECUTION_OWNER_WITNESS_INCOMPLETE;
#endif
}

sg_tactic_execution_owner_status_t SG_TacticExecutionOwnerCreate(
	sg_tactic_execution_owner_t **owner_out,
	sg_tactic_execution_diagnostic_t *diagnostic_out)
{
	sg_tactic_execution_owner_t *owner;

	DiagnosticClear(diagnostic_out);
	if (owner_out)
		*owner_out = NULL;
	if (!owner_out || !diagnostic_out)
		return DiagnosticSet(diagnostic_out,
			SG_TACTIC_EXECUTION_OWNER_INVALID_ARGUMENT,
			SG_TACTIC_EXECUTION_DIAGNOSTIC_OWNER,
			SG_TACTIC_EXECUTION_REASON_ARGUMENT, 0U);
	owner = calloc(1U, sizeof(*owner));
	if (!owner)
		return DiagnosticSet(diagnostic_out,
			SG_TACTIC_EXECUTION_OWNER_ALLOCATION_REJECTED,
			SG_TACTIC_EXECUTION_DIAGNOSTIC_OWNER,
			SG_TACTIC_EXECUTION_REASON_ALLOCATION, 0U);
	if (!SG_AuthorityEntropyFill(&owner->owner_epoch,
		sizeof(owner->owner_epoch)) || owner->owner_epoch == 0U)
	{
		SecureClear(owner, sizeof(*owner));
		free(owner);
		return DiagnosticSet(diagnostic_out,
			SG_TACTIC_EXECUTION_OWNER_ENTROPY_REJECTED,
			SG_TACTIC_EXECUTION_DIAGNOSTIC_OWNER,
			SG_TACTIC_EXECUTION_REASON_ENTROPY, 0U);
	}
	owner->next_sequence = 1U;
	*owner_out = owner;
	return SG_TACTIC_EXECUTION_OWNER_OK;
}

void SG_TacticExecutionOwnerDestroy(sg_tactic_execution_owner_t *owner)
{
	if (!owner)
		return;
	owner->lost = 1U;
	OwnerPendingRelease(owner);
	SecureClear(owner, sizeof(*owner));
	free(owner);
}

sg_tactic_execution_owner_status_t SG_TacticExecutionOwnerPrepare(
	sg_tactic_execution_owner_t *owner,
	const sg_tactic_runtime_step_input_t *input,
	sg_tactic_execution_token_t *token_out,
	sg_tactic_execution_diagnostic_t *diagnostic_out)
{
	sg_tactic_runtime_prepared_step_t prepared;
	sg_tactic_execution_pending_t candidate;
	sg_tactic_runtime_status_t runtime_status;
	sg_tactic_execution_owner_status_t seal_status;
	sg_tactic_execution_diagnostic_family_t family;

	DiagnosticClear(diagnostic_out);
	if (token_out)
		memset(token_out, 0, sizeof(*token_out));
	if (!owner || !input || !token_out || !diagnostic_out)
		return DiagnosticSet(diagnostic_out,
			SG_TACTIC_EXECUTION_OWNER_INVALID_ARGUMENT,
			SG_TACTIC_EXECUTION_DIAGNOSTIC_OWNER,
			SG_TACTIC_EXECUTION_REASON_ARGUMENT, 0U);
	if (owner->lost)
		return DiagnosticSet(diagnostic_out,
			SG_TACTIC_EXECUTION_OWNER_LOST,
			SG_TACTIC_EXECUTION_DIAGNOSTIC_OWNER,
			SG_TACTIC_EXECUTION_REASON_OWNER_LOST, 0U);
	if (owner->pending.occupied)
		return DiagnosticSet(diagnostic_out,
			SG_TACTIC_EXECUTION_OWNER_BUSY,
			SG_TACTIC_EXECUTION_DIAGNOSTIC_OWNER,
			SG_TACTIC_EXECUTION_REASON_OCCUPIED,
			owner->pending.diagnostic_digest);
	if (owner->next_sequence == 0U ||
		owner->next_sequence == UINT64_MAX)
		return DiagnosticSet(diagnostic_out,
			SG_TACTIC_EXECUTION_OWNER_ENTROPY_REJECTED,
			SG_TACTIC_EXECUTION_DIAGNOSTIC_OWNER,
			SG_TACTIC_EXECUTION_REASON_ENTROPY, 0U);
	memset(&prepared, 0, sizeof(prepared));
	runtime_status = SG_TacticRuntimePrepareStep(input, &prepared);
	if (runtime_status != SG_TACTIC_RUNTIME_OK)
		return DiagnosticSet(diagnostic_out,
			SG_TACTIC_EXECUTION_OWNER_PREPARE_REJECTED,
			SG_TACTIC_EXECUTION_DIAGNOSTIC_RUNTIME,
			SG_TACTIC_EXECUTION_REASON_RUNTIME, 0U);
	if (prepared.result.status != SG_TACTIC_RESULT_PROGRESS)
	{
		(void)SG_TacticRuntimePreparedStepRelease(&prepared);
		return DiagnosticSet(diagnostic_out,
			SG_TACTIC_EXECUTION_OWNER_PREPARE_REJECTED,
			SG_TACTIC_EXECUTION_DIAGNOSTIC_RUNTIME,
			SG_TACTIC_EXECUTION_REASON_RUNTIME, 0U);
	}
	family = PreparedFamily(&prepared);
	memset(&candidate, 0, sizeof(candidate));
	candidate.prepared = prepared;
	SecureClear(&prepared, sizeof(prepared));
	seal_status = SealAction(&candidate.prepared, &candidate.live,
		&candidate.action);
	if (seal_status != SG_TACTIC_EXECUTION_OWNER_OK)
	{
		(void)SG_TacticRuntimePreparedStepRelease(&candidate.prepared);
		SecureClear(&candidate, sizeof(candidate));
		return DiagnosticSet(diagnostic_out, seal_status, family,
			seal_status == SG_TACTIC_EXECUTION_OWNER_WITNESS_INCOMPLETE ?
			SG_TACTIC_EXECUTION_REASON_WITNESS :
			SG_TACTIC_EXECUTION_REASON_RUNTIME, 0U);
	}
	runtime_status = SG_TacticRuntimePreparedStepConsume(&candidate.prepared);
	if (runtime_status != SG_TACTIC_RUNTIME_OK)
	{
		(void)SG_TacticRuntimePreparedStepRelease(&candidate.prepared);
		SecureClear(&candidate, sizeof(candidate));
		return DiagnosticSet(diagnostic_out,
			SG_TACTIC_EXECUTION_OWNER_PREPARE_REJECTED,
			SG_TACTIC_EXECUTION_DIAGNOSTIC_STRATEGY,
			SG_TACTIC_EXECUTION_REASON_STRATEGY, 0U);
	}
	candidate.owner_epoch = owner->owner_epoch;
	candidate.sequence = owner->next_sequence++;
	if (!SG_AuthorityEntropyFill(&candidate.token,
		sizeof(candidate.token)) || TokenIsZero(&candidate.token))
	{
		(void)SG_TacticRuntimePreparedStepRelease(&candidate.prepared);
		SecureClear(&candidate, sizeof(candidate));
		return DiagnosticSet(diagnostic_out,
			SG_TACTIC_EXECUTION_OWNER_ENTROPY_REJECTED,
			SG_TACTIC_EXECUTION_DIAGNOSTIC_OWNER,
			SG_TACTIC_EXECUTION_REASON_ENTROPY, 0U);
	}
	candidate.diagnostic_digest = PendingDiagnosticDigest(&candidate);
	candidate.occupied = 1U;
	owner->pending = candidate;
	*token_out = candidate.token;
	seal_status = DiagnosticSet(diagnostic_out,
		SG_TACTIC_EXECUTION_OWNER_OK,
		family, SG_TACTIC_EXECUTION_REASON_NONE,
		candidate.diagnostic_digest);
	SecureClear(&candidate, sizeof(candidate));
	return seal_status;
}

static int ActionCurrent(const sg_tactic_execution_pending_t *pending)
{
#ifdef SG_TACTIC_EXECUTION_OWNER_TESTING
	return pending->action.kind == SG_TACTIC_EXECUTION_ACTION_TEST &&
		pending->action.value.test.sealed_epoch != 0U &&
		pending->action.value.test.sealed_epoch == test_current_epoch;
#else
	(void)pending;
	return 0;
#endif
}

static sg_tactic_execution_owner_status_t ExecuteAction(
	sg_tactic_execution_owner_t *owner,
	const sg_tactic_execution_pending_t *pending)
{
#ifdef SG_TACTIC_EXECUTION_OWNER_TESTING
	(void)owner;
	(void)pending;
	return test_action_outcome;
#else
	(void)owner;
	(void)pending;
	return SG_TACTIC_EXECUTION_OWNER_ACTION_REJECTED;
#endif
}

sg_tactic_execution_owner_status_t SG_TacticExecutionOwnerCommit(
	sg_tactic_execution_owner_t *owner,
	const sg_tactic_execution_token_t *token,
	sg_tactic_execution_diagnostic_t *diagnostic_out)
{
	sg_tactic_execution_pending_t local;
	sg_tactic_execution_owner_status_t status;
	sg_tactic_execution_diagnostic_family_t family;

	DiagnosticClear(diagnostic_out);
	if (!owner || !token || !diagnostic_out)
		return DiagnosticSet(diagnostic_out,
			SG_TACTIC_EXECUTION_OWNER_INVALID_ARGUMENT,
			SG_TACTIC_EXECUTION_DIAGNOSTIC_OWNER,
			SG_TACTIC_EXECUTION_REASON_ARGUMENT, 0U);
	if (owner->lost)
		return DiagnosticSet(diagnostic_out,
			SG_TACTIC_EXECUTION_OWNER_LOST,
			SG_TACTIC_EXECUTION_DIAGNOSTIC_OWNER,
			SG_TACTIC_EXECUTION_REASON_OWNER_LOST, 0U);
	if (!owner->pending.occupied)
		return DiagnosticSet(diagnostic_out,
			SG_TACTIC_EXECUTION_OWNER_EMPTY,
			SG_TACTIC_EXECUTION_DIAGNOSTIC_OWNER,
			SG_TACTIC_EXECUTION_REASON_EMPTY, 0U);
	if (!TokenEqual(token, &owner->pending.token))
		return DiagnosticSet(diagnostic_out,
			SG_TACTIC_EXECUTION_OWNER_TOKEN_REJECTED,
			SG_TACTIC_EXECUTION_DIAGNOSTIC_OWNER,
			SG_TACTIC_EXECUTION_REASON_TOKEN,
			owner->pending.diagnostic_digest);

	PendingTake(owner, &local);
	family = PreparedFamily(&local.prepared);
#ifdef SG_TACTIC_EXECUTION_OWNER_TESTING
	memset(&owner->last_receipt, 0, sizeof(owner->last_receipt));
	owner->last_receipt.attempt_sequence = local.sequence;
	owner->last_receipt.diagnostic_digest = local.diagnostic_digest;
	owner->last_receipt.family = family;
	owner->last_receipt.consumed_before_currentness =
		owner->pending.occupied == 0U ? 1U : 0U;
#endif
	if (local.owner_epoch != owner->owner_epoch)
	{
		status = SG_TACTIC_EXECUTION_OWNER_NOT_CURRENT;
		(void)DiagnosticSet(diagnostic_out, status,
			SG_TACTIC_EXECUTION_DIAGNOSTIC_OWNER,
			SG_TACTIC_EXECUTION_REASON_OWNER_LOST,
			local.diagnostic_digest);
	}
	else if (!SG_TacticRuntimePreparedStepCurrent(&local.prepared))
	{
		status = SG_TACTIC_EXECUTION_OWNER_NOT_CURRENT;
		(void)DiagnosticSet(diagnostic_out, status,
			SG_TACTIC_EXECUTION_DIAGNOSTIC_RUNTIME,
			SG_TACTIC_EXECUTION_REASON_FRAME,
			local.diagnostic_digest);
	}
	else if (!ActionCurrent(&local))
	{
		status = SG_TACTIC_EXECUTION_OWNER_NOT_CURRENT;
		(void)DiagnosticSet(diagnostic_out, status, family,
			SG_TACTIC_EXECUTION_REASON_LIVE_STATE,
			local.diagnostic_digest);
	}
	else
	{
#ifdef SG_TACTIC_EXECUTION_OWNER_TESTING
		owner->last_receipt.action_attempted = 1U;
#endif
		status = ExecuteAction(owner, &local);
		(void)DiagnosticSet(diagnostic_out, status, family,
			status == SG_TACTIC_EXECUTION_OWNER_OK ?
			SG_TACTIC_EXECUTION_REASON_NONE :
			status == SG_TACTIC_EXECUTION_OWNER_NOT_CURRENT ?
			SG_TACTIC_EXECUTION_REASON_LIVE_STATE :
			SG_TACTIC_EXECUTION_REASON_ACTION,
			local.diagnostic_digest);
	}
#ifdef SG_TACTIC_EXECUTION_OWNER_TESTING
	owner->last_receipt.status = status;
#endif
	PendingReleaseAndClear(&local);
	return status;
}

sg_tactic_execution_owner_status_t SG_TacticExecutionOwnerCancel(
	sg_tactic_execution_owner_t *owner,
	const sg_tactic_execution_token_t *token,
	sg_tactic_execution_diagnostic_t *diagnostic_out)
{
	sg_tactic_execution_pending_t local;
	uint64_t digest;

	DiagnosticClear(diagnostic_out);
	if (!owner || !token || !diagnostic_out)
		return DiagnosticSet(diagnostic_out,
			SG_TACTIC_EXECUTION_OWNER_INVALID_ARGUMENT,
			SG_TACTIC_EXECUTION_DIAGNOSTIC_OWNER,
			SG_TACTIC_EXECUTION_REASON_ARGUMENT, 0U);
	if (owner->lost)
		return DiagnosticSet(diagnostic_out,
			SG_TACTIC_EXECUTION_OWNER_LOST,
			SG_TACTIC_EXECUTION_DIAGNOSTIC_OWNER,
			SG_TACTIC_EXECUTION_REASON_OWNER_LOST, 0U);
	if (!owner->pending.occupied)
		return DiagnosticSet(diagnostic_out,
			SG_TACTIC_EXECUTION_OWNER_EMPTY,
			SG_TACTIC_EXECUTION_DIAGNOSTIC_OWNER,
			SG_TACTIC_EXECUTION_REASON_EMPTY, 0U);
	if (!TokenEqual(token, &owner->pending.token))
		return DiagnosticSet(diagnostic_out,
			SG_TACTIC_EXECUTION_OWNER_TOKEN_REJECTED,
			SG_TACTIC_EXECUTION_DIAGNOSTIC_OWNER,
			SG_TACTIC_EXECUTION_REASON_TOKEN,
			owner->pending.diagnostic_digest);
	PendingTake(owner, &local);
	digest = local.diagnostic_digest;
	PendingReleaseAndClear(&local);
	return DiagnosticSet(diagnostic_out, SG_TACTIC_EXECUTION_OWNER_OK,
		SG_TACTIC_EXECUTION_DIAGNOSTIC_OWNER,
		SG_TACTIC_EXECUTION_REASON_NONE, digest);
}

void SG_TacticExecutionOwnerCancelSubject(
	sg_tactic_execution_owner_t *owner,
	const sg_localization_subject_t *subject)
{
	const sg_localization_subject_t *pending_subject;

	if (!owner || !subject || owner->lost || !owner->pending.occupied)
		return;
	pending_subject = &owner->pending.prepared.frame.subject;
	if (pending_subject->client_id == subject->client_id &&
		pending_subject->spawn_generation == subject->spawn_generation)
		OwnerPendingRelease(owner);
}

void SG_TacticExecutionOwnerCancelAll(sg_tactic_execution_owner_t *owner)
{
	OwnerPendingRelease(owner);
}

void SG_TacticExecutionOwnerLost(sg_tactic_execution_owner_t *owner)
{
	if (!owner)
		return;
	owner->lost = 1U;
	OwnerPendingRelease(owner);
}

int SG_TacticExecutionOwnerPending(
	const sg_tactic_execution_owner_t *owner)
{
	return owner && !owner->lost && owner->pending.occupied != 0U;
}

int SG_TacticExecutionOwnerCurrent(
	const sg_tactic_execution_owner_t *owner)
{
	return owner && !owner->lost && owner->owner_epoch != 0U &&
		owner->next_sequence != 0U;
}

#ifdef SG_TACTIC_EXECUTION_OWNER_TESTING
void SG_TacticExecutionOwnerTestConfigure(uint64_t seal_epoch,
	uint64_t current_epoch,
	sg_tactic_execution_owner_status_t action_outcome)
{
	test_seal_epoch = seal_epoch;
	test_current_epoch = current_epoch;
	test_action_outcome = action_outcome;
}

void SG_TacticExecutionOwnerTestSetCurrentEpoch(uint64_t current_epoch)
{
	test_current_epoch = current_epoch;
}

int SG_TacticExecutionOwnerTestLastReceipt(
	const sg_tactic_execution_owner_t *owner,
	sg_tactic_execution_owner_test_receipt_t *receipt_out)
{
	if (!owner || !receipt_out || owner->last_receipt.attempt_sequence == 0U)
		return 0;
	*receipt_out = owner->last_receipt;
	return 1;
}
#endif
