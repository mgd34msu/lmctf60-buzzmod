#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../slipgate/sg_host_collision.h"
#include "../slipgate/sg_host_pmove.h"

#ifndef q_exported
#define q_exported
#endif
#include "../game.h"

/* sg_host_pmove carries the exact engine adapter alongside the legacy
 * callback evaluator.  This collision fixture does not invoke that path, but
 * supplies the import slot so the combined object set remains link-complete. */
game_import_t gi;

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

static uint32_t FloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

/* This is the selected host's q_shared AngleVectors and SV_Push arithmetic,
 * kept locally as a differential oracle for the public carry primitive. */
static void HostAngleVectors(const float angles[3], float forward[3],
	float right[3], float up[3])
{
	float angle;
	float sr;
	float sp;
	float sy;
	float cr;
	float cp;
	float cy;

	angle = (float)(angles[1] * (M_PI * 2 / 360));
	sy = (float)sin((double)angle);
	cy = (float)cos((double)angle);
	angle = (float)(angles[0] * (M_PI * 2 / 360));
	sp = (float)sin((double)angle);
	cp = (float)cos((double)angle);
	angle = (float)(angles[2] * (M_PI * 2 / 360));
	sr = (float)sin((double)angle);
	cr = (float)cos((double)angle);
	forward[0] = cp * cy;
	forward[1] = cp * sy;
	forward[2] = -sp;
	right[0] = (-1.0f * sr * sp * cy + -1.0f * cr * -sy);
	right[1] = (-1.0f * sr * sp * sy + -1.0f * cr * cy);
	right[2] = -1.0f * sr * cp;
	up[0] = cr * sp * cy + -sr * -sy;
	up[1] = cr * sp * sy + -sr * cy;
	up[2] = cr * cp;
}

static void HostSVPushCarryReference(
	const sg_host_collision_transform_t *pusher_transform,
	const float move[3], const float amove[3], const float rider_start[3],
	float rider_end_out[3])
{
	float inverse_angles[3];
	float forward[3];
	float right[3];
	float up[3];
	float relative[3];
	float rotated[3];
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++) {
		const float rider_after_translation = rider_start[axis] + move[axis];
		const float pusher_after_translation =
			pusher_transform->origin[axis] + move[axis];

		inverse_angles[axis] = -amove[axis];
		relative[axis] = rider_after_translation - pusher_after_translation;
	}
	HostAngleVectors(inverse_angles, forward, right, up);
	rotated[0] = relative[0] * forward[0] + relative[1] * forward[1] +
		relative[2] * forward[2];
	rotated[1] = -(relative[0] * right[0] + relative[1] * right[1] +
		relative[2] * right[2]);
	rotated[2] = relative[0] * up[0] + relative[1] * up[1] +
		relative[2] * up[2];
	for (axis = 0U; axis < 3U; axis++)
		rider_end_out[axis] = rider_start[axis] + move[axis] +
			(rotated[axis] - relative[axis]);
}

#ifdef SG_HOST_REAL_PMOVE_TEST
static void SelectedHostSVPushCarry(const float move[3], const float amove[3],
	const float rider_start[3], float rider_end_out[3])
{
	float inverse_angles[3];
	float forward[3];
	float right[3];
	float up[3];
	float relative[3];
	float rotated[3];
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++) {
		inverse_angles[axis] = -amove[axis];
		relative[axis] = rider_start[axis];
	}
	AngleVectors(inverse_angles, forward, right, up);
	rotated[0] = relative[0] * forward[0] + relative[1] * forward[1] +
		relative[2] * forward[2];
	rotated[1] = -(relative[0] * right[0] + relative[1] * right[1] +
		relative[2] * right[2]);
	rotated[2] = relative[0] * up[0] + relative[1] * up[1] +
		relative[2] * up[2];
	for (axis = 0U; axis < 3U; axis++)
		rider_end_out[axis] = rider_start[axis] + move[axis] +
			(rotated[axis] - relative[axis]);
}
#endif

