#include "../g_local.h"
#undef world
#include "sg_rune_game.h"

int SG_RuneGameGenerate(const char *mapname)
{
	gi.dprintf("rune: generation refused stage=module map=%s: this is the "
		"shipped module; build the generator module\n",
		mapname && mapname[0] ? mapname : "missing");
	return 0;
}
