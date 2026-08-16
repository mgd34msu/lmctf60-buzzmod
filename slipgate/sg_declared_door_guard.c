/* sg_declared_door_guard.c -- ordinary RL_DOOR shared-mover authority. */
#include "../g_local.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "sg_local.h"
#include "sg_bot.h"
#include "sg_declared_door_guard.h"

void door_use(edict_t *self, edict_t *other, edict_t *activator);

typedef struct sg_declared_door_snapshot_s
{
	rune_t *rune;
	rune_link_t *links;
	rune_seed_t *seeds;
	int num_links;
	int num_seeds;
	int link_index;
	int from;
	int to;
	byte action;
	vec3_t anchor;
	vec3_t source;
} sg_declared_door_snapshot_t;

static int DeclaredDoorFinite3(const vec3_t value)
{
	return value && isfinite(value[0]) && isfinite(value[1]) &&
	       isfinite(value[2]);
}

static int DeclaredDoorWorldValid(void)
{
	return g_edicts && globals.edicts == g_edicts &&
	       globals.edict_size == (int)sizeof(edict_t) &&
	       globals.num_edicts > 1 &&
	       globals.num_edicts <= MAX_EDICTS &&
	       game.maxentities > BODY_QUEUE_SIZE &&
	       game.maxentities <= MAX_EDICTS &&
	       globals.max_edicts == game.maxentities &&
	       globals.num_edicts <= game.maxentities && game.clients &&
	       game.maxclients > 0 &&
	       game.maxclients < game.maxentities - BODY_QUEUE_SIZE &&
	       (uintmax_t)(globals.num_edicts - 1) <= (uintmax_t)UINT16_MAX;
}

static int DeclaredDoorPhysicalKey(int key)
{
	return key > game.maxclients + BODY_QUEUE_SIZE &&
	       key < globals.num_edicts;
}

/* Equality against the exact exported array elements is defined even for a
 * foreign pointer.  Subtracting an untrusted member from g_edicts would not
 * be, so deliberately use a bounded scan. */
static int DeclaredDoorEdictKey(const edict_t *entity, int *key_out)
{
	int key;

	if (key_out)
		*key_out = 0;
	if (!entity || !key_out || !DeclaredDoorWorldValid())
		return 0;
	for (key = 1; key < globals.num_edicts; key++)
		if (entity == &g_edicts[key])
		{
			if (!entity->inuse || entity->s.number != key)
				return 0;
			*key_out = key;
			return 1;
		}
	return 0;
}

static int DeclaredDoorSnapshot(int link_index,
	sg_declared_door_snapshot_t *snapshot)
{
	rune_t *rune;
	rune_link_t *link;

	if (!snapshot || link_index < 0)
		return 0;
	memset(snapshot, 0, sizeof(*snapshot));
	rune = SG_Rune();
	if (!rune || !SG_RunePublishedShapeValid(rune) ||
	    !SG_RunePhysicsCompatible(rune) ||
	    !rune->links || !rune->seeds ||
	    rune->hdr.num_links <= 0 || rune->hdr.num_links > RUNE_MAX_LINKS ||
	    rune->hdr.num_seeds <= 0 || rune->hdr.num_seeds > SG_MAX_SEEDS ||
	    link_index >= rune->hdr.num_links)
		return 0;
	link = &rune->links[link_index];
	if (link->action != RL_DOOR || link->from < 0 ||
	    link->from >= rune->hdr.num_seeds || link->to < 0 ||
	    link->to >= rune->hdr.num_seeds ||
	    !DeclaredDoorFinite3(link->anchor) ||
	    !DeclaredDoorFinite3(rune->seeds[link->from].origin))
		return 0;
	snapshot->rune = rune;
	snapshot->links = rune->links;
	snapshot->seeds = rune->seeds;
	snapshot->num_links = rune->hdr.num_links;
	snapshot->num_seeds = rune->hdr.num_seeds;
	snapshot->link_index = link_index;
	snapshot->from = link->from;
	snapshot->to = link->to;
	snapshot->action = link->action;
	memcpy(snapshot->anchor, link->anchor, sizeof(snapshot->anchor));
	memcpy(snapshot->source, rune->seeds[link->from].origin,
	       sizeof(snapshot->source));
	return 1;
}

