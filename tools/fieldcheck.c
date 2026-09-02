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
#include "slipgate/sg_rune_flight.h"
#include "slipgate/sg_rune_locate.h"

static double Now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static float Bits(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

/* Whether cell a's vertex centroid lies inside cell b, half a unit in. */
static int CentreInside(const sg_rune_cx_view_t *cx, uint32_t a, uint32_t b)
{
	const sg_rune_cx_cell_t *ca = &cx->cells[a], *cb = &cx->cells[b];
	double centre[3] = { 0.0, 0.0, 0.0 };
	uint32_t slot, n = 0U;

	for (slot = 0U; slot < ca->incidences.count; slot++)
	{
		const sg_rune_cx_incidence_t *inc = &cx->incidences[
			cx->cell_incidences[ca->incidences.first + slot]];
		const sg_rune_cx_facet_t *facet = &cx->facets[inc->facet];
		uint32_t v;

		for (v = 0U; v < facet->vertices.count; v++)
		{
			const sg_rune_cx_vec3_t *q = &cx->vertices[facet->vertices.first + v];

			centre[0] += q->value[0] / 8.0;
			centre[1] += q->value[1] / 8.0;
			centre[2] += q->value[2] / 8.0;
			n++;
		}
	}
	if (!n)
		return 0;
	centre[0] /= n; centre[1] /= n; centre[2] /= n;
	for (slot = 0U; slot < cb->incidences.count; slot++)
	{
		const sg_rune_cx_incidence_t *inc = &cx->incidences[
			cx->cell_incidences[cb->incidences.first + slot]];
		const sg_rune_cx_facet_t *facet = &cx->facets[inc->facet];
		double nx = Bits(facet->plane.normal_bits[0]), ny = Bits(facet->plane.normal_bits[1]);
		double nz = Bits(facet->plane.normal_bits[2]), d = Bits(facet->plane.distance_bits);

		if (inc->side == SG_RUNE_CX_POSITIVE_SIDE)
		{
			nx = -nx; ny = -ny; nz = -nz; d = -d;
		}
		if (nx * centre[0] + ny * centre[1] + nz * centre[2] - d > -0.5)
			return 0;
	}
	return 1;
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

	if (argc != 8 && argc != 2 && argc != 3 && argc != 9 && argc != 7)
	{
		fprintf(stderr, "usage: fieldcheck MAP.rune [x y z x y z | x y z c<cell> 0 0 | d<cell> | f x y z vx vy vz | n x y z r]\n");
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
		{
			uint32_t c, supported = 0U, kinds[SG_RUNE_MOVE_KIND_COUNT] = { 0 }, k;

			for (c = 0U; c < artifact.complex.cell_count; c++)
				if (artifact.complex.cells[c].semantics & SG_RUNE_CX_CELL_SUPPORTED)
					supported++;
			for (c = 0U; c < artifact.movement.capability_count; c++)
				kinds[artifact.movement.capabilities[c].kind]++;
			printf("supported cells %u\n", supported);
			for (k = 0U; k < SG_RUNE_MOVE_KIND_COUNT; k++)
				if (kinds[k])
					printf("  %-14s %u\n", SG_RuneMoveKindString(
						(sg_rune_move_kind_t)k), kinds[k]);
		}
		{
			const sg_rune_cx_view_t *cx = &artifact.complex;
			const sg_rune_move_table_t *mv = &artifact.movement;
#define SECTION(name, count, type) printf("  %-18s %9u x %3zu = %6.1f MB\n", name, \
	(unsigned)(count), sizeof(type), (double)(count) * sizeof(type) / 1048576.0)
			SECTION("cells", cx->cell_count, sg_rune_cx_cell_t);
			SECTION("facets", cx->facet_count, sg_rune_cx_facet_t);
			SECTION("incidences", cx->incidence_count, sg_rune_cx_incidence_t);
			SECTION("cell incidences", cx->cell_incidence_count, uint32_t);
			SECTION("vertices", cx->vertex_count, sg_rune_cx_vec3_t);
			SECTION("portals", cx->portal_count, sg_rune_cx_portal_t);
			SECTION("surfaces", cx->surface_count, sg_rune_cx_surface_t);
			SECTION("surface vertices", cx->surface_vertex_count, sg_rune_cx_vec3_t);
			SECTION("capabilities", mv->capability_count, sg_rune_move_capability_t);
			SECTION("fire cells", artifact.fires.cell_count, sg_rune_fire_cell_t);
			SECTION("fires", artifact.fires.record_count, sg_rune_fire_t);
#undef SECTION
		}
		printf("rune ok: cells %u portals %u capabilities %u bytes %lu\n",
			(unsigned)artifact.complex.cell_count,
			(unsigned)artifact.complex.portal_count,
			(unsigned)artifact.movement.capability_count,
			(unsigned long)artifact.image_size);
		SG_RuneArtifactRelease(&artifact);
		return 0;
	}
	/* "n x y z r": every cell whose box comes within r of the point. */
	if (argc == 7 && argv[2][0] == 'n' && argv[2][1] == 0)
	{
		double p[3] = { atof(argv[3]), atof(argv[4]), atof(argv[5]) }, r = atof(argv[6]);
		uint32_t i;

		status = SG_RuneArtifactLoadFile(argv[1], &artifact, &os_error, &fault);
		if (status != SG_RUNE_ARTIFACT_OK)
		{
			fprintf(stderr, "load: %s\n", SG_RuneArtifactStatusString(status));
			return 1;
		}
		for (i = 0; i < artifact.complex.cell_count; i++)
		{
			const sg_rune_cx_cell_t *c = &artifact.complex.cells[i];
			int k, near = 1;

			for (k = 0; k < 3; k++)
				if (p[k] < c->bounds.mins.value[k] / 8.0 - r || p[k] > c->bounds.maxs.value[k] / 8.0 + r)
					near = 0;
			if (near)
				printf("cell %u: z %g..%g xy %g..%g %g..%g stances %u semantics 0x%x leaf %u contents 0x%x\n", i,
					(double)c->bounds.mins.value[2] / 8.0, (double)c->bounds.maxs.value[2] / 8.0,
					(double)c->bounds.mins.value[0] / 8.0, (double)c->bounds.maxs.value[0] / 8.0,
					(double)c->bounds.mins.value[1] / 8.0, (double)c->bounds.maxs.value[1] / 8.0,
					c->valid_stances, c->semantics, (unsigned)c->source.leaf, (unsigned)c->contents);
		}
		SG_RuneArtifactRelease(&artifact);
		return 0;
	}
	/* "d<N>": every departure from cell N, with what the record holds. */
	if (argc == 3 && argv[2][0] == 'd' && argv[2][1] >= '0' && argv[2][1] <= '9')
	{
		uint32_t cell_index = (uint32_t)strtoul(argv[2] + 1, NULL, 10), slot;

		status = SG_RuneArtifactLoadFile(argv[1], &artifact, &os_error, &fault);
		if (status != SG_RUNE_ARTIFACT_OK)
		{
			fprintf(stderr, "load: %s\n", SG_RuneArtifactStatusString(status));
			return 1;
		}
		if (!SG_RuneRouterBuild(&router, &artifact) ||
			cell_index >= artifact.complex.cell_count)
		{
			fprintf(stderr, "no such cell\n");
			return 1;
		}
		{
			const sg_rune_cx_cell_t *c = &artifact.complex.cells[cell_index];

			printf("cell %u: z %g..%g xy %g..%g %g..%g stances %u semantics 0x%x "
				"centre (%.0f %.0f %.0f)\n", cell_index,
				(double)c->bounds.mins.value[2] / 8.0, (double)c->bounds.maxs.value[2] / 8.0,
				(double)c->bounds.mins.value[0] / 8.0, (double)c->bounds.maxs.value[0] / 8.0,
				(double)c->bounds.mins.value[1] / 8.0, (double)c->bounds.maxs.value[1] / 8.0,
				c->valid_stances, c->semantics,
				router.cell_center[cell_index * 3U], router.cell_center[cell_index * 3U + 1U],
				router.cell_center[cell_index * 3U + 2U]);
		}
		{
			const sg_rune_cx_view_t *cx = &artifact.complex;
			const sg_rune_cx_cell_t *c = &cx->cells[cell_index];
			uint32_t k;

			for (k = 0U; k < c->incidences.count; k++)
			{
				const sg_rune_cx_incidence_t *inc = &cx->incidences[
					cx->cell_incidences[c->incidences.first + k]];
				const sg_rune_cx_facet_t *facet = &cx->facets[inc->facet];
				float nx = Bits(facet->plane.normal_bits[0]), ny = Bits(facet->plane.normal_bits[1]);
				float nz = Bits(facet->plane.normal_bits[2]), d = Bits(facet->plane.distance_bits);

				if (inc->side == SG_RUNE_CX_POSITIVE_SIDE)
				{
					nx = -nx; ny = -ny; nz = -nz; d = -d;
				}
				printf("  facet %u out (%.2f %.2f %.2f) d %.1f %s source %u\n", inc->facet,
					nx, ny, nz, d, facet->incidences.count == 1U ? "closed" :
					(facet->portal != SG_RUNE_CX_INDEX_NONE ? "portal" : "shared"),
					facet->source.kind);
			}
		}
		for (slot = router.departure_first[cell_index];
			slot < router.departure_first[cell_index + 1U]; slot++)
		{
			uint32_t capability = router.departures[slot];
			const sg_rune_move_capability_t *r =
				&artifact.movement.capabilities[capability];
			const float *centre = &router.cell_center[r->destination * 3U];

			printf("  cap %u %-13s -> cell %u (%.0f %.0f %.0f) portal %u stances %u/%u "
				"cost %.2f seconds %.2f", capability,
				SG_RuneMoveKindString((sg_rune_move_kind_t)r->kind), r->destination,
				centre[0], centre[1], centre[2], r->portal, r->source_stances,
				r->destination_stances, router.edge_cost[capability], r->seconds);
			if (r->kind == SG_RUNE_MOVE_HOOK)
				printf(" anchor (%.0f %.0f %.0f) release %.0f", r->anchor[0],
					r->anchor[1], r->anchor[2], r->parameter);
			if (r->kind == SG_RUNE_MOVE_JUMP || r->kind == SG_RUNE_MOVE_DROP ||
				r->kind == SG_RUNE_MOVE_ROCKET_JUMP)
				printf(" launch (%.0f %.0f %.0f)", r->launch_velocity[0],
					r->launch_velocity[1], r->launch_velocity[2]);
			printf("\n");
		}
		SG_RuneRouterFree(&router);
		SG_RuneArtifactRelease(&artifact);
		return 0;
	}
	/* "o": cells whose boxes overlap another's by a body's width or more
	 * and whose centre lies inside the other; the complex is meant to
	 * partition free space, so these are defects. */
	if (argc == 3 && !strcmp(argv[2], "o"))
	{
		const sg_rune_cx_cell_t *cells;
		uint32_t count, a, b, found = 0U, shown = 0U;
		uint32_t *order;

		status = SG_RuneArtifactLoadFile(argv[1], &artifact, &os_error, &fault);
		if (status != SG_RUNE_ARTIFACT_OK)
		{
			fprintf(stderr, "load: %s\n", SG_RuneArtifactStatusString(status));
			return 1;
		}
		cells = artifact.complex.cells;
		count = artifact.complex.cell_count;
		order = malloc((size_t)count * sizeof(*order));
		for (a = 0U; a < count; a++)
			order[a] = a;
		/* Sorted by min x so the scan stops early. */
		{
			uint32_t i, j;

			for (i = 1U; i < count; i++)
			{
				uint32_t key = order[i];
				int32_t kx = cells[key].bounds.mins.value[0];

				for (j = i; j > 0U && cells[order[j - 1U]].bounds.mins.value[0] > kx; j--)
					order[j] = order[j - 1U];
				order[j] = key;
			}
		}
		for (a = 0U; a < count; a++)
		{
			const sg_rune_cx_cell_t *ca = &cells[order[a]];

			for (b = a + 1U; b < count; b++)
			{
				const sg_rune_cx_cell_t *cb = &cells[order[b]];
				int32_t ox, oy, oz;

				if (cb->bounds.mins.value[0] >= ca->bounds.maxs.value[0] - 8 * 4)
					break;
				ox = (ca->bounds.maxs.value[0] < cb->bounds.maxs.value[0] ? ca->bounds.maxs.value[0] : cb->bounds.maxs.value[0]) -
					(ca->bounds.mins.value[0] > cb->bounds.mins.value[0] ? ca->bounds.mins.value[0] : cb->bounds.mins.value[0]);
				oy = (ca->bounds.maxs.value[1] < cb->bounds.maxs.value[1] ? ca->bounds.maxs.value[1] : cb->bounds.maxs.value[1]) -
					(ca->bounds.mins.value[1] > cb->bounds.mins.value[1] ? ca->bounds.mins.value[1] : cb->bounds.mins.value[1]);
				oz = (ca->bounds.maxs.value[2] < cb->bounds.maxs.value[2] ? ca->bounds.maxs.value[2] : cb->bounds.maxs.value[2]) -
					(ca->bounds.mins.value[2] > cb->bounds.mins.value[2] ? ca->bounds.mins.value[2] : cb->bounds.mins.value[2]);
				if (ox < 8 * 4 || oy < 8 * 4 || oz < 8 * 4)
					continue;
				if (!CentreInside(&artifact.complex, order[a], order[b]) &&
					!CentreInside(&artifact.complex, order[b], order[a]))
					continue;
				found++;
				if (shown < 12U)
				{
					shown++;
					printf("overlap %u (z %g..%g xy %g..%g %g..%g st %u) and %u (z %g..%g xy %g..%g %g..%g st %u)\n",
						order[a], ca->bounds.mins.value[2] / 8.0, ca->bounds.maxs.value[2] / 8.0,
						ca->bounds.mins.value[0] / 8.0, ca->bounds.maxs.value[0] / 8.0,
						ca->bounds.mins.value[1] / 8.0, ca->bounds.maxs.value[1] / 8.0, ca->valid_stances,
						order[b], cb->bounds.mins.value[2] / 8.0, cb->bounds.maxs.value[2] / 8.0,
						cb->bounds.mins.value[0] / 8.0, cb->bounds.maxs.value[0] / 8.0,
						cb->bounds.mins.value[1] / 8.0, cb->bounds.maxs.value[1] / 8.0, cb->valid_stances);
				}
			}
		}
		printf("overlapping pairs (box overlap >= 4 on every axis): %u of %u cells\n", found, count);
		free(order);
		SG_RuneArtifactRelease(&artifact);
		return 0;
	}
	/* "f x y z vx vy vz": the flight of a body launched there. */
	if (argc == 9 && !strcmp(argv[2], "f"))
	{
		float origin[3], velocity[3];
		sg_rune_flight_t flight;
		uint32_t start;

		origin[0] = (float)atof(argv[3]); origin[1] = (float)atof(argv[4]);
		origin[2] = (float)atof(argv[5]);
		velocity[0] = (float)atof(argv[6]); velocity[1] = (float)atof(argv[7]);
		velocity[2] = (float)atof(argv[8]);
		status = SG_RuneArtifactLoadFile(argv[1], &artifact, &os_error, &fault);
		if (status != SG_RUNE_ARTIFACT_OK || !SG_RuneLocatorBuild(&locator, &artifact))
		{
			fprintf(stderr, "load failed\n");
			return 1;
		}
		start = SG_RuneLocate(&locator, origin, 0U, 8.0f, &violation);
		printf("start cell %u violation %g\n", start, violation);
		if (start != SG_RUNE_CX_INDEX_NONE &&
			SG_RuneFlightTrace(&artifact.complex, &artifact.law, start, origin,
				velocity, &flight))
			printf("flight: outcome %d landing cell %u at (%.0f %.0f %.0f) after %.2fs, "
				"%u crossings, velocity (%.0f %.0f %.0f)\n", (int)flight.outcome,
				flight.landing_cell, flight.landing[0], flight.landing[1],
				flight.landing[2], flight.seconds, flight.crossings,
				flight.landing_velocity[0], flight.landing_velocity[1],
				flight.landing_velocity[2]);
		SG_RuneLocatorFree(&locator);
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
	/* "c<N>" as the destination x names cell N directly. */
	if (argv[5][0] == 'c' && argv[5][1] >= '0' && argv[5][1] <= '9')
	{
		to_cell = (uint32_t)strtoul(argv[5] + 1, NULL, 10);
		if (to_cell < artifact.complex.cell_count)
		{
			const sg_rune_cx_cell_t *c = &artifact.complex.cells[to_cell];

			to[0] = (float)(c->bounds.mins.value[0] + c->bounds.maxs.value[0]) / 16.0f;
			to[1] = (float)(c->bounds.mins.value[1] + c->bounds.maxs.value[1]) / 16.0f;
			to[2] = (float)c->bounds.mins.value[2] / 8.0f + 1.0f;
		}
	}
	else
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
