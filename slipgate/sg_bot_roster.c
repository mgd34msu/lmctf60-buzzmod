/* sg_bot_roster.c -- bots joining and leaving.
 *
 * A bot is an engine client the game module owns: it is spawned as a fake
 * client, connected through the same ClientConnect, ClientBegin, and
 * ClientDisconnect every human passes through, put on a team, and given a
 * name from the roster.  Botfill keeps each team at the size sv_botfill
 * asks for.  The engine facts here are exact: the order of the lifecycle
 * calls, the userinfo keys, the team write before inuse, the second skin
 * pass once the team is known. */
#include "../g_local.h"
#include "../g_ctffunc.h"
#undef world
#include "sg_local.h"
#include "sg_bot.h"

#include <string.h>

#include "sg_bot_persona.h"
#include "sg_bot_host.h"
#include "sg_bot_util.h"

void ClientDisconnect(edict_t *ent);
qboolean ClientConnect(edict_t *ent, char *userinfo);
void ClientBegin(edict_t *ent);
void ClientUserinfoChanged(edict_t *ent, char *userinfo);

sg_bot_t sg_bots[SG_MAXBOTS];


/* Scoped across SG_AddBotTeam's synchronous ClientConnect call only, so a
 * server-owned fake client passes the password check without carrying the
 * secret in its userinfo. */
static edict_t *sg_internal_connect_ent;

qboolean SG_InternalClientConnect(edict_t *ent)
{
	return ent && ent == sg_internal_connect_ent;
}

/* A slot is process storage.  Reusing it is an initialization event. */
static void SlotClear(sg_bot_t *bot)
{
	int slot = (int)(bot - sg_bots);

	if (slot >= 0 && slot < SG_MAXBOTS && bot->instance_token != 0ULL)
		POVLock_SGInstanceRetired(slot, bot->instance_token);
	memset(bot, 0, sizeof(*bot));
	SG_BotSlotInit(bot);
}

/* Disconnect, free the edict, clear the slot: in that order, so the slot
 * is not reusable until the edict is gone. */
static void Drop(int slot)
{
	edict_t *ent = sg_bots[slot].ent;

	if (ent && ent->client && ent->inuse)
		ClientDisconnect(ent);
	if (ent)
		SG_BotHostFreeClient(ent);
	SlotClear(&sg_bots[slot]);
}

/* ---- botfill ---------------------------------------------------------- */

static unsigned long long sg_next_token = 1ULL;
static float sg_botfill_next_check;
static int sg_botfill_over_streak[2];
static int sg_botfill_under_streak[2];

void Botfill_Reset(void)
{
	sg_botfill_next_check = 0.0f;
	memset(sg_botfill_over_streak, 0, sizeof(sg_botfill_over_streak));
	memset(sg_botfill_under_streak, 0, sizeof(sg_botfill_under_streak));
}

/* The lowest-scoring bot on the team; a carrier is never the one to yield
 * automatically. */
static int WorstIndex(int team, qboolean automatic)
{
	int i, worst = -1, worst_score = 0x7fffffff;

	for (i = 0; i < SG_MAXBOTS; i++)
	{
		edict_t *ent = sg_bots[i].ent;

		if (!sg_bots[i].active || !ent || !ent->inuse || !ent->client)
			continue;
		if (team && ent->client->ctf.teamnum != team)
			continue;
		if (automatic && ClientHasFlag(ent))
			continue;
		if (ent->client->resp.score < worst_score)
		{
			worst_score = ent->client->resp.score;
			worst = i;
		}
	}
	return worst;
}

static qboolean RemoveOne(int team)
{
	int worst = WorstIndex(team, true);

	if (worst < 0)
		return false;
	gi.bprintf(PRINT_HIGH, "%s yields its slot.\n",
		sg_bots[worst].ent->client->pers.netname);
	Drop(worst);
	return true;
}

/* sv_botfill names the players each team should field: one value for
 * both, or "red:blue".  Bots fill vacancies and yield surplus one at a
 * time, after three consecutive one-second samples agree, except that an
 * empty server fills at once. */
