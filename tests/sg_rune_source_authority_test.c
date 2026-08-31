#include "g_local.h"
#undef world

#include "g_tourney.h"
#include "slipgate/sg_crc32.h"
#include "slipgate/sg_rune_source_authority_owner.h"
#include "slipgate/sg_weapon_effect_profile.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cvar_t ctfflags_value;
static cvar_t deathmatch_value;
static cvar_t fastswitch_value;

cvar_t *ctfflags = &ctfflags_value;
cvar_t *deathmatch = &deathmatch_value;
cvar_t *fastswitch = &fastswitch_value;
int matchstate = MATCH_NONE;

static sg_level_identity_t test_identity;
static int identity_available = 1;
static int host_current = 1;
static uint64_t host_epoch = UINT64_C(17);
static size_t allocation_call;
static size_t fail_allocation_call;

void *__real_malloc(size_t size);
void *__real_realloc(void *block, size_t size);

void *__wrap_malloc(size_t size)
{
	allocation_call++;
	if (fail_allocation_call != 0 &&
		allocation_call == fail_allocation_call)
		return NULL;
	return __real_malloc(size);
}

void *__wrap_realloc(void *block, size_t size)
{
	allocation_call++;
	if (fail_allocation_call != 0 &&
		allocation_call == fail_allocation_call)
		return NULL;
	return __real_realloc(block, size);
}

static void FailAllocation(size_t call)
{
	allocation_call = 0;
	fail_allocation_call = call;
}

static void AllowAllocations(void)
{
	allocation_call = 0;
	fail_allocation_call = 0;
}

sg_identity_status_t SG_LevelIdentitySnapshot(const char *expected_mapname,
	sg_level_identity_t *out)
{
	if (!expected_mapname || !out)
		return SG_IDENTITY_INVALID_ARGUMENT;
	if (!identity_available)
		return SG_IDENTITY_UNAVAILABLE;
	if (strcmp(expected_mapname, test_identity.mapname) != 0)
		return SG_IDENTITY_MAPNAME_MISMATCH;
	memset(out, 0, sizeof(*out));
	memcpy(out, &test_identity, sizeof(*out));
	return SG_IDENTITY_OK;
}

static sg_host_law_result_t HostResult(sg_host_law_status_t status)
{
	sg_host_law_result_t result;

	memset(&result, 0, sizeof(result));
	result.status = status;
	return result;
}

sg_host_law_result_t SG_HostLawProductionAcquire(
	sg_host_law_runtime_authority_t *authority_out)
{
	if (!authority_out || !host_current)
		return HostResult(SG_HOST_LAW_HOST_UNAVAILABLE);
	memset(authority_out, 0, sizeof(*authority_out));
	authority_out->version = SG_HOST_LAW_RUNTIME_AUTHORITY_VERSION;
	authority_out->epoch = host_epoch;
	authority_out->epoch_complement = ~host_epoch;
	return HostResult(SG_HOST_LAW_OK);
}

sg_host_law_result_t SG_HostLawProductionAuthorityCurrent(
	const sg_host_law_runtime_authority_t *authority)
{
	if (!authority || !host_current || authority->epoch != host_epoch ||
		authority->epoch_complement != ~authority->epoch)
		return HostResult(SG_HOST_LAW_PRODUCTION_DRIFT);
	return HostResult(SG_HOST_LAW_OK);
}

static int Check(int condition, const char *message)
{
	if (condition)
		return 1;
	fprintf(stderr, "FAIL: %s\n", message);
	return 0;
}

static void PrepareIdentity(const char *mapname, const char *entity_text)
{
	uint32_t crc = 0;

	memset(&test_identity, 0, sizeof(test_identity));
	test_identity.bsp_checksum = UINT32_C(0x10203040);
	test_identity.host_physics_id = SG_HOST_PHYSICS_EPOCH;
	test_identity.bsp_bytes = UINT64_C(123456);
	memset(test_identity.bsp_sha256, 0x5a,
		sizeof(test_identity.bsp_sha256));
	memcpy(test_identity.mapname, mapname, strlen(mapname) + 1);
	if (!SG_CRC32Buffer(entity_text, strlen(entity_text), &crc))
		abort();
	test_identity.entity_crc32 = crc;
	identity_available = 1;
	host_current = 1;
	host_epoch++;
	ctfflags_value.value = 0.0f;
	deathmatch_value.value = 1.0f;
	fastswitch_value.value = 0.0f;
	matchstate = MATCH_INPLAY;
}

