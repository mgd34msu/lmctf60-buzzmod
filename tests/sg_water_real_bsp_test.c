#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_bsp_world.h"
#include "slipgate/sg_host_collision.h"
#include "slipgate/sg_host_pmove.h"

void Pmove(pmove_t *pmove);
void Com_DPrintf(const char *format, ...);

void Com_DPrintf(const char *format, ...)
{
	(void)format;
}

void Com_Printf(char *format, ...)
{
	(void)format;
}

static void Set3(float value[3], float x, float y, float z)
{
	value[0] = x;
	value[1] = y;
	value[2] = z;
}

static sg_rune_model_identity_t Identity(float gravity)
{
	sg_rune_model_identity_t identity;

	memset(&identity, 0, sizeof(identity));
	identity.bsp_content_id = UINT64_C(0x5741544552425350);
	identity.entity_semantics_id = UINT64_C(0x5741544552454e54);
	identity.physics_abi_id = UINT64_C(0x5741544552504859);
	identity.source_set_identity = UINT64_C(0x5741544552534f55);
	identity.schema_id = UINT64_C(0x5741544552534348);
	identity.producer_identity = UINT64_C(0x574154455250524f);
	Set3(identity.standing_hull.mins.value, -16.0f, -16.0f, -24.0f);
	Set3(identity.standing_hull.maxs.value, 16.0f, 16.0f, 32.0f);
	Set3(identity.crouching_hull.mins.value, -16.0f, -16.0f, -24.0f);
	Set3(identity.crouching_hull.maxs.value, 16.0f, 16.0f, 4.0f);
	identity.physics.gravity = gravity;
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

static int AxialBrushBounds(const sg_bsp_world_t *world,
	const sg_bsp_brush_t *brush, float mins[3], float maxs[3])
{
	uint32_t axis;
	uint32_t side;

	for (axis = 0U; axis < 3U; axis++)
	{
		mins[axis] = -FLT_MAX;
		maxs[axis] = FLT_MAX;
	}
	for (side = 0U; side < brush->side_count; side++)
	{
		const sg_bsp_brush_side_t *brush_side =
			&world->brush_sides[brush->first_side + side];
		const sg_bsp_plane_t *plane = &world->planes[brush_side->plane];

		for (axis = 0U; axis < 3U; axis++)
		{
			uint32_t other = (axis + 1U) % 3U;
			uint32_t last = (axis + 2U) % 3U;

			if (plane->normal.value[other] != 0.0f ||
				plane->normal.value[last] != 0.0f)
				continue;
			if (plane->normal.value[axis] == 1.0f)
				maxs[axis] = fminf(maxs[axis], plane->distance);
			else if (plane->normal.value[axis] == -1.0f)
				mins[axis] = fmaxf(mins[axis], -plane->distance);
		}
	}
	for (axis = 0U; axis < 3U; axis++)
		if (!isfinite(mins[axis]) || !isfinite(maxs[axis]) ||
			mins[axis] >= maxs[axis])
			return 0;
	return 1;
}

static int FindHullValidWater(const sg_bsp_world_t *world,
	const sg_host_collision_authority_t *authority, float origin_out[3],
	sg_host_collision_pose_t *pose_out, uint32_t *medium_brushes_out)
{
	static const float fractions[] = { 0.25f, 0.5f, 0.75f };
	uint32_t brush;
	uint32_t medium_brushes = 0U;

	for (brush = 0U; brush < world->brush_count; brush++)
	{
		const sg_bsp_brush_t *record = &world->brushes[brush];
		float mins[3];
		float maxs[3];
		uint32_t x;
		uint32_t y;
		uint32_t z;

		if ((record->contents & SG_HOST_MASK_WATER) == 0)
			continue;
		medium_brushes++;
		if (!AxialBrushBounds(world, record, mins, maxs))
			continue;
		for (x = 0U; x < 3U; x++)
			for (y = 0U; y < 3U; y++)
				for (z = 0U; z < 3U; z++)
				{
					float origin[3] = {
						mins[0] + (maxs[0] - mins[0]) * fractions[x],
						mins[1] + (maxs[1] - mins[1]) * fractions[y],
						mins[2] + (maxs[2] - mins[2]) * fractions[z]
					};

					if (origin[0] < -4096.0f || origin[0] > 4095.875f ||
						origin[1] < -4096.0f || origin[1] > 4095.875f ||
						origin[2] < -4096.0f || origin[2] > 4095.875f)
						continue;
					if (SG_HostCollisionClassifyPose(authority, NULL, origin,
						SG_RUNE_STANCE_STANDING, pose_out) && pose_out->valid &&
						pose_out->water_level != 0U &&
						(pose_out->water_type & SG_HOST_MASK_WATER) != 0)
					{
						memcpy(origin_out, origin, sizeof(origin));
						*medium_brushes_out = medium_brushes;
						return 1;
					}
				}
	}
	*medium_brushes_out = medium_brushes;
	return 0;
}

static int RunGravityRegime(const sg_bsp_world_t *world,
	const float origin[3], int hook_zero_gravity)
{
	sg_rune_model_identity_t identity = Identity(800.0f);
	sg_host_collision_authority_t authority;
	sg_host_collision_error_t collision_error;
	sg_host_pmove_request_t request;
	sg_host_pmove_result_t result;
	sg_host_pmove_error_t pmove_error;
	uint32_t axis;

	if (!SG_HostCollisionInit(&authority, world, &identity, &collision_error))
		return 0;
	memset(&request, 0, sizeof(request));
	request.state.pm_type = PM_NORMAL;
	request.state.gravity = 800;
	for (axis = 0U; axis < 3U; axis++)
		request.state.origin[axis] = (short)lroundf(origin[axis] * 8.0f);
	request.previous_state = request.state;
	if (hook_zero_gravity)
	{
		request.hook_law_id = SG_HOST_PMOVE_HOOK_LAW_ID;
		request.hook_attached = 1U;
		request.hook_length =
			SG_HOST_PMOVE_HOOK_LENGTH_GRAVITY_ZERO - 1U;
	}
	if (!SG_HostPmoveEvaluateFrame(&authority, NULL, Pmove, &request,
		&result, &pmove_error))
		return 0;
	return result.evaluated_steps == 4U && result.elapsed_ms == 100U &&
		result.water_level != 0 &&
		(result.water_type & SG_HOST_MASK_WATER) != 0 &&
		(hook_zero_gravity ?
		 result.gravity == 0.0f &&
			result.gravity_law_id == SG_HOST_PMOVE_HOOK_LAW_ID :
		 result.gravity == 800.0f && result.gravity_law_id == 0U);
}

int main(int argc, char **argv)
{
	sg_bsp_world_t *world = NULL;
	sg_bsp_error_t bsp_error;
	sg_rune_model_identity_t identity = Identity(800.0f);
	sg_host_collision_authority_t authority;
	sg_host_collision_error_t collision_error;
	sg_host_collision_pose_t pose;
	float origin[3];
	uint32_t medium_brushes = 0U;

	if (argc != 2)
	{
		fprintf(stderr, "usage: %s WATER_MAP.bsp\n", argv[0]);
		return 2;
	}
	if (!SG_BspWorldLoadFile(argv[1], &world, &bsp_error))
	{
		fprintf(stderr, "real water BSP load failed: %s lump=%u record=%u\n",
			SG_BspWorldErrorString(bsp_error.code), (unsigned)bsp_error.lump,
			(unsigned)bsp_error.record);
		return 1;
	}
	if (!SG_HostCollisionInit(&authority, world, &identity, &collision_error) ||
		!FindHullValidWater(world, &authority, origin, &pose, &medium_brushes) ||
		!RunGravityRegime(world, origin, 0) ||
		!RunGravityRegime(world, origin, 1))
	{
		fprintf(stderr,
			"real water BSP proof failed: brushes=%u collision_error=%d\n",
			medium_brushes, (int)collision_error);
		SG_BspWorldDestroy(world);
		return 1;
	}
	printf("real water BSP proof: %s brushes=%u witness=[%.3f %.3f %.3f] "
		"medium=%d level=%d gravity=800 hook_gravity=0\n", argv[1],
		medium_brushes, origin[0], origin[1], origin[2], pose.water_type,
		pose.water_level);
	SG_BspWorldDestroy(world);
	return 0;
}
