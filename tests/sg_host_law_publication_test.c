#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../g_local.h"
#include "../g_ctffunc.h"
#undef world
#ifndef q_exported
#define q_exported
#endif
#include "../slipgate/sg_host_law_publication.h"
#include "../slipgate/sg_host_law_construction_offline.h"
#include "../slipgate/sg_host_law_publication_private.h"
#include "../slipgate/sg_host_engine_parity.h"
#include "../slipgate/sg_rune.h"
#include "../slipgate/sg_host_engine_runtime_private.h"
#include "../slipgate/sg_host_law_owner.h"
#include "../slipgate/sg_host_law_owner_internal.h"
#include "../slipgate/sg_hooks.h"
#include "../slipgate/sg_identity.h"
#include "../slipgate/sg_local.h"
#include "../slipgate/sg_bot.h"

game_import_t gi;
game_export_t globals;
level_locals_t level;
sg_host_t sg_host;
sg_bot_t sg_bots[SG_MAXBOTS];
cvar_t *sv_gravity;
cvar_t *sv_maxvelocity;
cvar_t *want_funky_gravity;
cvar_t *ctfflags;

static cvar_t gravity_cvar;
static cvar_t maxvelocity_cvar;
static cvar_t funky_gravity_cvar;
static cvar_t airaccelerate_cvar;
static cvar_t ctf_flags_cvar;
static float test_live_door_speed = 200.0f;
static float test_live_rotating_door_speed = 100.0f;
static sg_identity_status_t test_identity_snapshot_status =
	SG_IDENTITY_UNAVAILABLE;
static sg_level_identity_t test_level_identity;
static rune_t test_runtime_rune;
static sg_bsp_world_t test_world;
static uint8_t test_runtime_source[] = {
	0x52U, 0x55U, 0x4eU, 0x45U, 0x2dU, 0x68U, 0x6fU, 0x73U,
	0x74U, 0x2dU, 0x77U, 0x6fU, 0x72U, 0x6cU, 0x64U, 0x00U
};
static int failures;

#define CONSTRUCTION_BSP_HEADER_BYTES (8U + SG_BSP_LUMP_COUNT * 8U)
#define CONSTRUCTION_BSP_CAPACITY UINT32_C(2048)

typedef struct construction_bsp_fixture_s
{
	uint8_t bytes[CONSTRUCTION_BSP_CAPACITY];
	uint32_t size;
	uint32_t offsets[SG_BSP_LUMP_COUNT];
	uint32_t lengths[SG_BSP_LUMP_COUNT];
} construction_bsp_fixture_t;

rune_t *SG_Rune(void);

/* The publication unit test owns a synthetic authority.  The production
 * owner’s map-lifecycle bridge is linked in the game build and is deliberately
 * unavailable in this isolated fixture. */
sg_identity_status_t SG_LevelIdentitySnapshot(const char *expected_mapname,
	sg_level_identity_t *out)
{
	(void)expected_mapname;
	if (out)
	{
		memset(out, 0, sizeof(*out));
		if (test_identity_snapshot_status == SG_IDENTITY_OK)
			*out = test_level_identity;
	}
	return test_identity_snapshot_status;
}

/* The focused test links the host-law owner without the full RUNE loader.  It
 * still exposes the same owner functions used by production, so no test can
 * issue a caller-shaped acceptance tuple. */
rune_t *SG_Rune(void)
{
	return &test_runtime_rune;
}

qboolean ctf_validateplayer(edict_t *entity, int teamnum_wanted)
{
	if (!entity || !entity->client || !entity->inuse ||
		!entity->client->pers.connected || !entity->classname ||
		strcmp(entity->classname, "player") != 0)
		return false;
	if (teamnum_wanted == CTF_TEAM_IGNORETEAM)
		return true;
	if (teamnum_wanted == CTF_TEAM_ANYTEAM)
		return entity->client->ctf.teamnum > CTF_TEAM_UNDEFINED &&
			entity->client->ctf.teamnum < CTF_TEAM_LIMIT;
	return teamnum_wanted == entity->client->ctf.teamnum;
}

qboolean SG_RunePublishedShapeValid(const rune_t *rune)
{
	return rune == &test_runtime_rune;
}

const rune_artifact_t *SG_RuneArtifact(const rune_t *rune)
{
	return SG_RunePublishedShapeValid(rune) ? &rune->artifact : NULL;
}

int SG_RuneArtifactsEqual(const rune_artifact_t *left,
	const rune_artifact_t *right)
{
	return left && right && memcmp(left, right, sizeof(*left)) == 0;
}

qboolean SG_RunePhysicsCompatible(const rune_t *rune)
{
	const rune_identity_t *identity;

	if (!SG_RunePublishedShapeValid(rune) ||
		test_identity_snapshot_status != SG_IDENTITY_OK)
		return false;
	identity = &rune->artifact.identity;
	return identity->bsp_checksum == test_level_identity.bsp_checksum &&
		identity->entity_crc32 == test_level_identity.entity_crc32 &&
		identity->host_physics_id == test_level_identity.host_physics_id &&
		identity->gravity == gravity_cvar.value &&
		identity->airaccelerate == airaccelerate_cvar.value &&
		identity->maxvelocity == maxvelocity_cvar.value &&
		identity->pmove_substep_ms == SG_HOST_ENGINE_PMOVE_SUBSTEP_MS &&
		identity->server_frame_ms == SG_HOST_ENGINE_FRAME_MS &&
		memcmp(identity->map_name, test_level_identity.mapname,
			sizeof(identity->map_name)) == 0;
}

/* This is a real BSP-shaped world. Pmove reaches it through the production
 * host collision adapter; no test callback supplies movement authority. */
static sg_bsp_plane_t planes[1];
static sg_bsp_node_t nodes[1];
static sg_bsp_leaf_t leaves[2];
static sg_bsp_model_t models[1];
static sg_bsp_plane_t hook_planes[2];
static sg_bsp_node_t hook_nodes[1];
static sg_bsp_leaf_t hook_leaves[2];
static uint32_t hook_leaf_brushes[1];
static sg_bsp_brush_t hook_brushes[1];
static sg_bsp_brush_side_t hook_brush_sides[1];
static sg_bsp_model_t hook_models[1];
static sg_bsp_world_t hook_world;
static edict_t runtime_edicts[4];
static gclient_t runtime_clients[4];
static csurface_t runtime_surface;
static edict_t *runtime_last_passent;
static int runtime_trace_calls;
static int runtime_contents_calls;
static int runtime_pmove_calls;
static byte runtime_last_pmove_msec;
static int runtime_touch_on_first_pmove;
static edict_t *runtime_return_entity;

static trace_t RuntimeTrace(vec3_t start, vec3_t mins, vec3_t maxs,
	vec3_t end, edict_t *passent, int mask)
{
	trace_t trace;

	(void)mins;
	(void)maxs;
	(void)mask;
	memset(&trace, 0, sizeof(trace));
	trace.fraction = 1.0f;
	VectorCopy(end, trace.endpos);
	trace.plane.normal[2] = 1.0f;
	trace.surface = &runtime_surface;
	runtime_last_passent = passent;
	runtime_trace_calls++;
	if (runtime_return_entity)
	{
		trace.fraction = 0.5f;
		trace.endpos[0] = (start[0] + end[0]) * 0.5f;
		trace.ent = runtime_return_entity;
		trace.contents = CONTENTS_SOLID;
		trace.plane.normal[0] = -1.0f;
		trace.plane.normal[2] = 0.0f;
	}
	return trace;
}

static int RuntimePointContents(vec3_t point)
{
	(void)point;
	runtime_contents_calls++;
	return 0;
}

static void RuntimePmove(pmove_t *pmove)
{
	vec3_t start = { 0.0f, 0.0f, 0.0f };
	vec3_t mins = { -16.0f, -16.0f, -24.0f };
	vec3_t maxs = { 16.0f, 16.0f, 32.0f };
	vec3_t end = { 1.0f, 0.0f, 0.0f };
	trace_t trace;

	trace = pmove->trace(start, mins, maxs, end);
	(void)pmove->pointcontents(start);
	runtime_last_pmove_msec = pmove->cmd.msec;
	if (runtime_touch_on_first_pmove && runtime_pmove_calls == 0)
	{
		pmove->numtouch = 1;
		pmove->touchents[0] = &runtime_edicts[2];
	}
	runtime_pmove_calls++;
	pmove->groundentity = trace.ent;
	VectorCopy(mins, pmove->mins);
	VectorCopy(maxs, pmove->maxs);
	pmove->viewheight = 22.0f;
	pmove->watertype = 0;
	pmove->waterlevel = 0;
}

extern void Pmove(pmove_t *pmove);
int CTF_HookPullVelocity(const vec3_t start, const vec3_t bite,
	vec3_t velocity);
void CTF_HookMuzzle(const vec3_t origin, float viewheight, int hand,
	const vec3_t forward, const vec3_t right, vec3_t start);
void Com_DPrintf(const char *format, ...);
void Com_Printf(char *format, ...);

void Com_DPrintf(const char *format, ...)
{
	(void)format;
}

void Com_Printf(char *format, ...)
{
	(void)format;
}

void CTF_HookMuzzle(const vec3_t origin, float viewheight, int hand,
	const vec3_t forward, const vec3_t right, vec3_t start)
{
	vec3_t offset;

	VectorSet(offset, 8.0f, 8.0f, viewheight - 8.0f);
	if (hand == LEFT_HANDED)
		offset[1] = -offset[1];
	else if (hand == CENTER_HANDED)
		offset[1] = 0.0f;
	start[0] = origin[0] + forward[0] * offset[0] + right[0] * offset[1];
	start[1] = origin[1] + forward[1] * offset[0] + right[1] * offset[1];
	start[2] = origin[2] + forward[2] * offset[0] + right[2] * offset[1] +
		offset[2];
}

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static void ConstructionWriteU16(uint8_t *bytes, uint16_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
}

static void ConstructionWriteI16(uint8_t *bytes, int16_t value)
{
	ConstructionWriteU16(bytes, (uint16_t)value);
}

static void ConstructionWriteU32(uint8_t *bytes, uint32_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);
}

static void ConstructionWriteI32(uint8_t *bytes, int32_t value)
{
	ConstructionWriteU32(bytes, (uint32_t)value);
}

static void ConstructionWriteFloat(uint8_t *bytes, float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	ConstructionWriteU32(bytes, bits);
}

static uint8_t *ConstructionAddLump(construction_bsp_fixture_t *fixture,
	sg_bsp_lump_t lump, uint32_t length)
{
	uint8_t *record;

	CHECK(fixture->size <= CONSTRUCTION_BSP_CAPACITY - length);
	record = fixture->bytes + fixture->size;
	fixture->offsets[lump] = fixture->size;
	fixture->lengths[lump] = length;
	fixture->size += length;
	memset(record, 0, length);
	return record;
}

static void ConstructionFinishHeader(construction_bsp_fixture_t *fixture)
{
	uint32_t lump;

	memcpy(fixture->bytes, "IBSP", 4U);
	ConstructionWriteU32(fixture->bytes + 4U, SG_BSP_VERSION);
	for (lump = 0U; lump < SG_BSP_LUMP_COUNT; lump++)
	{
		ConstructionWriteU32(fixture->bytes + 8U + lump * 8U,
			fixture->offsets[lump]);
		ConstructionWriteU32(fixture->bytes + 12U + lump * 8U,
			fixture->lengths[lump]);
	}
}

