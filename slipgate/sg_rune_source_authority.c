#include "../g_local.h"
#undef world

#include "sg_rune_source_authority_owner.h"
#include "../g_tourney.h"
#include "sg_crc32.h"
#include "sg_weapon_effect_profile.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SG_RUNE_SOURCE_HANDLE_MAGIC UINT64_C(0x5352434155544831)

typedef enum source_owner_phase_e
{
	SOURCE_OWNER_EMPTY = 0,
	SOURCE_OWNER_CAPTURING,
	SOURCE_OWNER_PUBLISHED,
	SOURCE_OWNER_FAILED
} source_owner_phase_t;

typedef struct source_owner_s
{
	source_owner_phase_t phase;
	sg_rune_source_status_t failure;
	uint64_t generation;
	char mapname[SG_LEVEL_IDENTITY_MAPNAME_BYTES];
	char *entity_text;
	size_t entity_text_bytes;
	sg_rune_source_entity_record_t *records;
	size_t record_count;
	size_t record_capacity;
	sg_level_identity_t identity;
	sg_host_law_runtime_authority_t host;
} source_owner_t;

struct sg_rune_source_authority_s
{
	uint64_t magic;
	sg_rune_source_snapshot_t snapshot;
	char *entity_text;
	sg_rune_source_entity_record_t *records;
};

static source_owner_t source_owner;

static void SourceOwnerFreePayload(void)
{
	free(source_owner.entity_text);
	free(source_owner.records);
	source_owner.entity_text = NULL;
	source_owner.records = NULL;
	source_owner.entity_text_bytes = 0;
	source_owner.record_count = 0;
	source_owner.record_capacity = 0;
	memset(source_owner.mapname, 0, sizeof(source_owner.mapname));
	memset(&source_owner.identity, 0, sizeof(source_owner.identity));
	memset(&source_owner.host, 0, sizeof(source_owner.host));
}

static void SourceOwnerAdvanceGeneration(void)
{
	source_owner.generation++;
	if (source_owner.generation == 0)
		source_owner.generation = 1;
}

static sg_rune_source_status_t SourceOwnerFail(
	sg_rune_source_status_t status)
{
	SourceOwnerFreePayload();
	source_owner.phase = SOURCE_OWNER_FAILED;
	source_owner.failure = status;
	return status;
}

static int SourceIdentityEqual(const sg_level_identity_t *left,
	const sg_level_identity_t *right)
{
	return left->bsp_checksum == right->bsp_checksum &&
		left->entity_crc32 == right->entity_crc32 &&
		left->host_physics_id == right->host_physics_id &&
		left->bsp_bytes == right->bsp_bytes &&
		memcmp(left->bsp_sha256, right->bsp_sha256,
			sizeof(left->bsp_sha256)) == 0 &&
		memcmp(left->mapname, right->mapname,
			sizeof(left->mapname)) == 0;
}

static sg_rune_source_status_t SourceWeaponCapture(
	sg_rune_source_weapon_law_t *law_out)
{
#ifdef WEAP_BALANCE_OK
	float ctf_value;
#endif

	if (!law_out)
		return SG_RUNE_SOURCE_INVALID_ARGUMENT;
	if (!ctfflags || !deathmatch || !fastswitch)
		return SG_RUNE_SOURCE_WEAPON_UNAVAILABLE;

	memset(law_out, 0, sizeof(*law_out));
	law_out->weapon_balance_compiled =
		(uint8_t)SG_WEAPON_BALANCE_COMPILED;
#ifdef WEAP_BALANCE_OK
	ctf_value = ctfflags->value;
	if (!isfinite(ctf_value) || (double)ctf_value < (double)INT_MIN ||
		(double)ctf_value > (double)INT_MAX)
		return SG_RUNE_SOURCE_WEAPON_UNAVAILABLE;
	law_out->weapon_balance_enabled = (uint8_t)
		(((int)ctf_value & CTF_WEAP_BALANCE) != 0);
#endif
	law_out->rail_match_active =
		(uint8_t)(matchstate == MATCH_RAILGUN_INPLAY);
	law_out->deathmatch_active = (uint8_t)(deathmatch->value != 0.0f);
	law_out->fast_switch_enabled = (uint8_t)(fastswitch->value != 0.0f);
	return SG_RUNE_SOURCE_OK;
}

