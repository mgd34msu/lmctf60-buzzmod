/* Process-isolated mock-host tests for the per-level identity authority. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_crc32.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_identity.h"
#include "slipgate/sg_bot_ping.h"
#include "slipgate/sg_escort_dose.h"
#include "slipgate/sg_ribbon_random.h"

sg_host_t sg_host;

static int failures;
static int cvar_calls;
static int bad_cvar_call;
static int missing_map_cvar;
static int missing_physics_cvar;
static cvar_t map_cvar;
static cvar_t physics_cvar;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static cvar_t *Mock_Cvar(const char *name, const char *value, int flags)
{
	cvar_calls++;
	if (!value || strcmp(value, "") != 0 || flags != 0)
		bad_cvar_call++;
	if (strcmp(name, "sv_rune_mapchecksum") == 0)
		return missing_map_cvar ? NULL : &map_cvar;
	if (strcmp(name, "sv_rune_physics_id") == 0)
		return missing_physics_cvar ? NULL : &physics_cvar;
	bad_cvar_call++;
	return NULL;
}

static void Mock_Reset(const char *mapchecksum, int map_flags,
	const char *physics, int physics_flags)
{
	memset(&sg_host, 0, sizeof(sg_host));
	memset(&map_cvar, 0, sizeof(map_cvar));
	memset(&physics_cvar, 0, sizeof(physics_cvar));
	cvar_calls = 0;
	bad_cvar_call = 0;
	missing_map_cvar = 0;
	missing_physics_cvar = 0;
	map_cvar.name = "sv_rune_mapchecksum";
	map_cvar.string = (char *)mapchecksum;
	map_cvar.flags = map_flags;
	physics_cvar.name = "sv_rune_physics_id";
	physics_cvar.string = (char *)physics;
	physics_cvar.flags = physics_flags;
	sg_host.cvar = Mock_Cvar;
	SG_LevelIdentityReset();
}

static void Mock_Valid(const char *mapchecksum)
{
	Mock_Reset(mapchecksum, CVAR_NOSET, "1", CVAR_NOSET);
}

static void CommitIdentity(const char *mapname, const char *entities)
{
	CHECK(SG_LevelIdentityBegin(mapname) == SG_IDENTITY_OK);
	CHECK(SG_LevelIdentityCaptureEntities(mapname, entities) == SG_IDENTITY_OK);
	CHECK(SG_LevelIdentityCommit(mapname) == SG_IDENTITY_OK);
}

static void TestCRC32(void)
{
	static const char vector[] = "123456789";
	uint32_t state;
	uint32_t crc;

	CHECK(SG_CRC32Buffer(NULL, 0, &crc));
	CHECK(crc == UINT32_C(0));
	CHECK(SG_CRC32Buffer(vector, sizeof(vector) - 1, &crc));
	CHECK(crc == UINT32_C(0xcbf43926));
	state = SG_CRC32Init();
	CHECK(SG_CRC32Update(&state, vector, 4));
	CHECK(SG_CRC32Update(&state, NULL, 0));
	CHECK(SG_CRC32Update(&state, vector + 4, sizeof(vector) - 1 - 4));
	CHECK(SG_CRC32Final(state) == UINT32_C(0xcbf43926));
	state = SG_CRC32Init();
	CHECK(!SG_CRC32Update(&state, NULL, 1));
	CHECK(state == 0);
	CHECK(!SG_CRC32Update(NULL, vector, sizeof(vector) - 1));
	crc = UINT32_MAX;
	CHECK(!SG_CRC32Buffer(NULL, 1, &crc));
	CHECK(crc == 0);
	CHECK(!SG_CRC32Buffer(vector, sizeof(vector) - 1, NULL));
}

static void TestEscortDose(void)
{
	int expected_random;
	int first;

	srand(7331);
	expected_random = rand();
	srand(7331);
	first = SG_EscortDoseEnabled(0, 7, UINT32_C(42), 50);
	CHECK(first == SG_EscortDoseEnabled(0, 7, UINT32_C(42), 50));
	CHECK(first == 1);
	CHECK(rand() == expected_random);
	CHECK(!SG_EscortDoseEnabled(0, 7, UINT32_C(42), 0));
	CHECK(!SG_EscortDoseEnabled(0, 7, UINT32_C(42), -20));
	CHECK(SG_EscortDoseEnabled(0, 7, UINT32_C(42), 100));
	CHECK(SG_EscortDoseEnabled(0, 7, UINT32_C(42), 150));
	CHECK(!SG_EscortDoseEnabled(0, 7, UINT32_C(42), 16));
	CHECK(SG_EscortDoseEnabled(0, 7, UINT32_C(42), 17));
	CHECK(!SG_EscortDoseEnabled(0, 7, UINT32_C(43), 50));
}

static void TestRibbonRandomness(void)
{
	uint32_t state;
	int expected_random;
	float offset;
	float interval;

	srand(8123);
	expected_random = rand();
	srand(8123);
	state = SG_RibbonRandomInitial(UINT64_C(0x123456789abcdef0), 7);
	CHECK(state != 0);
	CHECK(state == SG_RibbonRandomInitial(
	      UINT64_C(0x123456789abcdef0), 7));
	CHECK(rand() == expected_random);
	offset = SG_RibbonRandomOffset(state, 48.0f);
	CHECK(offset >= -48.0f && offset <= 48.0f);
	interval = SG_RibbonRandomInterval(state);
	CHECK(interval >= 1.0f && interval < 2.0f);
	CHECK(SG_RibbonRandomNext(state) != state);
}

static void TestValidAndFloatPrecision(void)
{
	static const char entities[] = "{\n\"classname\" \"worldspawn\"\n}\n";
	sg_level_identity_t identity;
	uint32_t expected_crc = 0;

	CHECK(SG_CRC32Buffer(entities, sizeof(entities) - 1, &expected_crc));
	Mock_Valid("4294967295");
	/* This member cannot carry the exact uint32_t value on ordinary hosts. */
	map_cvar.value = 4294967296.0f;
	CHECK(SG_LevelIdentityBegin("Map_1-TEST") == SG_IDENTITY_OK);
	CHECK(cvar_calls == 2);
	CHECK(bad_cvar_call == 0);
	memset(&identity, 0xa5, sizeof(identity));
	CHECK(SG_LevelIdentitySnapshot("Map_1-TEST", &identity) ==
	      SG_IDENTITY_NOT_COMMITTED);
	CHECK(identity.mapname[0] == '\0');
	CHECK(SG_LevelIdentityCaptureEntities("Map_1-TEST", entities) ==
	      SG_IDENTITY_OK);
	CHECK(SG_LevelIdentitySnapshot("Map_1-TEST", &identity) ==
	      SG_IDENTITY_NOT_COMMITTED);
	CHECK(SG_LevelIdentityCommit("Map_1-TEST") == SG_IDENTITY_OK);
	CHECK(SG_LevelIdentitySnapshot("Map_1-TEST", &identity) == SG_IDENTITY_OK);
	CHECK(identity.bsp_checksum == UINT32_MAX);
	CHECK(identity.entity_crc32 == expected_crc);
	CHECK(identity.host_physics_id == SG_HOST_PHYSICS_EPOCH);
	CHECK(strcmp(identity.mapname, "Map_1-TEST") == 0);
	CHECK(SG_LevelIdentityMatch("Map_1-TEST", UINT32_MAX, expected_crc, 1) ==
	      SG_IDENTITY_OK);
	CHECK(SG_LevelIdentityMatch("Map_1-TEST", 1, expected_crc, 1) ==
	      SG_IDENTITY_BSP_CHECKSUM_MISMATCH);
	CHECK(SG_LevelIdentityMatch("Map_1-TEST", UINT32_MAX,
	      expected_crc ^ UINT32_C(1), 1) == SG_IDENTITY_ENTITY_CRC_MISMATCH);
	CHECK(SG_LevelIdentityMatch("Map_1-TEST", UINT32_MAX, expected_crc, 2) ==
	      SG_IDENTITY_PHYSICS_ID_MISMATCH);
	CHECK(SG_LevelIdentitySnapshot("map_1-TEST", &identity) ==
	      SG_IDENTITY_MAPNAME_MISMATCH);
	CHECK(SG_LevelIdentityMatch("map_1-TEST", UINT32_MAX, expected_crc, 1) ==
	      SG_IDENTITY_MAPNAME_MISMATCH);

	/* Capture and Commit cannot rewrite committed authority. */
	CHECK(SG_LevelIdentityCaptureEntities("Map_1-TEST", "different") ==
	      SG_IDENTITY_ALREADY_COMMITTED);
	CHECK(SG_LevelIdentityCommit("Map_1-TEST") == SG_IDENTITY_ALREADY_COMMITTED);
	CHECK(SG_LevelIdentitySnapshot("Map_1-TEST", &identity) == SG_IDENTITY_OK);
	CHECK(identity.entity_crc32 == expected_crc);
	/* Begin is the transition authority and fail-closes the previous map even
	 * when the outer lifecycle forgot its required Reset. */
	CHECK(SG_LevelIdentityBegin("bad.map") == SG_IDENTITY_INVALID_MAPNAME);
	memset(&identity, 0xa5, sizeof(identity));
	CHECK(SG_LevelIdentitySnapshot("Map_1-TEST", &identity) ==
	      SG_IDENTITY_INVALID_MAPNAME);
	CHECK(identity.mapname[0] == '\0');
}