static int DeclaredDoorSnapshotCurrent(
	const sg_declared_door_snapshot_t *snapshot)
{
	rune_t *rune;
	rune_link_t *link;

	if (!snapshot || !(rune = SG_Rune()) || rune != snapshot->rune ||
	    rune->links != snapshot->links || rune->seeds != snapshot->seeds ||
	    rune->hdr.num_links != snapshot->num_links ||
	    rune->hdr.num_seeds != snapshot->num_seeds ||
	    snapshot->link_index < 0 ||
	    snapshot->link_index >= rune->hdr.num_links)
		return 0;
	link = &rune->links[snapshot->link_index];
	return link->action == snapshot->action && link->action == RL_DOOR &&
	       link->from == snapshot->from && link->to == snapshot->to &&
	       link->from >= 0 && link->from < rune->hdr.num_seeds &&
	       memcmp(link->anchor, snapshot->anchor,
	              sizeof(snapshot->anchor)) == 0 &&
	       memcmp(rune->seeds[link->from].origin, snapshot->source,
	              sizeof(snapshot->source)) == 0;
}

static void DeclaredDoorSortKeys(sg_mover_key_t *keys, size_t count)
{
	size_t index;

	for (index = 1U; index < count; index++)
	{
		sg_mover_key_t key = keys[index];
		size_t cursor = index;

		while (cursor > 0U && keys[cursor - 1U] > key)
		{
			keys[cursor] = keys[cursor - 1U];
			cursor--;
		}
		keys[cursor] = key;
	}
}

static sg_compound_guard_result_t DeclaredDoorResolveTrigger(
	edict_t *trigger, sg_mover_key_t *keys_out, size_t *key_count_out,
	int *trigger_key_out)
{
	edict_t *members[SG_MOVER_LEASE_MAX_KEYS];
	size_t unique_count = 0U;
	int count;
	int index;
	int trigger_key;

	if (key_count_out)
		*key_count_out = 0U;
	if (trigger_key_out)
		*trigger_key_out = 0;
	if (!keys_out || !key_count_out)
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	if (!DeclaredDoorWorldValid())
		return SG_COMPOUND_GUARD_AUTHORITY_MISMATCH;
	if (!DeclaredDoorEdictKey(trigger, &trigger_key) ||
	    !DeclaredDoorPhysicalKey(trigger_key))
		return SG_COMPOUND_GUARD_AUTHORITY_MISMATCH;
	memset(members, 0, sizeof(members));
	count = SG_DeclaredDoorMembers(trigger, members,
	    (int)SG_MOVER_LEASE_MAX_KEYS);
	if (count <= 0 || count > (int)SG_MOVER_LEASE_MAX_KEYS)
		return SG_COMPOUND_GUARD_INVALID_KEYS;
	for (index = 0; index < count; index++)
	{
		int key;

		if (!DeclaredDoorEdictKey(members[index], &key) ||
		    !DeclaredDoorPhysicalKey(key) ||
		    key <= 0 || (uintmax_t)key > (uintmax_t)UINT16_MAX)
			return SG_COMPOUND_GUARD_INVALID_KEYS;
		keys_out[unique_count++] = (sg_mover_key_t)key;
	}
	DeclaredDoorSortKeys(keys_out, unique_count);
	if (unique_count > 1U)
	{
		size_t source;
		size_t destination = 1U;

		for (source = 1U; source < unique_count; source++)
			if (keys_out[source] != keys_out[destination - 1U])
				keys_out[destination++] = keys_out[source];
		unique_count = destination;
	}
	if (unique_count == 0U ||
	    unique_count > SG_MOVER_LEASE_MAX_KEYS)
		return SG_COMPOUND_GUARD_INVALID_KEYS;
	*key_count_out = unique_count;
	if (trigger_key_out)
		*trigger_key_out = trigger_key;
	return SG_COMPOUND_GUARD_OK;
}

static sg_compound_guard_result_t DeclaredDoorResolve(
	int link_index, sg_mover_key_t *keys_out, size_t *key_count_out,
	edict_t **trigger_out, int *trigger_key_out)
{
	sg_declared_door_snapshot_t snapshot;
	edict_t *trigger;
	sg_compound_guard_result_t result;

	if (key_count_out)
		*key_count_out = 0U;
	if (trigger_out)
		*trigger_out = NULL;
	if (trigger_key_out)
		*trigger_key_out = 0;
	if (!keys_out || !key_count_out || link_index < 0)
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	if (!DeclaredDoorSnapshot(link_index, &snapshot))
		return SG_COMPOUND_GUARD_AUTHORITY_MISMATCH;
	trigger = SG_DeclaredDoorForLink(snapshot.anchor, snapshot.source);
	result = DeclaredDoorResolveTrigger(trigger, keys_out, key_count_out,
	    trigger_key_out);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	/* The resolver and enumeration are host/world calls.  Refuse their result
	 * if the current rune, link, anchor, or source changed underneath them. */
	if (!DeclaredDoorSnapshotCurrent(&snapshot))
		return SG_COMPOUND_GUARD_AUTHORITY_MISMATCH;
	if (trigger_out)
		*trigger_out = trigger;
	return SG_COMPOUND_GUARD_OK;
}

