#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../slipgate/sg_host_collision.h"
#include "../slipgate/sg_host_pmove.h"

static int failures;
static uint32_t fake_pmove_calls;
static byte fake_pmove_msec;
static int fake_many_traces;

#ifdef SG_HOST_REAL_PMOVE_TEST
/* Linked from the repository's selected Yamagi Pmove oracle. */
extern void Pmove(pmove_t *pmove);
void Com_DPrintf(const char *format, ...);
void Com_DPrintf(const char *format, ...)
{
	(void)format;
}
void Com_Printf(char *format, ...)
{
	(void)format;
}
#endif

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct fixture_s
{
	sg_bsp_world_t world;
	sg_bsp_plane_t *planes;
	sg_bsp_node_t *nodes;
	sg_bsp_leaf_t *leaves;
	uint32_t *leaf_brushes;
	sg_bsp_model_t *models;
	sg_bsp_brush_t *brushes;
	sg_bsp_brush_side_t *brush_sides;
	sg_bsp_texinfo_t *texinfos;
} fixture_t;

static void SetVector(float value[3], float x, float y, float z)
{
	value[0] = x;
	value[1] = y;
	value[2] = z;
}

static void SetRuneVector(sg_rune_vec3_t *value, float x, float y, float z)
{
	SetVector(value->value, x, y, z);
}

static sg_rune_model_identity_t Identity(float gravity, uint32_t frame_ms,
	uint32_t substep_ms)
{
	sg_rune_model_identity_t identity;

	memset(&identity, 0, sizeof(identity));
	identity.bsp_content_id = UINT64_C(0x1234);
	identity.physics_abi_id = UINT64_C(0x5678);
	SetRuneVector(&identity.standing_hull.mins, -16.0f, -16.0f, -24.0f);
	SetRuneVector(&identity.standing_hull.maxs, 16.0f, 16.0f, 32.0f);
	SetRuneVector(&identity.crouching_hull.mins, -16.0f, -16.0f, -24.0f);
	SetRuneVector(&identity.crouching_hull.maxs, 16.0f, 16.0f, 4.0f);
	identity.physics.gravity = gravity;
	identity.physics.ground_acceleration = 10.0f;
	identity.physics.air_acceleration = 1.0f;
	identity.physics.water_acceleration = 10.0f;
	identity.physics.hook_acceleration = 800.0f;
	identity.physics.external_acceleration = 1.0f;
	identity.physics.water_drag = 1.0f;
	identity.physics.max_velocity = 2000.0f;
	identity.physics.frame_ms = frame_ms;
	identity.physics.substep_ms = substep_ms;
	return identity;
}

static void SetPlane(sg_bsp_plane_t *plane, float x, float y, float z,
	float distance)
{
	SetVector(plane->normal.value, x, y, z);
	plane->distance = distance;
	if (x == 1.0f)
		plane->type = 0;
	else if (y == 1.0f)
		plane->type = 1;
	else if (z == 1.0f)
		plane->type = 2;
	else if (x == -1.0f)
		plane->type = 3;
	else if (y == -1.0f)
		plane->type = 4;
	else if (z == -1.0f)
		plane->type = 5;
	else
		plane->type = 3;
}

static int AllocateFixture(fixture_t *fixture, uint32_t plane_count,
	uint32_t node_count, uint32_t leaf_count, uint32_t model_count,
	uint32_t brush_count, uint32_t side_count, uint32_t leaf_brush_count)
{
	memset(fixture, 0, sizeof(*fixture));
	fixture->planes = calloc(plane_count, sizeof(*fixture->planes));
	fixture->nodes = calloc(node_count, sizeof(*fixture->nodes));
	fixture->leaves = calloc(leaf_count, sizeof(*fixture->leaves));
	fixture->models = calloc(model_count, sizeof(*fixture->models));
	fixture->brushes = calloc(brush_count, sizeof(*fixture->brushes));
	fixture->brush_sides = calloc(side_count, sizeof(*fixture->brush_sides));
	fixture->leaf_brushes = calloc(leaf_brush_count,
		sizeof(*fixture->leaf_brushes));
	fixture->texinfos = calloc(brush_count ? brush_count : 1U,
		sizeof(*fixture->texinfos));
	if (!fixture->planes || !fixture->nodes || !fixture->leaves ||
		!fixture->models || (brush_count && !fixture->brushes) ||
		(side_count && !fixture->brush_sides) ||
		(leaf_brush_count && !fixture->leaf_brushes) || !fixture->texinfos)
		return 0;
	fixture->world.planes = fixture->planes;
	fixture->world.plane_count = plane_count;
	fixture->world.nodes = fixture->nodes;
	fixture->world.node_count = node_count;
	fixture->world.leaves = fixture->leaves;
	fixture->world.leaf_count = leaf_count;
	fixture->world.models = fixture->models;
	fixture->world.model_count = model_count;
	fixture->world.brushes = fixture->brushes;
	fixture->world.brush_count = brush_count;
	fixture->world.brush_sides = fixture->brush_sides;
	fixture->world.brush_side_count = side_count;
	fixture->world.leaf_brushes = fixture->leaf_brushes;
	fixture->world.leaf_brush_count = leaf_brush_count;
	fixture->world.texinfos = fixture->texinfos;
	fixture->world.texinfo_count = brush_count ? brush_count : 1U;
	return 1;
}

