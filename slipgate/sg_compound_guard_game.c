/* sg_compound_guard_game.c -- live edict lifecycle for compound mover guard. */
#include "../g_local.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "sg_local.h"
#include "sg_bot.h"
#include "sg_compound_world.h"
#include "sg_compound_guard_game.h"
#include "sg_compound_swim_game.h"
#include "sg_compound_drop_game.h"
#include "sg_compound_hook_game.h"

typedef struct sg_compound_guard_game_identity_s
{
	uint64_t generation[MAX_EDICTS];
	uint64_t next_generation;
	uint8_t kind[MAX_EDICTS];
	uint8_t present[MAX_EDICTS];
	uint8_t protected_subject[MAX_EDICTS];
	uint8_t initialized;
	uint8_t initialization_failed;
	uint8_t generation_exhausted;
} sg_compound_guard_game_identity_t;

static sg_compound_guard_game_identity_t game_guard;

void hook_touch(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surf);
void hook_die(edict_t *self, edict_t *inflictor, edict_t *attacker,
	int damage, vec3_t point);
void body_die(edict_t *self, edict_t *inflictor, edict_t *attacker,
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

/* SV_Push does not inspect `solid`.  A nonsolid incarnation is physically
 * irrelevant only when the pusher loop itself will skip it. */
static int GamePusherSkipsSubject(const edict_t *subject)
{
	return subject &&
	       (subject->movetype == MOVETYPE_PUSH ||
	        subject->movetype == MOVETYPE_STOP ||
	        subject->movetype == MOVETYPE_NONE ||
	        subject->movetype == MOVETYPE_NOCLIP || !subject->area.prev);
}

static sg_compound_guard_observation_t GameOutsideSweepMode(void *context,
	const sg_mover_subject_t *subject, const sg_mover_key_t *keys,
	size_t key_count, int prospective_push)
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
		if (!(prospective_push
		      ? SG_MoverSubjectOutsideProspectivePush(
		            &g_edicts[keys[index]], subject_entity)
		      : SG_MoverSubjectOutsideSweep(&g_edicts[keys[index]],
		            subject_entity)))
			return SG_COMPOUND_GUARD_NO;
	}
	return SG_COMPOUND_GUARD_YES;
}

static sg_compound_guard_observation_t GameOutsideSweep(void *context,
	const sg_mover_subject_t *subject, const sg_mover_key_t *keys,
	size_t key_count)
{
	return GameOutsideSweepMode(context, subject, keys, key_count, 0);
}