static sg_compound_guard_result_t DeclaredDoorResolveBound(
	uint32_t mechanism_index, sg_mover_key_t *keys_out,
	size_t *key_count_out, edict_t **trigger_out)
{
	edict_t *trigger;
	int trigger_key;
	sg_compound_guard_result_t result;

	if (trigger_out)
		*trigger_out = NULL;
	if (mechanism_index == 0U || mechanism_index > (uint32_t)INT_MAX ||
	    !DeclaredDoorWorldValid() ||
	    !DeclaredDoorPhysicalKey((int)mechanism_index))
		return SG_COMPOUND_GUARD_AUTHORITY_MISMATCH;
	trigger = &g_edicts[(int)mechanism_index];
	result = DeclaredDoorResolveTrigger(trigger, keys_out, key_count_out,
	    &trigger_key);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	if ((uint32_t)trigger_key != mechanism_index)
		return SG_COMPOUND_GUARD_AUTHORITY_MISMATCH;
	if (trigger_out)
		*trigger_out = trigger;
	return SG_COMPOUND_GUARD_OK;
}

static int DeclaredDoorBoundTriggerAbsent(uint32_t mechanism_index)
{
	return mechanism_index > 0U && mechanism_index <= (uint32_t)INT_MAX &&
	       DeclaredDoorWorldValid() &&
	       DeclaredDoorPhysicalKey((int)mechanism_index) &&
	       !g_edicts[(int)mechanism_index].inuse;
}

static sg_compound_guard_result_t DeclaredDoorMembersFromKeys(
	const sg_mover_key_t *keys, size_t key_count, edict_t **members_out)
{
	size_t index;

	if (!keys || !members_out || key_count == 0U ||
	    key_count > SG_MOVER_LEASE_MAX_KEYS || !DeclaredDoorWorldValid())
		return SG_COMPOUND_GUARD_INVALID_KEYS;
	for (index = 0U; index < key_count; index++)
	{
		int key = (int)keys[index];

		if (!DeclaredDoorPhysicalKey(key) ||
		    (index > 0U && keys[index - 1U] >= keys[index]))
			return SG_COMPOUND_GUARD_INVALID_KEYS;
		members_out[index] = &g_edicts[key];
	}
	return SG_COMPOUND_GUARD_OK;
}

static sg_compound_guard_result_t DeclaredDoorAllBotsOutside(
	const sg_mover_key_t *keys, size_t key_count)
{
	edict_t *members[SG_MOVER_LEASE_MAX_KEYS];
	size_t member_index;
	int bot_index;
	sg_compound_guard_result_t result;

	result = DeclaredDoorMembersFromKeys(keys, key_count, members);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	/* An unleased contender can be displaced into another bot's mover sweep
	 * after losing acquisition.  Releasing on the owner's body alone would let
	 * the door close on that second SG client.  Require every live SG body to
	 * be outside the exact captured set; humans retain stock door semantics. */
	for (bot_index = 0; bot_index < SG_MAXBOTS; bot_index++)
	{
		edict_t *subject;

		if (!sg_bots[bot_index].active)
			continue;
		subject = sg_bots[bot_index].ent;
		if (!subject || !subject->inuse)
			return SG_COMPOUND_GUARD_HOST_ERROR;
		if (subject->solid == SOLID_NOT)
			continue;
		for (member_index = 0U; member_index < key_count; member_index++)
			if (!SG_MoverSubjectOutsideSweep(members[member_index], subject))
				return SG_COMPOUND_GUARD_NOT_CLEAR;
	}
	return SG_COMPOUND_GUARD_OK;
}

