#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../g_local.h"
#include "../g_ctffunc.h"
#undef world
#include "../slipgate/sg_host_collision.h"
#include "../slipgate/sg_identity.h"
#include "../slipgate/sg_local.h"
#include "../slipgate/sg_bot.h"
#include "../slipgate/sg_rune_compact_pmove_control_build_private.h"
#include "../slipgate/sg_tactic_pmove_control_runtime_private.h"

game_import_t gi;
game_export_t globals;
level_locals_t level;
sg_bot_t sg_bots[SG_MAXBOTS];
cvar_t *sv_gravity;
cvar_t *sv_maxvelocity;
cvar_t *want_funky_gravity;
cvar_t *ctfflags;

static cvar_t gravity_cvar;
static cvar_t maxvelocity_cvar;
static cvar_t funky_cvar;
static cvar_t airaccelerate_cvar;
static sg_level_identity_t level_identity;
static sg_host_collision_authority_t collision;
static sg_host_collision_scene_t collision_scene;
static sg_host_collision_instance_t collision_instance;
static edict_t edicts[4];
static gclient_t clients[4];
static csurface_t surface;
static int failures;

extern void Pmove(pmove_t *pmove);

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct fixture_s
{
	sg_bsp_world_t bsp;
	sg_bsp_plane_t planes[12];
	sg_bsp_node_t nodes[12];
	sg_bsp_leaf_t leaves[14];
	uint32_t leaf_brushes[2];
	sg_bsp_model_t models[2];
	sg_bsp_brush_t brushes[2];
	sg_bsp_brush_side_t brush_sides[12];
	sg_bsp_texinfo_t texinfos[2];
} fixture_t;

static fixture_t fixture;

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

static cvar_t *RuntimeCvar(char *name, char *value, int flags)
{
	(void)value;
	(void)flags;
	return strcmp(name, "sv_airaccelerate") == 0 ? &airaccelerate_cvar : NULL;
}

sg_identity_status_t SG_LevelIdentitySnapshot(const char *expected_mapname,
	sg_level_identity_t *out)
{
	if (!expected_mapname || !out || strcmp(expected_mapname, "corridor") != 0)
		return SG_IDENTITY_INVALID_ARGUMENT;
	*out = level_identity;
	return SG_IDENTITY_OK;
}

qboolean ctf_validateplayer(edict_t *entity, int teamnum_wanted)
{
	(void)teamnum_wanted;
	return entity && entity->inuse && entity->client;
}

void CTF_HookMuzzle(const vec3_t origin, float viewheight, int hand,
	const vec3_t forward, const vec3_t right, vec3_t start)
{
	(void)viewheight;
	(void)hand;
	(void)forward;
	(void)right;
	VectorCopy(origin, start);
}

static void SetVector(float value[3], float x, float y, float z)
{
	value[0] = x;
	value[1] = y;
	value[2] = z;
}

static void SetPlane(sg_bsp_plane_t *plane, float x, float y, float z,
	float distance)
{
	SetVector(plane->normal.value, x, y, z);
	plane->distance = distance;
	plane->type = x == 1.0f ? 0 : (y == 1.0f ? 1 : (z == 1.0f ? 2 : 3));
}

static void AddBox(uint32_t model, uint32_t first_plane,
	uint32_t first_node, uint32_t first_leaf, uint32_t brush,
	const float mins[3], const float maxs[3])
{
	uint32_t side;
	uint32_t inside_leaf = first_leaf + 6U;

	SetPlane(&fixture.planes[first_plane + 0U], 1, 0, 0, maxs[0]);
	SetPlane(&fixture.planes[first_plane + 1U], -1, 0, 0, -mins[0]);
	SetPlane(&fixture.planes[first_plane + 2U], 0, 1, 0, maxs[1]);
	SetPlane(&fixture.planes[first_plane + 3U], 0, -1, 0, -mins[1]);
	SetPlane(&fixture.planes[first_plane + 4U], 0, 0, 1, maxs[2]);
	SetPlane(&fixture.planes[first_plane + 5U], 0, 0, -1, -mins[2]);
	for (side = 0U; side < 6U; side++)
	{
		sg_bsp_node_t *node = &fixture.nodes[first_node + side];

		node->plane = first_plane + side;
		node->children[0] = -1 - (int32_t)(first_leaf + side);
		node->children[1] = side == 5U ? -1 - (int32_t)inside_leaf :
			(int32_t)(first_node + side + 1U);
		fixture.brush_sides[brush * 6U + side].plane = first_plane + side;
		fixture.brush_sides[brush * 6U + side].texinfo = (int32_t)brush;
	}
	fixture.leaves[inside_leaf].contents = SG_HOST_CONTENTS_SOLID;
	fixture.leaves[inside_leaf].first_leaf_brush = brush;
	fixture.leaves[inside_leaf].leaf_brush_count = 1;
	fixture.leaf_brushes[brush] = brush;
	fixture.brushes[brush].first_side = brush * 6U;
	fixture.brushes[brush].side_count = 6;
	fixture.brushes[brush].contents = SG_HOST_CONTENTS_SOLID;
	fixture.models[model].headnode = (int32_t)first_node;
	SetVector(fixture.models[model].mins.value,
		mins[0] - 1.0f, mins[1] - 1.0f, mins[2] - 1.0f);
	SetVector(fixture.models[model].maxs.value,
		maxs[0] + 1.0f, maxs[1] + 1.0f, maxs[2] + 1.0f);
}