static construction_bsp_fixture_t ConstructionValidBsp(void)
{
	construction_bsp_fixture_t fixture;
	uint8_t *record;
	uint32_t lump;

	memset(&fixture, 0, sizeof(fixture));
	fixture.size = CONSTRUCTION_BSP_HEADER_BYTES;
	for (lump = 0U; lump < SG_BSP_LUMP_COUNT; lump++)
		fixture.offsets[lump] = CONSTRUCTION_BSP_HEADER_BYTES;

	record = ConstructionAddLump(&fixture, SG_BSP_LUMP_ENTITIES, 4U);
	memcpy(record, "{}\n", 4U);
	record = ConstructionAddLump(&fixture, SG_BSP_LUMP_PLANES, 20U);
	ConstructionWriteFloat(record + 8U, 1.0f);
	ConstructionWriteI32(record + 16U, 2);
	record = ConstructionAddLump(&fixture, SG_BSP_LUMP_VERTICES, 36U);
	ConstructionWriteFloat(record + 0U, -16.0f);
	ConstructionWriteFloat(record + 4U, -16.0f);
	ConstructionWriteFloat(record + 12U, 16.0f);
	ConstructionWriteFloat(record + 16U, -16.0f);
	ConstructionWriteFloat(record + 24U, 0.0f);
	ConstructionWriteFloat(record + 28U, 16.0f);
	record = ConstructionAddLump(&fixture, SG_BSP_LUMP_VISIBILITY, 13U);
	ConstructionWriteU32(record + 0U, 1U);
	ConstructionWriteU32(record + 4U, 12U);
	ConstructionWriteU32(record + 8U, 12U);
	record[12] = 1U;
	record = ConstructionAddLump(&fixture, SG_BSP_LUMP_NODES, 28U);
	ConstructionWriteU32(record + 0U, 0U);
	ConstructionWriteI32(record + 4U, -1);
	ConstructionWriteI32(record + 8U, -2);
	ConstructionWriteI16(record + 12U, -16);
	ConstructionWriteI16(record + 14U, -16);
	ConstructionWriteI16(record + 16U, -16);
	ConstructionWriteI16(record + 18U, 16);
	ConstructionWriteI16(record + 20U, 16);
	ConstructionWriteI16(record + 22U, 16);
	ConstructionWriteU16(record + 24U, 0U);
	ConstructionWriteU16(record + 26U, 1U);
	record = ConstructionAddLump(&fixture, SG_BSP_LUMP_TEXINFO, 76U);
	ConstructionWriteFloat(record + 0U, 1.0f);
	ConstructionWriteFloat(record + 20U, 1.0f);
	ConstructionWriteI32(record + 32U, 4);
	ConstructionWriteI32(record + 36U, 7);
	memcpy(record + 40U, "stone", 5U);
	ConstructionWriteI32(record + 72U, -1);
	record = ConstructionAddLump(&fixture, SG_BSP_LUMP_FACES, 20U);
	ConstructionWriteU16(record + 0U, 0U);
	ConstructionWriteI16(record + 2U, 0);
	ConstructionWriteI32(record + 4U, 0);
	ConstructionWriteI16(record + 8U, 3);
	ConstructionWriteI16(record + 10U, 0);
	record[12] = 0U;
	record[13] = 255U;
	record[14] = 255U;
	record[15] = 255U;
	ConstructionWriteI32(record + 16U, 0);
	record = ConstructionAddLump(&fixture, SG_BSP_LUMP_LIGHTING, 3U);
	record[0] = 10U;
	record[1] = 20U;
	record[2] = 30U;
	record = ConstructionAddLump(&fixture, SG_BSP_LUMP_LEAVES, 56U);
	ConstructionWriteI32(record + 0U, 1);
	ConstructionWriteU16(record + 4U, UINT16_MAX);
	ConstructionWriteI16(record + 6U, 0);
	ConstructionWriteI16(record + 8U, -16);
	ConstructionWriteI16(record + 10U, -16);
	ConstructionWriteI16(record + 12U, -16);
	ConstructionWriteI16(record + 14U, 16);
	ConstructionWriteI16(record + 16U, 16);
	ConstructionWriteI16(record + 18U, 16);
	ConstructionWriteU16(record + 20U, 0U);
	ConstructionWriteU16(record + 22U, 1U);
	ConstructionWriteU16(record + 24U, 0U);
	ConstructionWriteU16(record + 26U, 1U);
	ConstructionWriteI32(record + 28U, 0);
	ConstructionWriteU16(record + 32U, 0U);
	ConstructionWriteU16(record + 34U, 0U);
	ConstructionWriteI16(record + 36U, -16);
	ConstructionWriteI16(record + 38U, -16);
	ConstructionWriteI16(record + 40U, -16);
	ConstructionWriteI16(record + 42U, 16);
	ConstructionWriteI16(record + 44U, 16);
	ConstructionWriteI16(record + 46U, 16);
	record = ConstructionAddLump(&fixture, SG_BSP_LUMP_LEAF_FACES, 2U);
	ConstructionWriteU16(record, 0U);
	record = ConstructionAddLump(&fixture, SG_BSP_LUMP_LEAF_BRUSHES, 2U);
	ConstructionWriteU16(record, 0U);
	record = ConstructionAddLump(&fixture, SG_BSP_LUMP_EDGES, 12U);
	ConstructionWriteU16(record + 0U, 0U);
	ConstructionWriteU16(record + 2U, 1U);
	ConstructionWriteU16(record + 4U, 1U);
	ConstructionWriteU16(record + 6U, 2U);
	ConstructionWriteU16(record + 8U, 2U);
	ConstructionWriteU16(record + 10U, 0U);
	record = ConstructionAddLump(&fixture, SG_BSP_LUMP_SURFEDGES, 12U);
	ConstructionWriteI32(record + 0U, 0);
	ConstructionWriteI32(record + 4U, 1);
	ConstructionWriteI32(record + 8U, 2);
	record = ConstructionAddLump(&fixture, SG_BSP_LUMP_MODELS, 96U);
	ConstructionWriteFloat(record + 0U, -16.0f);
	ConstructionWriteFloat(record + 4U, -16.0f);
	ConstructionWriteFloat(record + 8U, -16.0f);
	ConstructionWriteFloat(record + 12U, 16.0f);
	ConstructionWriteFloat(record + 16U, 16.0f);
	ConstructionWriteFloat(record + 20U, 16.0f);
	ConstructionWriteI32(record + 36U, 0);
	ConstructionWriteI32(record + 40U, 0);
	ConstructionWriteI32(record + 44U, 1);
	ConstructionWriteFloat(record + 48U, -8.0f);
	ConstructionWriteFloat(record + 52U, -8.0f);
	ConstructionWriteFloat(record + 56U, -8.0f);
	ConstructionWriteFloat(record + 60U, 8.0f);
	ConstructionWriteFloat(record + 64U, 8.0f);
	ConstructionWriteFloat(record + 68U, 8.0f);
	ConstructionWriteFloat(record + 72U, 32.0f);
	ConstructionWriteI32(record + 84U, -1);
	ConstructionWriteI32(record + 88U, 0);
	ConstructionWriteI32(record + 92U, 0);
	record = ConstructionAddLump(&fixture, SG_BSP_LUMP_BRUSHES, 12U);
	ConstructionWriteI32(record + 0U, 0);
	ConstructionWriteI32(record + 4U, 1);
	ConstructionWriteI32(record + 8U, 1);
	record = ConstructionAddLump(&fixture, SG_BSP_LUMP_BRUSH_SIDES, 4U);
	ConstructionWriteU16(record + 0U, 0U);
	ConstructionWriteI16(record + 2U, 0);
	(void)ConstructionAddLump(&fixture, SG_BSP_LUMP_POP, 256U);
	record = ConstructionAddLump(&fixture, SG_BSP_LUMP_AREAS, 16U);
	ConstructionWriteI32(record + 0U, 1);
	ConstructionWriteI32(record + 4U, 0);
	ConstructionWriteI32(record + 8U, 1);
	ConstructionWriteI32(record + 12U, 1);
	record = ConstructionAddLump(&fixture, SG_BSP_LUMP_AREAPORTALS, 16U);
	ConstructionWriteI32(record + 0U, 0);
	ConstructionWriteI32(record + 4U, 1);
	ConstructionWriteI32(record + 8U, 0);
	ConstructionWriteI32(record + 12U, 0);
	ConstructionFinishHeader(&fixture);
	return fixture;
}

static void SetVector(float value[3], float x, float y, float z)
{
	value[0] = x;
	value[1] = y;
	value[2] = z;
}

static void InstallRuntimeBot(void)
{
	memset(runtime_edicts, 0, sizeof(runtime_edicts));
	memset(runtime_clients, 0, sizeof(runtime_clients));
	memset(sg_bots, 0, sizeof(sg_bots));
	memset(&level, 0, sizeof(level));
	runtime_edicts[0].inuse = true;
	runtime_edicts[0].s.number = 0;
	runtime_edicts[0].classname = "worldspawn";
	runtime_edicts[1].inuse = true;
	runtime_edicts[1].s.number = 1;
	runtime_edicts[1].s.modelindex = 1;
	runtime_edicts[1].client = &runtime_clients[1];
	runtime_edicts[1].classname = "player";
	runtime_edicts[1].flags = FL_BOT;
	runtime_edicts[1].health = 100;
	runtime_edicts[1].viewheight = 22.0f;
	runtime_clients[1].pers.connected = true;
	runtime_clients[1].ctf.teamnum = CTF_TEAM_RED;
	runtime_clients[1].ctf.ctfid = UINT64_C(0x1001);
	sg_bots[0].active = true;
	sg_bots[0].ent = &runtime_edicts[1];
	globals.edicts = runtime_edicts;
	globals.num_edicts = 4;
	runtime_return_entity = NULL;
	runtime_trace_calls = 0;
	runtime_contents_calls = 0;
	runtime_pmove_calls = 0;
	runtime_last_pmove_msec = 0U;
	runtime_touch_on_first_pmove = 0;
}

static void ClearRuntimeBot(void)
{
	memset(sg_bots, 0, sizeof(sg_bots));
	globals.edicts = NULL;
	globals.num_edicts = 0;
}

static void TestEngineChecksumVectors(void)
{
	static const uint8_t empty = 0U;
	static const uint8_t abc[] = { 'a', 'b', 'c' };
	uint32_t checksum = 0U;

	/* RFC 1320 MD4 digests, reduced exactly as Quake II's
	 * Com_BlockChecksum XORs its four little-endian words. */
	CHECK(SG_BspWorldEngineChecksum(&empty, 0U, &checksum));
	CHECK(checksum == UINT32_C(0xc6f640b7));
	CHECK(SG_BspWorldEngineChecksum(abc, sizeof(abc), &checksum));
	CHECK(checksum == UINT32_C(0x5da10e2e));
}

static void InitializeWorld(void)
{
	memset(&test_world, 0, sizeof(test_world));
	memset(planes, 0, sizeof(planes));
	memset(nodes, 0, sizeof(nodes));
	memset(leaves, 0, sizeof(leaves));
	memset(models, 0, sizeof(models));
	SetVector(planes[0].normal.value, 0.0f, 0.0f, 1.0f);
	planes[0].type = 2;
	nodes[0].plane = 0U;
	nodes[0].children[0] = -1;
	nodes[0].children[1] = -2;
	models[0].headnode = 0;
	test_world.planes = planes;
	test_world.plane_count = 1U;
	test_world.nodes = nodes;
	test_world.node_count = 1U;
	test_world.leaves = leaves;
	test_world.leaf_count = 2U;
	test_world.models = models;
	test_world.model_count = 1U;
	test_world.source_bytes = test_runtime_source;
	test_world.source_size = sizeof(test_runtime_source);
	CHECK(SG_BspWorldContentIdentity(test_runtime_source,
		sizeof(test_runtime_source), &test_world.content_identity));
	CHECK(SG_BspWorldEngineChecksum(test_runtime_source,
		sizeof(test_runtime_source), &test_world.engine_checksum));
	CHECK(SG_BspWorldSourceIdentityCurrent(&test_world));
}

static void InitializeHookWorld(void)
{
	memset(&hook_world, 0, sizeof(hook_world));
	memset(hook_planes, 0, sizeof(hook_planes));
	memset(hook_nodes, 0, sizeof(hook_nodes));
	memset(hook_leaves, 0, sizeof(hook_leaves));
	memset(hook_leaf_brushes, 0, sizeof(hook_leaf_brushes));
	memset(hook_brushes, 0, sizeof(hook_brushes));
	memset(hook_brush_sides, 0, sizeof(hook_brush_sides));
	memset(hook_models, 0, sizeof(hook_models));
	/* The node splits the trace at x=0.  The positive leaf contains a
	 * half-space brush, so an owner-to-bolt trace from -8 to +8 hits it. */
	SetVector(hook_planes[0].normal.value, 1.0f, 0.0f, 0.0f);
	hook_planes[0].type = 0;
	SetVector(hook_planes[1].normal.value, -1.0f, 0.0f, 0.0f);
	hook_planes[1].type = 0;
	hook_nodes[0].plane = 0U;
	hook_nodes[0].children[0] = -1;
	hook_nodes[0].children[1] = -2;
	hook_leaves[0].contents = SG_HOST_CONTENTS_SOLID;
	hook_leaves[0].first_leaf_brush = 0U;
	hook_leaves[0].leaf_brush_count = 1U;
	hook_leaf_brushes[0] = 0U;
	hook_brushes[0].first_side = 0U;
	hook_brushes[0].side_count = 1U;
	hook_brushes[0].contents = SG_HOST_CONTENTS_SOLID;
	hook_brush_sides[0].plane = 1U;
	hook_brush_sides[0].texinfo = -1;
	hook_models[0].headnode = 0;
	hook_world.planes = hook_planes;
	hook_world.plane_count = 2U;
	hook_world.nodes = hook_nodes;
	hook_world.node_count = 1U;
	hook_world.leaves = hook_leaves;
	hook_world.leaf_count = 2U;
	hook_world.leaf_brushes = hook_leaf_brushes;
	hook_world.leaf_brush_count = 1U;
	hook_world.models = hook_models;
	hook_world.model_count = 1U;
	hook_world.brushes = hook_brushes;
	hook_world.brush_count = 1U;
	hook_world.brush_sides = hook_brush_sides;
	hook_world.brush_side_count = 1U;
	hook_world.content_identity = test_world.content_identity;
}