static void DestroyFixture(fixture_t *fixture)
{
	free(fixture->planes);
	free(fixture->nodes);
	free(fixture->leaves);
	free(fixture->leaf_brushes);
	free(fixture->models);
	free(fixture->brushes);
	free(fixture->brush_sides);
	free(fixture->texinfos);
	memset(fixture, 0, sizeof(*fixture));
}

static void AddBox(fixture_t *fixture, uint32_t model, uint32_t first_plane,
	uint32_t first_node, uint32_t first_leaf, uint32_t brush,
	const float mins[3], const float maxs[3], int32_t contents,
	int32_t surface_flags)
{
	uint32_t side;
	uint32_t inside_leaf = first_leaf + 6U;

	SetPlane(&fixture->planes[first_plane + 0], 1, 0, 0, maxs[0]);
	SetPlane(&fixture->planes[first_plane + 1], -1, 0, 0, -mins[0]);
	SetPlane(&fixture->planes[first_plane + 2], 0, 1, 0, maxs[1]);
	SetPlane(&fixture->planes[first_plane + 3], 0, -1, 0, -mins[1]);
	SetPlane(&fixture->planes[first_plane + 4], 0, 0, 1, maxs[2]);
	SetPlane(&fixture->planes[first_plane + 5], 0, 0, -1, -mins[2]);
	for (side = 0; side < 6; side++)
	{
		sg_bsp_node_t *node = &fixture->nodes[first_node + side];

		node->plane = first_plane + side;
		node->children[0] = -1 - (int32_t)(first_leaf + side);
		node->children[1] = side == 5 ? -1 - (int32_t)inside_leaf :
			(int32_t)(first_node + side + 1U);
		fixture->brush_sides[brush * 6U + side].plane = first_plane + side;
		fixture->brush_sides[brush * 6U + side].texinfo = (int32_t)brush;
	}
	fixture->leaves[inside_leaf].contents = contents;
	fixture->leaves[inside_leaf].first_leaf_brush = brush;
	fixture->leaves[inside_leaf].leaf_brush_count = 1;
	fixture->leaf_brushes[brush] = brush;
	fixture->brushes[brush].first_side = brush * 6U;
	fixture->brushes[brush].side_count = 6;
	fixture->brushes[brush].contents = contents;
	fixture->texinfos[brush].flags = surface_flags;
	fixture->models[model].headnode = (int32_t)first_node;
	SetVector(fixture->models[model].mins.value,
		mins[0] - 1.0f, mins[1] - 1.0f, mins[2] - 1.0f);
	SetVector(fixture->models[model].maxs.value,
		maxs[0] + 1.0f, maxs[1] + 1.0f, maxs[2] + 1.0f);
}

static fixture_t TwoBoxFixture(const float world_mins[3],
	const float world_maxs[3], int32_t world_contents, int32_t world_flags,
	const float model_mins[3], const float model_maxs[3],
	int32_t model_contents, int32_t model_flags)
{
	fixture_t fixture;

	if (!AllocateFixture(&fixture, 12, 12, 14, 2, 2, 12, 2))
	{
		fputs("fixture allocation failed\n", stderr);
		exit(2);
	}
	AddBox(&fixture, 0, 0, 0, 0, 0, world_mins, world_maxs,
		world_contents, world_flags);
	AddBox(&fixture, 1, 6, 6, 7, 1, model_mins, model_maxs,
		model_contents, model_flags);
	return fixture;
}