static sg_compound_guard_observation_t GameAllSubjectsOutsideMode(void *context,
	const sg_mover_key_t *keys, size_t key_count, int prospective_push)
{
	sg_mover_subject_t subject;
	sg_compound_guard_observation_t observation;
	int body_keys[BODY_QUEUE_SIZE];
	uint64_t body_generations[BODY_QUEUE_SIZE];
	int body_count = 0;
	uint64_t generation;
	int key, body_index;

	if (context != &game_guard || !keys || key_count == 0U ||
	    key_count > SG_MOVER_LEASE_MAX_KEYS || !GameWorldValid() ||
	    game_guard.generation_exhausted)
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	for (key = 1; key < globals.num_edicts; key++)
	{
		if (!game_guard.present[key] || !game_guard.protected_subject[key])
			continue;
		observation = GameIdentity(context, key, &generation);
		if (observation == SG_COMPOUND_GUARD_NO)
			continue;
		if (observation != SG_COMPOUND_GUARD_YES || generation == 0U ||
		    game_guard.kind[key] < SG_MOVER_SUBJECT_CLIENT ||
		    game_guard.kind[key] > SG_MOVER_SUBJECT_HOOK_BOLT)
			return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
		memset(&subject, 0, sizeof(subject));
		subject.kind = game_guard.kind[key];
		subject.edict_key = key;
		subject.generation = generation;
		observation = GameSolid(context, &subject);
		if (observation == SG_COMPOUND_GUARD_NO)
		{
			if (!prospective_push ||
			    GamePusherSkipsSubject(&g_edicts[key]))
				continue;
			/* A stale-linked SOLID_NOT WALK/TOSS/FLYMISSILE entity is still
			 * eligible for stock SV_Push and therefore cannot be erased from
			 * the physical population by the higher-level solid abstraction. */
			return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
		}
		if (observation != SG_COMPOUND_GUARD_YES)
			return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
		observation = GameOutsideSweepMode(context, &subject, keys, key_count,
		    prospective_push);
		if (observation == SG_COMPOUND_GUARD_NO &&
		    subject.kind == SG_MOVER_SUBJECT_BODY_QUEUE)
		{
			edict_t *body = &g_edicts[key];

			/* Preflight the entire protected population before changing a corpse.
			 * A later client/hook blocker or malformed incarnation must leave every
			 * earlier body untouched. */
			if (body->solid != SOLID_BBOX || body->movetype != MOVETYPE_TOSS ||
			    body->client || body->die != body_die ||
			    body->takedamage != DAMAGE_YES ||
			    body_count >= BODY_QUEUE_SIZE)
				return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
			body_keys[body_count] = key;
			body_generations[body_count] = generation;
			body_count++;
			continue;
		}
		if (observation != SG_COMPOUND_GUARD_YES)
			return observation == SG_COMPOUND_GUARD_NO
			    ? SG_COMPOUND_GUARD_NO
			    : SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	}
	/* No bot think owns a body-queue edict, so a knocked SG corpse could
	 * otherwise retain an ownerless TOP lease forever.  Only after the complete
	 * read-only pass succeeded, finish the stock gib path for each exact
	 * SG-derived corpse and positively re-observe the same incarnation nonsolid. */
	for (body_index = 0; body_index < body_count; body_index++)
	{
		edict_t *body = &g_edicts[body_keys[body_index]];
		uint64_t after_generation = 0U;
		vec3_t point = { 0.0f, 0.0f, 0.0f };

		if (GameIdentity(context, body_keys[body_index],
		        &after_generation) != SG_COMPOUND_GUARD_YES ||
		    after_generation != body_generations[body_index] ||
		    game_guard.kind[body_keys[body_index]] !=
		        SG_MOVER_SUBJECT_BODY_QUEUE ||
		    body->solid != SOLID_BBOX || body->movetype != MOVETYPE_TOSS ||
		    body->client || body->die != body_die ||
		    body->takedamage != DAMAGE_YES)
			return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
		body->health = -100;
		body_die(body, body, body, 100000, point);
		if (GameIdentity(context, body_keys[body_index],
		        &after_generation) != SG_COMPOUND_GUARD_YES ||
		    after_generation != body_generations[body_index] ||
		    game_guard.kind[body_keys[body_index]] !=
		        SG_MOVER_SUBJECT_BODY_QUEUE)
			return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
		memset(&subject, 0, sizeof(subject));
		subject.kind = SG_MOVER_SUBJECT_BODY_QUEUE;
		subject.edict_key = body_keys[body_index];
		subject.generation = body_generations[body_index];
		if (GameSolid(context, &subject) != SG_COMPOUND_GUARD_NO)
			return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	}
	return SG_COMPOUND_GUARD_YES;
}

static sg_compound_guard_observation_t GameAllSubjectsOutside(void *context,
	const sg_mover_key_t *keys, size_t key_count)
{
	return GameAllSubjectsOutsideMode(context, keys, key_count, 0);
}

static sg_compound_guard_observation_t GameHoldOpen(void *context,
	sg_mover_lease_law_t law, const sg_mover_key_t *keys,
	size_t key_count, int lease_ms)
{
	edict_t *members[SG_MOVER_LEASE_MAX_KEYS];
	size_t index;

	if (context != &game_guard || !keys || key_count == 0U ||
	    key_count > SG_MOVER_LEASE_MAX_KEYS || !GameWorldValid())
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	for (index = 0U; index < key_count; index++)
	{
		if (keys[index] == 0U ||
		    keys[index] >= (sg_mover_key_t)globals.num_edicts ||
		    (index > 0U && keys[index - 1U] >= keys[index]))
			return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
		members[index] = &g_edicts[keys[index]];
	}
	if (law == SG_MOVER_LAW_COMPOUND_PREOPEN)
		return key_count == 1U &&
		    SG_CompoundWorldHoldMember(members[0], lease_ms)
		        ? SG_COMPOUND_GUARD_YES : SG_COMPOUND_GUARD_NO;
	if (law != SG_MOVER_LAW_DECLARED_DOOR)
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	return SG_DeclaredDoorHoldMembers(members, (int)key_count, lease_ms)
	    ? SG_COMPOUND_GUARD_YES : SG_COMPOUND_GUARD_NO;
}

