#include "../g_local.h"

#include "sg_bot_persona.h"

/* Sixteen personas, the most the roster fields at once. */
static const sg_bot_persona_t sg_personas[] = {
	{ "Arach",   1,  1.2f, 0.8f, 0.6f, 0.7f },
	{ "Caco",    0,  0.9f, 1.3f, 0.4f, 1.0f },
	{ "Rune",    2,  1.0f, 1.0f, 0.5f, 0.6f },
	{ "Slip",   -1,  1.4f, 1.5f, 0.2f, 1.3f },
	{ "Gate",    0,  0.7f, 0.6f, 0.9f, 0.8f },
	{ "Phase",   1,  1.1f, 1.2f, 0.3f, 1.1f },
	{ "Field",  -1,  0.8f, 0.9f, 0.8f, 0.9f },
	{ "Trace",   2,  0.6f, 0.7f, 1.0f, 0.5f },
	{ "Vore",    0,  1.3f, 1.1f, 0.4f, 1.2f },
	{ "Fiend",  -2,  1.5f, 1.4f, 0.1f, 1.4f },
	{ "Scrag",   0,  1.0f, 0.5f, 0.7f, 0.8f },
	{ "Ogre",   -1,  1.2f, 0.6f, 0.5f, 0.9f },
	{ "Knight",  1,  0.9f, 0.9f, 0.9f, 0.6f },
	{ "Wizard",  2,  0.5f, 1.2f, 0.8f, 1.0f },
	{ "Spawn",  -2,  1.4f, 1.0f, 0.2f, 1.5f },
	{ "Shal",    0,  1.0f, 1.0f, 0.6f, 1.0f },
};

#define PERSONA_COUNT ((int)(sizeof(sg_personas) / sizeof(sg_personas[0])))

static int sg_bound[MAX_CLIENTS];   /* persona index + 1 per client, 0 none */

int SG_BotPersonaCount(void)
{
	return PERSONA_COUNT;
}

const sg_bot_persona_t *SG_BotPersonaAt(int index)
{
	return index >= 0 && index < PERSONA_COUNT ? &sg_personas[index] : NULL;
}

int SG_BotPersonaPick(unsigned occupied, int from)
{
	int i;

	for (i = 0; i < PERSONA_COUNT; i++)
	{
		int index = ((from % PERSONA_COUNT) + PERSONA_COUNT + i) % PERSONA_COUNT;

		if (!(occupied & (1U << index)))
			return index;
	}
	return -1;
}

static int ClientIndex(const edict_t *ent)
{
	int index;

	if (!ent || !ent->client)
		return -1;
	index = (int)(ent->client - game.clients);
	return index >= 0 && index < MAX_CLIENTS ? index : -1;
}

void SG_BotPersonaBind(edict_t *ent, int index)
{
	int client = ClientIndex(ent);

	if (client >= 0 && index >= 0 && index < PERSONA_COUNT)
		sg_bound[client] = index + 1;
}

void SG_BotPersonaUnbind(edict_t *ent)
{
	int client = ClientIndex(ent);

	if (client >= 0)
		sg_bound[client] = 0;
}

const sg_bot_persona_t *SG_BotPersona(const edict_t *ent)
{
	int client = ClientIndex(ent);

	if (client < 0 || sg_bound[client] == 0)
		return NULL;
	return &sg_personas[sg_bound[client] - 1];
}