static sg_host_collision_authority_t Authority(fixture_t *fixture,
	float gravity, uint32_t frame_ms, uint32_t substep_ms)
{
	sg_host_collision_authority_t authority;
	sg_rune_model_identity_t identity = Identity(gravity, frame_ms, substep_ms);
	sg_host_collision_error_t error;

	memset(&authority, 0, sizeof(authority));
	CHECK(SG_HostCollisionInit(&authority, &fixture->world, &identity, &error));
	CHECK(error == SG_HOST_COLLISION_ERROR_NONE);
	return authority;
}

static void TestStandingCrouchingAndBoundaries(void)
{
	const float ceiling_mins[3] = { -100, -100, 28 };
	const float ceiling_maxs[3] = { 100, 100, 100 };
	const float remote_mins[3] = { 1000, 1000, 1000 };
	const float remote_maxs[3] = { 1010, 1010, 1010 };
	const float origin[3] = { 0, 0, 0 };
	fixture_t fixture = TwoBoxFixture(ceiling_mins, ceiling_maxs,
		SG_HOST_CONTENTS_SOLID, 0, remote_mins, remote_maxs,
		SG_HOST_CONTENTS_SOLID, 0);
	sg_host_collision_authority_t authority = Authority(&fixture, 800, 100, 100);
	sg_host_collision_pose_t standing, crouching;

	CHECK(SG_HostCollisionClassifyPose(&authority, NULL, origin,
		SG_RUNE_STANCE_STANDING, &standing));
	CHECK(SG_HostCollisionClassifyPose(&authority, NULL, origin,
		SG_RUNE_STANCE_CROUCHING, &crouching));
	CHECK(!standing.valid);
	CHECK(standing.occupancy.startsolid && standing.occupancy.allsolid);
	CHECK(crouching.valid);
	/* Exact contact is inside under CM_TestBoxInBrush's d <= 0 rule. */
	CHECK(crouching.occupancy.fraction == 1.0f);
	DestroyFixture(&fixture);
}

static void TestWindowHalfWallVoidAndSky(void)
{
	const float wall_mins[3] = { -2, -40, -40 };
	const float wall_maxs[3] = { 2, 40, 10 };
	const float remote_mins[3] = { 1000, 1000, 1000 };
	const float remote_maxs[3] = { 1010, 1010, 1010 };
	const float low_start[3] = { -40, 0, 30 };
	const float low_end[3] = { 40, 0, 30 };
	const float sky_start[3] = { -40, 0, 0 };
	const float sky_end[3] = { 40, 0, 0 };
	const float high_start[3] = { -40, 0, 40 };
	const float high_end[3] = { 40, 0, 40 };
	const float point_hull[3] = { 0, 0, 0 };
	const float void_start[3] = { 500, 0, 0 };
	const float void_end[3] = { 600, 0, 0 };
	fixture_t fixture = TwoBoxFixture(wall_mins, wall_maxs,
		SG_HOST_CONTENTS_WINDOW, SG_HOST_SURFACE_SKY, remote_mins, remote_maxs,
		SG_HOST_CONTENTS_SOLID, 0);
	sg_host_collision_authority_t authority = Authority(&fixture, 800, 100, 100);
	sg_host_collision_transition_t low, high;
	sg_host_collision_trace_t sky, empty;

	CHECK(SG_HostCollisionTransition(&authority, NULL, low_start, low_end,
		SG_RUNE_STANCE_STANDING, &low));
	CHECK(!low.clear && low.sweep.fraction < 1.0f);
	CHECK(low.sweep.contents == SG_HOST_CONTENTS_WINDOW);
	CHECK(low.sweep.surface_flags & SG_HOST_SURFACE_SKY);
	CHECK(SG_HostCollisionTransition(&authority, NULL, high_start, high_end,
		SG_RUNE_STANCE_STANDING, &high));
	CHECK(high.clear);
	CHECK(SG_HostCollisionTrace(&authority, NULL, sky_start, point_hull,
		point_hull, sky_end, SG_HOST_MASK_PLAYER_SOLID, &sky));
	CHECK(sky.surface_flags & SG_HOST_SURFACE_SKY);
	CHECK(SG_HostCollisionTrace(&authority, NULL, void_start, point_hull,
		point_hull, void_end, SG_HOST_MASK_PLAYER_SOLID, &empty));
	CHECK(empty.fraction == 1.0f && !empty.startsolid && !empty.allsolid);
	DestroyFixture(&fixture);
}