static sg_compound_guard_observation_t GameSetTerminal(void *context,
	sg_mover_lease_law_t law, const sg_mover_key_t *keys,
	size_t key_count)
{
	edict_t *members[SG_MOVER_LEASE_MAX_KEYS];
	size_t index;

	if (context != &game_guard || !keys || key_count == 0U ||
	    key_count > SG_MOVER_LEASE_MAX_KEYS || !GameWorldValid())
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	for (index = 0U; index < key_count; index++)
	{
		if (keys[index] == 0U ||
		    keys[index] >= (sg_mover_key_t)globals.num_edicts ||
		    (index > 0U && keys[index - 1U] >= keys[index]))
			return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
		members[index] = &g_edicts[keys[index]];
	}
	if (law == SG_MOVER_LAW_COMPOUND_PREOPEN)
		return key_count == 1U &&
		    SG_CompoundWorldMemberTerminal(members[0])
		        ? SG_COMPOUND_GUARD_YES : SG_COMPOUND_GUARD_NO;
	if (law != SG_MOVER_LAW_DECLARED_DOOR)
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	return SG_DeclaredDoorMembersTerminal(members, (int)key_count)
	    ? SG_COMPOUND_GUARD_YES : SG_COMPOUND_GUARD_NO;
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
	host.all_subjects_outside = GameAllSubjectsOutside;
	host.hold_open = GameHoldOpen;
	host.set_terminal = GameSetTerminal;
	result = SG_CompoundGuardInit(&host);
	if (result != SG_COMPOUND_GUARD_OK)
	{
		game_guard.initialization_failed = 1U;
		return result;
	}
	game_guard.initialized = 1U;
	return SG_COMPOUND_GUARD_OK;
}

static void GameSortKeys(sg_mover_key_t *keys, size_t key_count)
{
	size_t index;

	for (index = 1U; index < key_count; index++)
	{
		sg_mover_key_t value = keys[index];
		size_t at = index;

		while (at > 0U && keys[at - 1U] > value)
		{
			keys[at] = keys[at - 1U];
			at--;
		}
		keys[at] = value;
	}
}

/* Walk the exact forward chain SV_Physics_Pusher would consume.  Continue
 * beyond the 16-key lease bound (up to the finite edict population) so reverse
 * lookup can still classify a hidden appended member as malformed. */
static int GameForwardPusherTeam(edict_t *captain, edict_t *wanted,
	sg_mover_key_t keys[SG_MOVER_LEASE_MAX_KEYS], size_t *key_count_out,
	int *contains_out, int *complete_out, int *door_shape_out)
{
	sg_mover_key_t observed[MAX_EDICTS];
	edict_t *part;
	size_t count = 0U;

	if (key_count_out)
		*key_count_out = 0U;
	if (contains_out)
		*contains_out = 0;
	if (complete_out)
		*complete_out = 0;
	if (door_shape_out)
		*door_shape_out = 0;
	if (!captain || !wanted || !keys || !key_count_out || !contains_out ||
	    !complete_out || !door_shape_out ||
	    (captain->flags & FL_TEAMSLAVE))
		return 0;
	*door_shape_out = 1;
	for (part = captain; part; part = part->teamchain)
	{
		int key;
		size_t prior;

		if (count >= MAX_EDICTS || !GameEdictKey(part, &key))
			return 1;
		if (part == wanted)
			*contains_out = 1;
		if (!part->inuse)
			return 1;
		/* door_blocked walks back through self->teammaster and then consumes
		 * that captain's entire forward chain.  Record the exact G_FindTeams
		 * shape separately: standalone trains/plats legitimately omit it and
		 * retain stock behavior after a positive NO_LEASE query, while a
		 * protected door must authenticate it before SV_Push. */
		if ((count == 0U &&
		     (part != captain || part->teammaster != captain ||
		      (part->flags & FL_TEAMSLAVE))) ||
		    (count > 0U &&
		     (part->teammaster != captain ||
		      !(part->flags & FL_TEAMSLAVE))))
			*door_shape_out = 0;
		for (prior = 0U; prior < count; prior++)
			if (observed[prior] == (sg_mover_key_t)key)
				return 1;
		observed[count++] = (sg_mover_key_t)key;
	}
	if (count == 0U || count > SG_MOVER_LEASE_MAX_KEYS)
		return 1;
	memcpy(keys, observed, count * sizeof(keys[0]));
	GameSortKeys(keys, count);
	*key_count_out = count;
	*complete_out = 1;
	return 1;
}

