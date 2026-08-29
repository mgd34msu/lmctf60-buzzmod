#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef q_exported
#define q_exported
#endif
#include "../slipgate/sg_host_law_publication.h"
#include "../game.h"
#include "../slipgate/sg_host_engine_runtime_private.h"
#include "../slipgate/sg_host_law_owner.h"
#include "../slipgate/sg_hooks.h"
#include "../slipgate/sg_identity.h"

game_import_t gi;
game_export_t globals;
sg_host_t sg_host;
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
static int failures;

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

/* This is a real BSP-shaped world. Pmove reaches it through the production
 * host collision adapter; no test callback supplies movement authority. */
static sg_bsp_plane_t planes[1];
static sg_bsp_node_t nodes[1];
static sg_bsp_leaf_t leaves[2];
static sg_bsp_model_t models[1];
static sg_bsp_world_t test_world;
static sg_bsp_plane_t hook_planes[2];
static sg_bsp_node_t hook_nodes[1];
static sg_bsp_leaf_t hook_leaves[2];
static uint32_t hook_leaf_brushes[1];
static sg_bsp_brush_t hook_brushes[1];
static sg_bsp_brush_side_t hook_brush_sides[1];
static sg_bsp_model_t hook_models[1];
static sg_bsp_world_t hook_world;
static edict_t runtime_edicts[3];
static gclient_t runtime_clients[2];
static csurface_t runtime_surface;
static edict_t *runtime_last_passent;
static int runtime_trace_calls;
static int runtime_return_mover;
static int runtime_return_self;
static int runtime_return_world;

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
	if (runtime_return_world || runtime_return_mover || (runtime_return_self &&
		passent != &runtime_edicts[1]))
	{
		trace.fraction = 0.5f;
		trace.endpos[0] = (start[0] + end[0]) * 0.5f;
		trace.ent = runtime_return_world ? &runtime_edicts[0] : runtime_return_mover ?
			&runtime_edicts[2] :
			&runtime_edicts[1];
		trace.contents = CONTENTS_SOLID;
		trace.plane.normal[0] = -1.0f;
		trace.plane.normal[2] = 0.0f;
	}
	return trace;
}