static void TestCanonicalUint32(void)
{
	static const char *const invalid[] = {
		"", "00", "01", "+1", "-1", " 1", "1 ", "1.0", "1e0",
		"4294967296", "0000000000", "12345678901", "abc"
	};
	size_t i;
	sg_level_identity_t identity;

	for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
	{
		Mock_Reset(invalid[i], CVAR_NOSET, "1", CVAR_NOSET);
		CHECK(SG_LevelIdentityBegin("map1") ==
		      SG_IDENTITY_MAPCHECKSUM_NONCANONICAL);
		CHECK(cvar_calls == 2);
		CHECK(SG_LevelIdentitySnapshot("map1", &identity) ==
		      SG_IDENTITY_MAPCHECKSUM_NONCANONICAL);
	}

	Mock_Valid("0");
	CommitIdentity("map1", "");
	CHECK(SG_LevelIdentitySnapshot("map1", &identity) == SG_IDENTITY_OK);
	CHECK(identity.bsp_checksum == 0);
	CHECK(identity.entity_crc32 == 0);

	for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
	{
		Mock_Reset("1", CVAR_NOSET, invalid[i], CVAR_NOSET);
		CHECK(SG_LevelIdentityBegin("map1") ==
		      SG_IDENTITY_PHYSICS_ID_NONCANONICAL);
	}
	for (i = 0; i < 3; i++)
	{
		static const char *const unsupported[] = { "0", "2", "4294967295" };

		Mock_Reset("1", CVAR_NOSET, unsupported[i], CVAR_NOSET);
		CHECK(SG_LevelIdentityBegin("map1") ==
		      SG_IDENTITY_PHYSICS_ID_UNSUPPORTED);
	}
}

