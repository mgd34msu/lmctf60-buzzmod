/* sg_compound_guard_game.c -- live edict lifecycle for compound mover guard. */
#include "../g_local.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "sg_local.h"
#include "sg_bot.h"
#include "sg_compound_guard_game.h"

typedef struct sg_compound_guard_game_identity_s
{
	uint64_t generation[MAX_EDICTS];
	uint64_t next_generation;
	uint8_t kind[MAX_EDICTS];
	uint8_t present[MAX_EDICTS];
	uint8_t initialized;
	uint8_t initialization_failed;
	uint8_t generation_exhausted;
} sg_compound_guard_game_identity_t;

static sg_compound_guard_game_identity_t game_guard;

void hook_touch(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surf);
void hook_die(edict_t *self, edict_t *inflictor, edict_t *attacker,
	int damage, vec3_t point);

static int GameWorldValid(void)
{
	return g_edicts && globals.edicts == g_edicts &&
	       globals.edict_size == (int)sizeof(edict_t) &&
	       game.maxentities > 0 &&
	       game.maxentities <= MAX_EDICTS && globals.num_edicts > 0 &&
	       globals.num_edicts <= game.maxentities &&
	       globals.max_edicts == game.maxentities &&
	       game.maxentities > BODY_QUEUE_SIZE && game.maxclients > 0 &&
	       game.maxclients < game.maxentities - BODY_QUEUE_SIZE && game.clients;
}

static int GameEdictKey(const edict_t *entity, int *key_out)
{
	int key;

	if (key_out)
		*key_out = 0;
	if (!entity || !key_out || !GameWorldValid())
		return 0;
	/* Equality against each live array element is defined even for a foreign
	 * pointer.  Subtracting first would itself be undefined for stale input. */
	for (key = 1; key < globals.num_edicts; key++)
		if (entity == &g_edicts[key])
		{
			*key_out = key;
			return 1;
		}
	return 0;
}

static int GameHookShape(const edict_t *client, const edict_t *bolt,
	int expected_key)
{
	return client && client->client && bolt && bolt->inuse && !bolt->client &&
	       expected_key > 0 && bolt->s.number == expected_key &&
	       bolt->classname && strcmp(bolt->classname, "noclass") == 0 &&
	       bolt->owner == client && bolt->movetype == MOVETYPE_FLYMISSILE &&
	       bolt->solid != SOLID_NOT &&
	       bolt->touch == hook_touch && bolt->die == hook_die;
}

static int GameClientShape(const edict_t *client, int expected_key)
{
	return client && expected_key > 0 && expected_key <= game.maxclients &&
	       client->s.number == expected_key &&
	       client->client == &game.clients[expected_key - 1] &&
	       client->classname && strcmp(client->classname, "player") == 0;
}

static int GameEntityMatchesKind(const edict_t *entity, int key,
	sg_mover_subject_kind_t kind)
{
	int owner_key;

	if (!entity || key <= 0 || entity->s.number != key)
		return 0;
	switch (kind)
	{
	case SG_MOVER_SUBJECT_CLIENT:
		return GameClientShape(entity, key);
	case SG_MOVER_SUBJECT_BODY_QUEUE:
		return key > game.maxclients &&
		       key <= game.maxclients + BODY_QUEUE_SIZE && !entity->client &&
		       entity->classname && strcmp(entity->classname, "bodyque") == 0;
	case SG_MOVER_SUBJECT_HOOK_BOLT:
		return key > game.maxclients + BODY_QUEUE_SIZE && entity->owner &&
		       GameEdictKey(entity->owner, &owner_key) &&
		       GameClientShape(entity->owner, owner_key) &&
		       GameHookShape(entity->owner, entity, key);
	default:
		return 0;
	}
}

static int GameHookCurrent(int key)
{
	return key > 0 && key < globals.num_edicts &&
	       game_guard.present[key] && game_guard.generation[key] != 0U &&
	       game_guard.kind[key] == SG_MOVER_SUBJECT_HOOK_BOLT;
}

/* Resolve only the authoritative client hook.  A malformed pointer, missed
 * mint, or second hook-shaped edict is uncertainty, encoded as -1 so Orphan
 * quarantines instead of silently omitting a possible swept subject. */
static int GameResolveHook(const edict_t *client)
{
	const edict_t *candidate;
	int candidate_key = 0;
	int key;
	int matches = 0;

	if (!client || !client->client || !GameWorldValid() ||
	    game_guard.generation_exhausted)
		return -1;
	candidate = client->client->hook;
	if (candidate)
	{
		if (!GameEdictKey(candidate, &candidate_key) ||
		    !GameHookShape(client, candidate, candidate_key) ||
		    !GameHookCurrent(candidate_key))
			return -1;
	}
	for (key = 1; key < globals.num_edicts; key++)
	{
		if (!GameHookShape(client, &g_edicts[key], key))
			continue;
		if (!GameHookCurrent(key) || key != candidate_key)
			return -1;
		matches++;
	}
	if (candidate_key > 0)
		return matches == 1 ? candidate_key : -1;
	if (matches == 0 && client->client->hookstate == 0)
		return 0;
	return -1;
}

