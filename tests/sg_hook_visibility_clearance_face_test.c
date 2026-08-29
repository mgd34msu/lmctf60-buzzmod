#include <stdint.h>
#include <stdio.h>

#include "../slipgate/sg_hook_visibility_feasibility_internal.h"
#include "sg_hook_visibility_feasibility_fixture.h"

static void SetPlane(sg_bsp_plane_t *plane, float x, float y, float z,
	float distance)
{
	plane->normal.value[0] = x;
	plane->normal.value[1] = y;
	plane->normal.value[2] = z;
	plane->distance = distance;
	if (x == 1.0f) plane->type = 0;
	else if (y == 1.0f) plane->type = 1;
	else if (z == 1.0f) plane->type = 2;
	else if (x == -1.0f) plane->type = 3;
	else if (y == -1.0f) plane->type = 4;
	else plane->type = 5;
}

static void SetBox(hook_visibility_fixture_t *fixture, uint32_t brush,
	int16_t min_x, int16_t max_x, int16_t min_y, int16_t max_y,
	int16_t min_z, int16_t max_z)
{
	uint32_t first = 1U + brush * 6U;

	SetPlane(&fixture->planes[first], 1.0f, 0.0f, 0.0f,
		(float)max_x * 0.125f);
	SetPlane(&fixture->planes[first + 1U], -1.0f, 0.0f, 0.0f,
		(float)-min_x * 0.125f);
	SetPlane(&fixture->planes[first + 2U], 0.0f, 1.0f, 0.0f,
		(float)max_y * 0.125f);
	SetPlane(&fixture->planes[first + 3U], 0.0f, -1.0f, 0.0f,
		(float)-min_y * 0.125f);
	SetPlane(&fixture->planes[first + 4U], 0.0f, 0.0f, 1.0f,
		(float)max_z * 0.125f);
	SetPlane(&fixture->planes[first + 5U], 0.0f, 0.0f, -1.0f,
		(float)-min_z * 0.125f);
}

static int HostClearanceBlocked(const hook_visibility_fixture_t *fixture,
	const int16_t origin_q8[3])
{
	const float zero[3] = {0.0f, 0.0f, 0.0f};
	float origin[3], forward[3], right[3], muzzle[3];
	sg_host_collision_trace_t clearance;
	uint32_t axis;

	HookVisibilityProductionDirection(0, -1, forward, right);
	for (axis = 0U; axis < 3U; axis++)
	{
		origin[axis] = (float)origin_q8[axis] * 0.125f;
		muzzle[axis] = origin[axis] + forward[axis] * 8.0f -
			right[axis] * 8.0f;
	}
	muzzle[2] += 14.0f;
	if (!SG_HostCollisionTrace(&fixture->authority, NULL, origin, zero, zero,
			muzzle, SG_HOOK_VISIBILITY_MASK_SHOT, &clearance))
		return -1;
	return clearance.startsolid || clearance.allsolid ||
		clearance.fraction < 1.0f;
}

static const sg_hook_visibility_terminal_t *CatalogTerminal(
	const sg_hook_visibility_feasibility_catalog_t *catalog,
	const int16_t origin_q8[3])
{
	uint32_t terminal, axis;

	for (terminal = 0U; terminal < catalog->terminal_count; terminal++)
	{
		const sg_hook_visibility_terminal_t *record =
			&catalog->terminals[terminal];

		if (record->domain.pitch_min != 0 || record->domain.pitch_max != 0 ||
			record->domain.yaw_min != -1 || record->domain.yaw_max != -1 ||
			record->domain.hand_mask != SG_HOOK_VISIBILITY_HAND_BIT(
				SG_HOOK_VISIBILITY_HAND_LEFT))
			continue;
		for (axis = 0U; axis < 3U; axis++)
			if (origin_q8[axis] < record->domain.origins.mins[axis] ||
				origin_q8[axis] > record->domain.origins.maxs[axis])
				break;
		if (axis == 3U)
			return record;
	}
	return NULL;
}

int main(void)
{
	const int16_t action_origins[2][3] = {
		{-16, -11, 2},
		{-16, 2, -11}
	};
	hook_visibility_fixture_t fixture;
	sg_hook_visibility_feasibility_catalog_t *catalog = NULL;
	sg_hook_visibility_feasibility_error_t error;
	sg_host_collision_error_t collision_error;
	sg_rune_model_identity_t identity;
	uint32_t action, rule;

	if (!HookVisibilityFixtureInit(&fixture))
		return 2;
	SetBox(&fixture, 0U, 71, 73, -128, 3, -128, 112);
	SetBox(&fixture, 1U, 71, 73, 3, 128, -128, 105);
	SetBox(&fixture, 2U, 71, 73, -128, 3, 105, 128);
	SetBox(&fixture, 3U, 71, 73, 3, 128, 105, 128);
	SetBox(&fixture, 4U, -32, -9, -8, 8, -8, 8);
	fixture.texinfos[1].flags = 0U;
	for (rule = 0U; rule < 4U; rule++)
		fixture.rules[rule].classification =
			SG_HOOK_VISIBILITY_SURFACE_HOOKABLE;
	fixture.sources.origins.mins[0] = -16;
	fixture.sources.origins.maxs[0] = -1;
	fixture.sources.origins.mins[1] = -12;
	fixture.sources.origins.maxs[1] = 12;
	fixture.sources.origins.mins[2] = -12;
	fixture.sources.origins.maxs[2] = 12;
	fixture.controls[0].pitch_min = 0;
	fixture.controls[0].pitch_max = 0;
	fixture.controls[0].yaw_min = -1;
	fixture.controls[0].yaw_max = -1;
	fixture.sources.fire_law.maximum_range = 8.0f;
	identity = fixture.authority.identity;
	if (!SG_HostCollisionInit(&fixture.authority, &fixture.world, &identity,
			&collision_error))
		return 3;
	if (!SG_HookVisibilityFeasibilityBuild(&fixture.sources, &catalog, &error))
	{
		fprintf(stderr, "clearance-face build failed: %s source=%u\n",
			SG_HookVisibilityFeasibilityErrorString(error.code),
			error.source_index);
		return 4;
	}
	for (action = 0U; action < 2U; action++)
	{
		const sg_hook_visibility_terminal_t *terminal =
			CatalogTerminal(catalog, action_origins[action]);

		if (HostClearanceBlocked(&fixture, action_origins[action]) != 1 ||
			!terminal || terminal->outcome !=
				SG_HOOK_VISIBILITY_TERMINAL_CLEARANCE_BLOCKED)
		{
			fprintf(stderr, "%c-face clearance action disagreed: host=%d ",
				action == 0U ? 'Y' : 'Z',
				HostClearanceBlocked(&fixture, action_origins[action]));
			if (terminal)
				fprintf(stderr, "catalog=%d domain=%d..%d/%d..%d/%d..%d\n",
					(int)terminal->outcome,
					terminal->domain.origins.mins[0],
					terminal->domain.origins.maxs[0],
					terminal->domain.origins.mins[1],
					terminal->domain.origins.maxs[1],
					terminal->domain.origins.mins[2],
					terminal->domain.origins.maxs[2]);
			else
				fputs("catalog=missing\n", stderr);
			SG_HookVisibilityFeasibilityDestroy(catalog);
			return 1;
		}
	}
	SG_HookVisibilityFeasibilityDestroy(catalog);
	puts("Y/Z-face clearance entries matched the host terminals");
	return 0;
}