static sg_rune_model_identity_t CollisionIdentity(void)
{
	sg_rune_model_identity_t identity;

	memset(&identity, 0, sizeof(identity));
	identity.bsp_content_id = UINT64_C(0x2002);
	identity.physics_abi_id = SG_HOST_ENGINE_PMOVE_ABI_ID;
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
	identity.physics.frame_ms = 100U;
	identity.physics.substep_ms = 25U;
	return identity;
}

static void BuildCollisionFixture(void)
{
	const float floor_mins[3] = { -4096.0f, -1024.0f, -100.0f };
	const float floor_maxs[3] = { 4096.0f, 1024.0f, -24.1f };
	const float blocker_mins[3] = { 20.0f, -24.0f, -24.0f };
	const float blocker_maxs[3] = { 28.0f, 24.0f, 48.0f };
	sg_rune_model_identity_t identity = CollisionIdentity();
	sg_host_collision_error_t error;

	memset(&fixture, 0, sizeof(fixture));
	fixture.bsp.planes = fixture.planes;
	fixture.bsp.plane_count = 12U;
	fixture.bsp.nodes = fixture.nodes;
	fixture.bsp.node_count = 12U;
	fixture.bsp.leaves = fixture.leaves;
	fixture.bsp.leaf_count = 14U;
	fixture.bsp.models = fixture.models;
	fixture.bsp.model_count = 2U;
	fixture.bsp.brushes = fixture.brushes;
	fixture.bsp.brush_count = 2U;
	fixture.bsp.brush_sides = fixture.brush_sides;
	fixture.bsp.brush_side_count = 12U;
	fixture.bsp.leaf_brushes = fixture.leaf_brushes;
	fixture.bsp.leaf_brush_count = 2U;
	fixture.bsp.texinfos = fixture.texinfos;
	fixture.bsp.texinfo_count = 2U;
	AddBox(0U, 0U, 0U, 0U, 0U, floor_mins, floor_maxs);
	AddBox(1U, 6U, 6U, 7U, 1U, blocker_mins, blocker_maxs);
	memset(&collision, 0, sizeof(collision));
	CHECK(SG_HostCollisionInit(&collision, &fixture.bsp, &identity, &error));
	memset(&collision_instance, 0, sizeof(collision_instance));
	collision_instance.instance_id = 2U;
	collision_instance.model_index = 1U;
	collision_scene.instances = NULL;
	collision_scene.instance_count = 0U;
}

static trace_t RuntimeTrace(vec3_t start, vec3_t mins, vec3_t maxs,
	vec3_t end, edict_t *passent, int mask)
{
	sg_host_collision_trace_t source;
	trace_t result;

	(void)passent;
	memset(&result, 0, sizeof(result));
	if (!SG_HostCollisionTrace(&collision, &collision_scene, start, mins, maxs,
		end, (sg_host_collision_contents_t)mask, &source))
	{
		result.allsolid = true;
		return result;
	}
	result.allsolid = source.allsolid ? true : false;
	result.startsolid = source.startsolid ? true : false;
	result.fraction = source.fraction;
	VectorCopy(source.end, result.endpos);
	VectorCopy(source.plane.normal, result.plane.normal);
	result.plane.dist = source.plane.distance;
	result.plane.type = (byte)source.plane.type;
	result.contents = (int)source.contents;
	result.surface = &surface;
	result.ent = source.instance_id == 0U ? &edicts[0] : &edicts[2];
	return result;
}

