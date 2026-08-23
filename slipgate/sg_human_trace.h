/* Exact human Pmove evidence capture for offline traversal diagnosis. */
#ifndef SG_HUMAN_TRACE_H
#define SG_HUMAN_TRACE_H

#include "../g_local.h"

void SG_HumanTraceNewLevel(void);
void SG_HumanTracePmove(edict_t *entity,
	const pmove_state_t *before, const pmove_t *after);

#endif /* SG_HUMAN_TRACE_H */
