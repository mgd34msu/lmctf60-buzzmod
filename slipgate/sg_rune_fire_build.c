/* Era-4 fire relations, the build: traces and arcs between floor cells.
 * Offline only (the generator module and the tools); the runtime keeps the
 * lookup in sg_rune_fire.c. */
#include "sg_rune_fire.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "sg_rune_bsp.h"
#include "sg_rune_trace.h"
#include "sg_rune_law.h"
#include "sg_rune_artifact.h"
#include "sg_rune_cx_build.h"
#include "sg_rune_flight.h"
#include "sg_rune_locate.h"
#include "sg_rune_vis.h"
#include "sg_engine_facts.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FIRE_RANGE 1536.0f          /* beyond this nothing is recorded */
#define LOB_RANGE 1200.0f
#define FLOOR_MIN_EXTENT 32.0f      /* slivers are not posts or targets */
#define EYE_HEIGHT 22.0f
#define BODY_HEIGHT 24.0f
#define FEET_HEIGHT 8.0f            /* where a rocket at the feet bursts */
#define PROJECTILE_HALF 4.0f
#define BLAST_RADIUS SG_FACT_ROCKET_SPLASH_RADIUS
#define LOB_RADIUS SG_FACT_GRENADE_RADIUS
#define LOB_SPEED SG_FACT_GRENADE_SPEED
#define LOB_UP SG_FACT_GRENADE_RISE
#define LOB_FUSE SG_FACT_GRENADE_FUSE

static const float lob_pitches[] = { 10.0f, 25.0f, 40.0f, 55.0f };
#define LOB_PITCHES (sizeof(lob_pitches) / sizeof(lob_pitches[0]))

typedef struct floor_s
{
	uint32_t cell;
	float stand[3];
	int32_t cluster;
	uint32_t next;            /* next floor in the same cluster */
	uint8_t representative;
} floor_t;

typedef struct fire_build_s
{
	const sg_rune_bsp_t *bsp;
	sg_rune_cx_view_t view;
	const sg_rune_law_t *law;
	sg_rune_fire_store_t *store;
	sg_rune_artifact_t artifact;
	sg_rune_locator_t locator;
	sg_rune_vis_t vis;
	floor_t *floors;
	uint32_t floor_count;
	uint32_t *cluster_first;
	sg_rune_fire_t *scratch;  /* one source's records before sorting */
	uint32_t scratch_capacity;
	sg_rune_fire_report_t report;
} fire_build_t;

static int Reserve(sg_rune_fire_store_t *store, uint32_t more)
{
	uint32_t need = store->record_count + more, capacity;
	sg_rune_fire_t *grown;

	if (need <= store->record_capacity)
		return 1;
	capacity = store->record_capacity ? store->record_capacity : 4096U;
	while (capacity < need)
		capacity *= 2U;
	grown = realloc(store->records, (size_t)capacity * sizeof(*grown));
	if (!grown)
		return 0;
	store->records = grown;
	store->record_capacity = capacity;
	return 1;
}

/* ---- traces ------------------------------------------------------------------------ */

static int Clear(fire_build_t *build, const float from[3], const float to[3],
	float half, float *reach_out)
{
	sg_rune_trace_t trace;
	float mins[3] = { -half, -half, -half }, maxs[3] = { half, half, half };
	float dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
	float length = sqrtf(dx * dx + dy * dy + dz * dz);

	build->report.traces++;
	if (reach_out)
		*reach_out = 0.0f;
	if (!SG_RuneTraceBox(build->bsp, 0U, NULL, from, mins, maxs, to,
		SG_RUNE_MASK_PLAYER_SOLID, &trace) || trace.startsolid)
		return 0;
	if (reach_out)
		*reach_out = trace.fraction * length;
	return trace.fraction * length >= length - 1.0f;
}