static void ReplayWorldTransform(
	const sg_host_collision_world_transform_t *transform, const float local[3],
	float world_out[3])
{
	uint32_t world_axis;

	for (world_axis = 0U; world_axis < 3U; world_axis++) {
		world_out[world_axis] = local[0] * transform->axis[0][world_axis] +
			local[1] * transform->axis[1][world_axis] +
			local[2] * transform->axis[2][world_axis] +
			transform->origin[world_axis];
		if (world_out[world_axis] == 0.0f)
			world_out[world_axis] = 0.0f;
	}
}

static int WorldTransformFiniteCanonical(
	const sg_host_collision_world_transform_t *transform)
{
	uint32_t local_axis;
	uint32_t world_axis;

	if (!transform)
		return 0;
	for (world_axis = 0U; world_axis < 3U; world_axis++) {
		if (!isfinite(transform->origin[world_axis]) ||
			(transform->origin[world_axis] == 0.0f &&
				signbit(transform->origin[world_axis])))
			return 0;
		for (local_axis = 0U; local_axis < 3U; local_axis++)
			if (!isfinite(transform->axis[local_axis][world_axis]) ||
				(transform->axis[local_axis][world_axis] == 0.0f &&
					signbit(transform->axis[local_axis][world_axis])))
				return 0;
	}
	return 1;
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

static void TestPusherCarryHostParity(void)
{
	sg_host_collision_transform_t pusher;
	const float move[3] = { 0.0f, 0.0f, 0.0f };
	const float rider[3] = { 1.0f, 0.0f, 0.0f };
	float amove[3] = { 0.0f, 90.0f, 0.0f };
	float expected[3];
	float actual[3];

	memset(&pusher, 0, sizeof(pusher));
	HostSVPushCarryReference(&pusher, move, amove, rider, expected);
	CHECK(SG_HostCollisionPusherCarry(&pusher, move, amove, rider, actual));
	CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
	CHECK(FloatBits(actual[0]) == UINT32_C(0x00000000) &&
		FloatBits(actual[1]) == UINT32_C(0x3f800000) &&
		FloatBits(actual[2]) == UINT32_C(0x00000000));

	amove[1] = 45.0f;
	HostSVPushCarryReference(&pusher, move, amove, rider, expected);
	CHECK(SG_HostCollisionPusherCarry(&pusher, move, amove, rider, actual));
	CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
	CHECK(FloatBits(actual[0]) == UINT32_C(0x3f3504f3) &&
		FloatBits(actual[1]) == UINT32_C(0x3f3504f3));

	amove[1] = 359.998932f;
	HostSVPushCarryReference(&pusher, move, amove, rider, expected);
	CHECK(SG_HostCollisionPusherCarry(&pusher, move, amove, rider, actual));
	CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
	CHECK(FloatBits(actual[0]) == UINT32_C(0x3f800000) &&
		FloatBits(actual[1]) == UINT32_C(0xb79a8886));

	SetVector(amove, -720.25f, 17.25f, -720.25f);
	HostSVPushCarryReference(&pusher, move, amove, rider, expected);
	CHECK(SG_HostCollisionPusherCarry(&pusher, move, amove, rider, actual));
	CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
#ifdef SG_HOST_REAL_PMOVE_TEST
	SelectedHostSVPushCarry(move, amove, rider, expected);
	CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
#endif
}

static void TestModelToWorldTransformBoundary(void)
{
	const float remote_mins[3] = { 1000, 1000, 1000 };
	const float remote_maxs[3] = { 1010, 1010, 1010 };
	const float mover_mins[3] = { -20, -20, -20 };
	const float mover_maxs[3] = { 20, 20, 20 };
	const float cardinal_local[3] = { 4, -2, 3 };
	const float noncardinal_local[3] = { -7.25f, 3.5f, 1.125f };
	const float negative_zero[3] = { -0.0f, -0.0f, -0.0f };
	const float nonfinite[3] = { NAN, 0.0f, 0.0f };
	const float expected_cardinal[3] = { 52, 54, 8 };
	fixture_t fixture = TwoBoxFixture(remote_mins, remote_maxs,
		SG_HOST_CONTENTS_SOLID, 0, mover_mins, mover_maxs,
		SG_HOST_CONTENTS_SOLID, 0);
	sg_host_collision_authority_t authority = Authority(&fixture, 100, 100, 50);
	sg_host_collision_transform_t cardinal;
	sg_host_collision_transform_t noncardinal;
	sg_host_collision_world_transform_t cardinal_world_transform;
	sg_host_collision_world_transform_t noncardinal_world_transform;
	float world[3];
	float replayed_world[3];
	float zero_world[3] = { 1.0f, 1.0f, 1.0f };

	memset(&cardinal, 0, sizeof(cardinal));
	SetVector(cardinal.origin, 50, 50, 5);
	cardinal.angles[1] = 90.0f;
	CHECK(SG_HostCollisionModelToWorldPoint(&authority, 1U, &cardinal,
		cardinal_local, world));
	/* This is the exact yaw=90 angle-axis/translation result. */
	CHECK(memcmp(world, expected_cardinal, sizeof(world)) == 0);
	CHECK(SG_HostCollisionWorldTransform(&cardinal,
		&cardinal_world_transform));
	CHECK(WorldTransformFiniteCanonical(&cardinal_world_transform));
	CHECK(cardinal_world_transform.axis[0][1] > 0.0f &&
		cardinal_world_transform.axis[1][0] < 0.0f);
	ReplayWorldTransform(&cardinal_world_transform, cardinal_local,
		replayed_world);
	CHECK(memcmp(replayed_world, world, sizeof(world)) == 0);
	/* PointContentsModel is the pre-existing inverse collision transform. */
	CHECK(SG_HostCollisionPointContentsModel(&authority, 1U, &cardinal,
		world) & SG_HOST_CONTENTS_SOLID);

	memset(&noncardinal, 0, sizeof(noncardinal));
	SetVector(noncardinal.origin, 6.25f, -8.5f, 2.75f);
	SetVector(noncardinal.angles, 17.0f, 41.0f, -23.0f);
	CHECK(SG_HostCollisionModelToWorldPoint(&authority, 1U, &noncardinal,
		noncardinal_local, world));
	CHECK(isfinite(world[0]) && isfinite(world[1]) && isfinite(world[2]));
	CHECK(SG_HostCollisionWorldTransform(&noncardinal,
		&noncardinal_world_transform));
	CHECK(WorldTransformFiniteCanonical(&noncardinal_world_transform));
	ReplayWorldTransform(&noncardinal_world_transform, noncardinal_local,
		replayed_world);
	CHECK(memcmp(replayed_world, world, sizeof(world)) == 0);
	/* The existing model-space collision path must invert this exact forward
	 * transform for a non-cardinal rotation too. */
	CHECK(SG_HostCollisionPointContentsModel(&authority, 1U, &noncardinal,
		world) & SG_HOST_CONTENTS_SOLID);

	CHECK(SG_HostCollisionModelToWorldPoint(&authority, 1U, NULL,
		negative_zero, zero_world));
	CHECK(zero_world[0] == 0.0f && zero_world[1] == 0.0f &&
		zero_world[2] == 0.0f);
	CHECK(!signbit(zero_world[0]) && !signbit(zero_world[1]) &&
		!signbit(zero_world[2]));
	CHECK(!SG_HostCollisionModelToWorldPoint(&authority, 1U, NULL,
		nonfinite, zero_world));
	CHECK(!SG_HostCollisionModelToWorldPoint(&authority, 0U, &cardinal,
		cardinal_local, world));
	CHECK(!SG_HostCollisionModelToWorldPoint(&authority, 2U, &cardinal,
		cardinal_local, world));
	memset(&cardinal_world_transform, 0xa5, sizeof(cardinal_world_transform));
	noncardinal.angles[0] = NAN;
	CHECK(!SG_HostCollisionWorldTransform(&noncardinal,
		&cardinal_world_transform));
	CHECK(((const uint8_t *)&cardinal_world_transform)[0] == UINT8_C(0xa5));
	DestroyFixture(&fixture);
}

static void TestModelPolygonPositiveAreaOverlap(void)
{
	const float remote_mins[3] = { 1000, 1000, 1000 };
	const float remote_maxs[3] = { 1010, 1010, 1010 };
	const float mover_mins[3] = { 2, -2, -2 };
	const float mover_maxs[3] = { 6, 2, 2 };
	const sg_rune_vec3_t partial_portal[4] = {
		{ { -4.0f, -4.0f, 0.0f } },
		{ { 4.0f, -4.0f, 0.0f } },
		{ { 4.0f, 4.0f, 0.0f } },
		{ { -4.0f, 4.0f, 0.0f } }
	};
	const sg_rune_vec3_t edge_only_portal[4] = {
		{ { 2.0f, 2.0f, 0.0f } },
		{ { 4.0f, 2.0f, 0.0f } },
		{ { 4.0f, 4.0f, 0.0f } },
		{ { 2.0f, 4.0f, 0.0f } }
	};
	const sg_rune_vec3_t coplanar_face_portal[4] = {
		{ { 2.5f, -1.0f, 2.0f } },
		{ { 3.5f, -1.0f, 2.0f } },
		{ { 3.5f, 1.0f, 2.0f } },
		{ { 2.5f, 1.0f, 2.0f } }
	};
	fixture_t fixture = TwoBoxFixture(remote_mins, remote_maxs,
		SG_HOST_CONTENTS_SOLID, 0, mover_mins, mover_maxs,
		SG_HOST_CONTENTS_SOLID, 0);
	sg_host_collision_authority_t authority = Authority(&fixture, 100, 100, 50);
	sg_host_collision_transform_t offset;
	int overlap = 0;

	/* The polygon centroid is x=0, outside this x=[2,6] mover.  The exact
	 * brush clip still finds the positive-area x=[2,4] by y=[-2,2] patch. */
	CHECK(SG_HostCollisionModelPositiveAreaPolygonOverlap(&authority, 1U,
		NULL, partial_portal, 4U, SG_HOST_MASK_PLAYER_SOLID, &overlap));
	CHECK(overlap);
	memset(&offset, 0, sizeof(offset));
	offset.origin[0] = 10.0f;
	overlap = 1;
	CHECK(SG_HostCollisionModelPositiveAreaPolygonOverlap(&authority, 1U,
		&offset, partial_portal, 4U, SG_HOST_MASK_PLAYER_SOLID, &overlap));
	CHECK(!overlap);
	overlap = 1;
	CHECK(SG_HostCollisionModelPositiveAreaPolygonOverlap(&authority, 1U,
		NULL, edge_only_portal, 4U, SG_HOST_MASK_PLAYER_SOLID, &overlap));
	CHECK(!overlap);
	overlap = 1;
	CHECK(SG_HostCollisionModelPositiveAreaPolygonOverlap(&authority, 1U,
		NULL, coplanar_face_portal, 4U, SG_HOST_MASK_PLAYER_SOLID,
		&overlap));
	CHECK(!overlap);
	DestroyFixture(&fixture);
}

static void TestMoverSupportRequiresQ8Clearance(void)
{
	const float remote_mins[3] = { 1000, 1000, 1000 };
	const float remote_maxs[3] = { 1010, 1010, 1010 };
	const float floor_mins[3] = { -64, -64, -64 };
	const float floor_maxs[3] = { 64, 64, 0 };
	const float exact_contact[3] = { 0.0f, 0.0f, 24.0f };
	const float q8_clearance[3] = { 0.0f, 0.0f, 24.125f };
	fixture_t fixture = TwoBoxFixture(remote_mins, remote_maxs,
		SG_HOST_CONTENTS_SOLID, 0, floor_mins, floor_maxs,
		SG_HOST_CONTENTS_SOLID, 0);
	sg_host_collision_authority_t authority = Authority(&fixture, 800, 100, 100);
	sg_host_collision_instance_t instance;
	sg_host_collision_scene_t scene;
	sg_host_collision_pose_t exact, raised;

	memset(&instance, 0, sizeof(instance));
	instance.instance_id = UINT64_C(7);
	instance.model_index = 1U;
	scene.instances = &instance;
	scene.instance_count = 1U;
	CHECK(SG_HostCollisionClassifyPose(&authority, &scene, exact_contact,
		SG_RUNE_STANCE_STANDING, &exact));
	CHECK(!exact.valid && exact.occupancy.allsolid);
	CHECK(SG_HostCollisionClassifyPose(&authority, &scene, q8_clearance,
		SG_RUNE_STANCE_STANDING, &raised));
	CHECK(raised.valid && raised.supported && raised.support_is_mover);
	CHECK(raised.support.instance_id == instance.instance_id);
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

static void TestCoplanarBrushProvenance(void)
{
	const float mins[3] = { -1, -1, -1 };
	const float maxs[3] = { 1, 1, 1 };
	const float start[3] = { -3, 0, 0 };
	const float end[3] = { 3, 0, 0 };
	const float clear_end[3] = { -2, 0, 0 };
	const float zero[3] = { 0, 0, 0 };
	fixture_t fixture;
	sg_host_collision_authority_t authority;
	sg_host_collision_trace_t first;
	sg_host_collision_trace_t second;
	uint32_t side;

	CHECK(AllocateFixture(&fixture, 6U, 6U, 7U, 1U, 2U, 12U, 2U));
	AddBox(&fixture, 0U, 0U, 0U, 0U, 0U, mins, maxs,
		SG_HOST_CONTENTS_SOLID, 0);
	for (side = 0U; side < 6U; side++)
	{
		fixture.brush_sides[6U + side] = fixture.brush_sides[side];
		fixture.brush_sides[6U + side].texinfo = 1;
	}
	fixture.brushes[1].first_side = 6U;
	fixture.brushes[1].side_count = 6U;
	fixture.brushes[1].contents = SG_HOST_CONTENTS_SOLID;
	fixture.texinfos[1].flags = 0;
	fixture.leaves[6].first_leaf_brush = 0U;
	fixture.leaves[6].leaf_brush_count = 2U;
	fixture.leaf_brushes[0] = 0U;
	fixture.leaf_brushes[1] = 1U;
	authority = Authority(&fixture, 800, 100, 100);
	CHECK(SG_HostCollisionTraceModel(&authority,
		SG_HOST_COLLISION_MODEL_WORLD, NULL, start, zero, zero, end,
		SG_HOST_MASK_PLAYER_SOLID, &first));
	CHECK(first.fraction < 1.0f);
	CHECK(first.brush == 0U);
	CHECK(first.brush_side == 1U);
	CHECK(SG_HostCollisionTraceModel(&authority,
		SG_HOST_COLLISION_MODEL_WORLD, NULL, start, zero, zero, end,
		SG_HOST_MASK_PLAYER_SOLID, &second));
	CHECK(memcmp(&first, &second, sizeof(first)) == 0);
	fixture.leaf_brushes[0] = 1U;
	fixture.leaf_brushes[1] = 0U;
	CHECK(SG_HostCollisionTraceModel(&authority,
		SG_HOST_COLLISION_MODEL_WORLD, NULL, start, zero, zero, end,
		SG_HOST_MASK_PLAYER_SOLID, &second));
	CHECK(second.fraction == first.fraction);
	CHECK(second.brush == 1U);
	CHECK(second.brush_side == 7U);
	CHECK(SG_HostCollisionTraceModel(&authority,
		SG_HOST_COLLISION_MODEL_WORLD, NULL, start, zero, zero, clear_end,
		SG_HOST_MASK_PLAYER_SOLID, &second));
	CHECK(second.fraction == 1.0f);
	CHECK(second.brush == SG_HOST_COLLISION_BRUSH_NONE);
	CHECK(second.brush_side == SG_HOST_COLLISION_BRUSH_NONE);
	fixture.brushes[0].side_count = 0U;
	fixture.brushes[1].side_count = 0U;
	CHECK(SG_HostCollisionTraceModel(&authority,
		SG_HOST_COLLISION_MODEL_WORLD, NULL, start, zero, zero, end,
		SG_HOST_MASK_PLAYER_SOLID, &second));
	CHECK(second.fraction == 1.0f);
	CHECK(second.brush == SG_HOST_COLLISION_BRUSH_NONE);
	CHECK(second.brush_side == SG_HOST_COLLISION_BRUSH_NONE);
	fixture.brushes[0].side_count = 6U;
	fixture.brushes[0].first_side = fixture.world.brush_side_count;
	CHECK(SG_HostCollisionTraceModel(&authority,
		SG_HOST_COLLISION_MODEL_WORLD, NULL, start, zero, zero, end,
		SG_HOST_MASK_PLAYER_SOLID, &second));
	CHECK(second.fraction == 1.0f);
	CHECK(second.brush == SG_HOST_COLLISION_BRUSH_NONE);
	CHECK(second.brush_side == SG_HOST_COLLISION_BRUSH_NONE);
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
	sg_host_pmove_substep_t substeps[4];
	sg_host_pmove_trace_t traces[4];
	sg_host_pmove_replay_workspace_t workspace;
	sg_host_pmove_replay_t replay;
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
	CHECK(result.trace_count == 20004U);
	CHECK(result.collision_trace_count <= result.trace_count);
	CHECK(result.state.velocity[2] == -8);
	memset(substeps, 0xff, sizeof(substeps));
	workspace.substeps = substeps;
	workspace.substep_capacity = 4U;
	workspace.traces = traces;
	workspace.trace_capacity = 4U;
	fake_pmove_calls = 0;
	CHECK(SG_HostPmoveReplayFrame(&authority, NULL, FakePmove, &request,
		&workspace, &replay, &error));
	CHECK(error == SG_HOST_PMOVE_ERROR_NONE && fake_pmove_calls == 4);
	CHECK(replay.substeps == substeps && replay.substep_count == 4U);
	CHECK(replay.traces == traces && replay.trace_count == 4U);
	CHECK(replay.frame_ms == 100U && replay.substep_ms == 25U);
	CHECK(replay.physics_abi_id == authority.identity.physics_abi_id);
	CHECK(replay.bsp_content_id == authority.identity.bsp_content_id);
	CHECK(replay.result.trace_count == 4U);
	CHECK(replay.result.collision_trace_count <= replay.result.trace_count);
	CHECK(substeps[0].step == 0U && substeps[0].elapsed_ms == 25U);
	CHECK(substeps[3].step == 3U && substeps[3].elapsed_ms == 100U);
	CHECK(substeps[0].first_trace_ordinal == 1U &&
		substeps[0].trace_count == 1U);
	CHECK(substeps[3].first_trace_ordinal == 4U &&
		substeps[3].trace_count == 1U);
	CHECK(substeps[0].state.velocity[2] == -2);
	CHECK(substeps[3].state.velocity[2] == -8);
	CHECK(substeps[0].before_state.velocity[2] == 0);
	CHECK(substeps[3].before_state.velocity[2] == -6);
	CHECK(traces[0].ordinal == 1U && traces[0].substep == 0U);
	CHECK(traces[3].ordinal == 4U && traces[3].substep == 3U);
	CHECK(traces[0].start[2] == 0.0f && traces[0].end[2] == -0.25f);
	CHECK(traces[0].result.fraction < 1.0f);
	CHECK(traces[0].result.contents == SG_HOST_CONTENTS_SOLID);
	CHECK(traces[0].result.model_index == SG_HOST_COLLISION_MODEL_WORLD);
	workspace.substep_capacity = 3U;
	CHECK(!SG_HostPmoveReplayFrame(&authority, NULL, FakePmove, &request,
		&workspace, &replay, &error));
	CHECK(error == SG_HOST_PMOVE_ERROR_CAPACITY);
	workspace.substep_capacity = 4U;
	workspace.trace_capacity = 3U;
	CHECK(!SG_HostPmoveReplayFrame(&authority, NULL, FakePmove, &request,
		&workspace, &replay, &error));
	CHECK(error == SG_HOST_PMOVE_ERROR_CAPACITY);
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
	TestPusherCarryHostParity();
	TestModelToWorldTransformBoundary();
	TestModelPolygonPositiveAreaOverlap();
	TestMoverSupportRequiresQ8Clearance();
	TestStartsolidAllsolid();
	TestCoplanarBrushProvenance();
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