static sg_compound_guard_result_t DeclaredDoorResolveMaster(
	edict_t *door, sg_mover_key_t *keys_out, size_t *key_count_out)
{
	edict_t *master, *member;
	size_t count = 0U;
	int door_key;
	int door_found = 0;

	if (key_count_out)
		*key_count_out = 0U;
	if (!door || !keys_out || !key_count_out || !DeclaredDoorWorldValid())
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	/* Validate the caller's pointer before reading teammaster.  Subsequent
	 * teamchain elements are likewise proved exact array members before any
	 * classname, callback, or next-pointer dereference. */
	if (!DeclaredDoorEdictKey(door, &door_key) ||
	    !DeclaredDoorPhysicalKey(door_key))
		return SG_COMPOUND_GUARD_AUTHORITY_MISMATCH;
	master = door->teammaster ? door->teammaster : door;
	for (member = master; member; member = member->teamchain)
	{
		int key;
		size_t prior;

		if (count >= SG_MOVER_LEASE_MAX_KEYS ||
		    !DeclaredDoorEdictKey(member, &key) ||
		    !DeclaredDoorPhysicalKey(key) || !member->classname ||
		    (strcmp(member->classname, "func_door") != 0 &&
		     strcmp(member->classname, "func_door_rotating") != 0) ||
		    member->use != door_use ||
		    (uintmax_t)key > (uintmax_t)UINT16_MAX)
			return SG_COMPOUND_GUARD_AUTHORITY_MISMATCH;
		for (prior = 0U; prior < count; prior++)
			if (keys_out[prior] == (sg_mover_key_t)key)
				return SG_COMPOUND_GUARD_AUTHORITY_MISMATCH;
		if (key == door_key)
			door_found = 1;
		keys_out[count++] = (sg_mover_key_t)key;
	}
	if (count == 0U || !door_found)
		return SG_COMPOUND_GUARD_AUTHORITY_MISMATCH;
	DeclaredDoorSortKeys(keys_out, count);
	*key_count_out = count;
	return SG_COMPOUND_GUARD_OK;
}

static int DeclaredDoorRecordMatches(
	const sg_mover_lease_record_t *record, const sg_mover_key_t *keys,
	size_t key_count, int link_index, uint32_t mechanism_index,
	sg_mover_lease_state_t state)
{
	return record && keys && key_count > 0U &&
	       key_count <= SG_MOVER_LEASE_MAX_KEYS &&
	       record->law == SG_MOVER_LAW_DECLARED_DOOR &&
	       record->state == (uint8_t)state &&
	       record->link_index == link_index &&
	       record->mechanism_index == mechanism_index &&
	       record->key_count == key_count &&
	       memcmp(record->keys, keys,
	              key_count * sizeof(keys[0])) == 0;
}

static int DeclaredDoorRecordCanonical(
	const sg_mover_lease_record_t *record,
	const sg_mover_ticket_t *ticket)
{
	size_t index;
	int bolt_empty;

	if (!record || !ticket || !SG_MoverTicketValid(ticket) ||
	    !SG_MoverOwnerValid(&record->owner) ||
	    !SG_MoverSubjectValid(&record->body) || record->serial == 0U ||
	    ticket->serial != record->serial || record->reserved != 0U ||
	    record->link_index < 0 || record->mechanism_index == 0U ||
	    record->mechanism_index > (uint32_t)INT_MAX ||
	    !DeclaredDoorPhysicalKey((int)record->mechanism_index) ||
	    record->key_count == 0U ||
	    record->key_count > SG_MOVER_LEASE_MAX_KEYS ||
	    (record->law != SG_MOVER_LAW_DECLARED_DOOR &&
	     record->law != SG_MOVER_LAW_COMPOUND_PREOPEN) ||
	    (record->state != SG_MOVER_LEASE_ACTIVE &&
	     record->state != SG_MOVER_LEASE_PAUSED &&
	     record->state != SG_MOVER_LEASE_ORPHAN &&
	     record->state != SG_MOVER_LEASE_QUARANTINED))
		return 0;
	bolt_empty = record->bolt.generation == 0U &&
	    record->bolt.edict_key == 0 &&
	    record->bolt.kind == SG_MOVER_SUBJECT_NONE &&
	    record->bolt.reserved[0] == 0U &&
	    record->bolt.reserved[1] == 0U &&
	    record->bolt.reserved[2] == 0U;
	if (!bolt_empty && (!SG_MoverSubjectValid(&record->bolt) ||
	    record->bolt.kind != SG_MOVER_SUBJECT_HOOK_BOLT))
		return 0;
	for (index = 0U; index < record->key_count; index++)
		if (record->keys[index] == 0U ||
		    !DeclaredDoorPhysicalKey((int)record->keys[index]) ||
		    (index > 0U &&
		     record->keys[index - 1U] >= record->keys[index]))
			return 0;
	return 1;
}