static int RuntimePointContents(vec3_t point)
{
	(void)point;
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
	result = SG_HostLawPublicationIssue(&authority, &publication);
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

	result = SG_HostLawPublicationIssue(&authority, &publication);
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
	sg_host_engine_runtime_acceptance_t *acceptance = NULL;
	sg_host_engine_runtime_status_t runtime_status;
	sg_rune_model_t model;
	sg_rune_cell_t cells[1];
	sg_rune_phase_basis_t model_phases[1];
	sg_phase_coordinate_t snapshot_phases[1];
	sg_rune_runtime_snapshot_t snapshot;
	sg_bsp_content_identity_t content_identity;
	sg_host_collision_trace_t trace;
	sg_host_pmove_request_t request;
	sg_host_pmove_result_t pmove_result;
	sg_host_pmove_error_t pmove_error;
	sg_host_hook_fire_request_t fire_request;
	float start[3] = { 0.0f, 0.0f, 0.0f };
	float mins[3] = { -16.0f, -16.0f, -24.0f };
	float maxs[3] = { 16.0f, 16.0f, 32.0f };
	float end[3] = { 64.0f, 0.0f, 0.0f };
	float point[3] = { 0.0f, 0.0f, 0.0f };
	sg_host_collision_contents_t contents;

	memset(&test_level_identity, 0, sizeof(test_level_identity));
	test_level_identity.bsp_checksum = 0x12345678U;
	test_level_identity.entity_crc32 = 0x9abcdef0U;
	test_level_identity.host_physics_id = SG_HOST_PHYSICS_EPOCH;
	memcpy(test_level_identity.mapname, "runtime_map",
		sizeof("runtime_map"));
	memset(runtime_edicts, 0, sizeof(runtime_edicts));
	memset(runtime_clients, 0, sizeof(runtime_clients));
	runtime_edicts[0].inuse = true;
	runtime_edicts[0].s.number = 0;
	runtime_edicts[0].s.modelindex = 1;
	runtime_edicts[1].s.number = 1;
	runtime_edicts[1].inuse = true;
	runtime_edicts[1].client = &runtime_clients[1];
	runtime_edicts[2].s.number = 2;
	runtime_edicts[2].s.modelindex = 7;
	runtime_edicts[2].inuse = true;
	globals.edicts = runtime_edicts;
	globals.num_edicts = 3;
	gi.trace = RuntimeTrace;
	gi.pointcontents = RuntimePointContents;
	gi.Pmove = RuntimePmove;
	runtime_last_passent = NULL;
	runtime_trace_calls = 0;
	runtime_return_mover = 0;
	runtime_return_self = 1;
	runtime_return_world = 0;
	test_identity_snapshot_status = SG_IDENTITY_OK;
	runtime_status = SG_HostEngineRuntimeBegin("runtime_map", &runtime);
	CHECK(runtime_status == SG_HOST_ENGINE_RUNTIME_OK && runtime != NULL);
	memset(&model, 0, sizeof(model));
	model.version = SG_RUNE_MODEL_VERSION;
	model.schema_tag = SG_RUNE_MODEL_SCHEMA_TAG;
	model.flags = SG_RUNE_MODEL_IMMUTABLE | SG_RUNE_MODEL_EXACT_BOUND |
		SG_RUNE_MODEL_NO_RUNTIME_ACTORS;
	model.identity = Identity();
	model.completeness.state = SG_RUNE_COMPLETENESS_COMPLETE;
	model.completeness.expected_cells = 1U;
	model.completeness.covered_cells = 1U;
	model.cells = cells;
	model.cell_count = 1U;
	model.phases = model_phases;
	model.phase_count = 1U;
	memset(cells, 0, sizeof(cells));
	memset(model_phases, 0, sizeof(model_phases));
	snapshot_phases[0].phase_id = 0U;
	snapshot_phases[0].cell_id = 0U;
	memset(&snapshot, 0, sizeof(snapshot));
	snapshot.identity = UINT64_C(0x7001);
	snapshot.topology_revision = UINT64_C(0x9001);
	snapshot.cell_count = 1U;
	snapshot.phase_count = 1U;
	snapshot.region_count = 1U;
	snapshot.model = &model;
	snapshot.phases = snapshot_phases;
	memset(&content_identity, 0xa5, sizeof(content_identity));
	runtime_status = SG_HostEngineRuntimeAcceptanceIssueOwner(
		(void *)1, &snapshot, &content_identity, &acceptance);
	CHECK(runtime_status == SG_HOST_ENGINE_RUNTIME_INVALID_ARGUMENT &&
		acceptance == NULL);
	runtime_status = SG_HostEngineRuntimeAcceptanceIssueOwner(
		SG_HostEngineRuntimeOwnerToken(), &snapshot, &content_identity,
		&acceptance);
	CHECK(runtime_status == SG_HOST_ENGINE_RUNTIME_OK && acceptance != NULL);
	runtime_status = SG_HostEngineRuntimeJoinOwner(runtime, (void *)1,
		acceptance);
	CHECK(runtime_status == SG_HOST_ENGINE_RUNTIME_INVALID_ARGUMENT &&
		!SG_HostEngineRuntimeAccepted(runtime));
	runtime_status = SG_HostEngineRuntimeJoinOwner(runtime,
		SG_HostEngineRuntimeOwnerToken(), acceptance);
	CHECK(runtime_status == SG_HOST_ENGINE_RUNTIME_OK &&
		SG_HostEngineRuntimeAccepted(runtime));
	runtime_status = SG_HostEngineRuntimeBindSubjectOwner(runtime, (void *)1,
		1U);
	CHECK(runtime_status == SG_HOST_ENGINE_RUNTIME_INVALID_ARGUMENT);
	runtime_status = SG_HostEngineRuntimeBindSubjectOwner(runtime,
		SG_HostEngineRuntimeOwnerToken(), 1U);
	CHECK(runtime_status == SG_HOST_ENGINE_RUNTIME_OK);
	CHECK(SG_HostEngineRuntimeCurrent(runtime));
	CHECK(SG_HostEngineRuntimeTrace(runtime, start, mins, maxs, end,
		SG_HOST_MASK_PLAYER_SOLID, &trace));
	CHECK(runtime_last_passent == &runtime_edicts[1]);
	CHECK(trace.instance_id == 0U);
	/* A recycled or torn-down subject invalidates the owner-bound query even
	 * though the map/callback publication itself is still current. */
	runtime_edicts[1].inuse = false;
	CHECK(!SG_HostEngineRuntimeTrace(runtime, start, mins, maxs, end,
		SG_HOST_MASK_PLAYER_SOLID, &trace));
	runtime_edicts[1].inuse = true;
	runtime_edicts[1].s.number = 9;
	CHECK(!SG_HostEngineRuntimeTrace(runtime, start, mins, maxs, end,
		SG_HOST_MASK_PLAYER_SOLID, &trace));
	runtime_edicts[1].s.number = 1;
	runtime_return_self = 0;
	runtime_return_mover = 1;
	runtime_return_world = 0;
	CHECK(SG_HostEngineRuntimeTrace(runtime, start, mins, maxs, end,
		SG_HOST_MASK_PLAYER_SOLID, &trace));
	CHECK(trace.instance_id == 2U && trace.model_index == 7U);
	CHECK(SG_HostEngineRuntimePointContents(runtime, point, &contents));
	CHECK(contents == 0U);
	memset(&request, 0, sizeof(request));
	request.state.pm_type = PM_NORMAL;
	request.previous_state = request.state;
	request.state.origin[2] = 1600;
	request.previous_state.origin[2] = 1600;
	runtime_edicts[1].inuse = false;
	CHECK(!SG_HostEngineRuntimePmove(runtime, &request, &pmove_result,
		&pmove_error));
	runtime_edicts[1].inuse = true;
	CHECK(SG_HostEngineRuntimePmove(runtime, &request, &pmove_result,
		&pmove_error));
	CHECK(pmove_result.grounded && pmove_result.support_instance_id == 2U &&
		pmove_result.support_model_index == 7U);
	/* The engine may report the world edict itself (slot zero) as support;
	 * normalize it to the authenticated WORLD identity without confusing it
	 * with a caller-created mover. */
	runtime_return_mover = 0;
	runtime_return_world = 1;
	CHECK(SG_HostEngineRuntimePmove(runtime, &request, &pmove_result,
		&pmove_error));
	CHECK(pmove_result.grounded && pmove_result.support_instance_id == 0U &&
		pmove_result.support_model_index == SG_HOST_COLLISION_MODEL_WORLD);
	/* The active snapshot is the lifetime authority.  A generation or topology
	 * replacement, including a one-field mutation of the retained snapshot,
	 * invalidates every runtime query. */
	snapshot.identity++;
	CHECK(!SG_HostEngineRuntimeAccepted(runtime));
	CHECK(!SG_HostEngineRuntimePmove(runtime, &request, &pmove_result,
		&pmove_error));
	snapshot.identity--;
	CHECK(SG_HostEngineRuntimeAccepted(runtime));
	snapshot.topology_revision++;
	CHECK(!SG_HostEngineRuntimeAccepted(runtime));
	snapshot.topology_revision--;
	CHECK(SG_HostEngineRuntimeAccepted(runtime));
	model.identity.physics.gravity = 100.0f;
	CHECK(!SG_HostEngineRuntimeAccepted(runtime));
	model.identity.physics.gravity = 800.0f;
	CHECK(SG_HostEngineRuntimeAccepted(runtime));
	/* The exact callback identity is part of the runtime lifetime, not merely
	 * the ABI shape. */
	gi.Pmove = Pmove;
	CHECK(!SG_HostEngineRuntimeCurrent(runtime));
	CHECK(!SG_HostEngineRuntimeAccepted(runtime));
	gi.Pmove = RuntimePmove;
	CHECK(SG_HostEngineRuntimeCurrent(runtime));
	CHECK(SG_HostEngineRuntimeAccepted(runtime));
	test_level_identity.entity_crc32++;
	CHECK(!SG_HostEngineRuntimeCurrent(runtime));
	CHECK(!SG_HostEngineRuntimeAccepted(runtime));
	test_level_identity.entity_crc32--;
	CHECK(SG_HostEngineRuntimeCurrent(runtime));
	CHECK(SG_HostEngineRuntimeAccepted(runtime));
	/* The production owner consumes the same opaque acceptance handle.  Its
	 * publication exposes the owner-issued runtime collision view and no raw
	 * callback seam. */
	SG_HostLawProductionReset();
	CHECK(SG_HostLawProductionBeginLevel("runtime_map").status ==
		SG_HOST_LAW_OK);
	CHECK(SG_HostLawProductionInstallAccepted(acceptance).status ==
		SG_HOST_LAW_OK);
	CHECK(SG_HostLawProductionBindSubject(1U).status == SG_HOST_LAW_OK);
	CHECK(SG_HostLawProductionEnginePointContents(point, &contents).status ==
		SG_HOST_LAW_OK);
	CHECK(SG_HostLawProductionEngineTrace(start, mins, maxs, end,
		SG_HOST_MASK_PLAYER_SOLID, &trace).status == SG_HOST_LAW_OK);
	CHECK(runtime_last_passent == &runtime_edicts[1]);
	memset(&fire_request, 0, sizeof(fire_request));
	fire_request.start[0] = 0.0f;
	fire_request.end[0] = 64.0f;
	fire_request.phase = SG_HOST_HOOK_IDLE;
	fire_request.attack_held = 1;
	runtime_return_mover = 0;
	runtime_return_self = 0;
	runtime_return_world = 1;
	{
		sg_host_hook_step_t fire_step;

		CHECK(SG_HostLawPublicationHookFire(
			SG_HostLawProductionPublication(), NULL, &fire_request,
			&fire_step).status == SG_HOST_LAW_OK && fire_step.first_hit &&
			fire_step.attached && fire_step.target_kind ==
			SG_HOST_HOOK_TARGET_WORLD && fire_step.target_identity ==
			UINT64_C(0x101));
	}
	CHECK(SG_HostLawProductionRevalidate().status == SG_HOST_LAW_OK);
	SG_HostLawProductionReset();
	test_identity_snapshot_status = SG_IDENTITY_UNAVAILABLE;
	CHECK(!SG_HostEngineRuntimeCurrent(runtime));
	SG_HostEngineRuntimeAcceptanceDestroyOwner(acceptance);
	SG_HostEngineRuntimeDestroy(runtime);
	globals.edicts = NULL;
	globals.num_edicts = 0;
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
	SG_HostLawPublicationDestroy(publication);
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
	SG_HostLawPublicationDestroy(publication);
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
		SG_HostLawPublicationDestroy(hook_publication);
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
	SG_HostLawPublicationDestroy(publication);
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
	SG_HostLawPublicationDestroy(publication);
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
	SG_HostLawPublicationDestroy(publication);
}

