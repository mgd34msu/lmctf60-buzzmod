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

static int HostHit(const hook_visibility_fixture_t *fixture, int16_t yaw,
	int16_t origin_x_q8)
{
	const float zero[3] = {0.0f, 0.0f, 0.0f};
	float origin[3] = {(float)origin_x_q8 * 0.125f, -0.125f, 0.0f};
	float forward[3], right[3], muzzle[3], end[3];
	sg_host_collision_trace_t clearance, hit;
	uint32_t axis;

	HookVisibilityProductionDirection(0, yaw, forward, right);
	for (axis = 0U; axis < 3U; axis++)
	{
		muzzle[axis] = origin[axis] + forward[axis] * 8.0f;
		end[axis] = muzzle[axis] + forward[axis] * 1000.0f;
	}
	muzzle[2] += 14.0f;
	end[2] += 14.0f;
	if (!SG_HostCollisionTrace(&fixture->authority, NULL, origin, zero, zero,
			muzzle, SG_HOOK_VISIBILITY_MASK_SHOT, &clearance) ||
		clearance.startsolid || clearance.allsolid || clearance.fraction < 1.0f ||
		!SG_HostCollisionTrace(&fixture->authority, NULL, muzzle, zero, zero,
			end, SG_HOOK_VISIBILITY_MASK_SHOT, &hit))
		return -1;
	return hit.fraction < 1.0f && hit.texinfo == 0U;
}

static int CatalogOutcome(
	const sg_hook_visibility_feasibility_catalog_t *catalog, int16_t yaw,
	int16_t origin_x_q8)
{
	uint32_t terminal;

	for (terminal = 0U; terminal < catalog->terminal_count; terminal++)
	{
		const sg_hook_visibility_terminal_t *record =
			&catalog->terminals[terminal];

		if (record->domain.hand_mask == SG_HOOK_VISIBILITY_HAND_BIT(
				SG_HOOK_VISIBILITY_HAND_CENTER) &&
			origin_x_q8 >= record->domain.origins.mins[0] &&
			origin_x_q8 <= record->domain.origins.maxs[0] &&
			yaw >= record->domain.yaw_min && yaw <= record->domain.yaw_max)
			return record->outcome == SG_HOOK_VISIBILITY_TERMINAL_HOOKABLE;
	}
	return -1;
}

int main(void)
{
	hook_visibility_fixture_t fixture;
	sg_hook_visibility_feasibility_catalog_t *catalog = NULL;
	sg_hook_visibility_feasibility_error_t error;
	sg_hook_visibility_feasibility_audit_report_t audit;
	sg_rune_model_identity_t identity;
	sg_host_collision_error_t collision_error;

	if (!HookVisibilityFixtureInit(&fixture))
		return 2;
	SetPlane(&fixture.planes[1], 1.0f, 0.0f, 0.0f, -900.0f);
	SetPlane(&fixture.planes[2], -1.0f, 0.0f, 0.0f, 901.0f);
	SetPlane(&fixture.planes[3], 0.0f, 1.0f, 0.0f, 64.0f);
	SetPlane(&fixture.planes[4], 0.0f, -1.0f, 0.0f, 0.0f);
	SetPlane(&fixture.planes[5], 0.0f, 0.0f, 1.0f, 64.0f);
	SetPlane(&fixture.planes[6], 0.0f, 0.0f, -1.0f, 64.0f);
	fixture.brushes[1].contents = 0;
	fixture.brushes[2].contents = 0;
	fixture.brushes[3].contents = 0;
	fixture.brushes[4].contents = 0;
	identity = fixture.authority.identity;
	if (!SG_HostCollisionInit(&fixture.authority, &fixture.world, &identity,
			&collision_error))
		return 3;
	fixture.controls[0].pitch_min = 0;
	fixture.controls[0].pitch_max = 0;
	fixture.controls[0].yaw_min = 32766;
	fixture.controls[0].yaw_max = 32767;
	fixture.sources.control_count = 1U;
	fixture.sources.surface_rule_count = 1U;
	fixture.sources.origins.mins[0] = -4000;
	fixture.sources.origins.maxs[0] = 0;
	fixture.sources.origins.mins[1] = -1;
	fixture.sources.origins.maxs[1] = -1;
	fixture.sources.origins.mins[2] = 0;
	fixture.sources.origins.maxs[2] = 0;
	fixture.sources.fire_law.maximum_range = 1000.0f;
	if (!SG_HookVisibilityFeasibilityBuild(&fixture.sources, &catalog, &error))
	{
		fprintf(stderr, "reverse build failed: %s record=%u\n",
			SG_HookVisibilityFeasibilityErrorString(error.code),
			error.source_index);
		return 4;
	}
	if (!SG_HookVisibilityFeasibilityAudit(&fixture.sources, catalog, &audit))
	{
		const sg_hook_visibility_domain_term_t *bad =
			&catalog->terminals[audit.record].domain;
		fprintf(stderr, "reverse audit failed: %s record=%u "
			"x=%d..%d y=%d..%d z=%d..%d yaw=%d\n",
			SG_HookVisibilityFeasibilityAuditCodeString(audit.code), audit.record,
			bad->origins.mins[0], bad->origins.maxs[0],
			bad->origins.mins[1], bad->origins.maxs[1],
			bad->origins.mins[2], bad->origins.maxs[2], bad->yaw_min);
		return 5;
	}
	if (HostHit(&fixture, 32766, 0) != 1 ||
		HostHit(&fixture, 32767, 0) != 0 ||
		HostHit(&fixture, 32766, -4000) != 0 ||
		CatalogOutcome(catalog, 32766, 0) != 1 ||
		CatalogOutcome(catalog, 32767, 0) != 0 ||
		CatalogOutcome(catalog, 32766, -4000) != 0)
	{
		fprintf(stderr,
			"reverse span/origin not separated: host=%d/%d/%d "
			"catalog=%d/%d/%d\n",
			HostHit(&fixture, 32766, 0), HostHit(&fixture, 32767, 0),
			HostHit(&fixture, 32766, -4000),
			CatalogOutcome(catalog, 32766, 0),
			CatalogOutcome(catalog, 32767, 0),
			CatalogOutcome(catalog, 32766, -4000));
		SG_HookVisibilityFeasibilityDestroy(catalog);
		return 1;
	}
	SG_HookVisibilityFeasibilityDestroy(catalog);
	puts("reverse short-angle event cells matched production hits");
	return 0;
}