sg_compound_guard_result_t SG_DeclaredDoorGuardAcquire(sg_bot_t *bot,
	int link_index)
{
	sg_mover_key_t keys[SG_MOVER_LEASE_MAX_KEYS];
	size_t key_count;
	int trigger_key;
	sg_compound_guard_result_t result;

	if (!bot)
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	result = DeclaredDoorResolve(link_index, keys, &key_count, NULL,
	    &trigger_key);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	return SG_CompoundGuardAcquireDeclaredDoorBound(&bot->compound_guard,
	    keys, key_count, link_index, (uint32_t)trigger_key);
}

sg_compound_guard_result_t SG_DeclaredDoorGuardAuthorize(sg_bot_t *bot,
	int link_index)
{
	sg_mover_key_t keys[SG_MOVER_LEASE_MAX_KEYS];
	size_t key_count;
	int trigger_key;
	sg_compound_guard_result_t result;

	if (!bot)
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	result = DeclaredDoorResolve(link_index, keys, &key_count, NULL,
	    &trigger_key);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	return SG_CompoundGuardAuthorize(&bot->compound_guard,
	    SG_MOVER_LAW_DECLARED_DOOR, keys, key_count, link_index,
	    (uint32_t)trigger_key);
}

sg_compound_guard_result_t SG_DeclaredDoorGuardAuthorizeActivation(
	sg_bot_t *bot, int link_index)
{
	sg_compound_guard_result_t result;

	result = SG_DeclaredDoorGuardAuthorize(bot, link_index);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	return SG_CompoundGuardAllSubjectsOutside(&bot->compound_guard);
}

sg_compound_guard_result_t SG_DeclaredDoorGuardPause(sg_bot_t *bot)
{
	sg_mover_lease_record_t record;
	sg_mover_key_t keys[SG_MOVER_LEASE_MAX_KEYS];
	size_t key_count;
	sg_compound_guard_result_t result;

	if (!bot)
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	result = SG_CompoundGuardValidate(&bot->compound_guard, &record);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	result = DeclaredDoorResolveBound(record.mechanism_index, keys,
	    &key_count, NULL);
	if (result != SG_COMPOUND_GUARD_OK)
	{
		/* Losing an admitted, non-killtargetable activator is corruption, but
		 * reducing an exact ACTIVE claim to PAUSED remains safe and is required
		 * for bounded physical maintenance while fail-closed. */
		if (!DeclaredDoorBoundTriggerAbsent(record.mechanism_index) ||
		    record.key_count == 0U ||
		    record.key_count > SG_MOVER_LEASE_MAX_KEYS)
			return result;
		key_count = record.key_count;
		memcpy(keys, record.keys, key_count * sizeof(keys[0]));
	}
	if (DeclaredDoorRecordMatches(&record, keys, key_count,
	    record.link_index, record.mechanism_index,
	    SG_MOVER_LEASE_PAUSED))
		return SG_COMPOUND_GUARD_OK;
	if (!DeclaredDoorRecordMatches(&record, keys, key_count,
	    record.link_index, record.mechanism_index,
	    SG_MOVER_LEASE_ACTIVE))
		return SG_COMPOUND_GUARD_AUTHORITY_MISMATCH;
	result = SG_CompoundGuardAuthorize(&bot->compound_guard,
	    SG_MOVER_LAW_DECLARED_DOOR, keys, key_count, record.link_index,
	    record.mechanism_index);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	return SG_CompoundGuardPause(&bot->compound_guard);
}