static int PublishFixture(const char *text)
{
	PrepareIdentity("command", text);
	SG_RuneSourceAuthorityReset();
	if (!Check(SG_RuneSourceAuthorityBegin("command", text) ==
		SG_RUNE_SOURCE_OK, "begin fixture"))
		return 0;
	if (!Check(SG_RuneSourceAuthorityRecord(0, 0) == SG_RUNE_SOURCE_OK,
		"record world"))
		return 0;
	if (!Check(SG_RuneSourceAuthorityRecord(2, 0) == SG_RUNE_SOURCE_OK,
		"record command mutation"))
		return 0;
	if (!Check(SG_RuneSourceAuthorityRecord(3, 8) == SG_RUNE_SOURCE_OK,
		"record stripped spawnflags"))
		return 0;
	return Check(SG_RuneSourceAuthorityPublish("command") ==
		SG_RUNE_SOURCE_OK, "publish fixture");
}

static int TestExactCopies(void)
{
	static const char original[] =
		"{\n\"classname\" \"worldspawn\"\n}\n";
	static const char override_text[] =
		"{\n\"classname\" \"worldspawn\"\n\"message\" \"override\"\n}\n"
		"{\n\"classname\" \"item_quad\"\n\"spawnflags\" \"2048\"\n}\n"
		"{\n\"classname\" \"trigger_once\"\n\"model\" \"*27\"\n"
		"\"spawnflags\" \"1024\"\n}\n"
		"{\n\"classname\" \"info_player_start\"\n"
		"\"spawnflags\" \"776\"\n}\n";
	sg_rune_source_authority_t *authority = NULL;
	sg_rune_source_snapshot_t first_snapshot;
	sg_rune_source_snapshot_t second_snapshot;
	sg_rune_source_entity_record_t first_records[3];
	sg_rune_source_entity_record_t second_records[3];
	char first_text[sizeof(override_text)];
	char second_text[sizeof(override_text)];
	size_t text_bytes = 0;
	size_t record_count = 0;

	(void)original;
	if (!PublishFixture(override_text))
		return 0;
	if (!Check(SG_RuneSourceAuthorityAcquire(&authority) ==
		SG_RUNE_SOURCE_OK, "acquire exact-copy authority"))
		return 0;
	if (!Check(SG_RuneSourceAuthoritySizes(authority, &text_bytes,
		&record_count) == SG_RUNE_SOURCE_OK, "query exact sizes") ||
		!Check(text_bytes == sizeof(override_text), "exact text byte count") ||
		!Check(record_count == 3, "exact record count"))
		goto fail;
	if (!Check(SG_RuneSourceAuthorityCopy(authority, &first_snapshot,
		first_text, text_bytes - 1, first_records, record_count) ==
		SG_RUNE_SOURCE_BUFFER_TOO_SMALL, "reject short text buffer") ||
		!Check(SG_RuneSourceAuthorityCopy(authority, &first_snapshot,
		first_text, text_bytes, first_records, record_count - 1) ==
		SG_RUNE_SOURCE_BUFFER_TOO_SMALL, "reject short record buffer"))
		goto fail;
	if (!Check(SG_RuneSourceAuthorityCopy(authority, &first_snapshot,
		first_text, sizeof(first_text), first_records, 3) ==
		SG_RUNE_SOURCE_OK, "first exact copy") ||
		!Check(SG_RuneSourceAuthorityCopy(authority, &second_snapshot,
		second_text, sizeof(second_text), second_records, 3) ==
		SG_RUNE_SOURCE_OK, "second exact copy"))
		goto fail;
	if (!Check(memcmp(&first_snapshot, &second_snapshot,
		sizeof(first_snapshot)) == 0, "snapshot bytes repeat exactly") ||
		!Check(memcmp(first_text, second_text, text_bytes) == 0,
			"entity text repeats exactly") ||
		!Check(memcmp(first_records, second_records,
			sizeof(first_records)) == 0, "overlay repeats exactly") ||
		!Check(strcmp(first_text, override_text) == 0,
			"selected override text retained") ||
		!Check(strcmp(first_text, original) != 0,
			"original BSP entity text not substituted") ||
		!Check(first_records[0].source_ordinal == 0 &&
			first_records[1].source_ordinal == 2 &&
			first_records[2].source_ordinal == 3,
			"inhibited declaration is an ordinal gap") ||
		!Check(first_records[1].effective_spawnflags == 0,
			"command mutation records final spawnflags") ||
		!Check(first_records[2].effective_spawnflags == 8,
			"mode flags are absent from final spawnflags") ||
		!Check(first_snapshot.weapon_law.weapon_balance_compiled ==
			(uint8_t)SG_WEAPON_BALANCE_COMPILED,
			"compile gate is sealed") ||
		!Check(first_snapshot.weapon_law.reserved[0] == 0 &&
			first_snapshot.weapon_law.reserved[1] == 0 &&
			first_snapshot.weapon_law.reserved[2] == 0,
			"weapon reserved bytes are zero"))
		goto fail;
	SG_RuneSourceAuthorityDestroy(authority);
	SG_RuneSourceAuthorityReset();
	return 1;

fail:
	SG_RuneSourceAuthorityDestroy(authority);
	SG_RuneSourceAuthorityReset();
	return 0;
}

