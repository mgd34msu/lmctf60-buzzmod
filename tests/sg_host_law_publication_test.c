#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../g_local.h"
#include "../g_ctffunc.h"
#undef world
#ifndef q_exported
#define q_exported
#endif
#include "../slipgate/sg_host_law_publication.h"
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
	result = SG_HostLawProductionSubject(1U, &subject);
	CHECK(result.status == SG_HOST_LAW_OK && subject.client_id == 1U &&
		subject.spawn_generation == UINT64_C(0x1001));
	CHECK(SG_HostLawProductionSubjectCurrent(&subject).status ==
		SG_HOST_LAW_OK);
	CHECK(SG_HostLawProductionSubjectClassifyPose(&subject, start,
		SG_RUNE_STANCE_STANDING, &pose).status == SG_HOST_LAW_OK);
	CHECK(pose.valid && !pose.supported);
	memset(&replay_workspace, 0, sizeof(replay_workspace));
	replay_workspace.substeps = replay_substeps;
	replay_workspace.substep_capacity = 4U;
	replay_workspace.traces = replay_traces;
	replay_workspace.trace_capacity = 8U;
	pmove_request.command.msec = (byte)SG_HOST_ENGINE_FRAME_MS;
	result = SG_HostLawProductionReplayFrame(&subject, &pmove_request,
		&replay_workspace, &replay, &pmove_error);
	CHECK(result.status == SG_HOST_LAW_OK &&
		pmove_error == SG_HOST_PMOVE_ERROR_NONE && replay.substep_count == 4U &&
		replay.trace_count == 4U);
	runtime_clients[1].ctf.ctfid++;
	CHECK(SG_HostLawProductionSubjectCurrent(&subject).status ==
		SG_HOST_LAW_EVALUATION_FAILED);
	result = SG_HostLawProductionSubjectTrace(&subject, start, NULL, NULL, end,
		SG_HOST_MASK_PLAYER_SOLID, &trace);
	CHECK(result.status == SG_HOST_LAW_EVALUATION_FAILED);
	runtime_clients[1].ctf.ctfid = subject.spawn_generation;
	CHECK(SG_HostLawProductionSubjectCurrent(&subject).status ==
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
	CHECK(SG_HostLawProductionSubjectCurrent(&subject).status ==
		SG_HOST_LAW_HOST_UNAVAILABLE);
	runtime_clients[1].ctf.ctfid++;
	result = SG_HostLawProductionBeginLevel("packed_map");
	CHECK(result.status == SG_HOST_LAW_OK &&
		SG_HostLawProductionPublication() != NULL &&
		SG_HostLawProductionStaticPublication() != NULL);
	CHECK(SG_HostLawProductionSubjectCurrent(&subject).status ==
		SG_HOST_LAW_EVALUATION_FAILED);
	result = SG_HostLawProductionSubject(1U, &subject);
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
	sg_host_law_publication_t *publication = NULL;
	sg_host_law_result_t result;
	sg_host_law_view_t view;
	sg_host_static_identity_t identity = StaticIdentity();
	sg_host_pmove_request_t request;
	sg_host_pmove_result_t pmove_result;
	sg_host_pmove_error_t pmove_error = SG_HOST_PMOVE_ERROR_NONE;

	result = SG_HostLawPublicationOwnerIssueStatic(&identity, &publication);
	CHECK(result.status == SG_HOST_LAW_OK && publication != NULL);
	result = SG_HostLawPublicationRead(publication, &view);
	CHECK(result.status == SG_HOST_LAW_OK &&
		view.bsp_bytes == test_world.source_size &&
		view.identity.bsp_content_id == 0U &&
		view.static_identity.entity_crc32 == identity.entity_crc32 &&
		memcmp(view.bsp_identity.bytes, test_world.content_identity.bytes,
			sizeof(view.bsp_identity.bytes)) == 0);
	result = SG_HostLawPublicationRevalidateProduction(publication);
	CHECK(result.status == SG_HOST_LAW_OK);
	memset(&request, 0, sizeof(request));
	result = SG_HostLawPublicationPmove(publication, NULL, &request,
		&pmove_result, &pmove_error);
	CHECK(result.status == SG_HOST_LAW_HOST_UNAVAILABLE &&
		pmove_error == SG_HOST_PMOVE_ERROR_HOST_UNAVAILABLE);
	gravity_cvar.value = 801.0f;
	result = SG_HostLawPublicationRevalidateProduction(publication);
	CHECK(result.status == SG_HOST_LAW_PRODUCTION_DRIFT &&
		result.field == SG_HOST_LAW_FIELD_GRAVITY);
	gravity_cvar.value = 800.0f;
	SG_HostLawPublicationOwnerDestroy(publication);
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
	TestOwnerFailClosedAndDrift();
	SG_HostLawProductionReset();
	if (failures != 0)
		return 1;
	puts("host-law publication tests passed");
	return 0;
}