static int GamePusherTeamForEntity(edict_t *entity,
	sg_mover_key_t keys[SG_MOVER_LEASE_MAX_KEYS], size_t *key_count_out,
	int *door_shape_out)
{
	sg_mover_key_t candidate_keys[SG_MOVER_LEASE_MAX_KEYS];
	edict_t *candidate;
	size_t candidate_count, matches = 0U;
	int matched_door_shape = 0;
	int index;

	if (key_count_out)
		*key_count_out = 0U;
	if (door_shape_out)
		*door_shape_out = 0;
	if (!entity || !keys || !key_count_out || !door_shape_out ||
	    !GameWorldValid())
		return 0;
	for (index = 1; index < globals.num_edicts; index++)
	{
		int contains = 0, complete = 0, door_shape = 0;

		candidate = &g_edicts[index];
		if (!candidate->inuse || (candidate->flags & FL_TEAMSLAVE) ||
		    (candidate != entity && !candidate->teamchain))
			continue;
		memset(candidate_keys, 0, sizeof(candidate_keys));
		candidate_count = 0U;
		if (!GameForwardPusherTeam(candidate, entity, candidate_keys,
		        &candidate_count, &contains, &complete, &door_shape))
			return 0;
		if (!contains)
			continue;
		/* Membership in an invalid/oversized chain is itself ambiguous. */
		if (!complete || matches > 0U)
			return 0;
		memcpy(keys, candidate_keys,
		    candidate_count * sizeof(keys[0]));
		*key_count_out = candidate_count;
		matched_door_shape = door_shape;
		matches++;
	}
	if (matches != 1U)
		return 0;
	*door_shape_out = matched_door_shape;
	return 1;
}

int SG_CompoundGuardGameEntityMayDispatch(edict_t *entity)
{
	sg_mover_key_t keys[SG_MOVER_LEASE_MAX_KEYS];
	sg_mover_key_t entity_key;
	sg_compound_guard_result_t entity_fence, team_fence;
	size_t key_count = 0U;
	int door_shape = 0, resolved_key;
	size_t index;

	memset(keys, 0, sizeof(keys));
	if (!GameEdictKey(entity, &resolved_key) || resolved_key <= 0 ||
	    (uintmax_t)resolved_key > (uintmax_t)UINT16_MAX)
		return 0;
	entity_key = (sg_mover_key_t)resolved_key;
	entity_fence = SG_CompoundGuardDoorPusherFence(&entity_key, 1U);
	if (entity_fence == SG_COMPOUND_GUARD_NOT_INITIALIZED)
		return 1;
	if (entity_fence != SG_COMPOUND_GUARD_OK &&
	    entity_fence != SG_COMPOUND_GUARD_NO_LEASE)
		return 0;
	/* Most entities are neither captured keys nor possible pusher teammates.
	 * Their single validated NO_LEASE result is the exact stock fast path. */
	if (entity_fence == SG_COMPOUND_GUARD_NO_LEASE &&
	    entity->movetype != MOVETYPE_PUSH &&
	    entity->movetype != MOVETYPE_STOP &&
	    !(entity->flags & FL_TEAMSLAVE) && !entity->teammaster &&
	    !entity->teamchain)
		return 1;
	if (!GamePusherTeamForEntity(entity, keys, &key_count, &door_shape))
	{
		/* An invalid/overlong/cyclic team can hide a captured suffix.  It keeps
		 * stock behavior only after a global proof that no transaction exists. */
		team_fence = SG_CompoundGuardAnyDoorTransaction();
		return entity_fence == SG_COMPOUND_GUARD_NO_LEASE &&
		       (team_fence == SG_COMPOUND_GUARD_NO_LEASE ||
		        team_fence == SG_COMPOUND_GUARD_NOT_INITIALIZED);
	}
	team_fence = SG_CompoundGuardDoorPusherFence(keys, key_count);
	if (team_fence == SG_COMPOUND_GUARD_NOT_INITIALIZED)
		return entity_fence != SG_COMPOUND_GUARD_OK;
	if (team_fence == SG_COMPOUND_GUARD_NO_LEASE)
		return entity_fence == SG_COMPOUND_GUARD_NO_LEASE;
	if (team_fence != SG_COMPOUND_GUARD_OK)
		return 0;
	/* This stricter metadata is needed only for a protected door: ordinary
	 * standalone trains, plats, and rotators commonly have no teammaster and
	 * keep their exact stock dispatch after NO_LEASE above. */
	if (!door_shape)
		return 0;
	/* Validate the pusher independently of population: an empty retirement
	 * must not admit NaN clamp input, unsupported angles, stale linkage, or an
	 * arbitrary prethink callback. */
	for (index = 0U; index < key_count; index++)
		if (!SG_MoverProspectivePusherValid(&g_edicts[keys[index]]))
			return 0;
	return GameAllSubjectsOutsideMode(&game_guard, keys, key_count, 1) ==
	       SG_COMPOUND_GUARD_YES;
}

