#ifndef SG_HUMAN_TRACE_H
#define SG_HUMAN_TRACE_H

#include "../g_local.h"

void SG_HumanTraceNewLevel(void);
void SG_HumanTraceMatchEnd(void);
void SG_HumanTracePmove(edict_t *entity,
	const pmove_state_t *before, const pmove_t *after);
void SG_HumanTraceHookFire(edict_t *entity, edict_t *hook);
void SG_HumanTraceHookAttach(edict_t *entity, edict_t *hook,
	edict_t *target);
void SG_HumanTraceHookRelease(edict_t *entity);
void SG_HumanTraceHookReset(edict_t *entity, edict_t *hook);

#endif /* SG_HUMAN_TRACE_H */
