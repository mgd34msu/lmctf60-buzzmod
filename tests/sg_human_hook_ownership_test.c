#include "../g_local.h"
#include "../slipgate/sg_local.h"
#include "../slipgate/sg_bot.h"

#include <assert.h>
#include <string.h>

sg_bot_t sg_bots[SG_MAXBOTS];

int main(void)
{
	edict_t ent;

	memset(&ent, 0, sizeof(ent));
	memset(sg_bots, 0, sizeof(sg_bots));
	sg_bots[0].active = true;
	sg_bots[0].ent = &ent;

	assert(!SG_OwnsBot(NULL));
	assert(!SG_OwnsBot(&ent));
	ent.flags |= FL_BOT;
	assert(SG_OwnsBot(&ent));
	sg_bots[0].active = false;
	assert(!SG_OwnsBot(&ent));

	return 0;
}