static int ExpectWeaponDrift(float *field, float changed,
	const char *message)
{
	static const char text[] = "{\n\"classname\" \"worldspawn\"\n}\n";
	sg_rune_source_authority_t *authority = NULL;
	float saved;
	int ok;

	if (!PublishFixture(text))
		return 0;
	if (!Check(SG_RuneSourceAuthorityAcquire(&authority) ==
		SG_RUNE_SOURCE_OK, "acquire for weapon drift"))
		return 0;
	saved = *field;
	*field = changed;
	ok = Check(SG_RuneSourceAuthorityCurrent(authority) ==
		SG_RUNE_SOURCE_WEAPON_DRIFT, message);
	*field = saved;
	SG_RuneSourceAuthorityDestroy(authority);
	SG_RuneSourceAuthorityReset();
	return ok;
}

static int TestWeaponDrift(void)
{
	static const char text[] = "{\n\"classname\" \"worldspawn\"\n}\n";
	sg_rune_source_authority_t *old_authority = NULL;
	sg_rune_source_authority_t *new_authority = NULL;
	int ok = 1;

	ok &= ExpectWeaponDrift(&deathmatch_value.value, 0.0f,
		"deathmatch drift invalidates");
	ok &= ExpectWeaponDrift(&fastswitch_value.value, 1.0f,
		"fast-switch drift invalidates");
#ifdef WEAP_BALANCE_OK
	ok &= ExpectWeaponDrift(&ctfflags_value.value, (float)CTF_WEAP_BALANCE,
		"weapon-balance drift invalidates");
#endif
	if (!PublishFixture(text))
		return 0;
	if (!Check(SG_RuneSourceAuthorityAcquire(&old_authority) ==
		SG_RUNE_SOURCE_OK, "acquire for rail drift"))
		return 0;
	matchstate = MATCH_RAILGUN_INPLAY;
	ok &= Check(SG_RuneSourceAuthorityCurrent(old_authority) ==
		SG_RUNE_SOURCE_WEAPON_DRIFT, "rail-match drift invalidates");
	ok &= Check(SG_RuneSourceAuthorityAcquire(&new_authority) ==
		SG_RUNE_SOURCE_OK, "fresh acquisition seals changed weapon law");
	ok &= Check(SG_RuneSourceAuthorityCurrent(new_authority) ==
		SG_RUNE_SOURCE_OK, "fresh weapon-law authority is current");
	SG_RuneSourceAuthorityDestroy(old_authority);
	SG_RuneSourceAuthorityDestroy(new_authority);
	SG_RuneSourceAuthorityReset();
	return ok;
}

#ifdef WEAP_BALANCE_OK
static int TestInvalidWeaponBalanceValues(void)
{
	static const char text[] = "{\n\"classname\" \"worldspawn\"\n}\n";
	static const float invalid_values[] = {
		NAN, INFINITY, -INFINITY, FLT_MAX, -FLT_MAX
	};
	sg_rune_source_authority_t *authority = NULL;
	size_t index;
	int ok = 1;

	for (index = 0; index < sizeof(invalid_values) /
		sizeof(invalid_values[0]); index++)
	{
		if (!PublishFixture(text))
			return 0;
		ctfflags_value.value = invalid_values[index];
		ok &= Check(SG_RuneSourceAuthorityAcquire(&authority) ==
			SG_RUNE_SOURCE_WEAPON_UNAVAILABLE,
			"invalid weapon-balance cvar is rejected");
		ok &= Check(authority == NULL,
			"invalid weapon-balance cvar returns no handle");
		SG_RuneSourceAuthorityReset();
	}
	return ok;
}
#endif

