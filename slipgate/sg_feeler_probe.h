#ifndef SG_FEELER_PROBE_H
#define SG_FEELER_PROBE_H

#include "g_local.h"

typedef struct
{
	trace_t trace;
	float yaw;
	qboolean teammate_blocked;
} sg_feeler_probe_t;

sg_feeler_probe_t SG_FeelerProbe(edict_t *e, int team, float yaw,
	float reach, qboolean allow_teammate_redirect);

#endif