sg_compound_guard_result_t SG_DeclaredDoorGuardResume(sg_bot_t *bot,
	int link_index)
{
	sg_mover_lease_record_t record;
	sg_mover_key_t keys[SG_MOVER_LEASE_MAX_KEYS];
	size_t key_count;
	int trigger_key;
	sg_compound_guard_result_t result;

	if (!bot)
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	result = DeclaredDoorResolve(link_index, keys, &key_count, NULL,
	    &trigger_key);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	result = SG_CompoundGuardValidate(&bot->compound_guard, &record);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	if (!DeclaredDoorRecordMatches(&record, keys, key_count, link_index,
	    (uint32_t)trigger_key, SG_MOVER_LEASE_PAUSED))
		return SG_COMPOUND_GUARD_AUTHORITY_MISMATCH;
	result = SG_CompoundGuardResume(&bot->compound_guard);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	result = SG_CompoundGuardAuthorize(&bot->compound_guard,
	    SG_MOVER_LAW_DECLARED_DOOR, keys, key_count, link_index,
	    (uint32_t)trigger_key);
	if (result != SG_COMPOUND_GUARD_OK)
	{
		/* Never return from a failed post-transition proof while leaving the
		 * ordinary door claim ACTIVE.  A failed re-pause is itself uncertain,
		 * so terminalize the claim instead of exposing mutation authority. */
		if (SG_CompoundGuardPause(&bot->compound_guard) !=
		    SG_COMPOUND_GUARD_OK)
			(void)SG_CompoundGuardQuarantine(&bot->compound_guard);
	}
	return result;
}

sg_compound_guard_result_t SG_DeclaredDoorGuardHoldOpen(sg_bot_t *bot,
	int lease_ms)
{
	sg_mover_lease_record_t record;
	sg_mover_key_t keys[SG_MOVER_LEASE_MAX_KEYS];
	edict_t *members[SG_MOVER_LEASE_MAX_KEYS];
	size_t key_count;
	sg_compound_guard_result_t result;
	int active, paused, quarantined;
	int resumed = 0;

	if (!bot || lease_ms < 100 || lease_ms > 1000)
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	result = SG_CompoundGuardValidate(&bot->compound_guard, &record);
	quarantined = result == SG_COMPOUND_GUARD_QUARANTINE_LOCKED &&
	    record.law == SG_MOVER_LAW_DECLARED_DOOR &&
	    record.state == SG_MOVER_LEASE_QUARANTINED &&
	    record.link_index >= 0 && record.mechanism_index != 0U;
	if (result != SG_COMPOUND_GUARD_OK && !quarantined)
		return result;
	result = DeclaredDoorResolveBound(record.mechanism_index, keys,
	    &key_count, NULL);
	if (result != SG_COMPOUND_GUARD_OK)
	{
		/* The activator may be killtargeted after acquisition.  The durable
		 * record still owns its exact physical keys, so protective TOP renewal
		 * remains possible without granting touch/use authority. */
		if (!DeclaredDoorBoundTriggerAbsent(record.mechanism_index) ||
		    record.key_count == 0U ||
		    record.key_count > SG_MOVER_LEASE_MAX_KEYS)
			return result;
		key_count = record.key_count;
		memcpy(keys, record.keys, key_count * sizeof(keys[0]));
	}
	active = DeclaredDoorRecordMatches(&record, keys, key_count,
	    record.link_index, record.mechanism_index,
	    SG_MOVER_LEASE_ACTIVE);
	paused = DeclaredDoorRecordMatches(&record, keys, key_count,
	    record.link_index, record.mechanism_index,
	    SG_MOVER_LEASE_PAUSED);
	quarantined = quarantined && record.key_count == key_count &&
	    memcmp(record.keys, keys, key_count * sizeof(keys[0])) == 0;
	if (!active && !paused && !quarantined)
		return SG_COMPOUND_GUARD_AUTHORITY_MISMATCH;
	result = DeclaredDoorMembersFromKeys(keys, key_count, members);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	if (paused)
	{
		result = SG_CompoundGuardResume(&bot->compound_guard);
		if (result != SG_COMPOUND_GUARD_OK)
			return result;
		resumed = 1;
	}
	if (quarantined)
		result = SG_COMPOUND_GUARD_OK;
	else
		result = SG_CompoundGuardAuthorize(&bot->compound_guard,
		    SG_MOVER_LAW_DECLARED_DOOR, keys, key_count, record.link_index,
		    record.mechanism_index);
	if (result == SG_COMPOUND_GUARD_OK &&
	    !SG_DeclaredDoorHoldMembers(members, (int)key_count, lease_ms))
		result = SG_COMPOUND_GUARD_HOST_ERROR;
	if (resumed)
	{
		sg_compound_guard_result_t pause_result =
		    SG_CompoundGuardPause(&bot->compound_guard);

		if (pause_result != SG_COMPOUND_GUARD_OK)
		{
			(void)SG_CompoundGuardQuarantine(&bot->compound_guard);
			if (result == SG_COMPOUND_GUARD_OK)
				result = pause_result;
		}
	}
	return result;
}

