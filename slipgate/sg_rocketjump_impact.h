#ifndef SG_ROCKETJUMP_IMPACT_H
#define SG_ROCKETJUMP_IMPACT_H

#include "g_local.h"

static inline qboolean SG_RocketJumpStaticWorldAuthenticated(
	const edict_t *entity)
{
	return entity && g_edicts && globals.edicts == g_edicts &&
	    globals.edict_size == (int)sizeof(edict_t) &&
	    globals.num_edicts > 0 && globals.num_edicts <= MAX_EDICTS &&
	    game.maxentities >= globals.num_edicts &&
	    game.maxentities <= MAX_EDICTS &&
	    globals.max_edicts == game.maxentities && entity == g_edicts &&
	    entity->inuse && entity->s.number == 0;
}

static inline qboolean SG_RocketJumpWorldImpact(const trace_t *trace)
{
	return trace && isfinite(trace->fraction) &&
	    !trace->startsolid && !trace->allsolid &&
	    trace->fraction >= 0.0f && trace->fraction < 1.0f &&
	    SG_RocketJumpStaticWorldAuthenticated(trace->ent) &&
	    (!trace->surface || !(trace->surface->flags & SURF_SKY));
}

#endif /* SG_ROCKETJUMP_IMPACT_H */
