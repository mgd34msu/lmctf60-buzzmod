/* sg_compound_guard.h -- game-boundary adapter for shared mover leases. */
#ifndef SG_COMPOUND_GUARD_H
#define SG_COMPOUND_GUARD_H

#include <stddef.h>
#include <stdint.h>

#include "sg_mover_lease.h"

/* Host observations are deliberately tri-state.  An unavailable observation
 * is never interpreted as proof that a subject is clear. */
typedef enum sg_compound_guard_observation_e
{
	SG_COMPOUND_GUARD_OBSERVATION_ERROR = -1,
	SG_COMPOUND_GUARD_NO = 0,
	SG_COMPOUND_GUARD_YES = 1
} sg_compound_guard_observation_t;

typedef struct sg_compound_guard_host_s
{
	void *context;
	/* Back this with an adapter-owned process-lifetime generation array; no
	 * edict field or link counter is a stable identity.  Mint once per edict
	 * incarnation: bot attach/PutClientInServer completion, body-queue edict
	 * creation in InitBodyQue, each CopyToBodyQue overwrite completion, or bolt
	 * spawn--never an ordinary motion relink.
	 * Values are unique, nonzero, and never wrap/reuse.  Exhaustion makes this
	 * callback return ERROR permanently until process restart.  Retain the last
	 * value while absent.  YES returns it; NO means no live edict. */
	sg_compound_guard_observation_t (*identity)(void *context,
		int32_t edict_key, uint64_t *generation_out);
	/* Called only after identity matched the captured subject generation. */
	sg_compound_guard_observation_t (*solid)(void *context,
		const sg_mover_subject_t *subject);
	/* YES proves the complete subject bounds outside every declared sweep. */
	sg_compound_guard_observation_t (*outside_sweep)(void *context,
		const sg_mover_subject_t *subject, const sg_mover_key_t *keys,
		size_t key_count);
	/* YES proves every generation-tracked physical subject in the host outside
	 * the complete captured set.  This record-wide proof is required before
	 * acquisition and every release, including ownerless ORPHAN cleanup.  The
	 * game adapter may terminalize only a generation-matched, SG-provenance
	 * BODY_QUEUE corpse that is inside, and must re-observe it nonsolid before
	 * returning YES; CLIENT and HOOK_BOLT observations remain read-only. */
	sg_compound_guard_observation_t (*all_subjects_outside)(void *context,
		const sg_mover_key_t *keys, size_t key_count);
	/* Renew an already-open captured set without consulting mutable route or
	 * trigger metadata.  GuardFrame owns ORPHAN renewal; an owning controller
	 * may renew its synchronized ACTIVE/PAUSED/QUARANTINED record. */
	sg_compound_guard_observation_t (*hold_open)(void *context,
		sg_mover_lease_law_t law, const sg_mover_key_t *keys,
		size_t key_count, int lease_ms);
	/* YES proves a released captured set reached a stationary physical
	 * terminal: canonical closed pose, or a permanent-open terminal with no
	 * scheduled close.  A timer boundary or one bot loop is not a terminal. */
	sg_compound_guard_observation_t (*set_terminal)(void *context,
		sg_mover_lease_law_t law, const sg_mover_key_t *keys,
		size_t key_count);
} sg_compound_guard_host_t;

/* Embed this pointer-free handle in sg_bot_t.  It is process storage and must
 * be zero initially.  BotReset must run before its containing slot is erased. */
typedef struct sg_compound_guard_bot_s
{
	sg_mover_owner_t owner;
	sg_mover_subject_t client;
	sg_mover_ticket_t ticket;
	uint8_t attached;
	uint8_t reserved[7];
} sg_compound_guard_bot_t;

typedef enum sg_compound_guard_result_e
{
	SG_COMPOUND_GUARD_OK = 0,
	SG_COMPOUND_GUARD_INVALID_ARGUMENT,
	SG_COMPOUND_GUARD_INVALID_OWNER,
	SG_COMPOUND_GUARD_INVALID_SUBJECT,
	SG_COMPOUND_GUARD_INVALID_KEYS,
	SG_COMPOUND_GUARD_CONFLICT,
	SG_COMPOUND_GUARD_OWNER_BUSY,
	SG_COMPOUND_GUARD_FULL,
	SG_COMPOUND_GUARD_EXHAUSTED,
	SG_COMPOUND_GUARD_STALE_TICKET,
	SG_COMPOUND_GUARD_OWNER_MISMATCH,
	SG_COMPOUND_GUARD_SUBJECT_MISMATCH,
	SG_COMPOUND_GUARD_INVALID_TRANSITION,
	SG_COMPOUND_GUARD_QUARANTINE_LOCKED,
	SG_COMPOUND_GUARD_NOT_INITIALIZED,
	SG_COMPOUND_GUARD_NOT_ATTACHED,
	SG_COMPOUND_GUARD_NO_LEASE,
	SG_COMPOUND_GUARD_ENTITY_ABSENT,
	SG_COMPOUND_GUARD_HOST_ERROR,
	SG_COMPOUND_GUARD_IDENTITY_STALE,
	SG_COMPOUND_GUARD_NOT_CLEAR,
	SG_COMPOUND_GUARD_AUTHORITY_MISMATCH
} sg_compound_guard_result_t;