static int Lobs(fire_build_t *build, uint32_t source_cell, const float eye[3],
	const float feet[3])
{
	uint32_t eye_cell = SG_RuneLocate(&build->locator, eye, SG_RUNE_CX_STANCE_STANDING, 0.0f, NULL);
	float dx = feet[0] - eye[0], dy = feet[1] - eye[1];
	float flat = sqrtf(dx * dx + dy * dy);
	size_t p;

	if (eye_cell == SG_RUNE_CX_INDEX_NONE)
		eye_cell = source_cell;
	if (flat < 1.0f)
		return 0;
	dx /= flat;
	dy /= flat;
	for (p = 0U; p < LOB_PITCHES; p++)
	{
		float radians = lob_pitches[p] * (float)M_PI / 180.0f;
		float c = cosf(radians), s = sinf(radians);
		float velocity[3];
		sg_rune_flight_t flight;
		float lx, ly, lz;

		/* forward = (c d, s), up = (-s d, c): the launcher's own frame. */
		velocity[0] = dx * (c * LOB_SPEED - s * LOB_UP);
		velocity[1] = dy * (c * LOB_SPEED - s * LOB_UP);
		velocity[2] = s * LOB_SPEED + c * LOB_UP;
		build->report.arcs++;
		if (!SG_RuneFlightTrace(&build->view, build->law, eye_cell, eye, velocity,
			&flight))
			continue;
		if (flight.outcome != SG_RUNE_FLIGHT_LANDED || flight.seconds > LOB_FUSE)
			continue;
		lx = flight.landing[0] - feet[0];
		ly = flight.landing[1] - feet[1];
		lz = flight.landing[2] - feet[2];
		if (lx * lx + ly * ly + lz * lz <= LOB_RADIUS * LOB_RADIUS)
			return 1;
	}
	return 0;
}

/* ---- floors ------------------------------------------------------------------------ */

static int CollectFloors(fire_build_t *build)
{
	const sg_rune_cx_view_t *cx = &build->view;
	uint32_t cell, index;

	build->floors = malloc((size_t)(cx->cell_count ? cx->cell_count : 1U) *
		sizeof(*build->floors));
	build->cluster_first = malloc((size_t)build->vis.cluster_count * sizeof(uint32_t));
	if (!build->floors || !build->cluster_first)
		return 0;
	for (index = 0U; index < build->vis.cluster_count; index++)
		build->cluster_first[index] = SG_RUNE_CX_INDEX_NONE;
	for (cell = 0U; cell < cx->cell_count; cell++)
	{
		const sg_rune_cx_cell_t *record = &cx->cells[cell];
		floor_t *floor;

		if (!(record->semantics & SG_RUNE_CX_CELL_SUPPORTED) ||
			(record->semantics & SG_RUNE_CX_CELL_WATER))
			continue;
		floor = &build->floors[build->floor_count];
		floor->representative = (uint8_t)(
			(float)(record->bounds.maxs.value[0] - record->bounds.mins.value[0]) >=
			FLOOR_MIN_EXTENT * (float)SG_RUNE_CX_Q8_ONE &&
			(float)(record->bounds.maxs.value[1] - record->bounds.mins.value[1]) >=
			FLOOR_MIN_EXTENT * (float)SG_RUNE_CX_Q8_ONE);
		floor->cell = cell;
		floor->stand[0] = (float)((double)record->bounds.mins.value[0] +
			(double)record->bounds.maxs.value[0]) / (2.0f * (float)SG_RUNE_CX_Q8_ONE);
		floor->stand[1] = (float)((double)record->bounds.mins.value[1] +
			(double)record->bounds.maxs.value[1]) / (2.0f * (float)SG_RUNE_CX_Q8_ONE);
		floor->stand[2] = (float)record->bounds.mins.value[2] /
			(float)SG_RUNE_CX_Q8_ONE + 0.5f;
		floor->cluster = record->source.cluster;
		if (floor->cluster < 0)
		{
			float probe[3] = { floor->stand[0], floor->stand[1], floor->stand[2] + EYE_HEIGHT };

			floor->cluster = SG_RuneVisClusterAt(build->bsp, probe);
		}
		if (floor->cluster < 0 || (uint32_t)floor->cluster >= build->vis.cluster_count)
			continue;
		floor->next = SG_RUNE_CX_INDEX_NONE;
		if (floor->representative)
		{
			floor->next = build->cluster_first[floor->cluster];
			build->cluster_first[floor->cluster] = build->floor_count;
			build->report.sources++;
		}
		build->floor_count++;
	}
	return 1;
}

