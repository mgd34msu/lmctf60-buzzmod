#include <stdio.h>

#include "../slipgate/sg_hook_visibility_feasibility.h"
#include "sg_hook_visibility_feasibility_fixture.h"

int main(void)
{
	hook_visibility_fixture_t fixture;
	sg_hook_visibility_feasibility_catalog_t *catalog = NULL;
	sg_hook_visibility_feasibility_error_t error;
	sg_host_collision_error_t collision_error;
	sg_rune_model_identity_t identity;

	if (!HookVisibilityFixtureInit(&fixture))
		return 2;
	fixture.planes[1].normal.value[0] = 0.70710677f;
	fixture.planes[1].normal.value[1] = 0.70710677f;
	fixture.planes[1].normal.value[2] = 0.0f;
	fixture.planes[1].distance = 0.0f;
	fixture.planes[1].type = 3;
	identity = fixture.authority.identity;
	if (!SG_HostCollisionInit(&fixture.authority, &fixture.world, &identity,
			&collision_error))
	{
		fprintf(stderr, "slanted host fixture rejected: %s\n",
			SG_HostCollisionErrorString(collision_error));
		return 3;
	}
	if (SG_HookVisibilityFeasibilityBuild(&fixture.sources, &catalog, &error) ||
		error.code != SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_UNSUPPORTED)
	{
		fprintf(stderr, "slanted shifted-origin family did not fail closed: %s\n",
			SG_HookVisibilityFeasibilityErrorString(error.code));
		SG_HookVisibilityFeasibilityDestroy(catalog);
		return 1;
	}
	puts("slanted shifted-origin family failed closed");
	return 0;
}