static int RuntimePointContents(vec3_t point)
{
	return (int)SG_HostCollisionPointContents(&collision, &collision_scene,
		point);
}

static sg_rune_pmove_control_identity_t ControlIdentity(void)
{
	sg_rune_pmove_control_identity_t identity;

	memset(&identity, 0, sizeof(identity));
	identity.version = SG_RUNE_PMOVE_CONTROL_VERSION;
	identity.compact_artifact_id = UINT64_C(0x1001);
	identity.bsp_content_id = UINT64_C(0x2002);
	memset(identity.bsp_identity, 0x5a, sizeof(identity.bsp_identity));
	identity.physics_abi_id = SG_HOST_ENGINE_PMOVE_ABI_ID;
	identity.collision_law_id = SG_RUNE_PMOVE_CONTROL_COLLISION_LAW_ID;
	identity.pmove_law_id = SG_RUNE_PMOVE_CONTROL_PMOVE_LAW_ID;
	identity.pmove_behavior_id = identity.physics_abi_id;
	identity.frame_ms = 100U;
	identity.substep_ms = 25U;
	identity.substep_count = 4U;
	identity.frame_cost_units = 1U;
	identity.source_reserve_units = 1U;
	return identity;
}

static int BuildControl(sg_rune_pmove_control_storage_t *storage,
	sg_rune_pmove_control_model_t *model)
{
	sg_rune_pmove_control_build_input_t input;
	sg_rune_pmove_control_error_t error;

	memset(&input, 0, sizeof(input));
	input.identity = ControlIdentity();
	input.cell = 7U;
	input.portal = 3U;
	input.target_cell = 8U;
	input.corridor_min_q8[0] = -512 * 8;
	input.corridor_max_q8[0] = 272 * 8;
	input.corridor_min_q8[1] = -512 * 8;
	input.corridor_max_q8[1] = 512 * 8;
	input.portal_q8 = 256 * 8;
	input.support_z_q8 = 0;
	input.hull_half_width_q8 = 16 * 8;
	input.maximum_velocity_q8 = 2000 * 8;
	return SG_RunePmoveControlBuildAxisCorridorPrivate(&input, storage, model,
		&error);
}

static void InstallBot(float x, float y, float vx, float vy)
{
	memset(edicts, 0, sizeof(edicts));
	memset(clients, 0, sizeof(clients));
	memset(sg_bots, 0, sizeof(sg_bots));
	edicts[0].inuse = true;
	edicts[0].s.number = 0;
	edicts[0].classname = "worldspawn";
	edicts[1].inuse = true;
	edicts[1].s.number = 1;
	edicts[1].s.modelindex = 255;
	edicts[1].client = &clients[1];
	edicts[1].classname = "player";
	edicts[1].flags = FL_BOT;
	edicts[1].health = 100;
	edicts[1].viewheight = 22.0f;
	SetVector(edicts[1].s.origin, x, y, 0.0f);
	SetVector(edicts[1].velocity, vx, vy, 0.0f);
	clients[1].pers.connected = true;
	clients[1].ctf.ctfid = UINT64_C(0x7007);
	clients[1].ps.pmove.pm_type = PM_NORMAL;
	clients[1].ps.pmove.gravity = 800;
	clients[1].ps.pmove.origin[0] = (short)(x * 8.0f);
	clients[1].ps.pmove.origin[1] = (short)(y * 8.0f);
	clients[1].ps.pmove.velocity[0] = (short)(vx * 8.0f);
	clients[1].ps.pmove.velocity[1] = (short)(vy * 8.0f);
	clients[1].old_pmove = clients[1].ps.pmove;
	sg_bots[0].active = true;
	sg_bots[0].ent = &edicts[1];
	globals.edicts = edicts;
	globals.num_edicts = 4;
}

static sg_rune_pmove_control_state_t Live(void)
{
	sg_rune_pmove_control_state_t live;

	memset(&live, 0, sizeof(live));
	live.origin_q8[0] = clients[1].ps.pmove.origin[0];
	live.origin_q8[1] = clients[1].ps.pmove.origin[1];
	live.origin_q8[2] = clients[1].ps.pmove.origin[2];
	live.velocity_q8[0] = clients[1].ps.pmove.velocity[0];
	live.velocity_q8[1] = clients[1].ps.pmove.velocity[1];
	live.velocity_q8[2] = clients[1].ps.pmove.velocity[2];
	live.cell = live.origin_q8[0] >= 256 * 8 ? 8U : 7U;
	live.standing = 1U;
	live.dry = 1U;
	live.supported = 1U;
	live.support_is_static_world = 1U;
	return live;
}