typedef struct sg_compound_guard_frame_stats_s
{
	size_t inspected;
	size_t held;
	size_t evicted;
	size_t released;
	size_t quarantined;
	size_t host_errors;
} sg_compound_guard_frame_stats_t;

/* One adapter instance owns one singleton registry shared by ordinary
 * RL_DOOR and compound-preopen claims.  Reinitialization is a level reset. */
sg_compound_guard_result_t SG_CompoundGuardInit(
	const sg_compound_guard_host_t *host);
sg_compound_guard_result_t SG_CompoundGuardLevelReset(void);

sg_compound_guard_result_t SG_CompoundGuardBotAttach(
	sg_compound_guard_bot_t *bot, int32_t bot_slot,
	int32_t client_edict_key);
/* After corpse copy and final respawn link, refresh only the same client-edict
 * subject with a newly minted generation.  The owner and any orphan ticket
 * stay intact; a later Frame ticket rotation is reconciled by owner on the
 * next guarded operation. */
sg_compound_guard_result_t SG_CompoundGuardBotRespawn(
	sg_compound_guard_bot_t *bot, int32_t client_edict_key);
/* ClientDisconnect integration: call after unlink/SOLID_NOT/inuse=false and
 * before BotReset.  A same-generation absent/nonsolid client proves a live
 * lease clear; a recycled identity or failed observation quarantines.  An
 * ORPHAN lease remains owned by Frame. */
sg_compound_guard_result_t SG_CompoundGuardBotDisconnected(
	sg_compound_guard_bot_t *bot);
/* ACTIVE/PAUSED ownership is quarantined before the handle is erased.  An
 * ORPHAN record is deliberately left in the singleton registry: Frame owns
 * its copied owner/ticket and remains able to prove or quarantine it after
 * the sg_bot_t occupant is gone. */
sg_compound_guard_result_t SG_CompoundGuardBotReset(
	sg_compound_guard_bot_t *bot);

/* These calls do not change any RUNE action/revision gate.  In particular,
 * CompoundPreopen only reserves already-authorized execution; it cannot make
 * a dormant compound action live.  DECLARED_DOOR and COMPOUND_PREOPEN share
 * one process-wide transaction: either acquire conflicts while any lease or
 * physical retirement under either law remains, even for disjoint key sets. */
sg_compound_guard_result_t SG_CompoundGuardAcquireDeclaredDoor(
	sg_compound_guard_bot_t *bot, const sg_mover_key_t *keys,
	size_t key_count, int link_index);
/* Ordinary runtime may bind the durable record to its exact activator edict
 * key so protective maintenance remains possible if rune metadata vanishes. */
sg_compound_guard_result_t SG_CompoundGuardAcquireDeclaredDoorBound(
	sg_compound_guard_bot_t *bot, const sg_mover_key_t *keys,
	size_t key_count, int link_index, uint32_t mechanism_index);
sg_compound_guard_result_t SG_CompoundGuardAcquireCompoundPreopen(
	sg_compound_guard_bot_t *bot, const sg_mover_key_t *keys,
	size_t key_count, int link_index, uint32_t mechanism_index);

sg_compound_guard_result_t SG_CompoundGuardValidate(
	sg_compound_guard_bot_t *bot, sg_mover_lease_record_t *record_out);
typedef enum sg_compound_guard_run_e
{
	SG_COMPOUND_GUARD_RUN_READY = 0,
	SG_COMPOUND_GUARD_RUN_WAIT,
	SG_COMPOUND_GUARD_RUN_TERMINAL
} sg_compound_guard_run_t;
/* Scheduling barrier.  Exact ACTIVE/PAUSED owners may run; a non-owner proved
 * outside waits, while one already inside must take the ordinary nonsolid
 * death path so it cannot be crushed or deadlock release.  After release, all
 * bot commands remain fenced until the captured physical set proves closed. */
sg_compound_guard_run_t SG_CompoundGuardBotRunState(
	const sg_compound_guard_bot_t *bot);
/* Revalidate the exact record, then prove every host-tracked physical subject
 * outside its captured set.  Intended for the first mover mutation boundary;
 * later owner egress legitimately occupies the sweep. */
sg_compound_guard_result_t SG_CompoundGuardAllSubjectsOutside(
	sg_compound_guard_bot_t *bot);