static void TestRampSupportAndLedge(void)
{
	const float ramp_mins[3] = { -50, -50, -100 };
	const float ramp_maxs[3] = { 50, 50, 100 };
	const float remote_mins[3] = { 1000, 1000, 1000 };
	const float remote_maxs[3] = { 1010, 1010, 1010 };
	const float ramp_origin[3] = { 20, 0, 60.1f };
	const float ledge_origin[3] = { 70, 0, 60.1f };
	fixture_t fixture = TwoBoxFixture(ramp_mins, ramp_maxs,
		SG_HOST_CONTENTS_SOLID, 0, remote_mins, remote_maxs,
		SG_HOST_CONTENTS_SOLID, 0);
	sg_host_collision_authority_t authority;
	sg_host_collision_pose_t ramp, ledge;
	const float diagonal = 0.70710677f;

	SetPlane(&fixture.planes[4], -diagonal, 0, diagonal, 0);
	authority = Authority(&fixture, 800, 100, 100);
	CHECK(SG_HostCollisionClassifyPose(&authority, NULL, ramp_origin,
		SG_RUNE_STANCE_STANDING, &ramp));
	CHECK(ramp.valid && ramp.supported);
	CHECK(ramp.support.plane.normal[2] >= 0.7f);
	CHECK(SG_HostCollisionClassifyPose(&authority, NULL, ledge_origin,
		SG_RUNE_STANCE_STANDING, &ledge));
	CHECK(ledge.valid && !ledge.supported);
	DestroyFixture(&fixture);
}

static fixture_t WaterFixture(void)
{
	fixture_t fixture;

	if (!AllocateFixture(&fixture, 1, 1, 2, 1, 0, 0, 0))
		exit(2);
	SetPlane(&fixture.planes[0], 0, 0, 1, 0);
	fixture.nodes[0].plane = 0;
	fixture.nodes[0].children[0] = -1;
	fixture.nodes[0].children[1] = -2;
	fixture.leaves[1].contents = SG_HOST_CONTENTS_WATER |
		SG_HOST_CONTENTS_CURRENT_UP;
	fixture.models[0].headnode = 0;
	return fixture;
}

static void TestWaterLevels(void)
{
	const float level_one_origin[3] = { 0, 0, 20 };
	const float level_two_origin[3] = { 0, 0, 0 };
	const float level_three_origin[3] = { 0, 0, -30 };
	fixture_t fixture = WaterFixture();
	sg_host_collision_authority_t authority = Authority(&fixture, 800, 100, 100);
	sg_host_collision_pose_t pose;

	CHECK(SG_HostCollisionClassifyPose(&authority, NULL, level_one_origin,
		SG_RUNE_STANCE_STANDING, &pose));
	CHECK(pose.water_level == 1);
	CHECK(pose.water_type & SG_HOST_CONTENTS_CURRENT_UP);
	CHECK(SG_HostCollisionRuneContents(pose.water_type) &
		SG_RUNE_CONTENTS_CURRENT_UP);
	CHECK(SG_HostCollisionClassifyPose(&authority, NULL, level_two_origin,
		SG_RUNE_STANCE_STANDING, &pose));
	CHECK(pose.water_level == 2);
	CHECK(SG_HostCollisionClassifyPose(&authority, NULL, level_three_origin,
		SG_RUNE_STANCE_STANDING, &pose));
	CHECK(pose.water_level == 3);
	DestroyFixture(&fixture);
}