static void ApplyAccepted(const sg_tactic_pmove_control_result_t *result)
{
	uint32_t axis;

	clients[1].old_pmove = clients[1].ps.pmove;
	for (axis = 0U; axis < 3U; axis++)
	{
		clients[1].ps.pmove.origin[axis] = (short)result->state.origin_q8[axis];
		clients[1].ps.pmove.velocity[axis] =
			(short)result->state.velocity_q8[axis];
		edicts[1].s.origin[axis] =
			(float)result->state.origin_q8[axis] * 0.125f;
		edicts[1].velocity[axis] =
			(float)result->state.velocity_q8[axis] * 0.125f;
	}
}

static int Admit(const sg_rune_pmove_control_model_t *model,
	const sg_host_engine_runtime_t *runtime,
	const sg_host_engine_subject_identity_t *subject, uint32_t portal,
	size_t substep_capacity, sg_tactic_pmove_control_result_t *result,
	sg_rune_pmove_control_error_t *error)
{
	sg_host_pmove_substep_t substeps[4];
	sg_host_pmove_trace_t traces[128];
	sg_host_pmove_replay_workspace_t workspace;
	sg_rune_pmove_control_state_t live = Live();

	memset(&workspace, 0, sizeof(workspace));
	workspace.substeps = substeps;
	workspace.substep_capacity = substep_capacity;
	workspace.traces = traces;
	workspace.trace_capacity = sizeof(traces) / sizeof(traces[0]);
	return SG_TacticPmoveControlRuntimeAdmit(model, runtime, subject, 0U,
		portal, &live, 100U, &workspace, result, error);
}