static int SourceWeaponEqual(const sg_rune_source_weapon_law_t *left,
	const sg_rune_source_weapon_law_t *right)
{
	return left->weapon_balance_compiled ==
			right->weapon_balance_compiled &&
		left->weapon_balance_enabled == right->weapon_balance_enabled &&
		left->rail_match_active == right->rail_match_active &&
		left->deathmatch_active == right->deathmatch_active &&
		left->fast_switch_enabled == right->fast_switch_enabled;
}

static sg_rune_source_status_t SourcePublicationCurrent(uint64_t generation,
	const sg_level_identity_t *identity,
	const sg_host_law_runtime_authority_t *host)
{
	sg_level_identity_t current_identity;
	sg_host_law_result_t host_result;
	sg_identity_status_t identity_status;

	if (source_owner.phase != SOURCE_OWNER_PUBLISHED ||
		source_owner.generation != generation)
		return SG_RUNE_SOURCE_GENERATION_DRIFT;

	memset(&current_identity, 0, sizeof(current_identity));
	identity_status = SG_LevelIdentitySnapshot(identity->mapname,
		&current_identity);
	if (identity_status != SG_IDENTITY_OK)
		return SG_RUNE_SOURCE_IDENTITY_UNAVAILABLE;
	if (!SourceIdentityEqual(identity, &current_identity))
		return SG_RUNE_SOURCE_IDENTITY_MISMATCH;

	host_result = SG_HostLawProductionAuthorityCurrent(host);
	if (host_result.status != SG_HOST_LAW_OK)
		return SG_RUNE_SOURCE_HOST_DRIFT;
	return SG_RUNE_SOURCE_OK;
}

static sg_rune_source_status_t SourceHandleCurrent(
	const sg_rune_source_authority_t *authority)
{
	sg_rune_source_status_t status;
	sg_rune_source_weapon_law_t current_weapon;

	if (!authority || authority->magic != SG_RUNE_SOURCE_HANDLE_MAGIC ||
		authority->snapshot.version != SG_RUNE_SOURCE_AUTHORITY_VERSION ||
		authority->snapshot.publication_generation_complement !=
			~authority->snapshot.publication_generation)
		return SG_RUNE_SOURCE_INVALID_ARGUMENT;

	status = SourcePublicationCurrent(
		authority->snapshot.publication_generation,
		&authority->snapshot.level_identity,
		&authority->snapshot.host_authority);
	if (status != SG_RUNE_SOURCE_OK)
		return status;
	status = SourceWeaponCapture(&current_weapon);
	if (status != SG_RUNE_SOURCE_OK)
		return status;
	if (!SourceWeaponEqual(&authority->snapshot.weapon_law,
		&current_weapon))
		return SG_RUNE_SOURCE_WEAPON_DRIFT;
	return SG_RUNE_SOURCE_OK;
}

void SG_RuneSourceAuthorityReset(void)
{
	SourceOwnerFreePayload();
	SourceOwnerAdvanceGeneration();
	source_owner.phase = SOURCE_OWNER_EMPTY;
	source_owner.failure = SG_RUNE_SOURCE_OK;
}

