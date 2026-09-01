#include "../g_local.h"
#undef world

#include "sg_rune_compact_game.h"

int SG_RuneCompactGameGenerate(const char *mapname)
{
	if (mapname == NULL || mapname[0] == '\0')
	{
		gi.dprintf("rune: compact generation refused stage=offline-module "
			"map=missing\n");
		return 0;
	}
	gi.dprintf("rune: compact generation refused stage=offline-module map=%s\n",
		mapname);
	return 0;
}