static sg_rune_model_identity_t Identity(void)
{
	sg_rune_model_identity_t identity;

	memset(&identity, 0, sizeof(identity));
	identity.bsp_content_id = UINT64_C(0x101);
	identity.entity_semantics_id = UINT64_C(0x202);
	identity.physics_abi_id = UINT64_C(0x303);
	identity.source_set_identity = UINT64_C(0x404);
	identity.schema_id = UINT64_C(0x505);
	identity.producer_identity = UINT64_C(0x606);
	SetVector(identity.standing_hull.mins.value, -16.0f, -16.0f, -24.0f);
	SetVector(identity.standing_hull.maxs.value, 16.0f, 16.0f, 32.0f);
	SetVector(identity.crouching_hull.mins.value, -16.0f, -16.0f, -24.0f);
	SetVector(identity.crouching_hull.maxs.value, 16.0f, 16.0f, 4.0f);
	identity.physics.gravity = 800.0f;
	identity.physics.ground_acceleration = 10.0f;
	identity.physics.air_acceleration = 1.0f;
	identity.physics.water_acceleration = 10.0f;
	identity.physics.hook_acceleration = 800.0f;
	identity.physics.external_acceleration = 1.0f;
	identity.physics.water_drag = 1.0f;
	identity.physics.max_velocity = 2000.0f;
	identity.physics.frame_ms = SG_HOST_ENGINE_FRAME_MS;
	identity.physics.substep_ms = SG_HOST_ENGINE_PMOVE_SUBSTEP_MS;
	return identity;
}

static sg_host_static_identity_t StaticIdentity(void)
{
	sg_host_static_identity_t identity;
	sg_rune_model_identity_t model = Identity();

	memset(&identity, 0, sizeof(identity));
	identity.bsp_identity = test_world.content_identity;
	identity.bsp_bytes = test_world.source_size;
	identity.engine_checksum = test_world.engine_checksum;
	identity.entity_crc32 = UINT32_C(0x12345678);
	identity.host_physics_epoch = SG_HOST_PHYSICS_EPOCH;
	identity.physics_abi_id = SG_HOST_ENGINE_PMOVE_ABI_ID;
	identity.standing_hull = model.standing_hull;
	identity.crouching_hull = model.crouching_hull;
	identity.physics = model.physics;
	return identity;
}

static sg_host_collision_authority_t Authority(void)
{
	sg_host_collision_authority_t authority;
	sg_rune_model_identity_t identity = Identity();
	sg_host_collision_error_t error;

	memset(&authority, 0, sizeof(authority));
	CHECK(SG_HostCollisionInit(&authority, &test_world, &identity, &error));
	CHECK(error == SG_HOST_COLLISION_ERROR_NONE);
	return authority;
}

static sg_host_static_identity_t ConstructionStaticIdentity(
	const sg_bsp_world_t *world_value)
{
	sg_host_static_identity_t identity = StaticIdentity();

	CHECK(world_value != NULL);
	if (world_value)
	{
		identity.bsp_identity = world_value->content_identity;
		identity.bsp_bytes = (uint64_t)world_value->source_size;
		identity.engine_checksum = world_value->engine_checksum;
	}
	return identity;
}

static sg_host_collision_authority_t ConstructionAuthority(
	const sg_bsp_world_t *world_value)
{
	sg_host_collision_authority_t authority;
	sg_rune_model_identity_t identity = Identity();
	sg_host_collision_error_t error = SG_HOST_COLLISION_ERROR_NONE;

	memset(&authority, 0, sizeof(authority));
	identity.physics_abi_id = SG_HOST_ENGINE_PMOVE_ABI_ID;
	CHECK(SG_HostCollisionInit(&authority, world_value, &identity, &error));
	CHECK(error == SG_HOST_COLLISION_ERROR_NONE);
	return authority;
}

static sg_bsp_world_t *ConstructionLoadBsp(
	const construction_bsp_fixture_t *fixture)
{
	sg_bsp_error_t error = { SG_BSP_ERROR_NONE, SG_BSP_LUMP_ENTITIES, 0U };
	sg_bsp_world_t *world_value = NULL;

	CHECK(fixture != NULL);
	if (fixture)
		CHECK(SG_BspWorldLoadMemory(fixture->bytes, fixture->size,
			&world_value, &error));
	CHECK(error.code == SG_BSP_ERROR_NONE);
	CHECK(world_value != NULL);
	return world_value;
}

static void ConfigureConstructionLevel(const sg_bsp_world_t *world_value,
	const char *mapname)
{
	size_t mapname_length = strlen(mapname);

	CHECK(world_value != NULL);
	CHECK(mapname_length < sizeof(test_level_identity.mapname));
	memset(&test_level_identity, 0, sizeof(test_level_identity));
	test_level_identity.bsp_checksum = world_value->engine_checksum;
	test_level_identity.entity_crc32 = UINT32_C(0x12345678);
	test_level_identity.host_physics_id = SG_HOST_PHYSICS_EPOCH;
	test_level_identity.bsp_bytes = (uint64_t)world_value->source_size;
	memcpy(test_level_identity.bsp_sha256,
		world_value->content_identity.bytes,
		sizeof(test_level_identity.bsp_sha256));
	memcpy(test_level_identity.mapname, mapname, mapname_length + 1U);
}

static sg_host_law_publication_t *HookWorldPublication(void)
{
	sg_host_collision_authority_t authority;
	sg_rune_model_identity_t identity = Identity();
	sg_host_collision_error_t error;
	sg_host_law_publication_t *publication = NULL;
	sg_host_law_result_t result;

	CHECK(SG_HostCollisionInit(&authority, &hook_world, &identity, &error));
	CHECK(error == SG_HOST_COLLISION_ERROR_NONE);
	result = SG_HostLawPublicationOwnerIssue(&authority, &publication);
	CHECK(result.status == SG_HOST_LAW_OK && publication != NULL);
	return publication;
}

static cvar_t *TestCvar(char *name, char *value, int flags)
{
	(void)value;
	(void)flags;
	if (strcmp(name, "sv_airaccelerate") == 0)
		return &airaccelerate_cvar;
	return NULL;
}

static int HookSpeed(const vec3_t start, const vec3_t bite, vec3_t velocity)
{
	float distance;
	int speed;

	VectorSubtract(bite, start, velocity);
	distance = VectorLength(velocity);
	speed = (int)distance;
	VectorNormalize(velocity);
	if (speed > 120)
		VectorScale(velocity, SG_HOST_HOOK_PULL_SPEED, velocity);
	else if (speed > 100)
		VectorScale(velocity, (float)(speed * 5), velocity);
	else if (speed > 80)
		VectorScale(velocity, (float)(speed * 4), velocity);
	else if (speed > 40)
		VectorScale(velocity, (float)(speed * 3), velocity);
	else if (speed > 20)
		VectorScale(velocity, (float)(speed * 2), velocity);
	else if (speed > 10)
		VectorScale(velocity, (float)speed, velocity);
	return speed;
}

int CTF_HookPullVelocity(const vec3_t start, const vec3_t bite,
	vec3_t velocity)
{
	return HookSpeed(start, bite, velocity);
}

int SG_HostHookLiveCapture(sg_host_hook_law_t *law_out)
{
	if (!law_out)
		return 0;
	SG_HostHookLawDefault(law_out);
	law_out->no_grapple_damage =
		((uint32_t)ctf_flags_cvar.value & SG_HOST_HOOK_CTF_NO_GRAP_DAMAGE) != 0U;
	return 1;
}

int SG_HostMechanismLiveCapture(sg_host_mechanism_law_t *law_out)
{
	if (!law_out)
		return 0;
	SG_HostMechanismLawDefault(law_out);
	law_out->door_default_speed = test_live_door_speed;
	law_out->door_rotating_default_speed = test_live_rotating_door_speed;
	return 1;
}

static void ForgedPmove(pmove_t *pmove)
{
	Pmove(pmove);
	pmove->maxs[2] += 1.0f;
}

static sg_host_law_publication_t *Issue(void)
{
	sg_host_collision_authority_t authority = Authority();
	sg_host_law_publication_t *publication = NULL;
	sg_host_law_result_t result;

	result = SG_HostLawPublicationOwnerIssue(&authority, &publication);
	CHECK(result.status == SG_HOST_LAW_OK);
	CHECK(publication != NULL);
	return publication;
}

static sg_host_law_view_t Read(const sg_host_law_publication_t *publication)
{
	sg_host_law_view_t view;
	sg_host_law_result_t result;

	memset(&view, 0xa5, sizeof(view));
	result = SG_HostLawPublicationRead(publication, &view);
	CHECK(result.status == SG_HOST_LAW_OK);
	return view;
}

static void TestEngineBindingAndParity(void)
{
	sg_host_engine_pmove_abi_t abi;
	sg_host_engine_pmove_binding_t binding;
	sg_host_engine_parity_inputs_t low_gravity_inputs;
	sg_host_engine_parity_result_t parity;

	gi.Pmove = Pmove;
	CHECK(SG_HostEnginePmoveABI(&abi));
	CHECK(abi.version == SG_HOST_ENGINE_PMOVE_ABI_VERSION);
	CHECK(abi.game_api_version == GAME_API_VERSION);
	CHECK(abi.import_size == (uint32_t)sizeof(game_import_t));
	CHECK(abi.pmove_offset == (uint32_t)offsetof(game_import_t, Pmove));
	CHECK(abi.pmove_size == (uint32_t)sizeof(pmove_t));
	CHECK(abi.state_size == (uint32_t)sizeof(pmove_state_t));
	CHECK(abi.command_size == (uint32_t)sizeof(usercmd_t));
	CHECK(abi.fraction_bits == SG_HOST_ENGINE_PMOVE_FRACTION_BITS);
	CHECK(abi.substep_ms == SG_HOST_ENGINE_PMOVE_SUBSTEP_MS);
	CHECK(abi.identity == SG_HOST_ENGINE_PMOVE_ABI_ID);
	CHECK(SG_HostEnginePmoveBindingCapture(&binding));
	CHECK(binding.entry == Pmove);
	CHECK(binding.owner == (const void *)&gi);
	CHECK(SG_HostEnginePmoveBindingCurrent(&binding));
	CHECK(SG_HostEnginePmoveParity(&parity));
	CHECK(parity.cases == SG_HOST_ENGINE_PARITY_ALL);
	CHECK(parity.fingerprint != 0U);
	CHECK(parity.engine_calls > 8U);
	CHECK(parity.trace_calls != 0U);
	CHECK(parity.contents_calls != 0U);
	low_gravity_inputs.gravity = 100.0f;
	low_gravity_inputs.max_velocity = 2000.0f;
	low_gravity_inputs.airaccelerate = 0.0f;
	low_gravity_inputs.frame_ms = SG_HOST_ENGINE_FRAME_MS;
	low_gravity_inputs.substep_ms = SG_HOST_ENGINE_PMOVE_SUBSTEP_MS;
	CHECK(SG_HostEnginePmoveParityBound(&binding, &low_gravity_inputs,
		&parity));
	CHECK(parity.cases == SG_HOST_ENGINE_PARITY_ALL);
	CHECK(parity.engine_calls > 8U);
	CHECK(parity.fingerprint != 0U);
}