/* Every floor cell borrows the nearest representative's row: the nearest
 * by floor point within reach, at nearly the same height. */
#define BORROW_REACH 128.0f
#define BORROW_RISE 40.0f

static void Borrow(fire_build_t *build)
{
	uint32_t f, r;

	for (f = 0U; f < build->floor_count; f++)
	{
		const floor_t *floor = &build->floors[f];
		float best = BORROW_REACH * BORROW_REACH;
		uint32_t best_cell = SG_RUNE_CX_INDEX_NONE;

		if (floor->representative)
			continue;
		for (r = 0U; r < build->floor_count; r++)
		{
			const floor_t *other = &build->floors[r];
			float dx, dy, dz, d;

			if (!other->representative)
				continue;
			dz = other->stand[2] - floor->stand[2];
			if (dz > BORROW_RISE || dz < -BORROW_RISE)
				continue;
			dx = other->stand[0] - floor->stand[0];
			dy = other->stand[1] - floor->stand[1];
			d = dx * dx + dy * dy;
			if (d < best)
			{
				best = d;
				best_cell = other->cell;
			}
		}
		if (best_cell != SG_RUNE_CX_INDEX_NONE)
			build->store->cells[floor->cell] = build->store->cells[best_cell];
	}
}

static int CompareTarget(const void *a, const void *b)
{
	uint32_t ta = ((const sg_rune_fire_t *)a)->target;
	uint32_t tb = ((const sg_rune_fire_t *)b)->target;

	return ta < tb ? -1 : ta > tb ? 1 : 0;
}

static int PushScratch(fire_build_t *build, uint32_t *count, uint32_t target,
	uint32_t flags)
{
	if (*count >= build->scratch_capacity)
	{
		uint32_t capacity = build->scratch_capacity ? build->scratch_capacity * 2U : 1024U;
		sg_rune_fire_t *grown = realloc(build->scratch, (size_t)capacity * sizeof(*grown));

		if (!grown)
			return 0;
		build->scratch = grown;
		build->scratch_capacity = capacity;
	}
	build->scratch[*count].target = target;
	build->scratch[*count].flags = flags;
	(*count)++;
	return 1;
}