static sg_bot_t *GameBotForClient(const edict_t *client)
{
	int slot;

	if (!client)
		return NULL;
	for (slot = 0; slot < SG_MAXBOTS; slot++)
		if (sg_bots[slot].active && sg_bots[slot].ent == client)
			return &sg_bots[slot];
	return NULL;
}

static void GameQuarantineBot(sg_bot_t *bot)
{
	if (bot && bot->compound_guard.attached)
		(void)SG_CompoundGuardQuarantine(&bot->compound_guard);
}

static sg_compound_guard_observation_t GameIdentity(void *context,
	int32_t edict_key, uint64_t *generation_out)
{
	edict_t *entity;

	if (generation_out)
		*generation_out = 0U;
	if (context != &game_guard || !generation_out ||
	    game_guard.initialization_failed || game_guard.generation_exhausted ||
	    !GameWorldValid() || edict_key <= 0 ||
	    edict_key >= game.maxentities || edict_key >= MAX_EDICTS)
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	if (edict_key >= globals.num_edicts || !game_guard.present[edict_key])
		return SG_COMPOUND_GUARD_NO;
	if (game_guard.generation[edict_key] == 0U)
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	entity = &g_edicts[edict_key];
	if (!entity->inuse)
		return SG_COMPOUND_GUARD_NO;
	if (!GameEntityMatchesKind(entity, edict_key,
	    (sg_mover_subject_kind_t)game_guard.kind[edict_key]))
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	*generation_out = game_guard.generation[edict_key];
	return SG_COMPOUND_GUARD_YES;
}

static sg_compound_guard_observation_t GameSolid(void *context,
	const sg_mover_subject_t *subject)
{
	uint64_t generation;
	sg_compound_guard_observation_t observation;

	if (context != &game_guard || !SG_MoverSubjectValid(subject))
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	observation = GameIdentity(context, subject->edict_key, &generation);
	if (observation != SG_COMPOUND_GUARD_YES)
		return observation == SG_COMPOUND_GUARD_NO
		    ? SG_COMPOUND_GUARD_NO
		    : SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	if (generation != subject->generation ||
	    game_guard.kind[subject->edict_key] != subject->kind)
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	return g_edicts[subject->edict_key].solid == SOLID_NOT
	    ? SG_COMPOUND_GUARD_NO : SG_COMPOUND_GUARD_YES;
}

static sg_compound_guard_observation_t GameOutsideSweep(void *context,
	const sg_mover_subject_t *subject, const sg_mover_key_t *keys,
	size_t key_count)
{
	edict_t *subject_entity;
	uint64_t generation;
	size_t index;
	sg_compound_guard_observation_t observation;

	if (context != &game_guard || !SG_MoverSubjectValid(subject) || !keys ||
	    key_count == 0U || key_count > SG_MOVER_LEASE_MAX_KEYS)
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	observation = GameIdentity(context, subject->edict_key, &generation);
	if (observation != SG_COMPOUND_GUARD_YES ||
	    generation != subject->generation ||
	    game_guard.kind[subject->edict_key] != subject->kind)
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	subject_entity = &g_edicts[subject->edict_key];
	for (index = 0U; index < key_count; index++)
	{
		if (keys[index] == 0U || keys[index] >= (sg_mover_key_t)MAX_EDICTS ||
		    keys[index] >= (sg_mover_key_t)globals.num_edicts ||
		    !g_edicts[keys[index]].inuse)
			return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
		if (index > 0U && keys[index - 1U] >= keys[index])
			return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
		if (!SG_MoverSubjectOutsideSweep(&g_edicts[keys[index]],
		    subject_entity))
			return SG_COMPOUND_GUARD_NO;
	}
	return SG_COMPOUND_GUARD_YES;
}

static sg_compound_guard_result_t GameEnsureInitialized(void)
{
	sg_compound_guard_host_t host;
	sg_compound_guard_result_t result;

	if (game_guard.initialization_failed)
		return SG_COMPOUND_GUARD_HOST_ERROR;
	if (game_guard.initialized)
		return SG_COMPOUND_GUARD_OK;
	memset(&host, 0, sizeof(host));
	host.context = &game_guard;
	host.identity = GameIdentity;
	host.solid = GameSolid;
	host.outside_sweep = GameOutsideSweep;
	result = SG_CompoundGuardInit(&host);
	if (result != SG_COMPOUND_GUARD_OK)
	{
		game_guard.initialization_failed = 1U;
		return result;
	}
	game_guard.initialized = 1U;
	return SG_COMPOUND_GUARD_OK;
}

