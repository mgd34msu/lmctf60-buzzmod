#include <stdint.h>
#include <stdio.h>

#include "../slipgate/sg_hook_visibility_feasibility.h"
#include "sg_hook_visibility_feasibility_fixture.h"

static int BuildMetrics(hook_visibility_fixture_t *fixture, int16_t minimum_x,
	sg_hook_visibility_feasibility_metrics_t *metrics)
{
	sg_hook_visibility_feasibility_catalog_t *catalog = NULL;
	sg_hook_visibility_feasibility_error_t error;

	fixture->sources.origins.mins[0] = minimum_x;
	if (!SG_HookVisibilityFeasibilityBuild(&fixture->sources, &catalog, &error))
	{
		fprintf(stderr, "scaling build x=%d failed: %s source=%u\n", minimum_x,
			SG_HookVisibilityFeasibilityErrorString(error.code),
			error.source_index);
		return 0;
	}
	if (!SG_HookVisibilityFeasibilityMetrics(catalog, metrics))
	{
		SG_HookVisibilityFeasibilityDestroy(catalog);
		return 0;
	}
	SG_HookVisibilityFeasibilityDestroy(catalog);
	return 1;
}

int main(void)
{
	hook_visibility_fixture_t fixture;
	sg_hook_visibility_feasibility_metrics_t narrow, wide;

	if (!HookVisibilityFixtureInit(&fixture))
		return 2;
	fixture.brushes[4].contents = 0;
	fixture.sources.surface_rule_count = 4U;
	fixture.controls[0].yaw_min = 0;
	fixture.controls[0].yaw_max = 0;
	if (!BuildMetrics(&fixture, -648, &narrow) ||
		!BuildMetrics(&fixture, INT16_MIN, &wide))
		return 3;
	if (narrow.predicate_domains != wide.predicate_domains ||
		narrow.muzzle_clearance_traces != wide.muzzle_clearance_traces ||
		wide.legal_action_tuples <= narrow.legal_action_tuples * UINT64_C(50))
	{
		fprintf(stderr, "event-free widening scaled work: "
			"narrow=%llu/%llu wide=%llu/%llu\n",
			(unsigned long long)narrow.legal_action_tuples,
			(unsigned long long)narrow.predicate_domains,
			(unsigned long long)wide.legal_action_tuples,
			(unsigned long long)wide.predicate_domains);
		return 1;
	}
	printf("event-free widening kept predicate work at %llu while tuples "
		"grew %llu -> %llu\n",
		(unsigned long long)wide.predicate_domains,
		(unsigned long long)narrow.legal_action_tuples,
		(unsigned long long)wide.legal_action_tuples);
	return 0;
}
