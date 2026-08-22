/* sg_declared_door_guard.h -- authenticated door-plan shared-mover authority. */
#ifndef SG_DECLARED_DOOR_GUARD_H
#define SG_DECLARED_DOOR_GUARD_H

#include "sg_compound_guard.h"

struct sg_bot_s;
struct edict_s;

/* These entry points adapt an already-live authenticated door declaration to
 * the process-wide mover guard. */
sg_compound_guard_result_t SG_DeclaredDoorGuardAcquire(
	struct sg_bot_s *bot, int link_index);
sg_compound_guard_result_t SG_DeclaredCarrierDoorGuardAcquire(
	struct sg_bot_s *bot, int link_index, int stage);
sg_compound_guard_result_t SG_DeclaredDoorGuardAuthorize(
	struct sg_bot_s *bot, int link_index);
/* Exact first-mutation authority: tuple authorization plus a fresh proof that
 * every tracked client, corpse, and hook is outside the captured set. */
sg_compound_guard_result_t SG_DeclaredDoorGuardAuthorizeActivation(
	struct sg_bot_s *bot, int link_index);
sg_compound_guard_result_t SG_DeclaredDoorGuardPause(
	struct sg_bot_s *bot);
sg_compound_guard_result_t SG_DeclaredDoorGuardResume(
	struct sg_bot_s *bot, int link_index);
/* Extend an already-TOP physical set after exact current-set authentication.
 * PAUSED permits only this bounded protective timer mutation, never movement
 * or trigger authority. */
sg_compound_guard_result_t SG_DeclaredDoorGuardHoldOpen(
	struct sg_bot_s *bot, int lease_ms);
sg_compound_guard_result_t SG_DeclaredDoorGuardReleaseProvedClear(
	struct sg_bot_s *bot);
/* Unsupported trigger/button paths retain stock behavior only when their
 * actual physical door team has no live shared-mover claim. */
int SG_DeclaredDoorGuardActivationAvailable(struct edict_s *door_master);
/* Unsupported multi-trigger callbacks have no exact physical closure.  While
 * any shared mover record exists they must fail closed before debounce,
 * killtarget, or arbitrary target side effects. */
int SG_DeclaredDoorGuardAnyClaim(void);
/* Conservative cross-bot scheduling boundary.  While any mover record exists,
 * only its exact ACTIVE/PAUSED declared-door owner may submit a bot frame;
 * this prevents a later bot slot entering a set after same-frame activation. */
sg_compound_guard_run_t SG_DeclaredDoorGuardRunState(struct sg_bot_s *bot);

#endif /* SG_DECLARED_DOOR_GUARD_H */