static void TestEngineRuntimeOwnerBinding(void)
{
	sg_host_engine_runtime_t *runtime = NULL;
	sg_host_engine_runtime_status_t runtime_status;
	sg_host_collision_trace_t trace;
	sg_host_pmove_request_t request;
	sg_host_pmove_result_t pmove_result;
	sg_host_pmove_error_t pmove_error;
	sg_host_engine_subject_identity_t subject;
	sg_host_pmove_substep_t replay_substeps[4];
	sg_host_pmove_trace_t replay_traces[8];
	sg_host_pmove_replay_workspace_t replay_workspace;
	sg_host_pmove_replay_t replay;
	sg_host_collision_pose_t pose;
	float start[3] = { 0.0f, 0.0f, 0.0f };
	float mins[3] = { -16.0f, -16.0f, -24.0f };
	float maxs[3] = { 16.0f, 16.0f, 32.0f };
	float end[3] = { 64.0f, 0.0f, 0.0f };

	memset(&test_level_identity, 0, sizeof(test_level_identity));
	test_level_identity.bsp_checksum = test_world.engine_checksum;
	test_level_identity.entity_crc32 = UINT32_C(0x9abcdef0);
	test_level_identity.host_physics_id = SG_HOST_PHYSICS_EPOCH;
	test_level_identity.bsp_bytes = test_world.source_size;
	memcpy(test_level_identity.bsp_sha256, test_world.content_identity.bytes,
		sizeof(test_level_identity.bsp_sha256));
	memcpy(test_level_identity.mapname, "runtime_map",
		sizeof("runtime_map"));
	InstallRuntimeBot();
	gi.trace = RuntimeTrace;
	gi.pointcontents = RuntimePointContents;
	gi.Pmove = RuntimePmove;
	test_identity_snapshot_status = SG_IDENTITY_OK;
	runtime_status = SG_HostEngineRuntimeBegin("runtime_map", &runtime);
	CHECK(runtime_status == SG_HOST_ENGINE_RUNTIME_OK && runtime != NULL);
	CHECK(SG_HostEngineRuntimeCurrent(runtime));
	runtime_status = SG_HostEngineRuntimeOwnerActivate(runtime);
	CHECK(runtime_status == SG_HOST_ENGINE_RUNTIME_OK);
	CHECK(SG_HostEngineRuntimeAccepted(runtime));
	CHECK(SG_HostEngineRuntimeOwnerSubject(runtime, 1U, &subject));
	CHECK(subject.client_id == 1U && subject.reserved == 0U &&
		subject.spawn_generation == UINT64_C(0x1001));
	CHECK(SG_HostEngineRuntimeOwnerSubjectCurrent(runtime, &subject));
	CHECK(SG_HostEngineRuntimeTrace(runtime, 1U, start, mins, maxs, end,
		SG_HOST_MASK_PLAYER_SOLID, &trace));
	CHECK(runtime_last_passent == &runtime_edicts[1]);
	/* ClientThink calls the engine exactly once with the caller's duration.
	 * A contact produced by that callback must survive result publication. */
	runtime_edicts[2].inuse = true;
	runtime_edicts[2].s.number = 2;
	runtime_edicts[2].s.modelindex = 2;
	runtime_edicts[2].classname = "func_door";
	memset(&request, 0, sizeof(request));
	request.state.pm_type = PM_NORMAL;
	request.previous_state = request.state;
	request.command.msec = 25U;
	runtime_touch_on_first_pmove = 1;
	CHECK(SG_HostEngineRuntimePmove(runtime, 1U, &request, &pmove_result,
		&pmove_error));
	CHECK(pmove_error == SG_HOST_PMOVE_ERROR_NONE);
	CHECK(runtime_pmove_calls == 1);
	CHECK(runtime_contents_calls == 1);
	CHECK(runtime_last_pmove_msec == 25U);
	CHECK(pmove_result.evaluated_steps == 1U);
	CHECK(pmove_result.elapsed_ms == 25U);
	CHECK(pmove_result.touch_count == 1U);
	CHECK(pmove_result.touch_instance_ids[0] == 2U);
	memset(&replay_workspace, 0, sizeof(replay_workspace));
	replay_workspace.substeps = replay_substeps;
	replay_workspace.substep_capacity = 4U;
	replay_workspace.traces = replay_traces;
	replay_workspace.trace_capacity = 8U;
	request.command.msec = (byte)SG_HOST_ENGINE_FRAME_MS;
	runtime_touch_on_first_pmove = 0;
	CHECK(SG_HostEngineRuntimeOwnerReplayFrame(runtime, &subject, &request,
		&replay_workspace, &replay, &pmove_error));
	CHECK(pmove_error == SG_HOST_PMOVE_ERROR_NONE);
	CHECK(replay.substep_count == 4U && replay.trace_count == 4U);
	CHECK(replay.result.evaluated_steps == 4U &&
		replay.result.elapsed_ms == SG_HOST_ENGINE_FRAME_MS);
	CHECK(memcmp(replay.bsp_identity.bytes, test_world.content_identity.bytes,
		sizeof(replay.bsp_identity.bytes)) == 0);
	CHECK(SG_HostEngineRuntimeOwnerClassifyPose(runtime, &subject, start,
		SG_RUNE_STANCE_STANDING, &pose));
	CHECK(pose.valid && !pose.supported && pose.water_level == 0U);
	runtime_clients[1].ctf.ctfid++;
	CHECK(!SG_HostEngineRuntimeOwnerSubjectCurrent(runtime, &subject));
	CHECK(!SG_HostEngineRuntimeOwnerReplayFrame(runtime, &subject, &request,
		&replay_workspace, &replay, &pmove_error));
	runtime_clients[1].ctf.ctfid = subject.spawn_generation;
	CHECK(SG_HostEngineRuntimeOwnerSubjectCurrent(runtime, &subject));
	sg_bots[0].active = false;
	CHECK(!SG_HostEngineRuntimeTrace(runtime, 1U, start, mins, maxs, end,
		SG_HOST_MASK_PLAYER_SOLID, &trace));
	sg_bots[0].active = true;
	gi.Pmove = Pmove;
	CHECK(!SG_HostEngineRuntimeCurrent(runtime));
	CHECK(!SG_HostEngineRuntimeAccepted(runtime));
	gi.Pmove = RuntimePmove;
	CHECK(SG_HostEngineRuntimeCurrent(runtime));
	SG_HostEngineRuntimeDestroy(runtime);
	ClearRuntimeBot();
	gi.trace = NULL;
	gi.pointcontents = NULL;
	gi.Pmove = Pmove;
	test_identity_snapshot_status = SG_IDENTITY_UNAVAILABLE;
}

static void TestPublicationAndCallbackIsolation(void)
{
	sg_host_law_publication_t *publication = Issue();
	sg_host_law_view_t view = Read(publication);
	sg_host_law_result_t result;

	CHECK(view.version == SG_HOST_LAW_PUBLICATION_VERSION);
	CHECK(view.identity.physics.gravity == 800.0f);
	CHECK(view.pmove_behavior_fingerprint != 0U);
	CHECK(view.hook.identity == SG_HOST_HOOK_LAW_ID);
	CHECK(view.mechanism.identity == SG_HOST_MECHANISM_LAW_ID);
	/* A forged legacy callback has no authority over a publication. */
	sg_host.pmove = ForgedPmove;
	result = SG_HostLawPublicationRevalidateProduction(publication);
	CHECK(result.status == SG_HOST_LAW_OK);
	/* The actual engine slot is authoritative and behavior drift is caught. */
	gi.Pmove = ForgedPmove;
	result = SG_HostLawPublicationRevalidateProduction(publication);
	CHECK(result.status == SG_HOST_LAW_PRODUCTION_DRIFT);
	CHECK(result.field == SG_HOST_LAW_FIELD_PMOVE_BEHAVIOR);
	gi.Pmove = Pmove;
	sg_host.pmove = NULL;
	result = SG_HostLawPublicationRevalidateProduction(publication);
	CHECK(result.status == SG_HOST_LAW_OK);
	SG_HostLawPublicationOwnerDestroy(publication);
}

static void TestPmoveAndCollisionExecution(void)
{
	sg_host_law_publication_t *publication = Issue();
	const sg_host_collision_authority_t *borrowed_authority = NULL;
	sg_host_collision_trace_t trace;
	sg_host_pmove_request_t request;
	sg_host_pmove_result_t pmove_result;
	sg_host_pmove_error_t pmove_error;
	const float zero[3] = { 0.0f, 0.0f, 0.0f };
	const float end[3] = { 64.0f, 0.0f, 0.0f };
	sg_host_law_result_t result;

	result = SG_HostLawPublicationCollisionAuthority(publication,
		&borrowed_authority);
	CHECK(result.status == SG_HOST_LAW_OK && borrowed_authority != NULL);
	CHECK(borrowed_authority->world == &test_world);
	test_world.content_identity.bytes[0] ^= 1U;
	borrowed_authority = (const sg_host_collision_authority_t *)1;
	result = SG_HostLawPublicationCollisionAuthority(publication,
		&borrowed_authority);
	CHECK(result.status == SG_HOST_LAW_CORRUPT_PUBLICATION &&
		borrowed_authority == NULL);
	test_world.content_identity.bytes[0] ^= 1U;
	result = SG_HostLawPublicationCollisionAuthority(publication, NULL);
	CHECK(result.status == SG_HOST_LAW_INVALID_ARGUMENT);
	memset(&trace, 0, sizeof(trace));
	result = SG_HostLawPublicationCollisionTrace(publication, NULL, zero, zero,
		zero, end, SG_HOST_MASK_PLAYER_SOLID, &trace);
	CHECK(result.status == SG_HOST_LAW_OK);
	CHECK(trace.fraction == 1.0f);
	memset(&request, 0, sizeof(request));
	request.state.pm_type = PM_NORMAL;
	request.state.origin[2] = 1600;
	request.previous_state = request.state;
	request.command.forwardmove = 300;
	result = SG_HostLawPublicationPmove(publication, NULL, &request,
		&pmove_result, &pmove_error);
	CHECK(result.status == SG_HOST_LAW_OK);
	CHECK(pmove_error == SG_HOST_PMOVE_ERROR_NONE);
	CHECK(pmove_result.evaluated_steps == 4U);
	CHECK(pmove_result.elapsed_ms == SG_HOST_ENGINE_FRAME_MS);
	CHECK(pmove_result.physics_abi_id == UINT64_C(0x303));
	request.hook_law_id = SG_HOST_PMOVE_HOOK_LAW_ID;
	request.hook_attached = 1U;
	request.hook_length = SG_HOST_PMOVE_HOOK_LENGTH_GRAVITY_ZERO - 1U;
	result = SG_HostLawPublicationPmove(publication, NULL, &request,
		&pmove_result, &pmove_error);
	CHECK(result.status == SG_HOST_LAW_OK &&
		pmove_result.gravity == 0.0f &&
		pmove_result.gravity_law_id == SG_HOST_PMOVE_HOOK_LAW_ID);
	request.hook_length = SG_HOST_PMOVE_HOOK_LENGTH_GRAVITY_ZERO;
	result = SG_HostLawPublicationPmove(publication, NULL, &request,
		&pmove_result, &pmove_error);
	CHECK(result.status == SG_HOST_LAW_OK && pmove_result.gravity == 800.0f &&
		pmove_result.gravity_law_id == 0U);
	request.hook_law_id = UINT64_C(0xdeadbeef);
	result = SG_HostLawPublicationPmove(publication, NULL, &request,
		&pmove_result, &pmove_error);
	CHECK(result.status == SG_HOST_LAW_EVALUATION_FAILED &&
		pmove_error == SG_HOST_PMOVE_ERROR_IDENTITY_MISMATCH);
	SG_HostLawPublicationOwnerDestroy(publication);
}

static void TestHookChronology(void)
{
	sg_host_law_publication_t *publication = Issue();
	sg_host_hook_observation_t observation;
	sg_host_hook_step_t step;
	float origin[3] = { 10.0f, 20.0f, 30.0f };
	float forward[3] = { 1.0f, 0.0f, 0.0f };
	float right[3] = { 0.0f, 1.0f, 0.0f };
	float muzzle[3];
	sg_host_law_result_t result;

	result = SG_HostLawPublicationHookMuzzle(publication, origin, 22.0f,
		2, forward, right, muzzle);
	CHECK(result.status == SG_HOST_LAW_OK);
	CHECK(muzzle[0] == 18.0f && muzzle[1] == 20.0f && muzzle[2] == 44.0f);
	memset(&observation, 0, sizeof(observation));
	observation.event = SG_HOST_HOOK_FIRE;
	observation.phase = SG_HOST_HOOK_IDLE;
	observation.muzzle_clear = 1;
	observation.attack_held = 1;
	result = SG_HostLawPublicationHookStep(publication, &observation, &step);
	CHECK(result.status == SG_HOST_LAW_OK && step.accepted);
	CHECK(step.next_phase == SG_HOST_HOOK_IN_FLIGHT);
	observation.event = SG_HOST_HOOK_FLIGHT_HIT;
	observation.phase = SG_HOST_HOOK_IN_FLIGHT;
	observation.first_hit = 1;
	observation.target_kind = SG_HOST_HOOK_TARGET_PLAYER;
	observation.target_identity = UINT64_C(0x99);
	result = SG_HostLawPublicationHookStep(publication, &observation, &step);
	CHECK(result.status == SG_HOST_LAW_OK && step.attached);
	CHECK(step.first_hit && step.damage == SG_HOST_HOOK_INITIAL_DAMAGE);
	CHECK(step.target_identity == UINT64_C(0x99));
	{
		sg_host_hook_observation_t killed = observation;

		killed.target_died_after_damage = 1;
		result = SG_HostLawPublicationHookStep(publication, &killed, &step);
		CHECK(result.status == SG_HOST_LAW_OK && step.accepted &&
			step.first_hit && !step.attached && step.aborted &&
			step.damage == SG_HOST_HOOK_INITIAL_DAMAGE &&
			step.next_phase == SG_HOST_HOOK_IDLE);
	}
	observation.event = SG_HOST_HOOK_ATTACHED_TICK;
	observation.phase = SG_HOST_HOOK_ATTACHED;
	observation.attached_target_identity = UINT64_C(0x99);
	observation.frame = 7U;
	observation.last_damage_frame = 0U;
	observation.bite_distance = 200.0f;
	result = SG_HostLawPublicationHookStep(publication, &observation, &step);
	CHECK(result.status == SG_HOST_LAW_OK && step.accepted);
	CHECK(step.pull_after_pmove && !step.pull_before_pmove &&
		step.gravity_applied && !step.gravity_zeroed);
	CHECK(step.damage == SG_HOST_HOOK_ATTACHED_DAMAGE);
	observation.frame = 8U;
	observation.bite_distance = SG_HOST_HOOK_NEAR_BITE_DISTANCE;
	result = SG_HostLawPublicationHookStep(publication, &observation, &step);
	CHECK(result.status == SG_HOST_LAW_OK && step.accepted);
	CHECK(!step.gravity_applied && !step.gravity_zeroed && step.damage == 0U);
	observation.frame = 9U;
	observation.bite_distance =
		SG_HOST_HOOK_NEAR_BITE_GRAVITY_ZERO_DISTANCE - 1.0f;
	result = SG_HostLawPublicationHookStep(publication, &observation, &step);
	CHECK(result.status == SG_HOST_LAW_OK && step.accepted &&
		step.gravity_zeroed && !step.gravity_applied);
	observation.frame = 10U;
	observation.bite_distance = SG_HOST_HOOK_NEAR_BITE_GRAVITY_ZERO_DISTANCE;
	result = SG_HostLawPublicationHookStep(publication, &observation, &step);
	CHECK(result.status == SG_HOST_LAW_OK && step.accepted &&
		!step.gravity_zeroed && !step.gravity_applied);
	observation.target_identity = UINT64_C(0x100);
	result = SG_HostLawPublicationHookStep(publication, &observation, &step);
	CHECK(result.status == SG_HOST_LAW_OK && !step.accepted);
	CHECK(step.target_identity == UINT64_C(0x99) &&
		step.target_kind == SG_HOST_HOOK_TARGET_NONE);
	observation.target_identity = UINT64_C(0x99);
	observation.sky = 1;
	result = SG_HostLawPublicationHookStep(publication, &observation, &step);
	CHECK(result.status == SG_HOST_LAW_OK && !step.accepted);
	observation.event = SG_HOST_HOOK_RELEASE;
	observation.sky = 0;
	result = SG_HostLawPublicationHookStep(publication, &observation, &step);
	CHECK(result.status == SG_HOST_LAW_OK && step.released && step.coast_velocity);
	CHECK(step.next_phase == SG_HOST_HOOK_COAST);
	observation.event = SG_HOST_HOOK_REFIRE;
	observation.phase = SG_HOST_HOOK_COAST;
	observation.muzzle_clear = 1;
	observation.attack_held = 1;
	result = SG_HostLawPublicationHookStep(publication, &observation, &step);
	CHECK(result.status == SG_HOST_LAW_OK && step.accepted);
	CHECK(step.next_phase == SG_HOST_HOOK_IN_FLIGHT);
	{
		sg_host_hook_observation_t immediate;
		sg_host_hook_fire_request_t fire_request;
		sg_host_law_publication_t *hook_publication;

		memset(&immediate, 0, sizeof(immediate));
		immediate.event = SG_HOST_HOOK_FIRE;
		immediate.phase = SG_HOST_HOOK_IDLE;
		immediate.attack_held = 1;
		immediate.muzzle_clear = 0;
		immediate.immediate_hit = 1;
		immediate.target_kind = SG_HOST_HOOK_TARGET_WORLD;
		immediate.target_identity = UINT64_C(0x77);
		/* The legacy observation seam still spawns, but its forged hit and
		 * target fields are ignored. */
		result = SG_HostLawPublicationHookStep(publication, &immediate, &step);
		CHECK(result.status == SG_HOST_LAW_OK && step.accepted &&
			!step.first_hit && !step.attached && !step.trace_epsilon_applied &&
			step.target_identity == 0U &&
			step.next_phase == SG_HOST_HOOK_IN_FLIGHT);
		InitializeHookWorld();
		hook_publication = HookWorldPublication();
		memset(&fire_request, 0, sizeof(fire_request));
		fire_request.start[0] = -8.0f;
		fire_request.end[0] = 8.0f;
		fire_request.phase = SG_HOST_HOOK_IDLE;
		fire_request.attack_held = 1;
		result = SG_HostLawPublicationHookFire(hook_publication, NULL,
			&fire_request, &step);
		CHECK(result.status == SG_HOST_LAW_OK && step.accepted &&
			step.first_hit && step.attached && step.trace_epsilon_applied &&
			step.target_kind == SG_HOST_HOOK_TARGET_WORLD &&
			step.target_identity == UINT64_C(0x101) &&
			step.next_phase == SG_HOST_HOOK_ATTACHED);
		SG_HostLawPublicationOwnerDestroy(hook_publication);
	}
	{
		sg_host_hook_observation_t grounded = observation;

		grounded.event = SG_HOST_HOOK_RELEASE;
		grounded.phase = SG_HOST_HOOK_ATTACHED;
		grounded.grounded = 1;
		result = SG_HostLawPublicationHookStep(publication, &grounded, &step);
		CHECK(result.status == SG_HOST_LAW_OK && step.released &&
			!step.coast_velocity && step.zero_velocity_z &&
			step.zero_oldvelocity_z);
	}
	SG_HostLawPublicationOwnerDestroy(publication);
}