static void TestRealOwnerReplay(void)
{
	static const float velocities[][2] = {
		{ 0.0f, 0.0f }, { 300.0f, 0.0f }, { -300.0f, 0.0f },
		{ 0.0f, 300.0f }, { -300.0f, 300.0f }, { 1999.0f, 0.0f },
		{ -1999.0f, 0.0f }, { 0.0f, -2000.0f }, { 0.0f, 2000.0f }
	};
	sg_rune_pmove_control_storage_t storage;
	sg_rune_pmove_control_model_t model;
	sg_host_engine_runtime_t *runtime = NULL;
	sg_host_engine_subject_identity_t subject;
	sg_tactic_pmove_control_result_t result;
	sg_rune_pmove_control_error_t error;
	uint32_t index;
	uint32_t same_region_frames = 0U;
	uint32_t frames = 0U;

	CHECK(BuildControl(&storage, &model));
	InstallBot(0.0f, 0.0f, 0.0f, 0.0f);
	CHECK(SG_HostEngineRuntimeBegin("corridor", &runtime) ==
		SG_HOST_ENGINE_RUNTIME_OK);
	CHECK(SG_HostEngineRuntimeOwnerActivate(runtime) ==
		SG_HOST_ENGINE_RUNTIME_OK);
	CHECK(SG_HostEngineRuntimeOwnerSubject(runtime, 1U, &subject));
	for (index = 0U; index < sizeof(velocities) / sizeof(velocities[0]); index++)
	{
		InstallBot(0.0f, 0.0f, velocities[index][0], velocities[index][1]);
		CHECK(SG_HostEngineRuntimeOwnerSubject(runtime, 1U, &subject));
		CHECK(Admit(&model, runtime, &subject, 3U, 4U, &result, &error));
		CHECK(result.next_units + result.live_local_units < result.source_units);
	}
	InstallBot(0.0f, 500.0f, 0.0f, 2000.0f);
	CHECK(SG_HostEngineRuntimeOwnerSubject(runtime, 1U, &subject));
	CHECK(!Admit(&model, runtime, &subject, 3U, 4U, &result, &error));
	CHECK(error == SG_RUNE_PMOVE_CONTROL_ERROR_REGION_MISS);
	InstallBot(0.0f, -500.0f, 0.0f, -2000.0f);
	CHECK(SG_HostEngineRuntimeOwnerSubject(runtime, 1U, &subject));
	CHECK(!Admit(&model, runtime, &subject, 3U, 4U, &result, &error));
	CHECK(error == SG_RUNE_PMOVE_CONTROL_ERROR_REGION_MISS);
	InstallBot(0.0f, 0.0f, 0.0f, 0.0f);
	CHECK(SG_HostEngineRuntimeOwnerSubject(runtime, 1U, &subject));
	while (frames++ < 32U)
	{
		CHECK(Admit(&model, runtime, &subject, 3U, 4U, &result, &error));
		if (failures)
			break;
		if (result.transition == 0U)
			same_region_frames++;
		ApplyAccepted(&result);
		if (result.transition == 1U)
			break;
	}
	CHECK(same_region_frames >= 3U);
	CHECK(result.transition == 1U && result.state.cell == 8U);
	InstallBot(0.0f, 0.0f, 0.0f, 0.0f);
	CHECK(SG_HostEngineRuntimeOwnerSubject(runtime, 1U, &subject));
	CHECK(!Admit(&model, runtime, &subject, 4U, 4U, &result, &error));
	CHECK(error == SG_RUNE_PMOVE_CONTROL_ERROR_PORTAL_MISMATCH);
	CHECK(!Admit(&model, runtime, &subject, 3U, 3U, &result, &error));
	CHECK(error == SG_RUNE_PMOVE_CONTROL_ERROR_INCOMPLETE_REPLAY);
	clients[1].ctf.ctfid++;
	CHECK(!Admit(&model, runtime, &subject, 3U, 4U, &result, &error));
	CHECK(error == SG_RUNE_PMOVE_CONTROL_ERROR_STALE_IDENTITY);
	CHECK(SG_HostEngineRuntimeOwnerSubject(runtime, 1U, &subject));
	model.identity.physics_abi_id++;
	model.identity.pmove_behavior_id++;
	CHECK(!Admit(&model, runtime, &subject, 3U, 4U, &result, &error));
	CHECK(error == SG_RUNE_PMOVE_CONTROL_ERROR_STALE_IDENTITY);
	model.identity.physics_abi_id--;
	model.identity.pmove_behavior_id--;
	model.identity.bsp_identity[0]++;
	CHECK(!Admit(&model, runtime, &subject, 3U, 4U, &result, &error));
	CHECK(error == SG_RUNE_PMOVE_CONTROL_ERROR_STALE_IDENTITY);
	model.identity.bsp_identity[0]--;
	edicts[2].inuse = true;
	edicts[2].s.number = 2;
	edicts[2].s.modelindex = 1;
	edicts[2].classname = "func_door";
	collision_scene.instances = &collision_instance;
	collision_scene.instance_count = 1U;
	CHECK(!Admit(&model, runtime, &subject, 3U, 4U, &result, &error));
	if (error != SG_RUNE_PMOVE_CONTROL_ERROR_DYNAMIC_COLLISION)
		fprintf(stderr, "dynamic rejection was %s\n",
			SG_RunePmoveControlErrorString(error));
	CHECK(error == SG_RUNE_PMOVE_CONTROL_ERROR_DYNAMIC_COLLISION);
	collision_scene.instances = NULL;
	collision_scene.instance_count = 0U;
	SG_HostEngineRuntimeDestroy(runtime);
}

int main(void)
{
	memset(&gravity_cvar, 0, sizeof(gravity_cvar));
	memset(&maxvelocity_cvar, 0, sizeof(maxvelocity_cvar));
	memset(&funky_cvar, 0, sizeof(funky_cvar));
	memset(&airaccelerate_cvar, 0, sizeof(airaccelerate_cvar));
	gravity_cvar.value = 800.0f;
	maxvelocity_cvar.value = 2000.0f;
	sv_gravity = &gravity_cvar;
	sv_maxvelocity = &maxvelocity_cvar;
	want_funky_gravity = &funky_cvar;
	gi.cvar = RuntimeCvar;
	gi.trace = RuntimeTrace;
	gi.pointcontents = RuntimePointContents;
	gi.Pmove = Pmove;
	memset(&level_identity, 0, sizeof(level_identity));
	level_identity.bsp_checksum = UINT32_C(0x1111);
	level_identity.entity_crc32 = UINT32_C(0x2222);
	level_identity.host_physics_id = SG_HOST_PHYSICS_EPOCH;
	level_identity.bsp_bytes = 1024U;
	memset(level_identity.bsp_sha256, 0x5a,
		sizeof(level_identity.bsp_sha256));
	memcpy(level_identity.mapname, "corridor", sizeof("corridor"));
	BuildCollisionFixture();
	TestRealOwnerReplay();
	if (failures)
		return 1;
	puts("v13 PMove control real owner/YQ2 tests passed");
	return 0;
}
