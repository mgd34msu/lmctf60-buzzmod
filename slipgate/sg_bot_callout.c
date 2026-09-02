#include "../g_local.h"
#include "../g_ctffunc.h"

#include "sg_local.h"
#include "sg_bot.h"
#include "sg_bot_callout.h"

#include <string.h>

#include "sg_persona.h"
#include "sg_rune_level.h"
#include "sg_util.h"

#define BASE_SECONDS 5.0f          /* route time that still counts as a base */
#define BOT_GAP 4.0f               /* seconds between one bot's lines */
#define TEAM_GAP 2.5f              /* seconds between a team's lines of one kind */
#define COVER_HEALTH 50

typedef enum kind_e
{
	KIND_CARRIER_SEEN = 0,
	KIND_FLAG_TAKEN,
	KIND_FLAG_DROPPED,
	KIND_FLAG_BACK,
	KIND_HAVE_FLAG,
	KIND_COVER,
	KIND_ROLE,
	KIND_COUNT
} kind_t;

typedef struct team_state_s
{
	float last[KIND_COUNT];   /* level.time of the last line of each kind */
	int flag_state;           /* 0 home, 1 carried, 2 dropped */
} team_state_t;

static team_state_t sg_teams[3];       /* by CTF_TEAM_* (1, 2) */
static float sg_bot_last[MAX_CLIENTS];
static int sg_bot_role[MAX_CLIENTS];
static qboolean sg_bot_cover_said[MAX_CLIENTS];
static vec3_t sg_flag_base[3];
static qboolean sg_flag_base_known[3];

void SG_BotCalloutReset(void)
{
	memset(sg_teams, 0, sizeof(sg_teams));
	memset(sg_bot_last, 0, sizeof(sg_bot_last));
	memset(sg_bot_role, -1, sizeof(sg_bot_role));
	memset(sg_bot_cover_said, 0, sizeof(sg_bot_cover_said));
	memset(sg_flag_base_known, 0, sizeof(sg_flag_base_known));
}

static team_state_t *Team(int team)
{
	return team == CTF_TEAM_RED || team == CTF_TEAM_BLUE ? &sg_teams[team] : NULL;
}

/* The line reaches every teammate, in the game's team-chat form. */
static void Say(edict_t *self, kind_t kind, const char *text)
{
	team_state_t *team = Team(self->client->ctf.teamnum);
	int index = (int)(self - g_edicts) - 1;
	char line[256];
	int i;

	if (!team || index < 0 || index >= MAX_CLIENTS)
		return;
	if (level.time - sg_bot_last[index] < BOT_GAP ||
		level.time - team->last[kind] < TEAM_GAP)
		return;
	sg_bot_last[index] = level.time;
	team->last[kind] = level.time;
	Com_sprintf(line, sizeof(line), "(%s): %s\n", self->client->pers.netname, text);
	for (i = 1; i <= game.maxclients; i++)
	{
		edict_t *other = &g_edicts[i];

		if (!other->inuse || !other->client ||
			other->client->ctf.teamnum != self->client->ctf.teamnum)
			continue;
		gi.cprintf(other, PRINT_CHAT, "%s", line);
	}
	if (dedicated && dedicated->value)
		gi.dprintf("%s", line);
}

static uint32_t FlagCell(int team)
{
	edict_t *flag = ctf_flagsearch(team);
	vec3_t point;

	if (!flag)
		return SG_RUNE_CX_INDEX_NONE;
	VectorCopy(flag->s.origin, point);
	return SG_BotStandingCellNear(point);
}

static float RouteSeconds(uint32_t flag_cell, const vec3_t point)
{
	const sg_rune_field_t *field;
	uint32_t cell;

	if (flag_cell == SG_RUNE_CX_INDEX_NONE)
		return 1.0e9f;
	field = SG_RuneLevelField(flag_cell);
	cell = SG_BotStandingCellNear(point);
	if (!field || cell == SG_RUNE_CX_INDEX_NONE)
		return 1.0e9f;
	return field->cost[SG_RUNE_FIELD_STATE(cell, 0)];
}

const char *SG_BotCalloutWhere(int team, const vec3_t point)
{
	float ours = RouteSeconds(FlagCell(team), point);
	float theirs = RouteSeconds(FlagCell(SG_EnemyTeam(team)), point);

	if (ours <= BASE_SECONDS && ours <= theirs)
		return "our base";
	if (theirs <= BASE_SECONDS)
		return "their base";
	if (ours < 1.0e8f && theirs < 1.0e8f)
		return ours < theirs ? "our side" : "their side";
	return "mid";
}

static qboolean Bot(const edict_t *e)
{
	return e && e->inuse && e->client && SG_OwnsBot((edict_t *)e);
}

/* Any bot on the team says the team-wide line (the first found). */
static edict_t *Speaker(int team)
{
	int i;

	for (i = 1; i <= game.maxclients; i++)
	{
		edict_t *e = &g_edicts[i];

		if (Bot(e) && e->client->ctf.teamnum == team && e->health > 0)
			return e;
	}
	return NULL;
}

