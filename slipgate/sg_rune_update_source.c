/* Update source lifetime without publishing or mutating gameplay state. */
#include "../g_local.h"
#include "sg_local.h"
#include "sg_rune_update_source.h"

qboolean SG_RuneUpdateSourceAcquire(const char *mapname,
	sg_rune_update_source_t *source)
{
	if (!source)
		return false;
	source->rune = SG_Rune();
	source->transient = false;
	if (source->rune)
		return true;
	source->rune = Rune_Load(mapname);
	source->transient = source->rune != NULL;
	return source->transient;
}

void SG_RuneUpdateSourceRelease(sg_rune_update_source_t *source)
{
	if (!source)
		return;
	if (source->transient)
		Rune_Free(source->rune);
	source->rune = NULL;
	source->transient = false;
}
