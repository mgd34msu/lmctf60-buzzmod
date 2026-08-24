#include "../g_local.h"

#include "sg_local.h"
#include "sg_bot.h"
#include "sg_client_ownership.h"

qboolean SG_OwnsBot(edict_t *ent)
{
	int i;

	if (!ent || !(ent->flags & FL_BOT))
		return false;

	for (i = 0; i < SG_MAXBOTS; i++)
		if (sg_bots[i].active && sg_bots[i].ent == ent)
			return true;
	return false;
}