static void TestProtectionAndMissingValues(void)
{
	sg_level_identity_t identity;

	Mock_Reset("123", 0, "1", CVAR_NOSET);
	CHECK(SG_LevelIdentityBegin("map1") == SG_IDENTITY_MAPCHECKSUM_UNPROTECTED);
	CHECK(SG_LevelIdentitySnapshot("map1", &identity) ==
	      SG_IDENTITY_MAPCHECKSUM_UNPROTECTED);

	Mock_Reset("123", CVAR_NOSET, "1", 0);
	CHECK(SG_LevelIdentityBegin("map1") == SG_IDENTITY_PHYSICS_ID_UNPROTECTED);

	Mock_Reset("123", CVAR_NOSET | CVAR_ARCHIVE,
	           "1", CVAR_NOSET | CVAR_SERVERINFO);
	CommitIdentity("map1", "entities");
	CHECK(SG_LevelIdentitySnapshot("map1", &identity) == SG_IDENTITY_OK);

	Mock_Valid("123");
	missing_map_cvar = 1;
	CHECK(SG_LevelIdentityBegin("map1") == SG_IDENTITY_MAPCHECKSUM_MISSING);
	CHECK(cvar_calls == 2);

	Mock_Valid("123");
	missing_physics_cvar = 1;
	CHECK(SG_LevelIdentityBegin("map1") == SG_IDENTITY_PHYSICS_ID_MISSING);

	Mock_Valid("123");
	map_cvar.string = NULL;
	CHECK(SG_LevelIdentityBegin("map1") == SG_IDENTITY_MAPCHECKSUM_MISSING);

	Mock_Valid("123");
	physics_cvar.string = NULL;
	CHECK(SG_LevelIdentityBegin("map1") == SG_IDENTITY_PHYSICS_ID_MISSING);

	Mock_Valid("123");
	sg_host.cvar = NULL;
	CHECK(SG_LevelIdentityBegin("map1") ==
	      SG_IDENTITY_HOST_CVAR_UNAVAILABLE);
}