static void TestHookDamagePolicy(void)
{
	sg_host_law_publication_t *publication;
	sg_host_hook_observation_t observation;
	sg_host_hook_step_t step;
	sg_host_law_result_t result;

	ctf_flags_cvar.value = (float)SG_HOST_HOOK_CTF_NO_GRAP_DAMAGE;
	publication = Issue();
	memset(&observation, 0, sizeof(observation));
	observation.event = SG_HOST_HOOK_FLIGHT_HIT;
	observation.phase = SG_HOST_HOOK_IN_FLIGHT;
	observation.first_hit = 1;
	observation.target_kind = SG_HOST_HOOK_TARGET_PLAYER;
	observation.target_identity = UINT64_C(0x42);
	result = SG_HostLawPublicationHookStep(publication, &observation, &step);
	CHECK(result.status == SG_HOST_LAW_OK && step.attached && step.damage == 0U);
	ctf_flags_cvar.value = 0.0f;
	result = SG_HostLawPublicationRevalidateProduction(publication);
	CHECK(result.status == SG_HOST_LAW_PRODUCTION_DRIFT &&
		result.field == SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY);
	SG_HostLawPublicationOwnerDestroy(publication);
}

static void TestMechanismEquations(void)
{
	sg_host_law_publication_t *publication = Issue();
	sg_host_mechanism_move_result_t move;
	sg_host_mechanism_transition_t transition;
	sg_host_law_result_t result;

	result = SG_HostLawPublicationMoveSchedule(publication, 25.0f, 100.0f,
		100.0f, 100.0f, 0, &move);
	CHECK(result.status == SG_HOST_LAW_OK && move.valid && !move.accelerated);
	CHECK(move.first_think_ms == 100U && move.full_speed_frames == 2U);
	CHECK(move.residual_distance == 5.0f && move.final_speed == 50.0f);
	CHECK(move.completion_ms == 400U);
	result = SG_HostLawPublicationMoveSchedule(publication, 25.0f, 100.0f,
		100.0f, 100.0f, 1, &move);
	CHECK(result.status == SG_HOST_LAW_OK && move.first_think_ms == 0U);
	CHECK(move.completion_ms == 300U);
	result = SG_HostLawPublicationMoveSchedule(publication, 20.0f, 100.0f,
		100.0f, 100.0f, 1, &move);
	CHECK(result.status == SG_HOST_LAW_OK && move.residual_distance == 0.0f &&
		move.completion_ms == 200U);
	result = SG_HostLawPublicationMoveSchedule(publication, 100.0f, 2.0f,
		0.5f, 0.5f, 1, &move);
	CHECK(result.status == SG_HOST_LAW_OK && move.valid && move.accelerated);
	CHECK(move.first_think_ms == 100U && move.completion_ms > 100U);
	result = SG_HostLawPublicationDoorStep(publication,
		SG_HOST_MECHANISM_DOOR_TOP, 0U, SG_HOST_MECHANISM_STATE_TOP, 0.0f,
		1000U, 0U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && transition.accepted);
	CHECK(transition.next_think_ms == 4000U);
	CHECK(Read(publication).mechanism.door_default_speed == 200.0f &&
		Read(publication).mechanism.door_rotating_default_speed == 100.0f &&
		transition.blocker_kind == SG_HOST_MECHANISM_BLOCKER_NONE);
	result = SG_HostLawPublicationDoorStep(publication,
		SG_HOST_MECHANISM_DOOR_TOP, SG_HOST_MECHANISM_DOOR_TOGGLE,
		SG_HOST_MECHANISM_STATE_TOP, 0.0f, 1000U, 0U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && transition.next_think_ms == 0U);
	result = SG_HostLawPublicationDoorStep(publication,
		SG_HOST_MECHANISM_DOOR_TRIGGER_TOUCH, 0U,
		SG_HOST_MECHANISM_STATE_BOTTOM, 100.0f, 1000U, 1500U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && !transition.accepted);
	result = SG_HostLawPublicationDoorStep(publication,
		SG_HOST_MECHANISM_DOOR_TRIGGER_TOUCH, 0U,
		SG_HOST_MECHANISM_STATE_BOTTOM, 100.0f, 1500U, 1500U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && transition.next_debounce_ms == 2500U);
	result = SG_HostLawPublicationDoorStep(publication,
		SG_HOST_MECHANISM_DOOR_BLOCKED, 0U, SG_HOST_MECHANISM_STATE_DOWN,
		1.0f, 0U, 0U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && transition.reversed &&
		transition.next_state == SG_HOST_MECHANISM_STATE_UP);
	result = SG_HostLawPublicationDoorStep(publication,
		SG_HOST_MECHANISM_DOOR_BLOCKED, SG_HOST_MECHANISM_DOOR_CRUSHER,
		SG_HOST_MECHANISM_STATE_DOWN, 1.0f, 0U, 0U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && !transition.reversed);
	result = SG_HostLawPublicationDoorStep(publication,
		SG_HOST_MECHANISM_DOOR_BLOCKED, 0U, SG_HOST_MECHANISM_STATE_TOP,
		1.0f, 0U, 0U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && !transition.reversed &&
		transition.next_state == SG_HOST_MECHANISM_STATE_TOP);
	result = SG_HostLawPublicationDoorStepEx(publication,
		SG_HOST_MECHANISM_DOOR_BLOCKED, 0U, SG_HOST_MECHANISM_STATE_DOWN,
		1.0f, 0U, 0U, SG_HOST_MECHANISM_BLOCKER_OTHER, 2U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && transition.accepted &&
		transition.damaged && transition.destroyed &&
		transition.damage == SG_HOST_MECHANISM_NONCLIENT_DAMAGE &&
		transition.blocker_kind == SG_HOST_MECHANISM_BLOCKER_OTHER &&
		!transition.reversed);
	result = SG_HostLawPublicationDoorStepEx(publication,
		SG_HOST_MECHANISM_DOOR_BLOCKED, 0U, SG_HOST_MECHANISM_STATE_DOWN,
		1.0f, 0U, 0U, SG_HOST_MECHANISM_BLOCKER_CLIENT, 9U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && transition.damaged &&
		transition.damage == 9U && transition.reversed &&
		transition.blocker_kind == SG_HOST_MECHANISM_BLOCKER_CLIENT &&
		transition.next_state == SG_HOST_MECHANISM_STATE_UP);
	result = SG_HostLawPublicationDoorStepEx(publication,
		SG_HOST_MECHANISM_DOOR_BLOCKED, 0U, SG_HOST_MECHANISM_STATE_DOWN,
		1.0f, 0U, 0U, SG_HOST_MECHANISM_BLOCKER_NONE, 2U, &transition);
	CHECK(result.status == SG_HOST_LAW_EVALUATION_FAILED);
	result = SG_HostLawPublicationPlatformStep(publication,
		SG_HOST_MECHANISM_PLATFORM_TOP, SG_HOST_MECHANISM_STATE_TOP, 200U,
		0U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && transition.next_think_ms == 3200U);
	result = SG_HostLawPublicationPlatformStep(publication,
		SG_HOST_MECHANISM_PLATFORM_TRIGGER_TOUCH,
		SG_HOST_MECHANISM_STATE_TOP, 500U, 0U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && transition.next_think_ms == 1500U);
	result = SG_HostLawPublicationPlatformStep(publication,
		SG_HOST_MECHANISM_PLATFORM_TRIGGER_TOUCH,
		SG_HOST_MECHANISM_STATE_BOTTOM, 500U, 0U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK &&
		transition.next_state == SG_HOST_MECHANISM_STATE_UP);
	result = SG_HostLawPublicationPlatformStepEx(publication,
		SG_HOST_MECHANISM_PLATFORM_BLOCKED, SG_HOST_MECHANISM_STATE_UP,
		500U, 0U, SG_HOST_MECHANISM_BLOCKER_OTHER, 2U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && transition.accepted &&
		transition.damaged && transition.destroyed &&
		transition.damage == SG_HOST_MECHANISM_NONCLIENT_DAMAGE &&
		transition.blocker_kind == SG_HOST_MECHANISM_BLOCKER_OTHER &&
		!transition.reversed);
	result = SG_HostLawPublicationTriggerStep(publication, 0, 0.0f, 100U,
		&transition);
	CHECK(result.status == SG_HOST_LAW_OK && transition.next_think_ms == 300U);
	result = SG_HostLawPublicationTriggerStep(publication, 0, -1.0f, 100U,
		&transition);
	CHECK(result.status == SG_HOST_LAW_OK && transition.next_think_ms == 200U);
	result = SG_HostLawPublicationTrainStep(publication,
		SG_HOST_MECHANISM_TRAIN_BLOCKED, 0U, 0.0f,
		SG_HOST_MECHANISM_STATE_UP, 0, 0,
		SG_HOST_MECHANISM_BLOCKER_CLIENT,
		SG_HOST_MECHANISM_DEFAULT_TRAIN_DAMAGE, 99U, 100U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && !transition.accepted &&
		transition.blocker_kind == SG_HOST_MECHANISM_BLOCKER_CLIENT &&
		transition.damage == SG_HOST_MECHANISM_DEFAULT_TRAIN_DAMAGE);
	result = SG_HostLawPublicationTrainStep(publication,
		SG_HOST_MECHANISM_TRAIN_BLOCKED, 0U, 0.0f,
		SG_HOST_MECHANISM_STATE_UP, 0, 0,
		SG_HOST_MECHANISM_BLOCKER_CLIENT,
		17U, 100U, 100U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && transition.next_debounce_ms == 600U &&
		transition.damaged && transition.damage == 17U);
	result = SG_HostLawPublicationTrainStep(publication,
		SG_HOST_MECHANISM_TRAIN_BLOCKED, 0U, 0.0f,
		SG_HOST_MECHANISM_STATE_UP, 0, 0,
		SG_HOST_MECHANISM_BLOCKER_OTHER, 17U, 700U, 0U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && transition.accepted &&
		transition.damaged && transition.destroyed &&
		transition.blocker_kind == SG_HOST_MECHANISM_BLOCKER_OTHER &&
		transition.damage == SG_HOST_MECHANISM_NONCLIENT_DAMAGE);
	result = SG_HostLawPublicationTrainStep(publication,
		SG_HOST_MECHANISM_TRAIN_WAIT, SG_HOST_MECHANISM_TRAIN_TOGGLE, -1.0f,
		SG_HOST_MECHANISM_STATE_UP, 0, 0,
		SG_HOST_MECHANISM_BLOCKER_NONE, 0U, 0U, 0U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && transition.stopped);
	test_live_door_speed = 100.0f;
	result = SG_HostLawPublicationRevalidateProduction(publication);
	CHECK(result.status == SG_HOST_LAW_PRODUCTION_DRIFT &&
		result.field == SG_HOST_LAW_FIELD_MECHANISM_EQUATIONS);
	test_live_door_speed = 200.0f;
	test_live_rotating_door_speed = 99.0f;
	result = SG_HostLawPublicationRevalidateProduction(publication);
	CHECK(result.status == SG_HOST_LAW_PRODUCTION_DRIFT &&
		result.field == SG_HOST_LAW_FIELD_MECHANISM_EQUATIONS);
	test_live_rotating_door_speed = 100.0f;
	{
		sg_host_law_view_t view = Read(publication);
		view.hook.near_bite_gravity_zero_distance += 1.0f;
		result = SG_HostLawPublicationMatch(publication, &view);
		CHECK(result.status == SG_HOST_LAW_PRODUCTION_DRIFT &&
			result.field == SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY);
	}
	{
		sg_host_law_view_t view = Read(publication);
		view.mechanism.door_default_wait_ms++;
		result = SG_HostLawPublicationMatch(publication, &view);
		CHECK(result.status == SG_HOST_LAW_PRODUCTION_DRIFT &&
			result.field == SG_HOST_LAW_FIELD_MECHANISM_EQUATIONS);
	}
	SG_HostLawPublicationOwnerDestroy(publication);
}

