#include "g_local.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

edict_t *g_edicts;
cvar_t *maxclients;
cvar_t *maxspectators;

static edict_t edicts[8];
static gclient_t clients[8];
static cvar_t maxclients_value;
static cvar_t maxspectators_value;

static void ResetWorld(void)
{
	int slot;

	memset(edicts, 0, sizeof(edicts));
	memset(clients, 0, sizeof(clients));
	memset(&maxclients_value, 0, sizeof(maxclients_value));
	memset(&maxspectators_value, 0, sizeof(maxspectators_value));
	g_edicts = edicts;
	maxclients = &maxclients_value;
	maxspectators = &maxspectators_value;
	maxclients->value = 6.0f;
	maxspectators->value = 1.0f;
	for (slot = 1; slot <= 6; slot++)
		edicts[slot].client = &clients[slot];
}

static void SetSpectatorSlot(int slot, qboolean inuse)
{
	edicts[slot].inuse = inuse;
	clients[slot].pers.spectator = true;
}

static void TestAdmissionCensus(void)
{
	ResetWorld();
	SetSpectatorSlot(1, true);
	assert(!G_SpectatorLimitBlocksAdmission(&edicts[1], true));
	assert(!G_SpectatorLimitBlocksAdmission(&edicts[1], false));

	SetSpectatorSlot(2, true);
	assert(G_SpectatorLimitBlocksAdmission(&edicts[1], false));
	assert(!G_SpectatorLimitBlocksAdmission(&edicts[1], true));
	SetSpectatorSlot(3, true);
	assert(!G_SpectatorLimitBlocksAdmission(&edicts[1], true));

	SetSpectatorSlot(2, false);
	edicts[3].inuse = true;
	clients[3].pers.spectator = false;
	assert(!G_SpectatorLimitBlocksAdmission(&edicts[1], false));
}

static void TestLimitBoundaries(void)
{
	ResetWorld();
	maxspectators->value = 0.0f;
	assert(G_SpectatorLimitBlocksAdmission(&edicts[1], false));
	maxspectators->value = 1.5f;
	SetSpectatorSlot(2, true);
	assert(!G_SpectatorLimitBlocksAdmission(&edicts[1], false));
	SetSpectatorSlot(3, true);
	assert(G_SpectatorLimitBlocksAdmission(&edicts[1], false));
}

int main(void)
{
	TestAdmissionCensus();
	TestLimitBoundaries();
	puts("sg_spectator_limit_test: ok");
	return 0;
}