static void TestMoverTransformsAndDeterminism(void)
{
	const float remote_mins[3] = { 1000, 1000, 1000 };
	const float remote_maxs[3] = { 1010, 1010, 1010 };
	const float mover_mins[3] = { -20, -2, -20 };
	const float mover_maxs[3] = { 20, 2, 20 };
	const float start[3] = { 50, 0, 0 };
	const float end[3] = { 50, 100, 0 };
	const float point[3] = { 50, 50, 0 };
	const float zero[3] = { 0, 0, 0 };
	fixture_t fixture = TwoBoxFixture(remote_mins, remote_maxs,
		SG_HOST_CONTENTS_SOLID, 0, mover_mins, mover_maxs,
		SG_HOST_CONTENTS_SOLID, 0);
	sg_host_collision_authority_t authority = Authority(&fixture, 100, 100, 50);
	sg_host_collision_instance_t instances[2];
	sg_host_collision_scene_t scene;
	sg_host_collision_trace_t first, second;

	memset(instances, 0, sizeof(instances));
	instances[0].instance_id = 20;
	instances[0].model_index = 1;
	SetVector(instances[0].transform.origin, 50, 50, 0);
	instances[0].transform.angles[1] = 90;
	instances[1].instance_id = 10;
	instances[1].model_index = 1;
	SetVector(instances[1].transform.origin, 500, 500, 0);
	scene.instances = instances;
	scene.instance_count = 2;
	CHECK(SG_HostCollisionTrace(&authority, &scene, start, zero, zero, end,
		SG_HOST_MASK_PLAYER_SOLID, &first));
	CHECK(first.fraction < 1.0f && first.instance_id == 20);
	CHECK(fabsf(first.plane.normal[1]) > 0.99f);
	CHECK(SG_HostCollisionPointContents(&authority, &scene, point) &
		SG_HOST_CONTENTS_SOLID);
	instances[0] = instances[1];
	instances[1].instance_id = 20;
	instances[1].model_index = 1;
	SetVector(instances[1].transform.origin, 50, 50, 0);
	instances[1].transform.angles[1] = 90;
	CHECK(SG_HostCollisionTrace(&authority, &scene, start, zero, zero, end,
		SG_HOST_MASK_PLAYER_SOLID, &second));
	CHECK(memcmp(&first, &second, sizeof(first)) == 0);
	CHECK(authority.identity.physics.gravity == 100.0f);
	DestroyFixture(&fixture);
}

static void TestStartsolidAllsolid(void)
{
	const float box_mins[3] = { -10, -10, -10 };
	const float box_maxs[3] = { 10, 10, 10 };
	const float remote_mins[3] = { 1000, 1000, 1000 };
	const float remote_maxs[3] = { 1010, 1010, 1010 };
	const float inside[3] = { 0, 0, 0 };
	const float outside[3] = { 30, 0, 0 };
	const float still_inside[3] = { 1, 0, 0 };
	const float zero[3] = { 0, 0, 0 };
	fixture_t fixture = TwoBoxFixture(box_mins, box_maxs,
		SG_HOST_CONTENTS_SOLID, 0, remote_mins, remote_maxs,
		SG_HOST_CONTENTS_SOLID, 0);
	sg_host_collision_authority_t authority = Authority(&fixture, 800, 100, 100);
	sg_host_collision_trace_t stationary, escaping, moving_allsolid;

	CHECK(SG_HostCollisionTrace(&authority, NULL, inside, zero, zero, inside,
		SG_HOST_MASK_PLAYER_SOLID, &stationary));
	CHECK(stationary.startsolid && stationary.allsolid &&
		stationary.fraction == 0.0f);
	CHECK(SG_HostCollisionTrace(&authority, NULL, inside, zero, zero, outside,
		SG_HOST_MASK_PLAYER_SOLID, &escaping));
	CHECK(escaping.startsolid && !escaping.allsolid);
	CHECK(SG_HostCollisionTrace(&authority, NULL, inside, zero, zero,
		still_inside, SG_HOST_MASK_PLAYER_SOLID, &moving_allsolid));
	CHECK(moving_allsolid.startsolid && moving_allsolid.allsolid);
	CHECK(moving_allsolid.fraction == 1.0f && moving_allsolid.contents == 0);
	DestroyFixture(&fixture);
}

