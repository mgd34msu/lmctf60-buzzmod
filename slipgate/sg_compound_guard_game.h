/* sg_compound_guard_game.h -- live edict lifecycle for compound mover guard. */
#ifndef SG_COMPOUND_GUARD_GAME_H
#define SG_COMPOUND_GUARD_GAME_H

#include "sg_compound_guard.h"

struct edict_s;

/* These hooks only maintain identity and orphan lifecycle.  They do not
 * acquire a mover lease or enable any dormant RUNE action. */
sg_compound_guard_result_t SG_CompoundGuardGameLevelReset(void);
void SG_RosterStorageReset(void);
void SG_CompoundGuardGameStorageWillFree(void);
void SG_CompoundGuardGameFrame(void);
/* Called by G_RunEntity before any non-client edict callback or physics.
 * Exact unowned edicts retain stock behavior.  An owned door member (or a
 * current teammate of one) dispatches only after its complete pusher team and
 * every generation-tracked SG client, corpse, and hook bolt are positively
 * validated against the immediate quantized push sweep. */
int SG_CompoundGuardGameEntityMayDispatch(struct edict_s *entity);
/* Called when the gate denies an entity.  A valid guarded captain defers the
 * complete team's stock absolute think schedule by one server frame, matching
 * SV_Physics_Pusher's blocked rollback.  Malformed teams instead lose any
 * completion arm and remain fail-closed. */
void SG_CompoundGuardGameEntityDeferred(struct edict_s *entity);
/* Direct captain entry retained for focused callers/tests. */
int SG_CompoundGuardGamePusherMayAdvance(struct edict_s *captain);

sg_compound_guard_result_t SG_CompoundGuardGameBotSlotReset(
	sg_compound_guard_bot_t *guard_bot);
sg_compound_guard_result_t SG_CompoundGuardGameBotAttach(
	sg_compound_guard_bot_t *guard_bot, int bot_slot,
	struct edict_s *client);

sg_compound_guard_result_t SG_CompoundGuardGameBodyQueueInit(
	struct edict_s *body);
sg_compound_guard_result_t SG_CompoundGuardGameBodyWillReplace(
	struct edict_s *body);
sg_compound_guard_result_t SG_CompoundGuardGameBodyDidCopy(
	struct edict_s *client, struct edict_s *body);

sg_compound_guard_result_t SG_CompoundGuardGameClientSpawned(
	struct edict_s *client);
sg_compound_guard_result_t SG_CompoundGuardGamePlayerDie(
	struct edict_s *client);
sg_compound_guard_result_t SG_CompoundGuardGameClientDisconnected(
	struct edict_s *client);

/* A bolt generation begins at its first completed world link.  EntityFreed
 * is the centralized postcondition hook: protected client/body edicts that
 * G_FreeEdict refuses to free must not call it. */
sg_compound_guard_result_t SG_CompoundGuardGameHookLinked(
	struct edict_s *client, struct edict_s *bolt);
void SG_CompoundGuardGameEntityFreed(struct edict_s *entity);
sg_compound_guard_result_t SG_CompoundGuardGameBoltEvicted(
	struct edict_s *client, struct edict_s *bolt);

#ifdef SG_COMPOUND_GUARD_GAME_TEST
void SG_CompoundGuardGameTestSetNextGeneration(uint64_t next_generation);
void SG_CompoundGuardGameTestSetEntityGeneration(int edict_key,
	uint64_t generation, int present);
#endif

#endif /* SG_COMPOUND_GUARD_GAME_H */
