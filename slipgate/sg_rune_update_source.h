/* Ownership boundary for a resident or transient update source RUNE. */
#ifndef SG_RUNE_UPDATE_SOURCE_H
#define SG_RUNE_UPDATE_SOURCE_H

#include "sg_rune.h"

typedef struct sg_rune_update_source_s
{
	rune_t *rune;
	qboolean transient;
} sg_rune_update_source_t;

qboolean SG_RuneUpdateSourceAcquire(const char *mapname,
	sg_rune_update_source_t *source);
void SG_RuneUpdateSourceRelease(sg_rune_update_source_t *source);

#endif /* SG_RUNE_UPDATE_SOURCE_H */