static sg_compound_guard_result_t GameMint(edict_t *entity,
	sg_mover_subject_kind_t kind, int *key_out)
{
	int key;
	sg_compound_guard_result_t result;

	if (key_out)
		*key_out = 0;
	result = GameEnsureInitialized();
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	if (!GameEdictKey(entity, &key) || !entity->inuse ||
	    !GameEntityMatchesKind(entity, key, kind) ||
	    kind < SG_MOVER_SUBJECT_CLIENT || kind > SG_MOVER_SUBJECT_HOOK_BOLT)
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	if (game_guard.generation_exhausted ||
	    game_guard.next_generation == UINT64_MAX)
	{
		game_guard.generation_exhausted = 1U;
		return SG_COMPOUND_GUARD_EXHAUSTED;
	}
	game_guard.next_generation++;
	game_guard.generation[key] = game_guard.next_generation;
	game_guard.kind[key] = (uint8_t)kind;
	game_guard.present[key] = 1U;
	if (key_out)
		*key_out = key;
	return SG_COMPOUND_GUARD_OK;
}

sg_compound_guard_result_t SG_CompoundGuardGameLevelReset(void)
{
	sg_compound_guard_result_t result;

	result = GameEnsureInitialized();
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	result = SG_CompoundGuardLevelReset();
	/* Generations are process-unique and retained.  Presence is level-owned:
	 * the engine is about to discard every old incarnation synchronously. */
	memset(game_guard.present, 0, sizeof(game_guard.present));
	return result;
}

void SG_CompoundGuardGameStorageWillFree(void)
{
	int slot;

	if (!game_guard.initialized)
		return;
	(void)SG_CompoundGuardLevelReset();
	memset(game_guard.present, 0, sizeof(game_guard.present));
	for (slot = 0; slot < SG_MAXBOTS; slot++)
		(void)SG_CompoundGuardBotReset(&sg_bots[slot].compound_guard);
}

void SG_CompoundGuardGameFrame(void)
{
	if (GameEnsureInitialized() != SG_COMPOUND_GUARD_OK)
		return;
	SG_CompoundGuardFrame(NULL);
}

sg_compound_guard_result_t SG_CompoundGuardGameBotSlotReset(
	sg_compound_guard_bot_t *guard_bot)
{
	return SG_CompoundGuardBotReset(guard_bot);
}

sg_compound_guard_result_t SG_CompoundGuardGameBotAttach(
	sg_compound_guard_bot_t *guard_bot, int bot_slot, edict_t *client)
{
	int key;
	sg_compound_guard_result_t result;

	result = GameEnsureInitialized();
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	if (!GameEdictKey(client, &key) || !client->inuse || !client->client ||
	    !GameClientShape(client, key) ||
	    !game_guard.present[key] ||
	    game_guard.kind[key] != SG_MOVER_SUBJECT_CLIENT)
		return SG_COMPOUND_GUARD_HOST_ERROR;
	return SG_CompoundGuardBotAttach(guard_bot, bot_slot, key);
}

sg_compound_guard_result_t SG_CompoundGuardGameBodyQueueInit(edict_t *body)
{
	return GameMint(body, SG_MOVER_SUBJECT_BODY_QUEUE, NULL);
}

sg_compound_guard_result_t SG_CompoundGuardGameBodyWillReplace(edict_t *body)
{
	int key;
	sg_compound_guard_result_t result;

	result = GameEnsureInitialized();
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	if (!GameEdictKey(body, &key))
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	return SG_CompoundGuardBodyWillReplace(key);
}

sg_compound_guard_result_t SG_CompoundGuardGameBodyDidCopy(edict_t *client,
	edict_t *body)
{
	int key = 0, resolved_key = 0;
	sg_bot_t *bot;
	sg_compound_guard_result_t mint_result, copy_result;

	if (GameEdictKey(body, &resolved_key))
		mint_result = GameMint(body, SG_MOVER_SUBJECT_BODY_QUEUE, &key);
	else
		mint_result = SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	bot = GameBotForClient(client);
	copy_result = SG_CompoundGuardBodyDidCopy(
	    bot && bot->compound_guard.attached ? &bot->compound_guard : NULL,
	    key > 0 ? key : resolved_key);
	if (mint_result != SG_COMPOUND_GUARD_OK ||
	    copy_result != SG_COMPOUND_GUARD_OK)
		GameQuarantineBot(bot);
	return mint_result == SG_COMPOUND_GUARD_OK ? copy_result : mint_result;
}