sg_rune_source_status_t SG_RuneSourceAuthorityBegin(const char *mapname,
	const char *selected_entity_text)
{
	size_t length;
	size_t map_length;
	char *copy;

	SourceOwnerFreePayload();
	SourceOwnerAdvanceGeneration();
	source_owner.phase = SOURCE_OWNER_EMPTY;
	source_owner.failure = SG_RUNE_SOURCE_OK;

	if (!mapname || !mapname[0])
		return SourceOwnerFail(SG_RUNE_SOURCE_INVALID_ARGUMENT);
	for (map_length = 0; map_length < sizeof(source_owner.mapname);
		map_length++)
		if (mapname[map_length] == '\0')
			break;
	if (map_length == sizeof(source_owner.mapname))
		return SourceOwnerFail(SG_RUNE_SOURCE_INVALID_ARGUMENT);
	if (!selected_entity_text)
		return SourceOwnerFail(SG_RUNE_SOURCE_ENTITY_TEXT_MISSING);
	for (length = 0; length < SG_LEVEL_ENTITY_TEXT_LIMIT; length++)
		if (selected_entity_text[length] == '\0')
			break;
	if (length == SG_LEVEL_ENTITY_TEXT_LIMIT)
		return SourceOwnerFail(SG_RUNE_SOURCE_ENTITY_TEXT_UNTERMINATED);
	copy = (char *)malloc(length + 1);
	if (!copy)
		return SourceOwnerFail(SG_RUNE_SOURCE_ALLOCATION_FAILED);
	memcpy(copy, selected_entity_text, length + 1);
	source_owner.entity_text = copy;
	source_owner.entity_text_bytes = length + 1;
	memcpy(source_owner.mapname, mapname, map_length + 1);
	source_owner.phase = SOURCE_OWNER_CAPTURING;
	return SG_RUNE_SOURCE_OK;
}

sg_rune_source_status_t SG_RuneSourceAuthorityRecord(uint32_t source_ordinal,
	int32_t effective_spawnflags)
{
	size_t capacity;
	sg_rune_source_entity_record_t *records;

	if (source_owner.phase == SOURCE_OWNER_FAILED)
		return source_owner.failure;
	if (source_owner.phase != SOURCE_OWNER_CAPTURING)
		return SG_RUNE_SOURCE_INVALID_STATE;
	if (source_owner.record_count != 0 &&
		source_ordinal <= source_owner.records[
			source_owner.record_count - 1].source_ordinal)
		return SourceOwnerFail(SG_RUNE_SOURCE_RECORD_ORDER);
	if (source_owner.record_count == UINT32_MAX)
		return SourceOwnerFail(SG_RUNE_SOURCE_SIZE_OVERFLOW);
	if (source_owner.record_count == source_owner.record_capacity)
	{
		capacity = source_owner.record_capacity == 0 ? 32 :
			source_owner.record_capacity * 2;
		if (capacity < source_owner.record_capacity ||
			capacity > SIZE_MAX / sizeof(*records))
			return SourceOwnerFail(SG_RUNE_SOURCE_SIZE_OVERFLOW);
		records = (sg_rune_source_entity_record_t *)realloc(
			source_owner.records, capacity * sizeof(*records));
		if (!records)
			return SourceOwnerFail(SG_RUNE_SOURCE_ALLOCATION_FAILED);
		source_owner.records = records;
		source_owner.record_capacity = capacity;
	}
	source_owner.records[source_owner.record_count].source_ordinal =
		source_ordinal;
	source_owner.records[source_owner.record_count].effective_spawnflags =
		effective_spawnflags;
	source_owner.record_count++;
	return SG_RUNE_SOURCE_OK;
}

sg_rune_source_status_t SG_RuneSourceAuthorityPublish(const char *mapname)
{
	uint32_t crc;
	sg_level_identity_t identity;
	sg_host_law_runtime_authority_t host;
	sg_identity_status_t identity_status;
	sg_host_law_result_t host_result;

	if (source_owner.phase == SOURCE_OWNER_FAILED)
		return source_owner.failure;
	if (source_owner.phase != SOURCE_OWNER_CAPTURING || !mapname ||
		strcmp(mapname, source_owner.mapname) != 0)
		return SourceOwnerFail(SG_RUNE_SOURCE_INVALID_STATE);
	memset(&identity, 0, sizeof(identity));
	identity_status = SG_LevelIdentitySnapshot(mapname, &identity);
	if (identity_status != SG_IDENTITY_OK)
		return SourceOwnerFail(SG_RUNE_SOURCE_IDENTITY_UNAVAILABLE);
	if (!SG_CRC32Buffer(source_owner.entity_text,
		source_owner.entity_text_bytes - 1, &crc) ||
		crc != identity.entity_crc32)
		return SourceOwnerFail(SG_RUNE_SOURCE_IDENTITY_MISMATCH);

	memset(&host, 0, sizeof(host));
	host_result = SG_HostLawProductionAcquire(&host);
	if (host_result.status != SG_HOST_LAW_OK)
		return SourceOwnerFail(SG_RUNE_SOURCE_HOST_UNAVAILABLE);
	host_result = SG_HostLawProductionAuthorityCurrent(&host);
	if (host_result.status != SG_HOST_LAW_OK)
		return SourceOwnerFail(SG_RUNE_SOURCE_HOST_DRIFT);

	memset(&source_owner.identity, 0, sizeof(source_owner.identity));
	memcpy(&source_owner.identity, &identity, sizeof(identity));
	memset(&source_owner.host, 0, sizeof(source_owner.host));
	memcpy(&source_owner.host, &host, sizeof(host));
	source_owner.phase = SOURCE_OWNER_PUBLISHED;
	return SG_RUNE_SOURCE_OK;
}