void SG_CompoundGuardGameEntityDeferred(edict_t *entity)
{
	sg_mover_key_t keys[SG_MOVER_LEASE_MAX_KEYS];
	size_t key_count = 0U;
	int door_shape = 0, entity_key, valid = 1;
	size_t index;

	memset(keys, 0, sizeof(keys));
	if (!GameEdictKey(entity, &entity_key) || entity_key <= 0)
		return;
	if (!GamePusherTeamForEntity(entity, keys, &key_count, &door_shape) ||
	    !door_shape)
	{
		/* We cannot safely walk or delay an ambiguous chain.  Consume this
		 * member's movement arm so an overdue callback cannot publish a
		 * completion witness after the malformed state is repaired. */
		SG_MoverCompletionTransition(entity);
		return;
	}
	/* The captain is encountered first and owns the team's pusher dispatch.
	 * Its one deferral covers every slave; do not add FRAMETIME again when the
	 * entity loop later reaches a denied slave. */
	if (entity->flags & FL_TEAMSLAVE)
		return;
	for (index = 0U; index < key_count; index++)
		if (!SG_MoverProspectivePusherValid(&g_edicts[keys[index]]))
		{
			valid = 0;
			break;
		}
	if (!valid)
	{
		for (index = 0U; index < key_count; index++)
			SG_MoverCompletionTransition(&g_edicts[keys[index]]);
		return;
	}
	for (index = 0U; index < key_count; index++)
		if (g_edicts[keys[index]].nextthink > 0.0f &&
		    (!isfinite(g_edicts[keys[index]].nextthink + FRAMETIME) ||
		     g_edicts[keys[index]].nextthink + FRAMETIME <=
		         g_edicts[keys[index]].nextthink))
		{
			for (index = 0U; index < key_count; index++)
				SG_MoverCompletionTransition(&g_edicts[keys[index]]);
			return;
		}
	/* Move_Begin stores an absolute completion time after pre-subtracting all
	 * full pushes.  A guard freeze must advance that schedule exactly as stock
	 * blocked-pusher rollback does or Move_Final can complete at an intermediate
	 * pose and mint false endpoint authority. */
	for (index = 0U; index < key_count; index++)
		if (g_edicts[keys[index]].nextthink > 0.0f)
			g_edicts[keys[index]].nextthink += FRAMETIME;
}

int SG_CompoundGuardGamePusherMayAdvance(edict_t *captain)
{
	return SG_CompoundGuardGameEntityMayDispatch(captain);
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
	game_guard.protected_subject[key] = 0U;
	if (key_out)
		*key_out = key;
	return SG_COMPOUND_GUARD_OK;
}