static void TestOwnerFailClosedAndDrift(void)
{
	const sg_host_collision_authority_t *borrowed = NULL;
	const sg_host_law_publication_t *static_publication;
	sg_host_law_result_t result;
	sg_host_law_view_t view;
	sg_host_pmove_request_t pmove_request;
	sg_host_pmove_result_t pmove_result;
	sg_host_pmove_error_t pmove_error;
	sg_host_hook_step_t hook_step;
	sg_host_collision_trace_t trace;
	sg_host_collision_pose_t pose;
	sg_host_law_subject_t subject;
	sg_host_law_runtime_authority_t authority;
	sg_host_law_runtime_authority_t replacement_authority;
	sg_host_pmove_substep_t replay_substeps[4];
	sg_host_pmove_trace_t replay_traces[8];
	sg_host_pmove_replay_workspace_t replay_workspace;
	sg_host_pmove_replay_t replay;
	vec3_t velocity;
	int rope_length = -1;
	const float start[3] = { 0.0f, 0.0f, 0.0f };
	const float end[3] = { 64.0f, 0.0f, 0.0f };
	uint8_t replacement_digest[SG_LEVEL_BSP_SHA256_BYTES];

	SG_HostLawProductionReset();
	result = SG_HostLawProductionRevalidate();
	CHECK(result.status == SG_HOST_LAW_HOST_UNAVAILABLE);
	test_identity_snapshot_status = SG_IDENTITY_OK;
	result = SG_HostLawProductionBeginLevel("packed_map");
	CHECK(result.status == SG_HOST_LAW_HOST_UNAVAILABLE &&
		result.field == SG_HOST_LAW_FIELD_PMOVE_ABI &&
		SG_HostLawProductionPublication() == NULL);
	gi.trace = RuntimeTrace;
	gi.pointcontents = RuntimePointContents;
	gi.Pmove = RuntimePmove;
	InstallRuntimeBot();
	memset(&test_level_identity, 0, sizeof(test_level_identity));
	test_level_identity.bsp_checksum = test_world.engine_checksum;
	test_level_identity.entity_crc32 = UINT32_C(0x12345678);
	test_level_identity.host_physics_id = SG_HOST_PHYSICS_EPOCH;
	test_level_identity.bsp_bytes = test_world.source_size;
	memcpy(test_level_identity.bsp_sha256, test_world.content_identity.bytes,
		sizeof(test_level_identity.bsp_sha256));
	memcpy(test_level_identity.mapname, "packed_map", sizeof("packed_map"));
	result = SG_HostLawProductionEnsureLevel("packed_map");
	static_publication = SG_HostLawProductionStaticPublication();
	CHECK(result.status == SG_HOST_LAW_OK && static_publication != NULL &&
		SG_HostLawProductionPublication() != NULL);
	result = SG_HostLawPublicationRead(static_publication, &view);
	CHECK(result.status == SG_HOST_LAW_OK &&
		view.identity.bsp_content_id == 0U &&
		view.static_identity.engine_checksum == test_world.engine_checksum &&
		view.static_identity.entity_crc32 == UINT32_C(0x12345678) &&
		view.static_identity.host_physics_epoch == SG_HOST_PHYSICS_EPOCH &&
		view.static_identity.bsp_bytes == test_world.source_size &&
			memcmp(view.static_identity.bsp_identity.bytes,
				test_world.content_identity.bytes,
				sizeof(view.static_identity.bsp_identity.bytes)) == 0);
	result = SG_HostLawProductionAcquire(&authority);
	CHECK(result.status == SG_HOST_LAW_OK);
	SG_HostLawProductionReset();
	CHECK(SG_HostLawProductionAuthorityCurrent(&authority).status ==
		SG_HOST_LAW_HOST_UNAVAILABLE);
	result = SG_HostLawProductionBeginLevel("packed_map");
	CHECK(result.status == SG_HOST_LAW_OK);
	result = SG_HostLawProductionAcquire(&replacement_authority);
	CHECK(result.status == SG_HOST_LAW_OK &&
		replacement_authority.epoch != authority.epoch &&
		memcmp(&replacement_authority.view, &authority.view,
			sizeof(authority.view)) == 0 &&
		SG_HostLawProductionAuthorityCurrent(&authority).status ==
			SG_HOST_LAW_PRODUCTION_DRIFT);
	authority = replacement_authority;
	result = SG_HostLawProductionCollisionAuthority(&borrowed);
	CHECK(result.status == SG_HOST_LAW_HOST_UNAVAILABLE && borrowed == NULL);
	memset(&pmove_request, 0, sizeof(pmove_request));
	pmove_request.state.pm_type = PM_NORMAL;
	pmove_request.command.msec = 25U;
	result = SG_HostLawProductionPmove(1U, &pmove_request, &pmove_result,
		&pmove_error);
	CHECK(result.status == SG_HOST_LAW_OK);
	CHECK(pmove_error == SG_HOST_PMOVE_ERROR_NONE);
	CHECK(pmove_result.evaluated_steps == 1U);
	CHECK(pmove_result.elapsed_ms == 25U);
	CHECK(runtime_pmove_calls == 1);
	CHECK(runtime_contents_calls == 1);
	CHECK(runtime_last_pmove_msec == 25U);
	CHECK(SG_HostLawProductionPublication() != NULL);
	result = SG_HostLawProductionEngineTrace(1U, start, NULL, NULL, end,
		SG_HOST_MASK_PLAYER_SOLID, &trace);
	CHECK(result.status == SG_HOST_LAW_OK && trace.fraction == 1.0f &&
		runtime_last_passent == &runtime_edicts[1]);
	result = SG_HostLawProductionRead(&view);
	CHECK(result.status == SG_HOST_LAW_OK &&
		memcmp(view.bsp_identity.bytes, test_world.content_identity.bytes,
			sizeof(view.bsp_identity.bytes)) == 0);
	result = SG_HostLawProductionSubject(&authority, 1U, &subject);
	CHECK(result.status == SG_HOST_LAW_OK && subject.client_id == 1U &&
		subject.spawn_generation == UINT64_C(0x1001));
	CHECK(SG_HostLawProductionSubjectCurrent(&authority, &subject).status ==
		SG_HOST_LAW_OK);
	CHECK(SG_HostLawProductionSubjectClassifyPose(&authority, &subject, start,
		SG_RUNE_STANCE_STANDING, &pose).status == SG_HOST_LAW_OK);
	CHECK(pose.valid && !pose.supported);
	memset(&replay_workspace, 0, sizeof(replay_workspace));
	replay_workspace.substeps = replay_substeps;
	replay_workspace.substep_capacity = 4U;
	replay_workspace.traces = replay_traces;
	replay_workspace.trace_capacity = 8U;
	pmove_request.command.msec = (byte)SG_HOST_ENGINE_FRAME_MS;
	result = SG_HostLawProductionReplayFrame(&authority, &subject,
		&pmove_request,
		&replay_workspace, &replay, &pmove_error);
	CHECK(result.status == SG_HOST_LAW_OK &&
		pmove_error == SG_HOST_PMOVE_ERROR_NONE && replay.substep_count == 4U &&
		replay.trace_count == 4U);
	runtime_clients[1].ctf.ctfid++;
	CHECK(SG_HostLawProductionSubjectCurrent(&authority, &subject).status ==
		SG_HOST_LAW_EVALUATION_FAILED);
	result = SG_HostLawProductionSubjectTrace(&authority, &subject, start,
		NULL, NULL, end,
		SG_HOST_MASK_PLAYER_SOLID, &trace);
	CHECK(result.status == SG_HOST_LAW_EVALUATION_FAILED);
	runtime_clients[1].ctf.ctfid = subject.spawn_generation;
	CHECK(SG_HostLawProductionSubjectCurrent(&authority, &subject).status ==
		SG_HOST_LAW_OK);
	pmove_request.command.msec = 25U;

	runtime_edicts[2].inuse = true;
	runtime_edicts[2].s.number = 2;
	runtime_edicts[2].s.modelindex = 2;
	runtime_edicts[2].classname = "hook";
	runtime_edicts[2].owner = &runtime_edicts[1];
	SetVector(runtime_edicts[2].s.origin, 64.0f, 0.0f, 0.0f);
	runtime_clients[1].hook = &runtime_edicts[2];
	result = SG_HostLawProductionHookFire(1U, 2U, &hook_step);
	CHECK(result.status == SG_HOST_LAW_OK && hook_step.accepted &&
		!hook_step.aborted && !hook_step.collision_hit);
	level.framenum = 20;
	result = SG_HostLawProductionHookTouch(1U, 2U, 0U, 0, &hook_step);
	CHECK(result.status == SG_HOST_LAW_OK && hook_step.accepted &&
		!hook_step.aborted && hook_step.attached);
	runtime_edicts[2].hook_target = &runtime_edicts[0];
	result = SG_HostLawProductionHookTouch(1U, 2U, 0U, 0, &hook_step);
	CHECK(result.status == SG_HOST_LAW_OK && hook_step.accepted &&
		!hook_step.aborted);
	result = SG_HostLawProductionHookPullVelocity(1U, 2U, velocity,
		&rope_length);
	CHECK(result.status == SG_HOST_LAW_OK && rope_length >= 0 &&
		isfinite(velocity[0]) && isfinite(velocity[1]) && isfinite(velocity[2]));

	/* The edict flag alone cannot borrow a retired SG ownership slot. */
	sg_bots[0].active = false;
	result = SG_HostLawProductionPmove(1U, &pmove_request, &pmove_result,
		&pmove_error);
	CHECK(result.status == SG_HOST_LAW_EVALUATION_FAILED &&
		pmove_error == SG_HOST_PMOVE_ERROR_HOST_UNAVAILABLE);
	sg_bots[0].active = true;
	/* Callback drift is recoverable after the one-shot setup latch: EnsureLevel
	 * captures a fresh construction/runtime pair. */
	gi.Pmove = Pmove;
	result = SG_HostLawProductionEnsureLevel("packed_map");
	CHECK(result.status == SG_HOST_LAW_OK &&
		SG_HostLawProductionStaticPublication() != NULL &&
		SG_HostLawProductionPublication() != NULL);
	gi.Pmove = RuntimePmove;
	result = SG_HostLawProductionEnsureLevel("packed_map");
	CHECK(result.status == SG_HOST_LAW_OK &&
		SG_HostLawProductionPublication() != NULL);
	/* Exact bridge drift also rebuilds both views. */
	memcpy(replacement_digest, test_level_identity.bsp_sha256,
		sizeof(replacement_digest));
	test_level_identity.bsp_sha256[0] ^= UINT8_C(1);
	result = SG_HostLawProductionEnsureLevel("packed_map");
	CHECK(result.status == SG_HOST_LAW_OK);
	result = SG_HostLawPublicationRead(
		SG_HostLawProductionStaticPublication(), &view);
	CHECK(result.status == SG_HOST_LAW_OK &&
		view.static_identity.bsp_identity.bytes[0] ==
			test_level_identity.bsp_sha256[0]);
	memcpy(test_level_identity.bsp_sha256, replacement_digest,
		sizeof(test_level_identity.bsp_sha256));
	SG_HostLawProductionReset();
	CHECK(SG_HostLawProductionPublication() == NULL &&
		SG_HostLawProductionStaticPublication() == NULL);
	CHECK(SG_HostLawProductionSubjectCurrent(&authority, &subject).status ==
		SG_HOST_LAW_HOST_UNAVAILABLE);
	runtime_clients[1].ctf.ctfid++;
	result = SG_HostLawProductionBeginLevel("packed_map");
	CHECK(result.status == SG_HOST_LAW_OK &&
		SG_HostLawProductionPublication() != NULL &&
		SG_HostLawProductionStaticPublication() != NULL);
	CHECK(SG_HostLawProductionSubjectCurrent(&authority, &subject).status ==
		SG_HOST_LAW_PRODUCTION_DRIFT);
	result = SG_HostLawProductionAcquire(&authority);
	CHECK(result.status == SG_HOST_LAW_OK);
	result = SG_HostLawProductionSubject(&authority, 1U, &subject);
	CHECK(result.status == SG_HOST_LAW_OK &&
		subject.spawn_generation == runtime_clients[1].ctf.ctfid);
	SG_HostLawProductionReset();
	ClearRuntimeBot();
	gi.trace = NULL;
	gi.pointcontents = NULL;
	gi.Pmove = Pmove;
}