sg_rune_source_status_t SG_RuneSourceAuthorityAcquire(
	sg_rune_source_authority_t **authority_out)
{
	sg_rune_source_status_t status;
	sg_rune_source_authority_t *authority;
	sg_rune_source_weapon_law_t weapon;
	size_t records_bytes;

	if (!authority_out)
		return SG_RUNE_SOURCE_INVALID_ARGUMENT;
	*authority_out = NULL;
	if (source_owner.phase != SOURCE_OWNER_PUBLISHED)
		return SG_RUNE_SOURCE_INVALID_STATE;
	status = SourcePublicationCurrent(source_owner.generation,
		&source_owner.identity, &source_owner.host);
	if (status != SG_RUNE_SOURCE_OK)
		return status;
	status = SourceWeaponCapture(&weapon);
	if (status != SG_RUNE_SOURCE_OK)
		return status;

	authority = (sg_rune_source_authority_t *)malloc(sizeof(*authority));
	if (!authority)
		return SG_RUNE_SOURCE_ALLOCATION_FAILED;
	memset(authority, 0, sizeof(*authority));
	authority->entity_text = (char *)malloc(source_owner.entity_text_bytes);
	if (!authority->entity_text)
	{
		free(authority);
		return SG_RUNE_SOURCE_ALLOCATION_FAILED;
	}
	records_bytes = source_owner.record_count * sizeof(*authority->records);
	if (records_bytes != 0)
	{
		authority->records = (sg_rune_source_entity_record_t *)malloc(
			records_bytes);
		if (!authority->records)
		{
			free(authority->entity_text);
			free(authority);
			return SG_RUNE_SOURCE_ALLOCATION_FAILED;
		}
	}

	authority->magic = SG_RUNE_SOURCE_HANDLE_MAGIC;
	authority->snapshot.version = SG_RUNE_SOURCE_AUTHORITY_VERSION;
	authority->snapshot.publication_generation = source_owner.generation;
	authority->snapshot.publication_generation_complement =
		~source_owner.generation;
	memcpy(&authority->snapshot.level_identity, &source_owner.identity,
		sizeof(source_owner.identity));
	memcpy(&authority->snapshot.host_authority, &source_owner.host,
		sizeof(source_owner.host));
	memcpy(&authority->snapshot.weapon_law, &weapon, sizeof(weapon));
	authority->snapshot.entity_text_bytes =
		(uint32_t)source_owner.entity_text_bytes;
	authority->snapshot.entity_record_count =
		(uint32_t)source_owner.record_count;
	memcpy(authority->entity_text, source_owner.entity_text,
		source_owner.entity_text_bytes);
	if (records_bytes != 0)
		memcpy(authority->records, source_owner.records, records_bytes);

	status = SourceHandleCurrent(authority);
	if (status != SG_RUNE_SOURCE_OK)
	{
		SG_RuneSourceAuthorityDestroy(authority);
		return status;
	}
	*authority_out = authority;
	return SG_RUNE_SOURCE_OK;
}