static void TestMapGrammar(void)
{
	static const char *const invalid[] = {
		"", "-map", ".map", "map.name", "map/name", "map name", "map+name"
	};
	char maximum[SG_LEVEL_IDENTITY_MAPNAME_BYTES];
	char too_long[SG_LEVEL_IDENTITY_MAPNAME_BYTES + 1];
	sg_level_identity_t identity;
	size_t i;

	memset(maximum, 'a', sizeof(maximum));
	maximum[sizeof(maximum) - 1] = '\0';
	memset(too_long, 'a', sizeof(too_long));
	too_long[sizeof(too_long) - 1] = '\0';

	for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
	{
		Mock_Valid("1");
		CHECK(SG_LevelIdentityBegin(invalid[i]) == SG_IDENTITY_INVALID_MAPNAME);
		CHECK(cvar_calls == 0);
	}
	Mock_Valid("1");
	CHECK(SG_LevelIdentityBegin(too_long) == SG_IDENTITY_INVALID_MAPNAME);
	CHECK(cvar_calls == 0);

	Mock_Valid("1");
	CommitIdentity(maximum, "entities");
	CHECK(SG_LevelIdentitySnapshot(maximum, &identity) == SG_IDENTITY_OK);

	Mock_Valid("1");
	CHECK(SG_LevelIdentityBegin("Map") == SG_IDENTITY_OK);
	CHECK(SG_LevelIdentityCaptureEntities("map", "entities") ==
	      SG_IDENTITY_MAPNAME_MISMATCH);
	CHECK(SG_LevelIdentitySnapshot("Map", &identity) ==
	      SG_IDENTITY_MAPNAME_MISMATCH);

	Mock_Valid("1");
	CHECK(SG_LevelIdentityBegin("Map") == SG_IDENTITY_OK);
	CHECK(SG_LevelIdentityCaptureEntities("Map", "entities") == SG_IDENTITY_OK);
	CHECK(SG_LevelIdentityCommit("map") == SG_IDENTITY_MAPNAME_MISMATCH);

	Mock_Valid("1");
	CommitIdentity("Map", "entities");
	CHECK(SG_LevelIdentitySnapshot("bad.name", &identity) ==
	      SG_IDENTITY_INVALID_MAPNAME);
	CHECK(SG_LevelIdentityMatch("bad.name", 1, 1, 1) ==
	      SG_IDENTITY_INVALID_MAPNAME);
}