static void TestMoreThan1024StationaryLeaves(void)
{
	const uint32_t node_count = 1100;
	const uint32_t tail_leaf = node_count;
	const float origin[3] = { 0, 0, 0 };
	fixture_t fixture;
	sg_host_collision_authority_t authority;
	sg_host_collision_pose_t pose;
	uint32_t index;

	if (!AllocateFixture(&fixture, 7, node_count, node_count + 1U, 1, 1, 6, 1))
		exit(2);
	for (index = 0; index < node_count; index++)
	{
		fixture.nodes[index].plane = 0;
		fixture.nodes[index].children[0] = -1 - (int32_t)index;
		fixture.nodes[index].children[1] = index + 1U == node_count ?
			-1 - (int32_t)tail_leaf : (int32_t)(index + 1U);
	}
	SetPlane(&fixture.planes[0], 1, 0, 0, 0);
	SetPlane(&fixture.planes[1], 1, 0, 0, 100);
	SetPlane(&fixture.planes[2], -1, 0, 0, 100);
	SetPlane(&fixture.planes[3], 0, 1, 0, 100);
	SetPlane(&fixture.planes[4], 0, -1, 0, 100);
	SetPlane(&fixture.planes[5], 0, 0, 1, 100);
	SetPlane(&fixture.planes[6], 0, 0, -1, 100);
	for (index = 0; index < 6; index++)
	{
		fixture.brush_sides[index].plane = index + 1U;
		fixture.brush_sides[index].texinfo = 0;
	}
	fixture.brushes[0].side_count = 6;
	fixture.brushes[0].contents = SG_HOST_CONTENTS_SOLID;
	fixture.leaves[tail_leaf].contents = SG_HOST_CONTENTS_SOLID;
	fixture.leaves[tail_leaf].leaf_brush_count = 1;
	fixture.leaf_brushes[0] = 0;
	fixture.models[0].headnode = 0;
	authority = Authority(&fixture, 800, 100, 100);
	CHECK(SG_HostCollisionClassifyPose(&authority, NULL, origin,
		SG_RUNE_STANCE_STANDING, &pose));
	CHECK(!pose.valid && pose.occupancy.allsolid);
	DestroyFixture(&fixture);
}

static void FakePmove(pmove_t *pm)
{
	trace_t trace;
	vec3_t origin, down;
	uint32_t axis;

	fake_pmove_calls++;
	fake_pmove_msec = pm->cmd.msec;
	for (axis = 0; axis < 3; axis++)
		origin[axis] = down[axis] = pm->s.origin[axis] * 0.125f;
	SetVector(pm->mins, -16, -16, -24);
	SetVector(pm->maxs, 16, 16, 32);
	pm->viewheight = 22;
	down[2] -= 0.25f;
	trace = pm->trace(origin, pm->mins, pm->maxs, down);
	if (fake_many_traces)
	{
		uint32_t trace_index;

		for (trace_index = 0; trace_index < 5000; trace_index++)
			trace = pm->trace(origin, pm->mins, pm->maxs, down);
	}
	pm->groundentity = trace.ent;
	pm->watertype = pm->pointcontents(origin);
	pm->waterlevel = (pm->watertype & MASK_WATER) ? 1 : 0;
	pm->s.velocity[2] -= (short)((pm->s.gravity * pm->cmd.msec) / 1000);
}

static void TestHostPmoveBoundary(void)
{
	const float floor_mins[3] = { -100, -100, -100 };
	const float floor_maxs[3] = { 100, 100, -24.1f };
	const float remote_mins[3] = { 1000, 1000, 1000 };
	const float remote_maxs[3] = { 1010, 1010, 1010 };
	fixture_t fixture = TwoBoxFixture(floor_mins, floor_maxs,
		SG_HOST_CONTENTS_SOLID, 0, remote_mins, remote_maxs,
		SG_HOST_CONTENTS_SOLID, 0);
	sg_host_collision_authority_t authority = Authority(&fixture, 100, 100, 25);
	sg_host_pmove_request_t request;
	sg_host_pmove_result_t result;
	sg_host_pmove_error_t error;

	memset(&request, 0, sizeof(request));
	request.state.gravity = 800;
	request.command.msec = 99;
	fake_pmove_calls = 0;
	fake_many_traces = 1;
	CHECK(SG_HostPmoveEvaluateFrame(&authority, NULL, FakePmove, &request, &result,
		&error));
	fake_many_traces = 0;
	CHECK(error == SG_HOST_PMOVE_ERROR_NONE);
	CHECK(fake_pmove_calls == 4 && fake_pmove_msec == 25);
	CHECK(result.state.gravity == 100);
	CHECK(result.gravity == 100.0f && result.evaluated_steps == 4 &&
		result.elapsed_ms == 100);
	CHECK(result.state.velocity[2] == -8);
	authority.identity.physics.gravity = 40000.0f;
	CHECK(!SG_HostPmoveEvaluateFrame(&authority, NULL, FakePmove, &request,
		&result, &error));
	CHECK(error == SG_HOST_PMOVE_ERROR_UNSUPPORTED_GRAVITY);
	DestroyFixture(&fixture);
}