sg_rune_source_status_t SG_RuneSourceAuthoritySizes(
	const sg_rune_source_authority_t *authority,
	size_t *entity_text_bytes_out, size_t *entity_record_count_out)
{
	sg_rune_source_status_t status;

	if (!entity_text_bytes_out || !entity_record_count_out)
		return SG_RUNE_SOURCE_INVALID_ARGUMENT;
	status = SourceHandleCurrent(authority);
	if (status != SG_RUNE_SOURCE_OK)
		return status;
	*entity_text_bytes_out = authority->snapshot.entity_text_bytes;
	*entity_record_count_out = authority->snapshot.entity_record_count;
	return SourceHandleCurrent(authority);
}

sg_rune_source_status_t SG_RuneSourceAuthorityCopy(
	const sg_rune_source_authority_t *authority,
	sg_rune_source_snapshot_t *snapshot_out,
	char *entity_text_out, size_t entity_text_capacity,
	sg_rune_source_entity_record_t *entity_records_out,
	size_t entity_record_capacity)
{
	sg_rune_source_status_t status;
	size_t record_count;

	if (!snapshot_out || !entity_text_out ||
		(authority && authority->snapshot.entity_record_count != 0 &&
		 !entity_records_out))
		return SG_RUNE_SOURCE_INVALID_ARGUMENT;
	status = SourceHandleCurrent(authority);
	if (status != SG_RUNE_SOURCE_OK)
		return status;
	record_count = authority->snapshot.entity_record_count;
	if (entity_text_capacity < authority->snapshot.entity_text_bytes ||
		entity_record_capacity < record_count)
		return SG_RUNE_SOURCE_BUFFER_TOO_SMALL;

	memset(snapshot_out, 0, sizeof(*snapshot_out));
	memcpy(snapshot_out, &authority->snapshot, sizeof(*snapshot_out));
	memcpy(entity_text_out, authority->entity_text,
		authority->snapshot.entity_text_bytes);
	if (record_count != 0)
		memcpy(entity_records_out, authority->records,
			record_count * sizeof(*entity_records_out));
	return SourceHandleCurrent(authority);
}

sg_rune_source_status_t SG_RuneSourceAuthoritySnapshot(
	const sg_rune_source_authority_t *authority,
	sg_rune_source_snapshot_t *snapshot_out)
{
	sg_rune_source_status_t status;

	if (snapshot_out == NULL)
		return SG_RUNE_SOURCE_INVALID_ARGUMENT;
	memset(snapshot_out, 0, sizeof(*snapshot_out));
	status = SourceHandleCurrent(authority);
	if (status != SG_RUNE_SOURCE_OK)
		return status;
	memcpy(snapshot_out, &authority->snapshot, sizeof(*snapshot_out));
	return SourceHandleCurrent(authority);
}

sg_rune_source_status_t SG_RuneSourceAuthorityCurrent(
	const sg_rune_source_authority_t *authority)
{
	return SourceHandleCurrent(authority);
}

void SG_RuneSourceAuthorityDestroy(sg_rune_source_authority_t *authority)
{
	if (!authority)
		return;
	if (authority->magic == SG_RUNE_SOURCE_HANDLE_MAGIC)
	{
		free(authority->entity_text);
		free(authority->records);
		authority->entity_text = NULL;
		authority->records = NULL;
		authority->magic = 0;
	}
	free(authority);
}

const char *SG_RuneSourceAuthorityReason(sg_rune_source_status_t status)
{
	static const char *const reasons[SG_RUNE_SOURCE_STATUS_COUNT] = {
		"source authority is current",
		"invalid source-authority argument",
		"invalid source-authority lifecycle state",
		"selected entity text is missing",
		"selected entity text exceeds the engine limit",
		"source-authority allocation failed",
		"source-authority size arithmetic overflowed",
		"effective entity records are not in source order",
		"committed level identity is unavailable",
		"selected entity text does not match the committed identity",
		"published host authority is unavailable",
		"published host authority is no longer current",
		"weapon-law inputs are unavailable",
		"sealed weapon law is no longer current",
		"source publication generation is no longer current",
		"caller-owned copy buffer is too small"
	};

	if (status < SG_RUNE_SOURCE_OK || status >= SG_RUNE_SOURCE_STATUS_COUNT)
		return "unknown source-authority status";
	return reasons[status];
}