sg_compound_guard_result_t SG_CompoundGuardGameLevelReset(void)
{
	sg_compound_guard_result_t result;

	SG_MoverCompletionReset();
	result = GameEnsureInitialized();
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	result = SG_CompoundGuardLevelReset();
	/* Generations are process-unique and retained.  Presence is level-owned:
	 * the engine is about to discard every old incarnation synchronously. */
	memset(game_guard.present, 0, sizeof(game_guard.present));
	memset(game_guard.protected_subject, 0,
	       sizeof(game_guard.protected_subject));
	return result;
}

void SG_CompoundGuardGameStorageWillFree(void)
{
	int slot;

	SG_MoverCompletionReset();
	if (!game_guard.initialized)
		return;
	(void)SG_CompoundGuardLevelReset();
	memset(game_guard.present, 0, sizeof(game_guard.present));
	memset(game_guard.protected_subject, 0,
	       sizeof(game_guard.protected_subject));
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
	result = SG_CompoundGuardBotAttach(guard_bot, bot_slot, key);
	if (result == SG_COMPOUND_GUARD_OK)
		game_guard.protected_subject[key] = 1U;
	return result;
}

sg_compound_guard_result_t SG_CompoundGuardGameBodyQueueInit(edict_t *body)
{
	int key = 0;
	sg_compound_guard_result_t result;

	result = GameMint(body, SG_MOVER_SUBJECT_BODY_QUEUE, &key);
	if (result == SG_COMPOUND_GUARD_OK)
		game_guard.protected_subject[key] = 0U;
	return result;
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
	int client_key = 0, key = 0, resolved_key = 0;
	int protected_subject = 0;
	sg_bot_t *bot;
	sg_compound_guard_result_t mint_result, copy_result;

	if (client && GameEdictKey(client, &client_key) &&
	    game_guard.protected_subject[client_key])
		protected_subject = 1;
	if (GameEdictKey(body, &resolved_key))
		mint_result = GameMint(body, SG_MOVER_SUBJECT_BODY_QUEUE, &key);
	else
		mint_result = SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	bot = GameBotForClient(client);
	if (mint_result == SG_COMPOUND_GUARD_OK)
		game_guard.protected_subject[key] = protected_subject ? 1U : 0U;
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
	game_guard.protected_subject[key] = bot ? 1U : 0U;
	if (!bot || !bot->compound_guard.attached)
		return SG_COMPOUND_GUARD_OK;
	respawn_result = SG_CompoundGuardBotRespawn(&bot->compound_guard, key);
	if (respawn_result != SG_COMPOUND_GUARD_OK)
		GameQuarantineBot(bot);
	return respawn_result;
}

sg_compound_guard_result_t SG_CompoundGuardGameHookLinked(edict_t *client,
	edict_t *bolt, sg_mover_subject_t *subject_out)
{
	int client_key, key;
	sg_bot_t *bot = GameBotForClient(client);
	sg_compound_guard_result_t result;

	if (subject_out)
		memset(subject_out, 0, sizeof(*subject_out));
	if (!GameEdictKey(client, &client_key) ||
	    !GameClientShape(client, client_key) ||
	    !GameEdictKey(bolt, &key) || !GameHookShape(client, bolt, key))
	{
		GameQuarantineBot(bot);
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	}
	result = GameMint(bolt, SG_MOVER_SUBJECT_HOOK_BOLT, &key);
	if (result == SG_COMPOUND_GUARD_OK)
	{
		game_guard.protected_subject[key] =
		    game_guard.protected_subject[client_key] ? 1U : 0U;
		if (subject_out)
		{
			subject_out->kind = SG_MOVER_SUBJECT_HOOK_BOLT;
			subject_out->edict_key = key;
			subject_out->generation = game_guard.generation[key];
		}
	}
	if (result != SG_COMPOUND_GUARD_OK)
		GameQuarantineBot(bot);
	return result;
}