void Botfill_Frame(void)
{
	cvar_t *fill = gi.cvar("sv_botfill", "0", 0);
	int want[2];
	int humans[2] = { 0, 0 }, bots[2] = { 0, 0 };
	int i, t;
	qboolean acted = false;

	if (sscanf(fill->string, "%d:%d", &want[0], &want[1]) < 2)
		want[1] = want[0] = (int)fill->value;
	if ((want[0] <= 0 && want[1] <= 0) || SG_TimerPending(sg_botfill_next_check))
		return;
	SG_TimerArm(&sg_botfill_next_check, 1.0f);
	for (i = 0; i < game.maxclients; i++)
	{
		edict_t *e = g_edicts + 1 + i;

		if (!e->inuse || !e->client)
			continue;
		t = e->client->ctf.teamnum;
		if (t != CTF_TEAM_RED && t != CTF_TEAM_BLUE)
			continue;
		if (e->flags & FL_BOT)
			bots[SG_TeamIdx(t)]++;
		else
			humans[SG_TeamIdx(t)]++;
	}
	for (t = 0; t < 2 && !acted; t++)
	{
		if (humans[t] + bots[t] > want[t] && bots[t] > 0)
		{
			if (++sg_botfill_over_streak[t] >= 3 && RemoveOne(SG_TeamFromIdx(t)))
			{
				sg_botfill_over_streak[t] = 0;
				acted = true;
			}
		}
		else
			sg_botfill_over_streak[t] = 0;
	}
	for (t = 0; t < 2 && !acted; t++)
	{
		if (humans[t] + bots[t] < want[t])
		{
			if (bots[0] + bots[1] == 0 || ++sg_botfill_under_streak[t] >= 3)
			{
				SG_AddBotTeam(SG_TeamFromIdx(t));
				sg_botfill_under_streak[t] = 0;
				acted = true;
			}
		}
		else
			sg_botfill_under_streak[t] = 0;
	}
}

/* ---- ownership boundary ------------------------------------------------- */

/* The engine allocates real clients without consulting inuse.  When it
 * picks a fake client's slot, retire that bot before the human's
 * ClientConnect: the disconnect drops a carried flag and frees a live hook
 * instead of handing them to the next occupant. */
qboolean SG_RetireBotForClient(edict_t *ent)
{
	int i;

	for (i = 0; i < SG_MAXBOTS; i++)
	{
		if (!sg_bots[i].active || sg_bots[i].ent != ent)
			continue;
		Drop(i);
		return true;
	}
	return false;
}

/* An external path already replaced or cleared FL_BOT: forget the slot,
 * never disconnect the occupant. */
void SG_DisownBot(edict_t *ent)
{
	int i;

	for (i = 0; i < SG_MAXBOTS; i++)
		if (sg_bots[i].active && sg_bots[i].ent == ent)
		{
			SlotClear(&sg_bots[i]);
			return;
		}
}

/* ---- add ------------------------------------------------------------------ */

static uint32_t OccupiedNames(void)
{
	uint32_t occupied = 0U;
	int i, row;

	for (i = 1; i <= game.maxclients; i++)
	{
		edict_t *e = &g_edicts[i];

		if (!e->inuse || !e->client)
			continue;
		for (row = 0; row < SG_BotPersonaCount(); row++)
		{
			char candidate[32];

			Com_sprintf(candidate, sizeof(candidate), "[SG]%s",
				SG_BotPersonaAt(row)->name);
			if (!Q_stricmp(e->client->pers.netname, candidate))
			{
				occupied |= UINT32_C(1) << row;
				break;
			}
		}
	}
	return occupied;
}

qboolean SG_AddBot(void)
{
	return SG_AddBotTeam(0);
}

qboolean SG_AddBotTeam(int teamnum)
{
	edict_t *ent;
	char userinfo[MAX_INFO_STRING];
	char name[32];
	int i, slot = -1;
	int persona;

	(void)SG_LevelSetup();
	for (i = 0; i < SG_MAXBOTS; i++)
		if (!sg_bots[i].active)
		{
			slot = i;
			break;
		}
	if (slot < 0)
		return false;
	SlotClear(&sg_bots[slot]);
	memset(userinfo, 0, sizeof(userinfo));
	/* "[SG]Arach": a name a human or an earlier bot wears is occupied. */
	persona = SG_BotPersonaPick(OccupiedNames(), slot);
	if (persona < 0)
		return false;
	Com_sprintf(name, sizeof(name), "[SG]%s", SG_BotPersonaAt(persona)->name);
	Info_SetValueForKey(userinfo, "name", name);
	/* A CTF-conforming skin request; the team letter is corrected once the
	 * team is known. */
	Info_SetValueForKey(userinfo, "skin", va("male/rb-rm%d", 1 + (slot % 6)));
	Info_SetValueForKey(userinfo, "hand", "0");
	ent = SG_BotHostSpawnClient();
	if (!ent)
		return false;
	/* Recycled gclient storage: CTF state is not part of the new identity. */
	memset(&ent->client->ctf, 0, sizeof(ent->client->ctf));
	ent->flags &= ~FL_BOT;
	ent->inuse = false;
	sg_internal_connect_ent = ent;
	if (!ClientConnect(ent, userinfo))
	{
		sg_internal_connect_ent = NULL;
		SG_BotHostFreeClient(ent);
		return false;
	}
	sg_internal_connect_ent = NULL;
	/* Written while inuse is still false, so ClientBegin sees a client
	 * already on a team and keeps it. */
	if (teamnum == CTF_TEAM_RED || teamnum == CTF_TEAM_BLUE)
	{
		ent->client->ctf.teamnum = teamnum;
		if (ent->client->p_stats_player)
			ent->client->p_stats_player->info.teamnum = teamnum;
	}
	else
	{
		ent->client->ctf.teamnum = CTF_TEAM_UNDEFINED;
		if (ent->client->p_stats_player)
			ent->client->p_stats_player->info.teamnum = CTF_TEAM_UNDEFINED;
	}
	ent->inuse = true;
	ent->flags |= FL_BOT;
	ClientUserinfoChanged(ent, userinfo);
	ClientBegin(ent);
	if (ent->client->ctf.teamnum != CTF_TEAM_RED &&
		ent->client->ctf.teamnum != CTF_TEAM_BLUE)
	{
		ClientDisconnect(ent);
		SG_BotHostFreeClient(ent);
		SlotClear(&sg_bots[slot]);
		return false;
	}
	/* The second userinfo pass paints the team's uniform. */
	Info_SetValueForKey(ent->client->pers.userinfo, "skin",
		va("male/rb-%cm%d", ent->client->ctf.teamnum == CTF_TEAM_RED ? 'r' : 'b',
			1 + (slot % 6)));
	ClientUserinfoChanged(ent, ent->client->pers.userinfo);
	ent->client->ctf.extra_flags |=
		(CTF_EXTRAFLAGS_RADIO_TEXT | CTF_EXTRAFLAGS_RADIO_SOUND);
	sg_bots[slot].ent = ent;
	sg_bots[slot].active = true;
	sg_bots[slot].instance_token = sg_next_token++;
	SG_BotSlotInit(&sg_bots[slot]);
	VectorCopy(ent->s.origin, sg_bots[slot].stuck_origin);
	sg_bots[slot].stuck_since = level.time;
	SG_BotPersonaBind(ent, persona);
	gi.dprintf("slipgate: %s entered\n", name);
	return true;
}

