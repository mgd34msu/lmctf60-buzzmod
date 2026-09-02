#include "../g_local.h"
#include "../g_ctffunc.h"
#undef world
#include "sg_local.h"
#include "sg_bot.h"
#include "sg_bot_orders.h"

#include <ctype.h>
#include <string.h>

#include "sg_util.h"

#define ORDER_SECONDS 90.0f

typedef struct order_s
{
	int role;                 /* -1 none */
	int escort_client;        /* g_edicts index of the escort target, or 0 */
	float until;
} order_t;

static order_t sg_orders[SG_MAXBOTS];

void SG_OrdersReset(void)
{
	int i;

	for (i = 0; i < SG_MAXBOTS; i++)
	{
		sg_orders[i].role = -1;
		sg_orders[i].escort_client = 0;
		sg_orders[i].until = 0.0f;
	}
}

static int SlotOf(edict_t *bot)
{
	int i;

	for (i = 0; i < SG_MAXBOTS; i++)
		if (sg_bots[i].active && sg_bots[i].ent == bot)
			return i;
	return -1;
}

int SG_OrderedRole(edict_t *bot)
{
	int slot = SlotOf(bot);

	if (slot < 0 || sg_orders[slot].role < 0 || level.time > sg_orders[slot].until)
		return -1;
	return sg_orders[slot].role;
}

edict_t *SG_OrderEscortTarget(edict_t *bot)
{
	int slot = SlotOf(bot);
	edict_t *target;

	if (slot < 0 || sg_orders[slot].role != SG_ROLE_ESCORT ||
		level.time > sg_orders[slot].until || sg_orders[slot].escort_client <= 0)
		return NULL;
	target = &g_edicts[sg_orders[slot].escort_client];
	return target->inuse && target->client && target->health > 0 ? target : NULL;
}

/* Lower-cased words of the line, at most eight. */
static int Words(const char *text, char words[8][32])
{
	int count = 0, n = 0;

	while (*text && count < 8)
	{
		if (isalpha((unsigned char)*text))
		{
			if (n < 31)
				words[count][n++] = (char)tolower((unsigned char)*text);
		}
		else if (n)
		{
			words[count][n] = '\0';
			count++;
			n = 0;
		}
		text++;
	}
	if (n && count < 8)
	{
		words[count][n] = '\0';
		count++;
	}
	return count;
}

static int RoleWord(const char *word)
{
	if (!strcmp(word, "attack") || !strcmp(word, "cap") || !strcmp(word, "go"))
		return SG_ROLE_ATTACK;
	if (!strcmp(word, "defend") || !strcmp(word, "def") || !strcmp(word, "d"))
		return SG_ROLE_DEFEND;
	if (!strcmp(word, "recover") || !strcmp(word, "return") ||
		!strcmp(word, "getflag"))
		return SG_ROLE_RECOVER;
	if (!strcmp(word, "escort") || !strcmp(word, "cover") ||
		!strcmp(word, "follow"))
		return SG_ROLE_ESCORT;
	if (!strcmp(word, "free") || !strcmp(word, "clear"))
		return -2;   /* release the order */
	return -1;
}

/* A bot on the speaker's team whose name (without the [SG] tag) is word. */
static int BotNamed(const char *word, int team)
{
	int i;

	for (i = 0; i < SG_MAXBOTS; i++)
	{
		const char *name;
		char bare[32];
		int n = 0;

		if (!sg_bots[i].active || !sg_bots[i].ent || !sg_bots[i].ent->client ||
			sg_bots[i].ent->client->ctf.teamnum != team)
			continue;
		name = sg_bots[i].ent->client->pers.netname;
		if (!strncmp(name, "[SG]", 4))
			name += 4;
		while (*name && n < 31)
			bare[n++] = (char)tolower((unsigned char)*name++);
		bare[n] = '\0';
		if (!strcmp(bare, word))
			return i;
	}
	return -1;
}

/* A player on the team named by word, or the speaker for "me". */
static edict_t *PlayerNamed(const char *word, edict_t *speaker, int team)
{
	int i;

	if (!strcmp(word, "me"))
		return speaker;
	for (i = 1; i <= game.maxclients; i++)
	{
		edict_t *e = &g_edicts[i];
		const char *name;
		char bare[32];
		int n = 0;

		if (!e->inuse || !e->client || e->client->ctf.teamnum != team)
			continue;
		name = e->client->pers.netname;
		if (!strncmp(name, "[SG]", 4))
			name += 4;
		while (*name && n < 31)
			bare[n++] = (char)tolower((unsigned char)*name++);
		bare[n] = '\0';
		if (!strcmp(bare, word))
			return e;
	}
	return NULL;
}

static void Give(int slot, int role, edict_t *escort)
{
	if (role == -2)
	{
		sg_orders[slot].role = -1;
		sg_orders[slot].escort_client = 0;
		sg_orders[slot].until = 0.0f;
		return;
	}
	sg_orders[slot].role = role;
	sg_orders[slot].escort_client = escort ? (int)(escort - g_edicts) : 0;
	sg_orders[slot].until = level.time + ORDER_SECONDS;
}

int SG_OrdersHear(edict_t *speaker, const char *text)
{
	char words[8][32];
	int count, i, team, role = -1, role_at = -1, target_slot = -2;
	edict_t *escort = NULL;

	if (!speaker || !speaker->client || (speaker->flags & FL_BOT) || !text)
		return 0;
	team = speaker->client->ctf.teamnum;
	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return 0;
	count = Words(text, words);
	for (i = 0; i < count; i++)
	{
		int r = RoleWord(words[i]);

		if (r != -1 && role == -1)
		{
			role = r;
			role_at = i;
		}
	}
	if (role == -1)
		return 0;
	/* The addressee: a bot's name anywhere in the line, or "all" / "bots" /
	 * "everyone"; a line with no addressee orders every bot too. */
	for (i = 0; i < count; i++)
	{
		int slot;

		if (i == role_at)
			continue;
		if (!strcmp(words[i], "all") || !strcmp(words[i], "bots") ||
			!strcmp(words[i], "everyone"))
		{
			target_slot = -1;
			break;
		}
		slot = BotNamed(words[i], team);
		if (slot >= 0)
		{
			target_slot = slot;
			break;
		}
	}
	if (target_slot == -2)
		target_slot = -1;
	if (role == SG_ROLE_ESCORT)
	{
		escort = speaker;
		for (i = role_at + 1; i < count; i++)
		{
			edict_t *named = PlayerNamed(words[i], speaker, team);

			if (named)
			{
				escort = named;
				break;
			}
		}
	}
	if (target_slot >= 0)
		Give(target_slot, role, escort);
	else
		for (i = 0; i < SG_MAXBOTS; i++)
			if (sg_bots[i].active && sg_bots[i].ent && sg_bots[i].ent->client &&
				sg_bots[i].ent->client->ctf.teamnum == team)
				Give(i, role, escort);
	return 1;
}
