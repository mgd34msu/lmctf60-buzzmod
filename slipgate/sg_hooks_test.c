/* Process-isolated tests for the SLIPGATE host boundary. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_hooks.h"

game_import_t gi;

enum { ARENA_BYTES = 4096, ARENA_RECORDS = 64 };

typedef union {
	max_align_t align;
	unsigned char bytes[ARENA_BYTES];
} arena_storage_t;

typedef struct {
	void *pointer;
	size_t size;
	qboolean live;
} arena_record_t;

typedef struct mock_arena_s {
	const char *name;
	arena_storage_t storage;
	size_t used;
	arena_record_t records[ARENA_RECORDS];
	int record_count;
	int live_count;
	int cross_frees;
	int unknown_frees;
	int double_frees;
} mock_arena_t;

static mock_arena_t level_arena = { .name = "level" };
static mock_arena_t game_arena = { .name = "game" };
static int failures;
static int engine_errors;
static int write_a_calls;
static int write_b_calls;
static int engine_tags[8];
static int engine_tag_count;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		failures++; \
	} \
} while (0)

static size_t AlignUp(size_t value, size_t alignment)
{
	return ((value + alignment - 1) / alignment) * alignment;
}

static arena_record_t *Arena_Find(mock_arena_t *arena, const void *pointer)
{
	int i;

	for (i = 0; i < arena->record_count; i++)
		if (arena->records[i].pointer == pointer)
			return &arena->records[i];
	return NULL;
}

static void *Arena_Alloc(mock_arena_t *arena, int size)
{
	size_t offset;
	arena_record_t *record;

	if (size <= 0 || arena->record_count >= ARENA_RECORDS)
		return NULL;
	offset = AlignUp(arena->used, _Alignof(max_align_t));
	if ((size_t)size > ARENA_BYTES - offset)
		return NULL;
	record = &arena->records[arena->record_count++];
	record->pointer = arena->storage.bytes + offset;
	record->size = (size_t)size;
	record->live = true;
	arena->used = offset + (size_t)size;
	arena->live_count++;
	return record->pointer;
}

static void Arena_Free(mock_arena_t *arena, mock_arena_t *other, void *pointer)
{
	arena_record_t *record;

	record = Arena_Find(arena, pointer);
	if (record)
	{
		if (!record->live)
		{
			arena->double_frees++;
			return;
		}
		record->live = false;
		arena->live_count--;
		return;
	}
	if (Arena_Find(other, pointer))
	{
		arena->cross_frees++;
		return;
	}
	arena->unknown_frees++;
}

static void Arena_BulkFree(mock_arena_t *arena)
{
	int i;

	for (i = 0; i < arena->record_count; i++)
	{
		if (!arena->records[i].live)
			continue;
		arena->records[i].live = false;
		arena->live_count--;
	}
}

static int Arena_LiveTotal(void)
{
	return level_arena.live_count + game_arena.live_count;
}

static qboolean Host_IsEmpty(const sg_host_t *host)
{
#define HOST_EMPTY_SLOT(name) if (host->name) return false;
	SG_HOST_REQUIRED_SERVICES(HOST_EMPTY_SLOT)
#undef HOST_EMPTY_SLOT
	return true;
}

/* No production consumer is linked into this process, so no cache can survive. */
static qboolean ResetBoundary(void)
{
	if (Arena_LiveTotal() != 0)
	{
		fprintf(stderr, "refusing boundary reset with %d live allocations\n",
			Arena_LiveTotal());
		failures++;
		return false;
	}
	SG_HostResetForTest();
	return Host_IsEmpty(&sg_host);
}