static void TestStaticPublicationRevalidation(void)
{
	construction_bsp_fixture_t fixture = ConstructionValidBsp();
	sg_bsp_world_t *caller_world = ConstructionLoadBsp(&fixture);
	sg_bsp_world_t forged_world;
	sg_bsp_plane_t forged_planes[1];
	sg_host_law_publication_t *publication = NULL;
	sg_host_law_construction_t *construction = NULL;
	sg_host_law_construction_t *forged_construction = NULL;
	sg_host_law_result_t result;
	sg_host_law_view_t view;
	sg_host_law_construction_view_t construction_view;
	sg_host_law_construction_view_t construction_view_copy;
	sg_host_static_identity_t identity =
		ConstructionStaticIdentity(caller_world);
	sg_host_collision_authority_t authority =
		ConstructionAuthority(caller_world);
	sg_host_collision_authority_t forged_authority;
	sg_host_collision_authority_t non_ibsp_authority = Authority();
	sg_host_pmove_request_t request;
	sg_host_pmove_result_t pmove_result;
	sg_host_pmove_error_t pmove_error = SG_HOST_PMOVE_ERROR_NONE;
	sg_host_pmove_substep_t substeps[
		SG_HOST_ENGINE_FRAME_MS / SG_HOST_ENGINE_PMOVE_SUBSTEP_MS];
	sg_host_pmove_trace_t traces[256];
	sg_host_pmove_replay_workspace_t workspace;
	sg_host_pmove_replay_t replay;
	sg_host_collision_trace_t collision_trace;
	sg_host_collision_pose_t pose;
	sg_host_collision_transition_t transition;
	sg_host_collision_contents_t contents = 0U;
	sg_configuration_space_t *configuration = NULL;
	sg_configuration_limits_t configuration_limits;
	sg_configuration_error_t configuration_error;
	sg_configuration_audit_result_t configuration_audit;
	sg_configuration_audit_result_t baseline_configuration_audit;
	sg_configuration_semantics_t *semantics = NULL;
	sg_configuration_semantics_limits_t semantics_limits;
	sg_configuration_semantics_error_t semantics_error;
	sg_configuration_semantics_audit_result_t semantics_audit;
	sg_configuration_semantics_audit_result_t baseline_semantics_audit;
	sg_bsp_completeness_result_t completeness;
	sg_bsp_completeness_result_t baseline_completeness;
	sg_bsp_completeness_result_t direct_completeness;
	const float zero[3] = { 0.0f, 0.0f, 0.0f };
	const float trace_start[3] = { 0.0f, 0.0f, -8.0f };
	const float trace_end[3] = { 0.0f, 0.0f, 8.0f };
	uint64_t replay_bsp_id;
	uint32_t forged_field;

	non_ibsp_authority.identity.physics_abi_id = SG_HOST_ENGINE_PMOVE_ABI_ID;

	result = SG_HostLawPublicationOwnerIssueStatic(&identity, &publication);
	CHECK(result.status == SG_HOST_LAW_OK && publication != NULL);
	result = SG_HostLawPublicationRead(publication, &view);
	CHECK(result.status == SG_HOST_LAW_OK &&
		view.bsp_bytes == caller_world->source_size &&
		view.identity.bsp_content_id == 0U &&
		view.static_identity.entity_crc32 == identity.entity_crc32 &&
		memcmp(view.bsp_identity.bytes, caller_world->content_identity.bytes,
			sizeof(view.bsp_identity.bytes)) == 0);
	result = SG_HostLawPublicationRevalidateProduction(publication);
	CHECK(result.status == SG_HOST_LAW_OK);
	memset(&request, 0, sizeof(request));
	result = SG_HostLawPublicationPmove(publication, NULL, &request,
		&pmove_result, &pmove_error);
	CHECK(result.status == SG_HOST_LAW_HOST_UNAVAILABLE &&
		pmove_error == SG_HOST_PMOVE_ERROR_HOST_UNAVAILABLE);

	/* Source bytes alone do not authenticate separately supplied arrays. */
	CHECK(caller_world->plane_count == 1U);
	forged_world = *caller_world;
	forged_planes[0] = caller_world->planes[0];
	forged_planes[0].distance += 1.0f;
	forged_world.planes = forged_planes;
	forged_authority = ConstructionAuthority(&forged_world);
	result = SG_HostLawPublicationOwnerConstructionIssue(publication,
		&forged_authority, &forged_construction);
	CHECK(result.status == SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW &&
		result.field == SG_HOST_LAW_FIELD_COLLISION_LAW &&
		forged_construction == NULL);
	result = SG_HostLawPublicationOwnerConstructionIssue(publication,
		&non_ibsp_authority, &forged_construction);
	CHECK(result.status == SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW &&
		result.field == SG_HOST_LAW_FIELD_BSP_CONTENT &&
		forged_construction == NULL);

	result = SG_HostLawPublicationOwnerConstructionIssue(publication, &authority,
		&construction);
	CHECK(result.status == SG_HOST_LAW_OK && construction != NULL);
	memset(&construction_view, 0xa5, sizeof(construction_view));
	result = SG_HostLawConstructionRead(construction, &construction_view);
	CHECK(result.status == SG_HOST_LAW_OK && construction_view.current == 1U &&
		construction_view.level_generation != 0U &&
		memcmp(&construction_view.host_static_identity, &identity,
			sizeof(identity)) == 0 &&
		memcmp(construction_view.geometry.bsp_identity.bytes,
			caller_world->content_identity.bytes,
			sizeof(construction_view.geometry.bsp_identity.bytes)) == 0 &&
		construction_view.geometry.bsp_bytes == caller_world->source_size &&
		construction_view.geometry.engine_checksum ==
			caller_world->engine_checksum &&
		construction_view.geometry.plane_count == caller_world->plane_count &&
		construction_view.geometry.node_count == caller_world->node_count &&
		construction_view.geometry.model_count == caller_world->model_count &&
		construction_view.geometry.brush_count == caller_world->brush_count &&
		construction_view.geometry.brush_side_count ==
			caller_world->brush_side_count &&
		construction_view.laws.collision_law_id == view.collision_law_id &&
		construction_view.laws.pmove_law_id == view.pmove_law_id &&
		construction_view.laws.pmove_abi.identity ==
			SG_HOST_ENGINE_PMOVE_ABI_ID &&
		construction_view.laws.identity.bsp_content_id == 0U &&
		construction_view.laws.identity.entity_semantics_id == 0U &&
		construction_view.laws.identity.source_set_identity == 0U &&
		construction_view.laws.identity.schema_id == 0U &&
		construction_view.laws.identity.producer_identity == 0U &&
		memcmp(&construction_view.laws.static_identity, &identity,
			sizeof(identity)) == 0);
	construction_view_copy = construction_view;
	/* Read returns copies only.  Mutating every nested metadata family cannot
	 * reach the handle-owned parse or sealed law state. */
	construction_view.host_static_identity.bsp_identity.bytes[0] ^= UINT8_C(1);
	construction_view.host_static_identity.physics.gravity += 1.0f;
	construction_view.geometry.engine_checksum ^= UINT32_C(1);
	construction_view.geometry.plane_count += 1U;
	construction_view.laws.collision_law_id ^= UINT64_C(1);
	memset(&construction_view, 0xa5, sizeof(construction_view));
	result = SG_HostLawConstructionRead(construction, &construction_view);
	CHECK(result.status == SG_HOST_LAW_OK &&
		memcmp(&construction_view, &construction_view_copy,
			sizeof(construction_view)) == 0);
	memset(&collision_trace, 0, sizeof(collision_trace));
	result = SG_HostLawConstructionCollisionTrace(construction, NULL,
		trace_start, zero, zero, trace_end, SG_HOST_CONTENTS_SOLID,
		&collision_trace);
	CHECK(result.status == SG_HOST_LAW_OK);
	result = SG_HostLawConstructionPointContents(construction, NULL,
		trace_start, &contents);
	CHECK(result.status == SG_HOST_LAW_OK);
	memset(&pose, 0, sizeof(pose));
	result = SG_HostLawConstructionClassifyPose(construction, NULL,
		trace_start, SG_RUNE_STANCE_STANDING, &pose);
	CHECK(result.status == SG_HOST_LAW_OK &&
		pose.physics_abi_id == SG_HOST_ENGINE_PMOVE_ABI_ID);
	memset(&transition, 0, sizeof(transition));
	result = SG_HostLawConstructionTransition(construction, NULL,
		trace_start, trace_start, SG_RUNE_STANCE_STANDING, &transition);
	CHECK(result.status == SG_HOST_LAW_OK);
	SG_ConfigurationDefaultLimits(&configuration_limits);
	memset(&configuration_error, 0, sizeof(configuration_error));
	CHECK(SG_ConfigurationBuild(&authority, &configuration_limits,
		&configuration, &configuration_error));
	if (configuration)
	{
		memset(&configuration_audit, 0, sizeof(configuration_audit));
		result = SG_HostLawConstructionConfigurationAudit(construction,
			configuration, &configuration_audit);
		CHECK(result.status == SG_HOST_LAW_OK &&
			configuration_audit.code == SG_CONFIGURATION_AUDIT_OK);
		baseline_configuration_audit = configuration_audit;
		configuration->identity.physics.gravity += 1.0f;
		result = SG_HostLawConstructionConfigurationAudit(construction,
			configuration, &configuration_audit);
		CHECK(result.status == SG_HOST_LAW_EVALUATION_FAILED);
		configuration->identity.physics.gravity -= 1.0f;
		memset(&direct_completeness, 0, sizeof(direct_completeness));
		CHECK(!SG_BspCompletenessProve(&authority, configuration,
			&direct_completeness));
		memset(&completeness, 0, sizeof(completeness));
		result = SG_HostLawConstructionCompletenessProve(construction,
			configuration, &completeness);
		CHECK(result.status == SG_HOST_LAW_EVALUATION_FAILED &&
			result.observed_bits == (uint64_t)direct_completeness.code &&
			memcmp(&completeness, &direct_completeness,
				sizeof(completeness)) == 0);
		baseline_completeness = completeness;
		SG_ConfigurationSemanticsDefaultLimits(&semantics_limits);
		memset(&semantics_error, 0, sizeof(semantics_error));
		CHECK(SG_ConfigurationSemanticsBuild(&authority, configuration,
			&semantics_limits, &semantics, &semantics_error));
		if (semantics)
		{
			memset(&semantics_audit, 0, sizeof(semantics_audit));
			result = SG_HostLawConstructionSemanticsAudit(construction,
				configuration, semantics, &semantics_audit);
			CHECK(result.status == SG_HOST_LAW_OK &&
				semantics_audit.code ==
					SG_CONFIGURATION_SEMANTICS_AUDIT_OK);
			baseline_semantics_audit = semantics_audit;
			/* Complete-model labels are carried only so a downstream artifact
			 * can be checked against itself.  They do not alter any host-static
			 * audit or completeness result. */
			for (forged_field = 0U; forged_field < 5U; forged_field++)
			{
				sg_rune_model_identity_t saved_configuration =
					configuration->identity;
				sg_rune_model_identity_t saved_semantics = semantics->identity;

				switch (forged_field)
				{
				case 0U:
					configuration->identity.bsp_content_id ^= UINT64_C(0x10);
					semantics->identity.bsp_content_id ^= UINT64_C(0x10);
					break;
				case 1U:
					configuration->identity.entity_semantics_id ^= UINT64_C(0x20);
					semantics->identity.entity_semantics_id ^= UINT64_C(0x20);
					break;
				case 2U:
					configuration->identity.source_set_identity ^= UINT64_C(0x40);
					semantics->identity.source_set_identity ^= UINT64_C(0x40);
					break;
				case 3U:
					configuration->identity.schema_id ^= UINT64_C(0x80);
					semantics->identity.schema_id ^= UINT64_C(0x80);
					break;
				default:
					configuration->identity.producer_identity ^= UINT64_C(0x100);
					semantics->identity.producer_identity ^= UINT64_C(0x100);
					break;
				}
				result = SG_HostLawConstructionConfigurationAudit(construction,
					configuration, &configuration_audit);
				CHECK(result.status == SG_HOST_LAW_OK && memcmp(
					&configuration_audit, &baseline_configuration_audit,
					sizeof(configuration_audit)) == 0);
				result = SG_HostLawConstructionSemanticsAudit(construction,
					configuration, semantics, &semantics_audit);
				CHECK(result.status == SG_HOST_LAW_OK && memcmp(&semantics_audit,
					&baseline_semantics_audit, sizeof(semantics_audit)) == 0);
				result = SG_HostLawConstructionCompletenessProve(construction,
					configuration, &completeness);
				CHECK(result.status == SG_HOST_LAW_EVALUATION_FAILED && memcmp(
					&completeness, &baseline_completeness,
					sizeof(completeness)) == 0);
				configuration->identity = saved_configuration;
				semantics->identity = saved_semantics;
			}
		}
	}
	request.state.pm_type = PM_NORMAL;
	request.state.origin[2] = -512;
	request.state.gravity = 800;
	request.previous_state = request.state;
	result = SG_HostLawConstructionPmove(construction, NULL, &request,
		&pmove_result, &pmove_error);
	CHECK(result.status == SG_HOST_LAW_OK &&
		pmove_error == SG_HOST_PMOVE_ERROR_NONE &&
		pmove_result.physics_abi_id == SG_HOST_ENGINE_PMOVE_ABI_ID &&
		pmove_result.gravity == 800.0f);
	memset(&workspace, 0, sizeof(workspace));
	workspace.substeps = substeps;
	workspace.substep_capacity = sizeof(substeps) / sizeof(substeps[0]);
	workspace.traces = traces;
	workspace.trace_capacity = sizeof(traces) / sizeof(traces[0]);
	memset(&replay, 0, sizeof(replay));
	result = SG_HostLawConstructionReplayFrame(construction, NULL, &request,
		&workspace, &replay, &pmove_error);
	CHECK(result.status == SG_HOST_LAW_OK &&
		pmove_error == SG_HOST_PMOVE_ERROR_NONE &&
		replay.substeps == substeps && replay.substep_count ==
			SG_HOST_ENGINE_FRAME_MS / SG_HOST_ENGINE_PMOVE_SUBSTEP_MS &&
		replay.traces == traces && replay.trace_count != 0U &&
		replay.substeps[replay.substep_count - 1U].elapsed_ms ==
			SG_HOST_ENGINE_FRAME_MS &&
		replay.physics_abi_id == SG_HOST_ENGINE_PMOVE_ABI_ID);
	replay_bsp_id = replay.bsp_content_id;
	CHECK(replay_bsp_id != 0U && replay_bsp_id != UINT64_MAX &&
		replay_bsp_id != authority.identity.bsp_content_id);

	/* Downstream complete-model labels are not host-law authority.  Every
	 * formerly unchecked label is ignored, while authenticated static output
	 * and replay identity remain invariant. */
	for (forged_field = 0U; forged_field < 5U; forged_field++)
	{
		sg_host_collision_authority_t labeled_authority = authority;
		sg_host_law_construction_t *labeled = NULL;
		sg_host_law_construction_view_t labeled_view;
		sg_host_pmove_replay_t labeled_replay;

		switch (forged_field)
		{
		case 0U: labeled_authority.identity.bsp_content_id ^= UINT64_C(0x10); break;
		case 1U: labeled_authority.identity.entity_semantics_id ^= UINT64_C(0x20); break;
		case 2U: labeled_authority.identity.source_set_identity ^= UINT64_C(0x40); break;
		case 3U: labeled_authority.identity.schema_id ^= UINT64_C(0x80); break;
		default: labeled_authority.identity.producer_identity ^= UINT64_C(0x100); break;
		}
		result = SG_HostLawPublicationOwnerConstructionIssue(publication,
			&labeled_authority, &labeled);
		CHECK(result.status == SG_HOST_LAW_OK && labeled != NULL);
		memset(&labeled_view, 0, sizeof(labeled_view));
		result = SG_HostLawConstructionRead(labeled, &labeled_view);
		CHECK(result.status == SG_HOST_LAW_OK &&
			memcmp(&labeled_view.host_static_identity,
				&construction_view_copy.host_static_identity,
				sizeof(labeled_view.host_static_identity)) == 0 &&
			memcmp(&labeled_view.geometry, &construction_view_copy.geometry,
				sizeof(labeled_view.geometry)) == 0);
		memset(&labeled_replay, 0, sizeof(labeled_replay));
		result = SG_HostLawConstructionReplayFrame(labeled, NULL, &request,
			&workspace, &labeled_replay, &pmove_error);
		CHECK(result.status == SG_HOST_LAW_OK &&
			labeled_replay.bsp_content_id == replay_bsp_id &&
			labeled_replay.physics_abi_id == replay.physics_abi_id);
		SG_HostLawConstructionDestroy(labeled);
	}

	/* The handle owns the accepted parse, so caller teardown is harmless. */
	SG_BspWorldDestroy(caller_world);
	caller_world = NULL;
	result = SG_HostLawConstructionPmove(construction, NULL, &request,
		&pmove_result, &pmove_error);
	CHECK(result.status == SG_HOST_LAW_OK &&
		pmove_error == SG_HOST_PMOVE_ERROR_NONE);
	result = SG_HostLawConstructionCollisionTrace(construction, NULL,
		trace_start, zero, zero, trace_end, SG_HOST_CONTENTS_SOLID,
		&collision_trace);
	CHECK(result.status == SG_HOST_LAW_OK);

	gravity_cvar.value = 801.0f;
	result = SG_HostLawConstructionPmove(construction, NULL, &request,
		&pmove_result, &pmove_error);
	CHECK(result.status == SG_HOST_LAW_PRODUCTION_DRIFT &&
		result.field == SG_HOST_LAW_FIELD_GRAVITY);
	result = SG_HostLawPublicationRevalidateProduction(publication);
	CHECK(result.status == SG_HOST_LAW_PRODUCTION_DRIFT &&
		result.field == SG_HOST_LAW_FIELD_GRAVITY);
	gravity_cvar.value = 800.0f;

	/* Owner teardown revokes the shared epoch without freeing handle storage. */
	SG_HostLawPublicationOwnerDestroy(publication);
	publication = NULL;
	result = SG_HostLawConstructionCurrent(construction);
	CHECK(result.status == SG_HOST_LAW_PRODUCTION_DRIFT &&
		result.field == SG_HOST_LAW_FIELD_BSP_CONTENT);
	memset(&construction_view, 0xa5, sizeof(construction_view));
	result = SG_HostLawConstructionRead(construction, &construction_view);
	CHECK(result.status == SG_HOST_LAW_PRODUCTION_DRIFT &&
		construction_view.current == 0U &&
		construction_view.geometry.bsp_bytes == 0U &&
		construction_view.host_static_identity.bsp_bytes == 0U);
	result = SG_HostLawConstructionReplayFrame(construction, NULL, &request,
		&workspace, &replay, &pmove_error);
	CHECK(result.status == SG_HOST_LAW_PRODUCTION_DRIFT);
	result = SG_HostLawConstructionCollisionTrace(construction, NULL,
		trace_start, zero, zero, trace_end, SG_HOST_CONTENTS_SOLID,
		&collision_trace);
	CHECK(result.status == SG_HOST_LAW_PRODUCTION_DRIFT);
	result = SG_HostLawConstructionConfigurationAudit(construction,
		configuration, &configuration_audit);
	CHECK(result.status == SG_HOST_LAW_PRODUCTION_DRIFT);
	SG_HostLawConstructionDestroy(construction);
	SG_ConfigurationSemanticsDestroy(semantics);
	SG_ConfigurationDestroy(configuration);
	SG_BspWorldDestroy(caller_world);
}

