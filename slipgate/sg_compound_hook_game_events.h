#ifndef SG_COMPOUND_HOOK_GAME_EVENTS_H
#define SG_COMPOUND_HOOK_GAME_EVENTS_H

#include "sg_compound_guard.h"
#include "sg_compound_hook_live.h"

struct edict_s;
struct sg_bot_s;

typedef struct sg_compound_hook_game_events_s
{
	sg_mover_subject_t bolt_subject;
	struct edict_s *abort_bolt;
	struct edict_s *attach_target;
	qboolean bolt_valid;
	qboolean attach_pending;
	qboolean attached;
	qboolean release_requested;
	qboolean abort_pending;
	qboolean abort_recovery;
	qboolean abort_consumed;
	qboolean abort_receipt;
	qboolean bolt_evicted;
	qboolean release_applied;
	qboolean door_passed;
	qboolean contaminated;
} sg_compound_hook_game_events_t;

typedef enum sg_compound_hook_game_event_gate_e
{
	SG_COMPOUND_HOOK_GAME_EVENT_BYPASS = -1,
	SG_COMPOUND_HOOK_GAME_EVENT_DENIED = 0,
	SG_COMPOUND_HOOK_GAME_EVENT_ACCEPTED = 1
} sg_compound_hook_game_event_gate_t;

#define SG_COMPOUND_HOOK_GAME_EVENTS_INITIALIZER { 0 }

void SG_CompoundHookGameEventsReset(struct sg_bot_s *bot);
qboolean SG_CompoundHookGameEventsIdle(const struct sg_bot_s *bot);
void SG_CompoundHookGameObserveSafety(struct sg_bot_s *bot,
	qboolean door_passed, qboolean contaminated);
qboolean SG_CompoundHookGamePeekSafety(const struct sg_bot_s *bot,
	qboolean *door_passed, qboolean *contaminated);
qboolean SG_CompoundHookGameTakeSafety(struct sg_bot_s *bot,
	qboolean *door_passed, qboolean *contaminated);

sg_compound_hook_live_result_t SG_CompoundHookGameLinked(
	struct edict_s *client, struct edict_s *bolt,
	const sg_mover_subject_t *subject);
sg_compound_hook_game_event_gate_t SG_CompoundHookGameAttachWillApply(
	struct edict_s *bolt,
	struct edict_s *target, const csurface_t *surface);
sg_compound_hook_live_result_t SG_CompoundHookGameAttached(
	struct edict_s *bolt);
sg_compound_hook_live_result_t SG_CompoundHookGamePullApplied(
	struct edict_s *client, struct edict_s *bolt);
sg_compound_hook_game_event_gate_t SG_CompoundHookGameReleaseRequested(
	struct edict_s *client,
	struct edict_s *bolt);
sg_compound_hook_game_event_gate_t SG_CompoundHookGameAbortBegin(
	struct edict_s *client,
	struct edict_s *bolt);
sg_compound_hook_live_result_t SG_CompoundHookGameAbortEnd(
	struct edict_s *client);

qboolean SG_CompoundHookGameRecoveryAbortBegin(struct sg_bot_s *bot,
	const sg_compound_hook_live_snapshot_t *snapshot,
	const sg_compound_hook_live_bolt_t *bolt);
qboolean SG_CompoundHookGameRecoveryAbortEnd(struct sg_bot_s *bot,
	const sg_compound_hook_live_snapshot_t *snapshot,
	const sg_compound_hook_live_bolt_t *bolt);

sg_compound_hook_live_host_result_t SG_CompoundHookGameAuthorizeEvent(
	struct sg_bot_s *bot,
	const sg_compound_hook_live_snapshot_t *snapshot,
	sg_compound_hook_live_event_t event,
	const sg_compound_hook_live_bolt_t *bolt);

#endif