static void TestOwnerFailClosedAndDrift(void)
{
	sg_host_collision_authority_t authority = Authority();
	const sg_host_collision_authority_t *borrowed = NULL;
	const sg_host_law_publication_t *publication;
	sg_host_law_result_t result;

	SG_HostLawProductionReset();
	result = SG_HostLawProductionRevalidate();
	CHECK(result.status == SG_HOST_LAW_HOST_UNAVAILABLE);
	test_identity_snapshot_status = SG_IDENTITY_OK;
	result = SG_HostLawProductionBeginLevel("packed_map");
	CHECK(result.status == SG_HOST_LAW_HOST_UNAVAILABLE &&
		result.field == SG_HOST_LAW_FIELD_PMOVE_ABI &&
		SG_HostLawProductionPublication() == NULL);
	test_identity_snapshot_status = SG_IDENTITY_UNAVAILABLE;
	result = SG_HostLawProductionInstall(&authority);
	CHECK(result.status == SG_HOST_LAW_OK);
	publication = SG_HostLawProductionPublication();
	CHECK(publication != NULL);
	result = SG_HostLawProductionCollisionAuthority(&borrowed);
	CHECK(result.status == SG_HOST_LAW_OK && borrowed != NULL &&
		borrowed->world == &test_world);
	result = SG_HostLawProductionRevalidate();
	CHECK(result.status == SG_HOST_LAW_OK);
	gravity_cvar.value = 799.0f;
	result = SG_HostLawProductionRevalidate();
	CHECK(result.status == SG_HOST_LAW_PRODUCTION_DRIFT);
	CHECK(SG_HostLawProductionPublication() == NULL);
	gravity_cvar.value = 800.0f;
	result = SG_HostLawProductionCollisionAuthority(&borrowed);
	CHECK(result.status == SG_HOST_LAW_HOST_UNAVAILABLE && borrowed == NULL);
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

	TestEngineBindingAndParity();
	TestEngineRuntimeOwnerBinding();
	TestPublicationAndCallbackIsolation();
	TestPmoveAndCollisionExecution();
	TestHookChronology();
	TestHookDamagePolicy();
	TestMechanismEquations();
	TestOwnerFailClosedAndDrift();
	SG_HostLawProductionReset();
	if (failures != 0)
		return 1;
	puts("host-law publication tests passed");
	return 0;
}
