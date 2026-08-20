#include <stdio.h>
#include <string.h>

#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"

static edict_t ents[3];
static gclient_t clients[3];
static int carrying[3];

int SG_BotfillTestWorstIndex(int team, int automatic);

edict_t *ClientHasFlag(edict_t *ent)
{
	int index;

	for (index = 0; index < 3; index++)
		if (ent == &ents[index] && carrying[index])
			return ent;
	return NULL;
}

static void Setup(void)
{
	int index;

	memset(sg_bots, 0, sizeof(sg_bots));
	memset(ents, 0, sizeof(ents));
	memset(clients, 0, sizeof(clients));
	memset(carrying, 0, sizeof(carrying));
	for (index = 0; index < 3; index++)
	{
		sg_bots[index].active = true;
		sg_bots[index].ent = &ents[index];
		ents[index].inuse = true;
		ents[index].client = &clients[index];
	}
	clients[0].ctf.teamnum = CTF_TEAM_RED;
	clients[0].resp.score = -20;
	clients[1].ctf.teamnum = CTF_TEAM_RED;
	clients[1].resp.score = 10;
	clients[2].ctf.teamnum = CTF_TEAM_BLUE;
	clients[2].resp.score = -30;
}

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "check failed: %s:%d: %s\n", \
		    __FILE__, __LINE__, #condition); \
		return 1; \
	} \
} while (0)

int main(void)
{
	Setup();
	carrying[0] = 1;
	CHECK(SG_BotfillTestWorstIndex(CTF_TEAM_RED, 1) == 1);
	CHECK(SG_BotfillTestWorstIndex(CTF_TEAM_RED, 0) == 0);
	CHECK(SG_BotfillTestWorstIndex(CTF_TEAM_BLUE, 1) == 2);
	CHECK(SG_BotfillTestWorstIndex(CTF_TEAM_RED, 2) == -1);

	carrying[1] = 1;
	CHECK(SG_BotfillTestWorstIndex(CTF_TEAM_RED, 1) == -1);
	carrying[0] = 0;
	CHECK(SG_BotfillTestWorstIndex(CTF_TEAM_RED, 1) == 0);
	puts("sg_botfill_selector_test: ok");
	return 0;
}
