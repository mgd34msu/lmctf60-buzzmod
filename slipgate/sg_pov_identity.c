#include "../g_local.h"
#include "sg_local.h"
#include "sg_bot.h"
#include "sg_pov_identity.h"

static unsigned long long sg_next_pov_instance = 1ULL;

static qboolean SG_BotPOVTokenInUse(unsigned long long token)
{
	int i;

	for (i = 0; i < SG_MAXBOTS; i++)
		if (sg_bots[i].instance_token == token)
			return true;
	return false;
}

qboolean SG_BotPOVInstanceAssign(struct sg_bot_s *bot)
{
	unsigned long long token;

	if (!bot || bot->active || bot->instance_token != 0ULL)
		return false;
	do
	{
		token = sg_next_pov_instance++;
		if (sg_next_pov_instance == 0ULL)
			sg_next_pov_instance = 1ULL;
	} while (token == 0ULL || SG_BotPOVTokenInUse(token));
	bot->instance_token = token;
	return true;
}

void SG_BotPOVInstanceReset(struct sg_bot_s *bot)
{
	if (bot)
		bot->instance_token = 0ULL;
}

qboolean SG_BotPOVIdentity(edict_t *ent, int *slot_out,
	unsigned long long *instance_out)
{
	int i;

	if (!ent || !ent->inuse || !ent->client || !(ent->flags & FL_BOT) ||
	    !ent->client->pers.connected)
		return false;
	for (i = 0; i < SG_MAXBOTS; i++)
	{
		if (!sg_bots[i].active || sg_bots[i].ent != ent ||
		    sg_bots[i].instance_token == 0ULL)
			continue;
		if (slot_out)
			*slot_out = i;
		if (instance_out)
			*instance_out = sg_bots[i].instance_token;
		return true;
	}
	return false;
}

edict_t *SG_BotPOVResolve(int slot, unsigned long long instance_token)
{
	sg_bot_t *bot;
	edict_t *ent;

	if (slot < 0 || slot >= SG_MAXBOTS || instance_token == 0ULL)
		return NULL;
	bot = &sg_bots[slot];
	ent = bot->ent;
	if (!bot->active || bot->instance_token != instance_token || !ent ||
	    !ent->inuse || !ent->client || !(ent->flags & FL_BOT) ||
	    !ent->client->pers.connected)
		return NULL;
	return ent;
}