static edict_t *Carrier(int team)
{
	int i;

	for (i = 1; i <= game.maxclients; i++)
	{
		edict_t *e = &g_edicts[i];

		if (e->inuse && e->client && e->client->ctf.teamnum == team &&
			ClientHasFlag(e))
			return e;
	}
	return NULL;
}

/* The game keeps one flag entity per team and moves it: dropped is when it
 * stands away from where it stood first. */
static qboolean FlagDropped(int team, vec3_t where)
{
	edict_t *flag = ctf_flagsearch(team);
	vec3_t delta;

	if (!flag || team < 0 || team > 2)
		return false;
	if (!sg_flag_base_known[team])
	{
		VectorCopy(flag->s.origin, sg_flag_base[team]);
		sg_flag_base_known[team] = true;
		return false;
	}
	VectorSubtract(flag->s.origin, sg_flag_base[team], delta);
	if (VectorLength(delta) < 32.0f || flag->solid == SOLID_NOT)
		return false;
	VectorCopy(flag->s.origin, where);
	return true;
}

void SG_BotCalloutFrame(void)
{
	int team;

	for (team = CTF_TEAM_RED; team <= CTF_TEAM_BLUE; team++)
	{
		team_state_t *state = Team(team);
		edict_t *carrier = Carrier(SG_EnemyTeam(team));
		edict_t *speaker;
		vec3_t where;
		int now;
		char text[128];

		if (!state)
			continue;
		if (carrier)
			now = 1;
		else if (FlagDropped(team, where))
			now = 2;
		else
			now = 0;
		if (now == state->flag_state)
			continue;
		speaker = Speaker(team);
		if (speaker)
		{
			if (now == 1)
			{
				Com_sprintf(text, sizeof(text), "%s has our flag at %s",
					carrier->client->pers.netname,
					SG_BotCalloutWhere(team, carrier->s.origin));
				Say(speaker, KIND_FLAG_TAKEN, text);
			}
			else if (now == 2)
			{
				Com_sprintf(text, sizeof(text), "our flag is down at %s",
					SG_BotCalloutWhere(team, where));
				Say(speaker, KIND_FLAG_DROPPED, text);
			}
			else
				Say(speaker, KIND_FLAG_BACK, "our flag is home");
		}
		state->flag_state = now;
	}
}

void SG_BotCalloutRole(sg_bot_t *bot, int role)
{
	edict_t *e = bot ? bot->ent : NULL;
	const sg_persona_t *persona;
	int index;
	char text[128];

	if (!Bot(e))
		return;
	index = (int)(e - g_edicts) - 1;
	if (index < 0 || index >= MAX_CLIENTS)
		return;
	if (role == SG_ROLE_CARRY && e->health < COVER_HEALTH && !sg_bot_cover_said[index])
	{
		Com_sprintf(text, sizeof(text), "need cover at %s, %d health",
			SG_BotCalloutWhere(e->client->ctf.teamnum, e->s.origin), e->health);
		Say(e, KIND_COVER, text);
		sg_bot_cover_said[index] = true;
	}
	if (role != SG_ROLE_CARRY)
		sg_bot_cover_said[index] = false;
	if (role == sg_bot_role[index])
		return;
	sg_bot_role[index] = role;
	persona = SG_PersonaFor(e);
	switch (role)
	{
	case SG_ROLE_CARRY:
		Com_sprintf(text, sizeof(text), "got their flag, coming through %s",
			SG_BotCalloutWhere(e->client->ctf.teamnum, e->s.origin));
		Say(e, KIND_HAVE_FLAG, text);
		return;
	case SG_ROLE_DEFEND:
		if (persona && persona->banter_freq < 0.8f)
			return;
		Say(e, KIND_ROLE, "I'm on defense");
		return;
	case SG_ROLE_ESCORT:
		Say(e, KIND_ROLE, "covering the carrier");
		return;
	case SG_ROLE_RECOVER:
		Say(e, KIND_ROLE, "going for our flag");
		return;
	default:
		return;
	}
}

void SG_BotCalloutSeen(edict_t *self, edict_t *enemy)
{
	char text[128];

	if (!Bot(self) || !enemy || !enemy->client)
		return;
	if (ClientHasFlag(enemy))
	{
		Com_sprintf(text, sizeof(text), "enemy carrier %s at %s",
			enemy->client->pers.netname,
			SG_BotCalloutWhere(self->client->ctf.teamnum, enemy->s.origin));
		Say(self, KIND_CARRIER_SEEN, text);
		return;
	}
	if (RouteSeconds(FlagCell(self->client->ctf.teamnum), enemy->s.origin) <=
		BASE_SECONDS)
	{
		Com_sprintf(text, sizeof(text), "incoming at our base: %s",
			enemy->client->pers.netname);
		Say(self, KIND_CARRIER_SEEN, text);
	}
}