static void TestProductionConstructionLifetime(void)
{
	construction_bsp_fixture_t fixture = ConstructionValidBsp();
	sg_bsp_world_t *caller_world = ConstructionLoadBsp(&fixture);
	sg_host_collision_authority_t authority =
		ConstructionAuthority(caller_world);
	sg_host_law_construction_t *first = NULL;
	sg_host_law_construction_t *second = NULL;
	sg_host_law_construction_view_t first_view;
	sg_host_law_construction_view_t second_view;
	sg_host_law_result_t result;
	sg_host_pmove_request_t request;
	sg_host_pmove_result_t pmove_result;
	sg_host_pmove_error_t pmove_error = SG_HOST_PMOVE_ERROR_NONE;

	SG_HostLawProductionReset();
	result = SG_HostLawProductionConstructionIssue(&authority, &first);
	CHECK(result.status == SG_HOST_LAW_HOST_UNAVAILABLE && first == NULL);
	gi.trace = RuntimeTrace;
	gi.pointcontents = RuntimePointContents;
	gi.Pmove = RuntimePmove;
	test_identity_snapshot_status = SG_IDENTITY_OK;
	ConfigureConstructionLevel(caller_world, "construction_a");
	result = SG_HostLawProductionBeginLevel("construction_a");
	CHECK(result.status == SG_HOST_LAW_OK);
	result = SG_HostLawProductionConstructionIssue(&authority, &first);
	CHECK(result.status == SG_HOST_LAW_OK && first != NULL);
	memset(&first_view, 0, sizeof(first_view));
	result = SG_HostLawConstructionRead(first, &first_view);
	CHECK(result.status == SG_HOST_LAW_OK && first_view.current == 1U &&
		first_view.geometry.bsp_bytes == caller_world->source_size &&
		first_view.level_generation != 0U);

	ConfigureConstructionLevel(caller_world, "construction_b");
	result = SG_HostLawProductionBeginLevel("construction_b");
	CHECK(result.status == SG_HOST_LAW_OK);
	result = SG_HostLawConstructionCurrent(first);
	CHECK(result.status == SG_HOST_LAW_PRODUCTION_DRIFT &&
		result.field == SG_HOST_LAW_FIELD_BSP_CONTENT);
	result = SG_HostLawProductionConstructionIssue(&authority, &second);
	CHECK(result.status == SG_HOST_LAW_OK && second != NULL);
	memset(&second_view, 0, sizeof(second_view));
	result = SG_HostLawConstructionRead(second, &second_view);
	CHECK(result.status == SG_HOST_LAW_OK && second_view.current == 1U &&
		second_view.level_generation != first_view.level_generation);

	memset(&request, 0, sizeof(request));
	request.state.pm_type = PM_NORMAL;
	request.state.origin[2] = -512;
	request.state.gravity = 800;
	request.previous_state = request.state;
	result = SG_HostLawConstructionPmove(second, NULL, &request,
		&pmove_result, &pmove_error);
	CHECK(result.status == SG_HOST_LAW_OK &&
		pmove_error == SG_HOST_PMOVE_ERROR_NONE);
	SG_HostLawProductionReset();
	result = SG_HostLawConstructionCurrent(second);
	CHECK(result.status == SG_HOST_LAW_PRODUCTION_DRIFT &&
		result.field == SG_HOST_LAW_FIELD_BSP_CONTENT);
	result = SG_HostLawConstructionPmove(second, NULL, &request,
		&pmove_result, &pmove_error);
	CHECK(result.status == SG_HOST_LAW_PRODUCTION_DRIFT);

	SG_HostLawConstructionDestroy(second);
	SG_HostLawConstructionDestroy(first);
	SG_BspWorldDestroy(caller_world);
	test_identity_snapshot_status = SG_IDENTITY_UNAVAILABLE;
	gi.trace = NULL;
	gi.pointcontents = NULL;
	gi.Pmove = Pmove;
}

int main(void)
{
	memset(&gi, 0, sizeof(gi));
	memset(&sg_host, 0, sizeof(sg_host));
	memset(&gravity_cvar, 0, sizeof(gravity_cvar));
	memset(&maxvelocity_cvar, 0, sizeof(maxvelocity_cvar));
	memset(&funky_gravity_cvar, 0, sizeof(funky_gravity_cvar));
	memset(&airaccelerate_cvar, 0, sizeof(airaccelerate_cvar));
	memset(&ctf_flags_cvar, 0, sizeof(ctf_flags_cvar));
	gravity_cvar.value = 800.0f;
	maxvelocity_cvar.value = 2000.0f;
	airaccelerate_cvar.value = 0.0f;
	sv_gravity = &gravity_cvar;
	sv_maxvelocity = &maxvelocity_cvar;
	want_funky_gravity = &funky_gravity_cvar;
	ctfflags = &ctf_flags_cvar;
	gi.Pmove = Pmove;
	gi.cvar = TestCvar;
	InitializeWorld();
	TestEngineChecksumVectors();

	TestEngineBindingAndParity();
	TestEngineRuntimeOwnerBinding();
	TestPublicationAndCallbackIsolation();
	TestPmoveAndCollisionExecution();
	TestHookChronology();
	TestHookDamagePolicy();
	TestMechanismEquations();
	TestStaticPublicationRevalidation();
	TestProductionConstructionLifetime();
	TestOwnerFailClosedAndDrift();
	SG_HostLawProductionReset();
	if (failures != 0)
		return 1;
	puts("host-law publication tests passed");
	return 0;
}