static void Mock_Dprint(const char *fmt, ...) { (void)fmt; }
static void Mock_Flush(void) {}
static void Mock_Cprint(edict_t *ent, int level, const char *fmt, ...)
{
	(void)ent; (void)level; (void)fmt;
}
static void Mock_Bprint(int level, const char *fmt, ...)
{
	(void)level; (void)fmt;
}
static trace_t Mock_Trace(const vec3_t start, const vec3_t mins,
	const vec3_t maxs, const vec3_t end, edict_t *passent, int mask)
{
	trace_t result;

	(void)start; (void)mins; (void)maxs; (void)end; (void)passent; (void)mask;
	memset(&result, 0, sizeof(result));
	result.fraction = 1.0f;
	return result;
}
static int Mock_PointContents(const vec3_t point) { (void)point; return 17; }
static int Mock_BoxEdicts(const vec3_t mins, const vec3_t maxs,
	edict_t **list, int maxcount, int areatype)
{
	(void)mins; (void)maxs; (void)list; (void)maxcount; (void)areatype;
	return 0;
}
static qboolean Mock_InPVS(const vec3_t p1, const vec3_t p2)
{
	(void)p1; (void)p2; return true;
}
static qboolean Mock_InPHS(const vec3_t p1, const vec3_t p2)
{
	(void)p1; (void)p2; return false;
}
static void Mock_Pmove(pmove_t *pmove) { (void)pmove; }
static void *Mock_LevelAlloc(int size) { return Arena_Alloc(&level_arena, size); }
static void Mock_LevelFree(void *block)
{
	Arena_Free(&level_arena, &game_arena, block);
}
static cvar_t *Mock_Cvar(const char *name, const char *value, int flags)
{
	static cvar_t value_object;
	(void)name; (void)value; (void)flags;
	return &value_object;
}
static char *Mock_Argv(int n) { static char value[] = "argv"; (void)n; return value; }
static void Mock_Sound(edict_t *ent, int channel, int soundindex,
	float volume, float attenuation, float timeofs)
{
	(void)ent; (void)channel; (void)soundindex; (void)volume;
	(void)attenuation; (void)timeofs;
}
static void Mock_PositionedSound(const vec3_t origin, edict_t *ent,
	int channel, int soundindex, float volume, float attenuation, float timeofs)
{
	(void)origin; (void)ent; (void)channel; (void)soundindex; (void)volume;
	(void)attenuation; (void)timeofs;
}
static int Mock_SoundIndex(const char *name) { (void)name; return 23; }
static void *Mock_GameAlloc(int size) { return Arena_Alloc(&game_arena, size); }
static void Mock_GameFree(void *block)
{
	Arena_Free(&game_arena, &level_arena, block);
}
static void Mock_LinkEntity(edict_t *ent) { (void)ent; }
static void Mock_SetModel(edict_t *ent, const char *name) { (void)ent; (void)name; }
static void Mock_CenterPrint(edict_t *ent, const char *fmt, ...)
{
	(void)ent; (void)fmt;
}
static int Mock_Argc(void) { return 2; }
static char *Mock_Args(void) { static char value[] = "args"; return value; }
static void Mock_WriteChar(int c) { (void)c; }
static void Mock_WriteByteA(int c) { (void)c; write_a_calls++; }
static void Mock_WriteByteB(int c) { (void)c; write_b_calls++; }
static void Mock_WriteShort(int c) { (void)c; }
static void Mock_WriteLong(int c) { (void)c; }
static void Mock_WriteFloat(float f) { (void)f; }
static void Mock_WriteString(const char *s) { (void)s; }
static void Mock_WritePosition(const vec3_t pos) { (void)pos; }
static void Mock_WriteDir(const vec3_t dir) { (void)dir; }
static void Mock_WriteAngle(float f) { (void)f; }
static void Mock_Unicast(edict_t *ent, qboolean reliable)
{
	(void)ent; (void)reliable;
}
static void Mock_Multicast(const vec3_t origin, multicast_t to)
{
	(void)origin; (void)to;
}

static sg_host_t CompleteHost(void)
{
	sg_host_t host = {
		.dprint = Mock_Dprint, .flush = Mock_Flush,
		.cprint = Mock_Cprint, .bprint = Mock_Bprint,
		.trace = Mock_Trace, .pointcontents = Mock_PointContents,
		.box_edicts = Mock_BoxEdicts, .in_pvs = Mock_InPVS,
		.in_phs = Mock_InPHS, .pmove = Mock_Pmove,
		.level_alloc = Mock_LevelAlloc, .level_free = Mock_LevelFree,
		.cvar = Mock_Cvar, .argv = Mock_Argv, .sound = Mock_Sound,
		.positioned_sound = Mock_PositionedSound,
		.soundindex = Mock_SoundIndex, .game_alloc = Mock_GameAlloc,
		.game_free = Mock_GameFree, .linkentity = Mock_LinkEntity,
		.setmodel = Mock_SetModel, .centerprint = Mock_CenterPrint,
		.argc = Mock_Argc, .args = Mock_Args, .write_char = Mock_WriteChar,
		.write_byte = Mock_WriteByteA, .write_short = Mock_WriteShort,
		.write_long = Mock_WriteLong, .write_float = Mock_WriteFloat,
		.write_string = Mock_WriteString, .write_position = Mock_WritePosition,
		.write_dir = Mock_WriteDir, .write_angle = Mock_WriteAngle,
		.unicast = Mock_Unicast, .multicast = Mock_Multicast
	};

	return host;
}

static void Engine_Error(char *fmt, ...)
{
	(void)fmt;
	engine_errors++;
}

static void *Engine_TagMalloc(int size, int tag)
{
	if (engine_tag_count < (int)(sizeof(engine_tags) / sizeof(engine_tags[0])))
		engine_tags[engine_tag_count++] = tag;
	if (tag == TAG_LEVEL)
		return Arena_Alloc(&level_arena, size);
	if (tag == TAG_GAME)
		return Arena_Alloc(&game_arena, size);
	return NULL;
}

static void Engine_TagFree(void *block)
{
	if (Arena_Find(&level_arena, block))
		Arena_Free(&level_arena, &game_arena, block);
	else
		Arena_Free(&game_arena, &level_arena, block);
}