static int TestPublicationDrift(void)
{
	static const char text[] = "{\n\"classname\" \"worldspawn\"\n}\n";
	sg_rune_source_authority_t *authority = NULL;
	uint32_t checksum;
	int ok = 1;

	if (!PublishFixture(text) ||
		!Check(SG_RuneSourceAuthorityAcquire(&authority) ==
			SG_RUNE_SOURCE_OK, "acquire for identity drift"))
		return 0;
	checksum = test_identity.bsp_checksum;
	test_identity.bsp_checksum++;
	ok &= Check(SG_RuneSourceAuthorityCurrent(authority) ==
		SG_RUNE_SOURCE_IDENTITY_MISMATCH, "full identity drift invalidates");
	test_identity.bsp_checksum = checksum;
	test_identity.bsp_sha256[31] ^= UINT8_C(1);
	ok &= Check(SG_RuneSourceAuthorityCurrent(authority) ==
		SG_RUNE_SOURCE_IDENTITY_MISMATCH,
		"full BSP SHA-256 drift invalidates");
	test_identity.bsp_sha256[31] ^= UINT8_C(1);
	host_current = 0;
	ok &= Check(SG_RuneSourceAuthorityCurrent(authority) ==
		SG_RUNE_SOURCE_HOST_DRIFT, "host drift invalidates");
	host_current = 1;
	SG_RuneSourceAuthorityReset();
	ok &= Check(SG_RuneSourceAuthorityCurrent(authority) ==
		SG_RUNE_SOURCE_GENERATION_DRIFT, "reset generation invalidates");
	SG_RuneSourceAuthorityDestroy(authority);
	return ok;
}

static int TestAllocationFailures(void)
{
	static const char text[] = "{\n\"classname\" \"worldspawn\"\n}\n";
	sg_rune_source_authority_t *authority = NULL;
	size_t fail_call;
	int ok = 1;

	PrepareIdentity("command", text);
	SG_RuneSourceAuthorityReset();
	FailAllocation(1);
	ok &= Check(SG_RuneSourceAuthorityBegin("command", text) ==
		SG_RUNE_SOURCE_ALLOCATION_FAILED, "begin OOM is reported");
	AllowAllocations();
	ok &= Check(SG_RuneSourceAuthorityPublish("command") ==
		SG_RUNE_SOURCE_ALLOCATION_FAILED, "failed begin cannot publish");
	SG_RuneSourceAuthorityReset();
	ok &= Check(SG_RuneSourceAuthorityBegin("command", text) ==
		SG_RUNE_SOURCE_OK, "reset cleans failed begin");
	FailAllocation(1);
	ok &= Check(SG_RuneSourceAuthorityRecord(0, 0) ==
		SG_RUNE_SOURCE_ALLOCATION_FAILED, "record growth OOM is reported");
	AllowAllocations();
	SG_RuneSourceAuthorityReset();

	for (fail_call = 1; fail_call <= 3; fail_call++)
	{
		if (!PublishFixture(text))
			return 0;
		FailAllocation(fail_call);
		ok &= Check(SG_RuneSourceAuthorityAcquire(&authority) ==
			SG_RUNE_SOURCE_ALLOCATION_FAILED,
			"acquisition OOM is transactional");
		ok &= Check(authority == NULL, "OOM returns no partial handle");
		AllowAllocations();
		ok &= Check(SG_RuneSourceAuthorityAcquire(&authority) ==
			SG_RUNE_SOURCE_OK, "publication survives acquisition OOM");
		SG_RuneSourceAuthorityDestroy(authority);
		authority = NULL;
		SG_RuneSourceAuthorityReset();
	}
	return ok;
}

int main(void)
{
	int ok = 1;

	ok &= TestExactCopies();
	ok &= TestWeaponDrift();
#ifdef WEAP_BALANCE_OK
	ok &= TestInvalidWeaponBalanceValues();
#endif
	ok &= TestPublicationDrift();
	ok &= TestAllocationFailures();
	if (!ok)
		return 1;
	puts("sg_rune_source_authority_test: ok");
	return 0;
}