sg_compound_guard_observation_t SG_CompoundGuardGameHookObserve(
	edict_t *client, const sg_mover_subject_t *subject,
	edict_t **current_out)
{
	uint64_t client_generation;
	int client_key;
	int key;

	if (current_out)
		*current_out = NULL;
	if (!current_out || !SG_MoverSubjectValid(subject) ||
	    subject->kind != SG_MOVER_SUBJECT_HOOK_BOLT ||
	    !game_guard.initialized || game_guard.initialization_failed ||
	    game_guard.generation_exhausted || !GameWorldValid() ||
	    !GameEdictKey(client, &client_key) ||
	    !GameClientShape(client, client_key) ||
	    GameIdentity(&game_guard, client_key, &client_generation) !=
	        SG_COMPOUND_GUARD_YES)
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	key = subject->edict_key;
	if (key <= game.maxclients + BODY_QUEUE_SIZE ||
	    key >= globals.num_edicts || key >= game.maxentities ||
	    key >= MAX_EDICTS || game_guard.generation[key] == 0U ||
	    game_guard.generation[key] != subject->generation ||
	    game_guard.kind[key] != SG_MOVER_SUBJECT_HOOK_BOLT)
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	if (!game_guard.present[key])
		return SG_COMPOUND_GUARD_NO;
	if (GameResolveHook(client) != key)
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	*current_out = &g_edicts[key];
	return SG_COMPOUND_GUARD_YES;
}

sg_compound_guard_observation_t SG_CompoundGuardGameHookAbsent(
	edict_t *client)
{
	uint64_t client_generation;
	int client_key;
	int hook_key;

	if (!game_guard.initialized || game_guard.initialization_failed ||
	    game_guard.generation_exhausted || !GameWorldValid() ||
	    !GameEdictKey(client, &client_key) ||
	    !GameClientShape(client, client_key) ||
	    GameIdentity(&game_guard, client_key, &client_generation) !=
	        SG_COMPOUND_GUARD_YES)
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	hook_key = GameResolveHook(client);
	if (hook_key < 0)
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	return hook_key == 0 ? SG_COMPOUND_GUARD_YES : SG_COMPOUND_GUARD_NO;
}

static int GameClientBlocksAnyClaim(edict_t *client, int client_key)
{
	sg_mover_lease_record_t record;
	sg_mover_ticket_t ticket;
	sg_mover_subject_t subject;
	sg_compound_guard_observation_t observation;
	size_t slot;

	if (!client || client_key <= 0 || !game_guard.present[client_key] ||
	    !game_guard.protected_subject[client_key] ||
	    game_guard.generation[client_key] == 0U)
		return 0;
	memset(&subject, 0, sizeof(subject));
	subject.kind = SG_MOVER_SUBJECT_CLIENT;
	subject.edict_key = client_key;
	subject.generation = game_guard.generation[client_key];
	if (GameSolid(&game_guard, &subject) == SG_COMPOUND_GUARD_NO)
		return 0;
	for (slot = 0U; slot < SG_MOVER_LEASE_MAX_RECORDS; slot++)
	{
		if (!SG_CompoundGuardRecordAt(slot, &record, &ticket))
			continue;
		observation = GameOutsideSweep(&game_guard, &subject, record.keys,
			record.key_count);
		if (observation != SG_COMPOUND_GUARD_YES)
			return 1;
	}
	return 0;
}

static sg_compound_guard_result_t GameActionOrphan(sg_bot_t *bot,
	int bolt_key, qboolean *handled)
{
	sg_mover_lease_record_t record;
	sg_compound_guard_result_t result;
	qboolean drop_owned;
	qboolean hook_owned;

	if (!bot)
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	if (handled)
		*handled = false;
	drop_owned = bot->compound_drop_live.guard_owned;
	hook_owned = bot->compound_hook_live.guard_owned;
	if (drop_owned && hook_owned)
	{
		GameQuarantineBot(bot);
		return SG_COMPOUND_GUARD_HOST_ERROR;
	}
	if (!drop_owned && !hook_owned)
		return SG_COMPOUND_GUARD_OK;
	if (handled)
		*handled = true;
	result = drop_owned ? SG_CompoundDropGameOrphan(bot, bolt_key) :
	                        SG_CompoundHookGameOrphan(bot);
	if (result != SG_COMPOUND_GUARD_OK)
	{
		GameQuarantineBot(bot);
		return result;
	}
	memset(&record, 0, sizeof(record));
	if (SG_CompoundGuardValidate(&bot->compound_guard, &record) !=
	        SG_COMPOUND_GUARD_OK ||
	    record.law != SG_MOVER_LAW_COMPOUND_PREOPEN ||
	    record.state != SG_MOVER_LEASE_ORPHAN)
	{
		GameQuarantineBot(bot);
		return SG_COMPOUND_GUARD_HOST_ERROR;
	}
	return SG_COMPOUND_GUARD_OK;
}