static int RelationsFrom(fire_build_t *build, const floor_t *source)
{
	float eye[3];
	uint32_t c, count = 0U;
	sg_rune_fire_cell_t *row = &build->store->cells[source->cell];

	row->representative = source->cell;

	memcpy(eye, source->stand, sizeof(eye));
	eye[2] += EYE_HEIGHT;
	SG_RuneVisSelect(&build->vis, source->cluster);
	for (c = 0U; c < build->vis.cluster_count; c++)
	{
		uint32_t f;

		if (!SG_RuneVisSees(&build->vis, (int32_t)c))
			continue;
		for (f = build->cluster_first[c]; f != SG_RUNE_CX_INDEX_NONE; f = build->floors[f].next)
		{
			const floor_t *target = &build->floors[f];
			float other_eye[3], body[3], feet[3], reach;
			float dx = target->stand[0] - source->stand[0];
			float dy = target->stand[1] - source->stand[1];
			float dz = target->stand[2] - source->stand[2];
			float distance = sqrtf(dx * dx + dy * dy + dz * dz);
			uint32_t flags = 0U;

			if (target == source || distance > FIRE_RANGE)
				continue;
			build->report.pairs++;
			memcpy(other_eye, target->stand, sizeof(other_eye));
			other_eye[2] += EYE_HEIGHT;
			memcpy(body, target->stand, sizeof(body));
			body[2] += BODY_HEIGHT;
			memcpy(feet, target->stand, sizeof(feet));
			feet[2] += FEET_HEIGHT;
			if (Clear(build, eye, other_eye, 0.0f, NULL))
			{
				flags |= SG_RUNE_FIRE_LINE | SG_RUNE_FIRE_BLAST;
				if (Clear(build, eye, body, PROJECTILE_HALF, NULL))
					flags |= SG_RUNE_FIRE_CORRIDOR;
			}
			/* No line: a rocket at the feet, the burst where the shot
			 * stops, may still reach. */
			else if (Clear(build, eye, feet, 0.0f, &reach) ||
				reach >= distance - BLAST_RADIUS)
				flags |= SG_RUNE_FIRE_BLAST;
			if (!(flags & SG_RUNE_FIRE_LINE) && distance <= LOB_RANGE &&
				Lobs(build, source->cell, eye, feet))
				flags |= SG_RUNE_FIRE_LOB;
			if (flags == 0U)
				continue;
			if (!PushScratch(build, &count, target->cell, flags))
				return 0;
		}
	}
	if (count)
	{
		uint32_t k;

		qsort(build->scratch, count, sizeof(*build->scratch), CompareTarget);
		if (!Reserve(build->store, count))
			return 0;
		row->first = build->store->record_count;
		row->count = count;
		memcpy(&build->store->records[build->store->record_count], build->scratch,
			(size_t)count * sizeof(*build->scratch));
		build->store->record_count += count;
		build->report.records += count;
		for (k = 0U; k < count; k++)
		{
			uint32_t flags = build->scratch[k].flags;

			build->report.line += (flags & SG_RUNE_FIRE_LINE) != 0U;
			build->report.corridor += (flags & SG_RUNE_FIRE_CORRIDOR) != 0U;
			build->report.blast += (flags & SG_RUNE_FIRE_BLAST) != 0U;
			build->report.lob += (flags & SG_RUNE_FIRE_LOB) != 0U;
		}
	}
	return 1;
}

int SG_RuneFireEmit(const sg_rune_bsp_t *bsp,
	const sg_rune_cx_t *cx,
	const sg_rune_law_t *law, sg_rune_fire_store_t *store,
	sg_rune_fire_progress_fn progress, void *progress_context,
	sg_rune_fire_report_t *report_out)
{
	fire_build_t build;
	uint32_t f;
	int ok = 0;

	if (report_out)
		memset(report_out, 0, sizeof(*report_out));
	if (!bsp || !cx || !law || !store)
		return 0;
	memset(&build, 0, sizeof(build));
	if (!SG_RuneCxRead(cx, &build.view))
		return 0;
	build.bsp = bsp;
	build.law = law;
	build.store = store;
	build.artifact.complex = build.view;
	build.artifact.law = *law;
	SG_RuneFireStoreFree(store);
	store->cell_count = build.view.cell_count;
	store->cells = calloc((size_t)(store->cell_count ? store->cell_count : 1U),
		sizeof(*store->cells));
	if (!store->cells || !SG_RuneVisInit(&build.vis, bsp))
		goto done;
	{
		uint32_t cell;

		for (cell = 0U; cell < store->cell_count; cell++)
			store->cells[cell].representative = SG_RUNE_CX_INDEX_NONE;
	}
	if (!SG_RuneLocatorBuild(&build.locator, &build.artifact) || !CollectFloors(&build))
		goto done;
	for (f = 0U; f < build.floor_count; f++)
	{
		if (!build.floors[f].representative)
			continue;
		if (!RelationsFrom(&build, &build.floors[f]))
			goto done;
		if (progress && (f % 2000U) == 1999U)
			progress(progress_context, f + 1U, build.floor_count);
	}
	Borrow(&build);
	ok = 1;
done:
	if (report_out)
		*report_out = build.report;
	SG_RuneLocatorFree(&build.locator);
	SG_RuneVisFree(&build.vis);
	free(build.floors);
	free(build.cluster_first);
	free(build.scratch);
	return ok;
}
