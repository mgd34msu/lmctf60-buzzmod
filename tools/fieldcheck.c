/* fieldcheck: load an era-4 artifact, locate two points, build the field to
 * the second, and walk the chain of steps from the first. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "slipgate/sg_rune_artifact.h"
#include "slipgate/sg_rune_field.h"
#include "slipgate/sg_rune_locate.h"

static double Now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
	sg_rune_artifact_t artifact;
	sg_rune_fault_t fault;
	sg_rune_locator_t locator;
	sg_rune_router_t router;
	sg_rune_field_t field;
	float from[3], to[3], violation;
	uint32_t from_cell, to_cell, cell, hops = 0U, kinds[SG_RUNE_MOVE_KIND_COUNT];
	int os_error, crouching = 0;
	double t0, t1, t2, t3;
	sg_rune_artifact_status_t status;

	if (argc != 8 && argc != 2)
	{
		fprintf(stderr, "usage: fieldcheck MAP.rune [x y z x y z]\n");
		return 2;
	}
	if (argc == 2)
	{
		/* Load only: the gate the generation script runs on a fresh file. */
		status = SG_RuneArtifactLoadFile(argv[1], &artifact, &os_error, &fault);
		if (status != SG_RUNE_ARTIFACT_OK)
		{
			fprintf(stderr, "load: %s%s%s\n",
				SG_RuneArtifactStatusString(status), fault.array ? " at " : "",
				fault.array ? fault.array : "");
			return 1;
		}
		printf("rune ok: cells %u portals %u capabilities %u bytes %lu\n",
			(unsigned)artifact.complex.cell_count,
			(unsigned)artifact.complex.portal_count,
			(unsigned)artifact.movement.capability_count,
			(unsigned long)artifact.image_size);
		SG_RuneArtifactRelease(&artifact);
		return 0;
	}
	from[0] = (float)atof(argv[2]); from[1] = (float)atof(argv[3]);
	from[2] = (float)atof(argv[4]);
	to[0] = (float)atof(argv[5]); to[1] = (float)atof(argv[6]);
	to[2] = (float)atof(argv[7]);
	t0 = Now();
	status = SG_RuneArtifactLoadFile(argv[1], &artifact, &os_error, &fault);
	if (status != SG_RUNE_ARTIFACT_OK)
	{
		fprintf(stderr, "load: %s\n", SG_RuneArtifactStatusString(status));
		return 1;
	}
	t1 = Now();
	if (!SG_RuneLocatorBuild(&locator, &artifact) ||
		!SG_RuneRouterBuild(&router, &artifact))
	{
		fprintf(stderr, "index build failed\n");
		return 1;
	}
	t2 = Now();
	printf("load %.2fs, locator+router %.2fs; buckets %ux%ux%u entries %u\n",
		t1 - t0, t2 - t1, locator.dims[0], locator.dims[1], locator.dims[2],
		locator.entry_count);
	{
		uint32_t usable = 0U, c;

		for (c = 0U; c < artifact.movement.capability_count; c++)
			if (router.edge_cost[c] < INFINITY)
				usable++;
		printf("edges usable %u of %u\n", usable,
			artifact.movement.capability_count);
	}
	from_cell = SG_RuneLocate(&locator, from, SG_RUNE_MOVE_STANDING, 8.0f,
		&violation);
	printf("from (%g %g %g): cell %u violation %g\n", from[0], from[1], from[2],
		from_cell, violation);
	to_cell = SG_RuneLocate(&locator, to, SG_RUNE_MOVE_STANDING, 8.0f,
		&violation);
	printf("to   (%g %g %g): cell %u violation %g\n", to[0], to[1], to[2],
		to_cell, violation);
	if (from_cell == SG_RUNE_CX_INDEX_NONE || to_cell == SG_RUNE_CX_INDEX_NONE)
		return 1;
	{
		uint32_t which[2] = { from_cell, to_cell };
		uint32_t i;

		for (i = 0U; i < 2U; i++)
		{
			const sg_rune_cx_cell_t *c = &artifact.complex.cells[which[i]];

			printf("  cell %u: z %g..%g xy %g..%g %g..%g stances %u semantics 0x%x "
				"incidences %u\n", which[i],
				(double)c->bounds.mins.value[2] / 8.0, (double)c->bounds.maxs.value[2] / 8.0,
				(double)c->bounds.mins.value[0] / 8.0, (double)c->bounds.maxs.value[0] / 8.0,
				(double)c->bounds.mins.value[1] / 8.0, (double)c->bounds.maxs.value[1] / 8.0,
				c->valid_stances, c->semantics, c->incidences.count);
		}
	}
	memset(&field, 0, sizeof(field));
	t2 = Now();
	if (!SG_RuneFieldBuild(&field, &router, to_cell))
	{
		fprintf(stderr, "field build failed\n");
		return 1;
	}
	t3 = Now();
	printf("field: settled %u of %u states  [%.3fs]; cost from start %g\n",
		field.settled, field.state_count, t3 - t2,
		field.cost[SG_RUNE_FIELD_STATE(from_cell, 0)]);
	memset(kinds, 0, sizeof(kinds));
	cell = from_cell;
	while (hops < 100000U)
	{
		sg_rune_step_t step;

		if (!SG_RuneStepSelect(&router, &field, cell, crouching, to, &step))
			break;
		if (step.kind != SG_RUNE_STEP_CROSS)
		{
			printf("chain ends: %s after %u hops\n",
				SG_RuneStepKindString(step.kind), hops);
			break;
		}
		if (hops < 12U)
			printf("  hop %u: cell %u %s%s -> portal %u at (%.0f %.0f %.0f) "
				"cost-to-go %.1f\n", hops, cell,
				SG_RuneMoveKindString((sg_rune_move_kind_t)step.move_kind),
				step.crouching_next ? " (crouch)" : "", step.portal,
				step.target[0], step.target[1], step.target[2], step.cost_to_go);
		kinds[step.move_kind]++;
		cell = router.destination[step.capability];
		crouching = step.crouching_next;
		hops++;
	}
	{
		uint32_t k;

		for (k = 0U; k < SG_RUNE_MOVE_KIND_COUNT; k++)
			if (kinds[k])
				printf("  %-14s %u\n", SG_RuneMoveKindString(
					(sg_rune_move_kind_t)k), kinds[k]);
	}
	SG_RuneFieldFree(&field);
	SG_RuneRouterFree(&router);
	SG_RuneLocatorFree(&locator);
	SG_RuneArtifactRelease(&artifact);
	return 0;
}