sg_compound_guard_result_t SG_CompoundGuardGamePlayerDie(edict_t *client)
{
	int bolt_key = 0;
	int client_key;
	sg_bot_t *bot;
	sg_mover_lease_record_t record;
	sg_compound_guard_result_t result;
	qboolean claimed = false;
	qboolean already_orphan = false;
	qboolean action_orphaned = false;

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
	memset(&record, 0, sizeof(record));
	(void)SG_CompoundGuardValidate(&bot->compound_guard, &record);
	claimed = (record.law == SG_MOVER_LAW_DECLARED_DOOR ||
	           record.law == SG_MOVER_LAW_COMPOUND_PREOPEN) &&
	          (record.state == SG_MOVER_LEASE_ACTIVE ||
	           record.state == SG_MOVER_LEASE_PAUSED ||
	           record.state == SG_MOVER_LEASE_QUARANTINED);
	already_orphan = (record.law == SG_MOVER_LAW_DECLARED_DOOR ||
	                  record.law == SG_MOVER_LAW_COMPOUND_PREOPEN) &&
	                 record.state == SG_MOVER_LEASE_ORPHAN;
	if (GameClientBlocksAnyClaim(client, client_key))
		claimed = true;
	if (SG_CompoundGuardBotRunState(&bot->compound_guard) ==
	    SG_COMPOUND_GUARD_RUN_TERMINAL)
		claimed = true;
	bolt_key = GameResolveHook(client);
	result = GameActionOrphan(bot, bolt_key, &action_orphaned);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	if (action_orphaned)
	{
		claimed = true;
		already_orphan = true;
	}
	if (already_orphan)
		result = SG_COMPOUND_GUARD_OK;
	else
	{
		result = SG_CompoundGuardOrphan(&bot->compound_guard, bolt_key);
		if (result != SG_COMPOUND_GUARD_OK)
			GameQuarantineBot(bot);
	}
	/* A normal player corpse remains SOLID_BBOX longer than the guard's short
	 * physical TOP lease.  Once an exact mover claim is orphaned (or terminally
	 * quarantined), force the ordinary player_die path into its stock gib branch:
	 * the death/obituary/respawn lifecycle is unchanged, but ThrowClientHead
	 * makes this client SOLID_NOT before a scheduled door can close onto it. */
	if (claimed && client->health > -100)
		client->health = -100;
	SG_CompoundSwimGameClientRetired(client);
	return result;
}

sg_compound_guard_result_t SG_CompoundGuardGameClientDisconnecting(
	edict_t *client)
{
	int bolt_key;
	int client_key;
	sg_bot_t *bot = GameBotForClient(client);

	if (!GameEdictKey(client, &client_key) ||
	    !GameClientShape(client, client_key))
	{
		GameQuarantineBot(bot);
		return bot && bot->compound_guard.attached ?
		    SG_COMPOUND_GUARD_HOST_ERROR :
		    SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	}
	if (!bot || !bot->compound_guard.attached)
		return SG_COMPOUND_GUARD_OK;
	bolt_key = GameResolveHook(client);
	return GameActionOrphan(bot, bolt_key, NULL);
}

void SG_CompoundGuardGameEntityFreed(edict_t *entity)
{
	int key;

	SG_MoverCompletionForget(entity);
	if (!game_guard.initialized || !GameEdictKey(entity, &key))
		return;
	game_guard.present[key] = 0U;
	game_guard.protected_subject[key] = 0U;
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
	game_guard.protected_subject[key] = 0U;
	if (result != SG_COMPOUND_GUARD_OK)
		GameQuarantineBot(bot);
	SG_CompoundSwimGameClientRetired(client);
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