static void TestIncompleteTables(void)
{
	sg_host_t complete = CompleteHost();
	sg_host_t candidate;
	int knockouts = 0;

	CHECK(!SG_HostInstall(NULL));
	CHECK(Host_IsEmpty(&sg_host));
#define TEST_MISSING_SLOT(name) do { \
	CHECK(ResetBoundary()); \
	candidate = complete; \
	candidate.name = 0; \
	CHECK(!SG_HostInstall(&candidate)); \
	CHECK(Host_IsEmpty(&sg_host)); \
	knockouts++; \
} while (0);
	SG_HOST_REQUIRED_SERVICES(TEST_MISSING_SLOT)
#undef TEST_MISSING_SLOT
	CHECK(knockouts == SG_HOST_SERVICE_COUNT);
}

static void TestInstallAndAllocators(void)
{
	sg_host_t host_a = CompleteHost();
	sg_host_t host_b = host_a;
	sg_host_t incomplete_b;
	void *level_block;
	void *game_block;
	int unknown;

	CHECK(ResetBoundary());
	CHECK(SG_HostInstall(&host_a));
	host_b.write_byte = Mock_WriteByteB;
	CHECK(!SG_HostInstall(&host_b));
	incomplete_b = host_b;
	incomplete_b.trace = NULL;
	CHECK(!SG_HostInstall(&incomplete_b));
#define CHECK_HOST_A_SLOT(name) CHECK(sg_host.name == host_a.name);
	SG_HOST_REQUIRED_SERVICES(CHECK_HOST_A_SLOT)
#undef CHECK_HOST_A_SLOT

	level_block = sg_host.level_alloc(32);
	game_block = sg_host.game_alloc(48);
	CHECK(level_block && game_block && level_block != game_block);
	CHECK(Arena_Find(&level_arena, level_block) != NULL);
	CHECK(Arena_Find(&game_arena, game_block) != NULL);
	sg_host.level_free(game_block);
	CHECK(level_arena.cross_frees == 1);
	CHECK(game_arena.live_count == 1);
	sg_host.game_free(game_block);
	CHECK(game_arena.live_count == 0);
	sg_host.level_free(level_block);
	sg_host.level_free(level_block);
	CHECK(level_arena.double_frees == 1);
	unknown = 0;
	sg_host.level_free(&unknown);
	CHECK(level_arena.unknown_frees == 1);

	level_block = sg_host.level_alloc(16);
	game_block = sg_host.game_alloc(16);
	CHECK(Arena_LiveTotal() == 2);
	Arena_BulkFree(&level_arena);
	CHECK(level_arena.live_count == 0 && game_arena.live_count == 1);
	CHECK(Arena_Find(&level_arena, level_block) != NULL);
	Arena_BulkFree(&game_arena);
	CHECK(Arena_LiveTotal() == 0);
	CHECK(Arena_Find(&game_arena, game_block) != NULL);

	CHECK(ResetBoundary());
	CHECK(SG_HostInstall(&host_b));
	sg_host.write_byte(9);
	CHECK(write_b_calls == 1);
}

static void TestFailClosedAndDynamicImports(void)
{
	void *level_block;
	void *game_block;

	CHECK(ResetBoundary());
	memset(&gi, 0, sizeof(gi));
	gi.error = Engine_Error;
	sg_host.write_byte = Mock_WriteByteA;
	SG_HooksInit();
	CHECK(engine_errors == 1);
	CHECK(!sg_host.dprint && sg_host.write_byte == Mock_WriteByteA);

	CHECK(ResetBoundary());
	memset(&gi, 0, sizeof(gi));
	gi.error = Engine_Error;
	gi.TagMalloc = Engine_TagMalloc;
	gi.TagFree = Engine_TagFree;
	gi.WriteByte = Mock_WriteByteA;
	SG_HooksInit();
	CHECK(engine_errors == 1);
	sg_host.write_byte(1);
	CHECK(write_a_calls == 1);
	gi.WriteByte = Mock_WriteByteB;
	sg_host.write_byte(2);
	CHECK(write_b_calls == 2);

	level_block = sg_host.level_alloc(24);
	game_block = sg_host.game_alloc(24);
	CHECK(level_block && game_block);
	CHECK(engine_tag_count == 2);
	CHECK(engine_tags[0] == TAG_LEVEL && engine_tags[1] == TAG_GAME);
	sg_host.level_free(level_block);
	sg_host.game_free(game_block);
	CHECK(Arena_LiveTotal() == 0);
	SG_HooksInit();
	CHECK(engine_errors == 1);
	CHECK(ResetBoundary());
}

int main(void)
{
	_Static_assert(SG_HOST_SERVICE_COUNT == 35,
		"host test must exercise exactly 35 required service slots");

	TestIncompleteTables();
	TestInstallAndAllocators();
	TestFailClosedAndDynamicImports();
	CHECK(Arena_LiveTotal() == 0);
	if (failures)
	{
		fprintf(stderr, "sg_hooks_test: %d failure(s)\n", failures);
		return EXIT_FAILURE;
	}
	printf("sg_hooks_test: PASS (35 slots, allocators, reset, dynamic gi)\n");
	return EXIT_SUCCESS;
}