sg_compound_guard_result_t SG_DeclaredDoorGuardReleaseProvedClear(
	sg_bot_t *bot)
{
	sg_mover_lease_record_t record;
	sg_mover_key_t keys[SG_MOVER_LEASE_MAX_KEYS];
	size_t key_count;
	sg_compound_guard_result_t result;

	if (!bot)
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	result = SG_CompoundGuardValidate(&bot->compound_guard, &record);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	if (record.law != SG_MOVER_LAW_DECLARED_DOOR ||
	    record.link_index < 0 || record.mechanism_index == 0U ||
	    (record.state != SG_MOVER_LEASE_ACTIVE &&
	     record.state != SG_MOVER_LEASE_PAUSED))
		return SG_COMPOUND_GUARD_AUTHORITY_MISMATCH;
	result = DeclaredDoorResolveBound(record.mechanism_index, keys,
	    &key_count, NULL);
	if (result != SG_COMPOUND_GUARD_OK)
	{
		/* A mapper may kill the bound trigger after acquisition.  With that
		 * exact edict absent, the core's captured-key subject proof is the only
		 * remaining authority and may safely release once every old mover sweep
		 * is clear.  A live-but-drifted/reused trigger remains fail-closed. */
		if (DeclaredDoorBoundTriggerAbsent(record.mechanism_index))
		{
			result = DeclaredDoorAllBotsOutside(record.keys,
			    record.key_count);
			if (result != SG_COMPOUND_GUARD_OK)
				return result;
			return SG_CompoundGuardReleaseProvedClear(&bot->compound_guard);
		}
		return result;
	}
	if (!DeclaredDoorRecordMatches(&record, keys, key_count,
	        record.link_index, record.mechanism_index,
	        SG_MOVER_LEASE_ACTIVE) &&
	    !DeclaredDoorRecordMatches(&record, keys, key_count,
	        record.link_index, record.mechanism_index,
	        SG_MOVER_LEASE_PAUSED))
		return SG_COMPOUND_GUARD_AUTHORITY_MISMATCH;
	result = DeclaredDoorAllBotsOutside(keys, key_count);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	return SG_CompoundGuardReleaseProvedClear(&bot->compound_guard);
}

int SG_DeclaredDoorGuardActivationAvailable(edict_t *door_master)
{
	sg_mover_key_t keys[SG_MOVER_LEASE_MAX_KEYS];
	size_t key_count, slot, left, right;
	sg_mover_lease_record_t record;
	sg_mover_ticket_t ticket;

	if (DeclaredDoorResolveMaster(door_master, keys, &key_count) !=
	    SG_COMPOUND_GUARD_OK)
		return 0;
	if (SG_CompoundGuardRetirementOverlaps(keys, key_count))
		return 0;
	for (slot = 0U; slot < SG_MOVER_LEASE_MAX_RECORDS; slot++)
	{
		if (!SG_CompoundGuardRecordAt(slot, &record, &ticket))
			continue;
		if (!DeclaredDoorRecordCanonical(&record, &ticket))
			return 0;
		for (left = 0U; left < key_count; left++)
			for (right = 0U; right < record.key_count; right++)
				if (keys[left] == record.keys[right])
					return 0;
	}
	return 1;
}

int SG_DeclaredDoorGuardAnyClaim(void)
{
	size_t slot;
	sg_mover_lease_record_t record;
	sg_mover_ticket_t ticket;

	/* A released set remains mutation-owned until its captured brushes reach a
	 * stationary terminal.  Unsupported delayed callbacks must not bypass that
	 * physical retirement simply because the logical lease is gone. */
	if (SG_CompoundGuardAnyRetirement())
		return 1;
	/* RecordAt returns every non-FREE slot.  That intentionally includes
	 * ORPHAN, QUARANTINED, and malformed fail-closed records: an unsupported
	 * trigger has no exact closure against which any narrower claim could be
	 * proved harmless. */
	for (slot = 0U; slot < SG_MOVER_LEASE_MAX_RECORDS; slot++)
		if (SG_CompoundGuardRecordAt(slot, &record, &ticket))
			return 1;
	return 0;
}

sg_compound_guard_run_t SG_DeclaredDoorGuardRunState(sg_bot_t *bot)
{
	return bot ? SG_CompoundGuardBotRunState(&bot->compound_guard)
	           : SG_COMPOUND_GUARD_RUN_WAIT;
}