#ifdef SG_HOST_REAL_PMOVE_TEST
static void TestSelectedHostPmove(void)
{
	const float ramp_mins[3] = { -50, -50, -100 };
	const float ramp_maxs[3] = { 50, 50, 100 };
	const float remote_mins[3] = { 1000, 1000, 1000 };
	const float remote_maxs[3] = { 1010, 1010, 1010 };
	fixture_t ramp_fixture = TwoBoxFixture(ramp_mins, ramp_maxs,
		SG_HOST_CONTENTS_SOLID, 0, remote_mins, remote_maxs,
		SG_HOST_CONTENTS_SOLID, 0);
	sg_host_collision_authority_t ramp_authority;
	sg_host_pmove_request_t request;
	sg_host_pmove_result_t result;
	sg_host_pmove_error_t error;
	fixture_t water_fixture;
	sg_host_collision_authority_t water_authority;
	fixture_t step_fixture;
	sg_host_collision_authority_t step_authority;
	sg_host_collision_instance_t step_instance;
	sg_host_collision_scene_t step_scene;
	const float diagonal = 0.70710677f;
	const float floor_mins[3] = { -100, -100, -100 };
	const float floor_maxs[3] = { 100, 100, -24.1f };
	const float step_mins[3] = { 20, -50, -24.1f };
	const float step_maxs[3] = { 30, 50, -8.1f };
	uint32_t frame;

	SetPlane(&ramp_fixture.planes[4], -diagonal, 0, diagonal, 0);
	ramp_authority = Authority(&ramp_fixture, 100, 100, 100);
	memset(&request, 0, sizeof(request));
	request.state.pm_type = PM_NORMAL;
	request.state.origin[0] = 20 * 8;
	request.state.origin[2] = 60 * 8;
	request.previous_state = request.state;
	CHECK(SG_HostPmoveEvaluateFrame(&ramp_authority, NULL, Pmove, &request,
		&result, &error));
	CHECK(error == SG_HOST_PMOVE_ERROR_NONE);
	CHECK(result.state.gravity == 100 && result.physics_abi_id == UINT64_C(0x5678));
	CHECK(result.grounded);
	CHECK(result.mins[2] == -24.0f && result.maxs[2] == 32.0f);
	DestroyFixture(&ramp_fixture);

	step_fixture = TwoBoxFixture(floor_mins, floor_maxs,
		SG_HOST_CONTENTS_SOLID, 0, step_mins, step_maxs,
		SG_HOST_CONTENTS_SOLID, 0);
	step_authority = Authority(&step_fixture, 100, 100, 100);
	memset(&step_instance, 0, sizeof(step_instance));
	step_instance.instance_id = 77;
	step_instance.model_index = 1;
	step_scene.instances = &step_instance;
	step_scene.instance_count = 1;
	memset(&request, 0, sizeof(request));
	request.state.pm_type = PM_NORMAL;
	request.previous_state = request.state;
	request.command.forwardmove = 300;
	for (frame = 0; frame < 10; frame++)
	{
		CHECK(SG_HostPmoveEvaluateFrame(&step_authority, &step_scene, Pmove,
			&request, &result, &error));
		if (result.origin[2] > 8.0f)
			break;
		request.state = result.state;
		request.previous_state = result.state;
	}
	CHECK(result.origin[2] > 8.0f);
	CHECK(result.support_instance_id == 77);
	DestroyFixture(&step_fixture);

	water_fixture = WaterFixture();
	water_authority = Authority(&water_fixture, 100, 100, 100);
	memset(&request, 0, sizeof(request));
	request.state.pm_type = PM_NORMAL;
	request.state.origin[2] = -60 * 8;
	request.previous_state = request.state;
	CHECK(SG_HostPmoveEvaluateFrame(&water_authority, NULL, Pmove, &request,
		&result, &error));
	CHECK(result.water_level == 3);
	CHECK(result.water_type & SG_HOST_CONTENTS_WATER);
	CHECK(result.state.gravity == 100);
	DestroyFixture(&water_fixture);
}
#endif

int main(void)
{
	TestStandingCrouchingAndBoundaries();
	TestWindowHalfWallVoidAndSky();
	TestRampSupportAndLedge();
	TestWaterLevels();
	TestMoverTransformsAndDeterminism();
	TestStartsolidAllsolid();
	TestMoreThan1024StationaryLeaves();
	TestHostPmoveBoundary();
#ifdef SG_HOST_REAL_PMOVE_TEST
	TestSelectedHostPmove();
#endif
	if (failures)
	{
		fprintf(stderr, "%d host collision test failure(s)\n", failures);
		return 1;
	}
	puts("host collision/Pmove tests passed");
	return 0;
}