static void TestLifecycleAndMapSwitch(void)
{
	sg_level_identity_t identity;
	uint32_t a_crc;
	unsigned char *unterminated;

	Mock_Valid("11");
	memset(&identity, 0xa5, sizeof(identity));
	CHECK(SG_LevelIdentitySnapshot("mapA", &identity) == SG_IDENTITY_UNAVAILABLE);
	CHECK(identity.mapname[0] == '\0');
	CHECK(SG_LevelIdentityCaptureEntities("mapA", "entities") ==
	      SG_IDENTITY_INVALID_TRANSITION);

	Mock_Valid("11");
	CHECK(SG_LevelIdentityBegin("mapA") == SG_IDENTITY_OK);
	/* A second Begin starts a fresh, still-uncommitted capture. */
	CHECK(SG_LevelIdentityBegin("mapA") == SG_IDENTITY_OK);
	CHECK(SG_LevelIdentityCaptureEntities("mapA", "entities-A") ==
	      SG_IDENTITY_OK);
	CHECK(SG_LevelIdentitySnapshot("mapA", &identity) ==
	      SG_IDENTITY_NOT_COMMITTED);
	/* Simulated spawn failure: no Commit, then the next map invalidates. */
	SG_LevelIdentityReset();
	CHECK(SG_LevelIdentitySnapshot("mapA", &identity) == SG_IDENTITY_UNAVAILABLE);

	Mock_Valid("11");
	CommitIdentity("mapA", "entities-A");
	CHECK(SG_LevelIdentitySnapshot("mapA", &identity) == SG_IDENTITY_OK);
	a_crc = identity.entity_crc32;
	SG_LevelIdentityReset();
	map_cvar.flags = 0;
	CHECK(SG_LevelIdentityBegin("mapB") == SG_IDENTITY_MAPCHECKSUM_UNPROTECTED);
	memset(&identity, 0xa5, sizeof(identity));
	CHECK(SG_LevelIdentitySnapshot("mapA", &identity) ==
	      SG_IDENTITY_MAPCHECKSUM_UNPROTECTED);
	CHECK(identity.bsp_checksum == 0);

	Mock_Valid("11");
	CommitIdentity("mapA", "entities-B");
	CHECK(SG_LevelIdentitySnapshot("mapA", &identity) == SG_IDENTITY_OK);
	CHECK(identity.entity_crc32 != a_crc);

	Mock_Valid("11");
	CHECK(SG_LevelIdentityBegin("mapA") == SG_IDENTITY_OK);
	CHECK(SG_LevelIdentityCaptureEntities("mapA", NULL) ==
	      SG_IDENTITY_ENTITY_TEXT_MISSING);

	unterminated = malloc(SG_LEVEL_ENTITY_TEXT_LIMIT);
	CHECK(unterminated != NULL);
	if (unterminated)
	{
		memset(unterminated, 'x', SG_LEVEL_ENTITY_TEXT_LIMIT);
		Mock_Valid("11");
		CHECK(SG_LevelIdentityBegin("mapA") == SG_IDENTITY_OK);
		CHECK(SG_LevelIdentityCaptureEntities("mapA",
		      (const char *)unterminated) == SG_IDENTITY_ENTITY_TEXT_UNTERMINATED);
		free(unterminated);
	}

	Mock_Valid("11");
	CHECK(SG_LevelIdentityBegin("mapA") == SG_IDENTITY_OK);
	CHECK(SG_LevelIdentityCommit("mapA") == SG_IDENTITY_NOT_COMMITTED);
}

static void TestReasons(void)
{
	int status;

	for (status = SG_IDENTITY_OK; status < SG_IDENTITY_STATUS_COUNT; status++)
	{
		const char *reason = SG_LevelIdentityReason((sg_identity_status_t)status);

		CHECK(reason != NULL);
		CHECK(reason[0] != '\0');
	}
	CHECK(strcmp(SG_LevelIdentityReason((sg_identity_status_t)-1),
	             "unknown level-identity status") == 0);
	CHECK(strcmp(SG_LevelIdentityReason(SG_IDENTITY_STATUS_COUNT),
	             "unknown level-identity status") == 0);
}

static void TestBotPingDoesNotOwnGameplayRandomness(void)
{
	int frame;
	int expected_random;
	int base;

	srand(5119);
	expected_random = rand();
	srand(5119);
	base = SG_BotPingBase(UINT64_C(0x123456789abcdef0), 7);
	CHECK(base >= 5 && base <= 15);
	CHECK(base == SG_BotPingBase(UINT64_C(0x123456789abcdef0), 7));
	CHECK(rand() == expected_random);

	for (frame = 0; frame < 1000; frame++)
	{
		int ping = SG_BotPingValue(10, UINT64_C(0x123456789abcdef0),
		    frame);

		CHECK(ping >= 9 && ping <= 11);
		CHECK(ping == SG_BotPingValue(10,
		    UINT64_C(0x123456789abcdef0), frame));
	}
	CHECK(SG_BotPingValue(5, 1, 0) >= 5);
	CHECK(SG_BotPingValue(15, 1, 0) <= 15);
}

int main(void)
{
	TestCRC32();
	TestValidAndFloatPrecision();
	TestCanonicalUint32();
	TestProtectionAndMissingValues();
	TestMapGrammar();
	TestLifecycleAndMapSwitch();
	TestReasons();
	TestBotPingDoesNotOwnGameplayRandomness();
	TestEscortDose();
	TestRibbonRandomness();

	if (failures)
	{
		fprintf(stderr, "sg_identity_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_identity_test: ok");
	return 0;
}