/* ---- remove, list ----------------------------------------------------------- */

int SG_RemoveBots(void)
{
	int i, n = 0;

	for (i = 0; i < SG_MAXBOTS; i++)
	{
		if (!sg_bots[i].active)
			continue;
		Drop(i);
		n++;
	}
	return n;
}

/* Called when the game's level storage is going away: forget every slot
 * without touching edicts the engine already retired. */
void SG_RosterStorageReset(void)
{
	int i;

	for (i = 0; i < SG_MAXBOTS; i++)
		SlotClear(&sg_bots[i]);
	Botfill_Reset();
}

void SG_ListBots(void)
{
	int i, n = 0;

	for (i = 0; i < SG_MAXBOTS; i++)
	{
		edict_t *ent = sg_bots[i].ent;

		if (!sg_bots[i].active || !ent || !ent->client)
			continue;
		gi.cprintf(NULL, PRINT_HIGH,
			"%2d %-16s team %d score %3d role %d cell %u\n", i,
			ent->client->pers.netname, ent->client->ctf.teamnum,
			ent->client->resp.score, sg_bots[i].role,
			(unsigned int)sg_bots[i].cell);
		n++;
	}
	gi.cprintf(NULL, PRINT_HIGH, "%d bot%s\n", n, n == 1 ? "" : "s");
}

/* By netname, with or without the [SG] tag, or by slot number. */
qboolean SG_RemoveBotNamed(const char *who)
{
	int i;

	if (!who || !who[0])
		return false;
	for (i = 0; i < SG_MAXBOTS; i++)
	{
		edict_t *ent = sg_bots[i].ent;
		const char *bare;

		if (!sg_bots[i].active || !ent || !ent->client)
			continue;
		bare = ent->client->pers.netname;
		if (!strncmp(bare, "[SG]", 4))
			bare += 4;
		if (!Q_stricmp(who, ent->client->pers.netname) ||
			!Q_stricmp(who, bare) || atoi(who) == i)
		{
			Drop(i);
			return true;
		}
	}
	return false;
}

qboolean SG_KickWorst(void)
{
	int worst = WorstIndex(0, false);

	if (worst < 0)
		return false;
	Drop(worst);
	return true;
}

/* Whether an entity is one of our bots. */
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

/* A bot's slot and the token of this spawn of it: the chase cam follows a
 * bot by these so a new bot in the same slot is not mistaken for it. */

qboolean SG_BotPOVIdentity(edict_t *ent, int *slot_out,
	unsigned long long *instance_out)
{
	int i;

	if (!ent || !ent->inuse || !ent->client || !(ent->flags & FL_BOT))
		return false;
	for (i = 0; i < SG_MAXBOTS; i++)
		if (sg_bots[i].active && sg_bots[i].ent == ent)
		{
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
	if (slot < 0 || slot >= SG_MAXBOTS || !sg_bots[slot].active ||
		sg_bots[slot].instance_token != instance_token || !sg_bots[slot].ent ||
		!sg_bots[slot].ent->inuse || !sg_bots[slot].ent->client)
		return NULL;
	return sg_bots[slot].ent;
}