/* Mover/trigger callbacks authorize only an exact ACTIVE claim.  Generic
 * Validate remains inspection and must not be used as callback authority. */
sg_compound_guard_result_t SG_CompoundGuardAuthorize(
	sg_compound_guard_bot_t *bot, sg_mover_lease_law_t expected_law,
	const sg_mover_key_t *expected_keys, size_t expected_key_count,
	int expected_link_index, uint32_t expected_mechanism_index);
sg_compound_guard_result_t SG_CompoundGuardPause(
	sg_compound_guard_bot_t *bot);
sg_compound_guard_result_t SG_CompoundGuardResume(
	sg_compound_guard_bot_t *bot);
/* Renew protective TOP maintenance from the synchronized record's captured
 * law and mover keys only.  ACTIVE, PAUSED, and QUARANTINED owners may renew;
 * ORPHAN maintenance remains exclusively owned by GuardFrame.  Host failure
 * quarantines a nonterminal live record before returning. */
sg_compound_guard_result_t SG_CompoundGuardMaintain(
	sg_compound_guard_bot_t *bot);

/* First-death hook: captures the client corpse and optional live hook bolt.
 * Pass zero for bolt_edict_key when no bolt exists. */
sg_compound_guard_result_t SG_CompoundGuardOrphan(
	sg_compound_guard_bot_t *bot, int32_t bolt_edict_key);

/* CopyToBodyQue integration: WillReplace runs before unlinking/overwriting the
 * selected queue edict; BodyDidCopy runs after the new corpse is linked and
 * its host identity generation has advanced.  The singleton holds the old
 * claim across that synchronous transaction.  bot may be NULL for a copied
 * non-SLIPGATE client, but DidCopy itself is mandatory.  When a current bot
 * handoff succeeds, DidCopy returns OK even if cleanup terminalized an older
 * claim; retrying a committed lifecycle handoff is never valid. */
sg_compound_guard_result_t SG_CompoundGuardBodyWillReplace(
	int32_t body_edict_key);
sg_compound_guard_result_t SG_CompoundGuardBodyDidCopy(
	sg_compound_guard_bot_t *bot, int32_t body_edict_key);

/* Run after unlink/free made the captured bolt absent, nonsolid, or outside
 * its mover sweep.  The matching subject is evicted with a rotated ticket. */
sg_compound_guard_result_t SG_CompoundGuardBoltEvicted(
	sg_compound_guard_bot_t *bot, int32_t bolt_edict_key);

sg_compound_guard_result_t SG_CompoundGuardReleaseProvedClear(
	sg_compound_guard_bot_t *bot);
sg_compound_guard_result_t SG_CompoundGuardQuarantine(
	sg_compound_guard_bot_t *bot);

/* Sweep orphan records once before bots think.  Only positive host proof can
 * evict/release; identity replacement or callback failure quarantines.  An
 * open synchronous body-reuse transaction here means DidCopy was missed and
 * is quarantined before the sweep. */
void SG_CompoundGuardFrame(sg_compound_guard_frame_stats_t *stats_out);

/* Read-only physical-fence queries for callback admission.  A retirement is
 * the captured mover set between logical release and positive host proof of a
 * stationary terminal pose with every tracked subject clear in the same
 * frame.  Invalid/uninitialized input is reported as occupied so callers fail
 * closed at mutation boundaries. */
int SG_CompoundGuardAnyRetirement(void);
int SG_CompoundGuardRetirementOverlaps(const sg_mover_key_t *keys,
	size_t key_count);
/* Engine-pusher fence.  OK means this team overlaps either door law (including
 * physical retirement), NO_LEASE means it is unprotected, and all other
 * results are fail-closed.  NOT_INITIALIZED cannot hide a claim and therefore
 * leaves the stock path enabled. */
sg_compound_guard_result_t SG_CompoundGuardDoorPusherFence(
	const sg_mover_key_t *keys, size_t key_count);
/* Same fully validated registry observation without a mover-key filter.  This
 * lets a malformed/unbounded pusher team preserve stock behavior only when no
 * protected door transaction or physical retirement exists anywhere. */
sg_compound_guard_result_t SG_CompoundGuardAnyDoorTransaction(void);
/* Exact callback fence for the restricted compound mover class.  This query
 * is observation-only and ignores valid declared-door records, but reports
 * occupied for invalid input, unavailable/corrupt state, or overlap with any
 * live COMPOUND_PREOPEN record or physical retirement. */
int SG_CompoundGuardCompoundOverlaps(const sg_mover_key_t *keys,
	size_t key_count);
int SG_CompoundGuardRecordAt(size_t slot,
	sg_mover_lease_record_t *record_out, sg_mover_ticket_t *ticket_out);
const char *SG_CompoundGuardReason(sg_compound_guard_result_t result);

#endif /* SG_COMPOUND_GUARD_H */