sg_compound_guard_result_t SG_CompoundGuardGameClientSpawned(edict_t *client)
{
	int key;
	sg_bot_t *bot;
	sg_compound_guard_result_t result, respawn_result;

	result = GameMint(client, SG_MOVER_SUBJECT_CLIENT, &key);
	bot = GameBotForClient(client);
	if (result != SG_COMPOUND_GUARD_OK)
	{
		if (bot && bot->compound_guard.attached)
			(void)SG_CompoundGuardQuarantine(&bot->compound_guard);
		return result;
	}
	if (!bot || !bot->compound_guard.attached)
		return SG_COMPOUND_GUARD_OK;
	respawn_result = SG_CompoundGuardBotRespawn(&bot->compound_guard, key);
	if (respawn_result != SG_COMPOUND_GUARD_OK)
		GameQuarantineBot(bot);
	return respawn_result;
}

sg_compound_guard_result_t SG_CompoundGuardGameHookLinked(edict_t *client,
	edict_t *bolt)
{
	int client_key, key;
	sg_bot_t *bot = GameBotForClient(client);
	sg_compound_guard_result_t result;

	if (!GameEdictKey(client, &client_key) ||
	    !GameClientShape(client, client_key) ||
	    !GameEdictKey(bolt, &key) || !GameHookShape(client, bolt, key))
	{
		GameQuarantineBot(bot);
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	}
	result = GameMint(bolt, SG_MOVER_SUBJECT_HOOK_BOLT, &key);
	if (result != SG_COMPOUND_GUARD_OK)
		GameQuarantineBot(bot);
	return result;
}

sg_compound_guard_result_t SG_CompoundGuardGamePlayerDie(edict_t *client)
{
	int bolt_key = 0;
	int client_key;
	sg_bot_t *bot;
	sg_compound_guard_result_t result;

	bot = GameBotForClient(client);
	if (!GameEdictKey(client, &client_key) ||
	    !GameClientShape(client, client_key))
	{
		GameQuarantineBot(bot);
		return bot && bot->compound_guard.attached
		    ? SG_COMPOUND_GUARD_HOST_ERROR
		    : SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	}
	if (!bot || !bot->compound_guard.attached)
		return SG_COMPOUND_GUARD_OK;
	bolt_key = GameResolveHook(client);
	result = SG_CompoundGuardOrphan(&bot->compound_guard, bolt_key);
	if (result != SG_COMPOUND_GUARD_OK)
		GameQuarantineBot(bot);
	return result;
}

void SG_CompoundGuardGameEntityFreed(edict_t *entity)
{
	int key;

	if (!game_guard.initialized || !GameEdictKey(entity, &key))
		return;
	game_guard.present[key] = 0U;
}

sg_compound_guard_result_t SG_CompoundGuardGameBoltEvicted(edict_t *client,
	edict_t *bolt)
{
	int key;
	sg_bot_t *bot = GameBotForClient(client);
	sg_compound_guard_result_t result;

	if (!GameEdictKey(bolt, &key))
	{
		GameQuarantineBot(bot);
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	}
	if (bolt->inuse)
	{
		GameQuarantineBot(bot);
		return SG_COMPOUND_GUARD_NOT_CLEAR;
	}
	if (!bot || !bot->compound_guard.attached)
		return SG_COMPOUND_GUARD_OK;
	result = SG_CompoundGuardBoltEvicted(&bot->compound_guard, key);
	if (result != SG_COMPOUND_GUARD_OK)
		GameQuarantineBot(bot);
	return result;
}

sg_compound_guard_result_t SG_CompoundGuardGameClientDisconnected(
	edict_t *client)
{
	int key;
	sg_bot_t *bot = GameBotForClient(client);
	sg_compound_guard_result_t result = SG_COMPOUND_GUARD_OK;

	if (!GameEdictKey(client, &key) || !GameClientShape(client, key))
	{
		GameQuarantineBot(bot);
		return bot && bot->compound_guard.attached
		    ? SG_COMPOUND_GUARD_HOST_ERROR
		    : SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	}
	if (bot && bot->compound_guard.attached)
		result = SG_CompoundGuardBotDisconnected(&bot->compound_guard);
	game_guard.present[key] = 0U;
	if (result != SG_COMPOUND_GUARD_OK)
		GameQuarantineBot(bot);
	return result;
}

#ifdef SG_COMPOUND_GUARD_GAME_TEST
void SG_CompoundGuardGameTestSetNextGeneration(uint64_t next_generation)
{
	game_guard.next_generation = next_generation;
	game_guard.generation_exhausted = 0U;
}

void SG_CompoundGuardGameTestSetEntityGeneration(int edict_key,
	uint64_t generation, int present)
{
	if (edict_key <= 0 || edict_key >= MAX_EDICTS)
		return;
	game_guard.generation[edict_key] = generation;
	game_guard.present[edict_key] = present ? 1U : 0U;
}
#endif
