#include "sg_rune_hook.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
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

#define ROPE_RANGE 1000.0f
#define BITE_BEYOND_BAND 48.0f    /* a bite at least this far past the slow band */
#define BITE_ABOVE_EYE 4.0f       /* level bites too: the players' pulls are mostly forward */
#define MOMENTUM_SPEED 400.0f     /* a release at this much forward speed carries into the next edge */
#define MOMENTUM_CREDIT 0.7f
#define CANDIDATES_PER_CELL 48U
#define RECORDS_PER_CELL 16U
#define EYE_HEIGHT 22.0f
#define BODY_ORIGIN 24.0f
#define NEAR_BITE_STOP 40.0f
#define MIN_PULL 64.0f
#define RELEASE_TOLERANCE 0.15f   /* the pull speed a release arc is checked against, either way */

typedef struct bite_s
{
	float point[3];
	float normal[3];
	int32_t cluster;
	uint32_t next;            /* next bite in the same cluster */
} bite_t;

typedef struct hook_build_s
{
	const sg_rune_bsp_t *bsp;
	sg_rune_cx_view_t view;
	const sg_rune_law_t *law;
	sg_rune_move_store_t *movement;
	sg_rune_artifact_t artifact;
	sg_rune_locator_t locator;
	bite_t *bites;
	uint32_t bite_count;
	uint32_t *cluster_first;  /* per cluster: first bite, NONE */
	uint32_t cluster_count;
	sg_rune_vis_t vis;
	sg_rune_hook_report_t report;
} hook_build_t;

static float FloatBits(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

/* ---- bites ---------------------------------------------------------------------- */

static char sg_human_bites_path[1024];

void SG_RuneHookSetHumanBites(const char *path)
{
	if (path && *path)
	{
		strncpy(sg_human_bites_path, path, sizeof(sg_human_bites_path) - 1U);
		sg_human_bites_path[sizeof(sg_human_bites_path) - 1U] = 0;
	}
	else
		sg_human_bites_path[0] = 0;
}

static uint32_t CountHumanBites(void)
{
	FILE *f;
	uint32_t n = 0U;
	char line[256];

	if (!sg_human_bites_path[0])
		return 0U;
	f = fopen(sg_human_bites_path, "r");
	if (!f)
		return 0U;
	while (fgets(line, sizeof(line), f))
		n++;
	fclose(f);
	return n;
}

/* The players' bites: each is traced from where it was fired; the wall
 * the bolt meets there gives the point and the normal. */
static void AddHumanBites(hook_build_t *build)
{
	FILE *f;
	char line[256];
	uint32_t added = 0U;

	if (!sg_human_bites_path[0])
		return;
	f = fopen(sg_human_bites_path, "r");
	if (!f)
		return;
	while (fgets(line, sizeof(line), f))
	{
		float from[3], to[3], dx, dy, dz;
		sg_rune_trace_t trace;
		bite_t *bite;

		if (sscanf(line, "%f %f %f %f %f %f", &from[0], &from[1], &from[2],
			&to[0], &to[1], &to[2]) != 6)
			continue;
		if (!SG_RuneTraceBox(build->bsp, 0U, NULL, from, NULL, NULL, to,
			SG_RUNE_MASK_PLAYER_SOLID, &trace) || trace.startsolid || trace.fraction >= 1.0f)
			continue;
		dx = trace.end[0] - to[0];
		dy = trace.end[1] - to[1];
		dz = trace.end[2] - to[2];
		if (dx * dx + dy * dy + dz * dz > 48.0f * 48.0f)
			continue;   /* the demo's bite is not where this world's wall is */
		bite = &build->bites[build->bite_count];
		memcpy(bite->normal, trace.normal, sizeof(bite->normal));
		bite->point[0] = trace.end[0] + trace.normal[0] * 2.0f;
		bite->point[1] = trace.end[1] + trace.normal[1] * 2.0f;
		bite->point[2] = trace.end[2] + trace.normal[2] * 2.0f;
		bite->cluster = SG_RuneVisClusterAt(build->bsp, bite->point);
		if (bite->cluster < 0 || (uint32_t)bite->cluster >= build->cluster_count)
			continue;
		bite->next = build->cluster_first[bite->cluster];
		build->cluster_first[bite->cluster] = build->bite_count;
		build->bite_count++;
		added++;
	}
	fclose(f);
	build->report.human_bites = added;
}

static int CollectBites(hook_build_t *build)
{
	const sg_rune_cx_view_t *cx = &build->view;
	uint32_t index;

	build->bites = malloc((size_t)(cx->surface_count + CountHumanBites() + 1U) *
		sizeof(*build->bites));
	build->cluster_first = malloc((size_t)(build->cluster_count ?
		build->cluster_count : 1U) * sizeof(uint32_t));
	if (!build->bites || !build->cluster_first)
		return 0;
	for (index = 0U; index < build->cluster_count; index++)
		build->cluster_first[index] = SG_RUNE_CX_INDEX_NONE;
	for (index = 0U; index < cx->surface_count; index++)
	{
		const sg_rune_cx_surface_t *surface = &cx->surfaces[index];
		bite_t *bite;
		float sum[3] = { 0.0f, 0.0f, 0.0f }, extent = 0.0f, first[3];
		uint32_t vertex;

		if (!(surface->flags & SG_RUNE_CX_SURFACE_HOOKABLE) ||
			surface->frame != SG_RUNE_CX_SURFACE_WORLD || surface->vertices.count < 3U)
			continue;
		for (vertex = 0U; vertex < surface->vertices.count; vertex++)
		{
			const sg_rune_cx_vec3_t *q8 =
				&cx->surface_vertices[surface->vertices.first + vertex];
			float p[3];
			uint32_t axis;

			for (axis = 0U; axis < 3U; axis++)
			{
				p[axis] = (float)q8->value[axis] / (float)SG_RUNE_CX_Q8_ONE;
				sum[axis] += p[axis];
			}
			if (vertex == 0U)
				memcpy(first, p, sizeof(first));
			else
			{
				float dx = p[0] - first[0], dy = p[1] - first[1], dz = p[2] - first[2];
				float d = sqrtf(dx * dx + dy * dy + dz * dz);

				if (d > extent)
					extent = d;
			}
		}
		if (extent < 24.0f)
			continue;   /* too small to bite reliably */
		bite = &build->bites[build->bite_count];
		bite->normal[0] = FloatBits(surface->plane.normal_bits[0]);
		bite->normal[1] = FloatBits(surface->plane.normal_bits[1]);
		bite->normal[2] = FloatBits(surface->plane.normal_bits[2]);
		bite->point[0] = sum[0] / (float)surface->vertices.count + bite->normal[0] * 2.0f;
		bite->point[1] = sum[1] / (float)surface->vertices.count + bite->normal[1] * 2.0f;
		bite->point[2] = sum[2] / (float)surface->vertices.count + bite->normal[2] * 2.0f;
		bite->cluster = SG_RuneVisClusterAt(build->bsp, bite->point);
		if (bite->cluster < 0 || (uint32_t)bite->cluster >= build->cluster_count)
			continue;
		bite->next = build->cluster_first[bite->cluster];
		build->cluster_first[bite->cluster] = build->bite_count;
		build->bite_count++;
	}
	AddHumanBites(build);
	build->report.bites = build->bite_count;
	return 1;
}

/* ---- traces ------------------------------------------------------------------------ */

static int LineClear(hook_build_t *build, const float from[3], const float to[3])
{
	sg_rune_trace_t trace;
	float dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
	float length = sqrtf(dx * dx + dy * dy + dz * dz);

	static const float point[3] = { 0.0f, 0.0f, 0.0f };

	build->report.traces++;
	if (!SG_RuneTraceBox(build->bsp, 0U, NULL, from, point, point, to,
		SG_RUNE_MASK_PLAYER_SOLID, &trace))
		return 0;
	if (trace.startsolid)
		return 0;
	/* The bite sits two units off its surface: the bolt must get within a
	 * few units of it. */
	return trace.fraction * length >= length - 6.0f;
}

/* How far the body can be pulled from start toward bite before it meets
 * the world, with its crouch hull. */
static float PullClear(hook_build_t *build, const float start[3],
	const float direction[3], float length)
{
	sg_rune_trace_t trace;
	float end[3];

	end[0] = start[0] + direction[0] * length;
	end[1] = start[1] + direction[1] * length;
	end[2] = start[2] + direction[2] * length;
	build->report.traces++;
	if (!SG_RuneTraceBox(build->bsp, 0U, NULL, start,
		build->law->crouching_mins, build->law->crouching_maxs, end,
		SG_RUNE_MASK_PLAYER_SOLID, &trace))
		return 0.0f;
	if (trace.startsolid)
		return 0.0f;
	return trace.fraction * length;
}

typedef struct landing_s
{
	uint32_t cell;
	float seconds;
	float bite[3];
	float velocity[3];
	float release_distance;
} landing_t;


static int RidesFromCell(hook_build_t *build, uint32_t cell,
	landing_t *landings, uint32_t *landing_count)
{
	const sg_rune_cx_view_t *cx = &build->view;
	const sg_rune_cx_cell_t *record = &cx->cells[cell];
	float stand[3], eye[3];
	int32_t cluster;
	uint32_t candidates[CANDIDATES_PER_CELL];
	float candidate_distance[CANDIDATES_PER_CELL];
	uint32_t candidate_count = 0U, c, i;

	stand[0] = (float)((double)record->bounds.mins.value[0] +
		(double)record->bounds.maxs.value[0]) / (2.0f * (float)SG_RUNE_CX_Q8_ONE);
	stand[1] = (float)((double)record->bounds.mins.value[1] +
		(double)record->bounds.maxs.value[1]) / (2.0f * (float)SG_RUNE_CX_Q8_ONE);
	stand[2] = (float)record->bounds.mins.value[2] / (float)SG_RUNE_CX_Q8_ONE + 0.5f;
	memcpy(eye, stand, sizeof(eye));
	eye[2] += EYE_HEIGHT;
	cluster = record->source.cluster;
	if (cluster < 0)
		cluster = SG_RuneVisClusterAt(build->bsp, stand);
	SG_RuneVisSelect(&build->vis, cluster);
	/* The farthest bites in view, above the eye, facing it. */
	for (c = 0U; c < build->cluster_count; c++)
	{
		uint32_t b;

		if (!SG_RuneVisSees(&build->vis, (int32_t)c))
			continue;
		for (b = build->cluster_first[c]; b != SG_RUNE_CX_INDEX_NONE; b = build->bites[b].next)
		{
			const bite_t *bite = &build->bites[b];
			float dx = bite->point[0] - eye[0], dy = bite->point[1] - eye[1];
			float dz = bite->point[2] - eye[2];
			float distance = sqrtf(dx * dx + dy * dy + dz * dz);
			uint32_t slot;

			/* A bite inside the pull's slow band never carries the body at
			 * speed; the best players fire far and ride long. */
			if (dz < BITE_ABOVE_EYE || distance > ROPE_RANGE ||
				distance < build->law->hook_near_bite + BITE_BEYOND_BAND)
				continue;
			if (bite->normal[0] * dx + bite->normal[1] * dy + bite->normal[2] * dz > 0.0f)
				continue;   /* the surface faces away */
			/* Insert by distance, keeping the farthest few: a long pull at
			 * full speed is what the rope is for. */
			for (slot = candidate_count; slot > 0U &&
				candidate_distance[slot - 1U] < distance; slot--)
			{
				if (slot < CANDIDATES_PER_CELL)
				{
					candidates[slot] = candidates[slot - 1U];
					candidate_distance[slot] = candidate_distance[slot - 1U];
				}
			}
			if (slot < CANDIDATES_PER_CELL)
			{
				candidates[slot] = b;
				candidate_distance[slot] = distance;
				if (candidate_count < CANDIDATES_PER_CELL)
					candidate_count++;
			}
		}
	}
	*landing_count = 0U;
	for (i = 0U; i < candidate_count; i++)
	{
		const bite_t *bite = &build->bites[candidates[i]];
		float direction[3], length, clear, bolt_seconds;
		static const float fractions[] = { 0.25f, 0.4f, 0.55f, 0.75f, 1.0f };
		uint32_t f;

		build->report.candidates++;
		if (!LineClear(build, eye, bite->point))
			continue;
		build->report.bolt_clear++;
		direction[0] = bite->point[0] - stand[0];
		direction[1] = bite->point[1] - stand[1];
		direction[2] = bite->point[2] - stand[2];
		length = sqrtf(direction[0] * direction[0] + direction[1] * direction[1] +
			direction[2] * direction[2]);
		if (length < MIN_PULL + NEAR_BITE_STOP)
			continue;
		direction[0] /= length;
		direction[1] /= length;
		direction[2] /= length;
		bolt_seconds = candidate_distance[i] / build->law->hook_fire_speed;
		clear = PullClear(build, stand, direction, length - NEAR_BITE_STOP);
		if (clear < MIN_PULL)
			continue;
		build->report.pull_clear++;
		/* The rope holds the body at the bite when the ride is not let go
		 * of earlier; from there it drops straight down.  A bite whose
		 * hanging drop ends in lava, in the void, or nowhere to stand is
		 * no ride: the release arcs below are the plan, the hanging drop
		 * is what a body that keeps riding gets. */
		{
			float hold[3], still[3] = { 0.0f, 0.0f, 0.0f };
			uint32_t hold_cell;
			sg_rune_flight_t drop;

			/* The rope stops pulling with the eye a hold's length short of
			 * the bite, back along the line it was pulled on. */
			hold[0] = bite->point[0] - direction[0] * build->law->hook_hold;
			hold[1] = bite->point[1] - direction[1] * build->law->hook_hold;
			hold[2] = bite->point[2] - direction[2] * build->law->hook_hold - EYE_HEIGHT;
			hold_cell = SG_RuneLocate(&build->locator, hold, 0U, 8.0f, NULL);
			if (hold_cell == SG_RUNE_CX_INDEX_NONE)
				continue;
			build->report.traces++;
			if (!SG_RuneFlightTrace(cx, build->law, hold_cell, hold, still, &drop) ||
				drop.outcome != SG_RUNE_FLIGHT_LANDED ||
				!(cx->cells[drop.landing_cell].semantics & SG_RUNE_CX_CELL_SUPPORTED) ||
				(cx->cells[drop.landing_cell].semantics & SG_RUNE_CX_CELL_HAZARD))
				continue;
		}
		for (f = 0U; f < sizeof(fractions) / sizeof(fractions[0]); f++)
		{
			float release[3], velocity[3];
			uint32_t start;
			sg_rune_flight_t flight;
			landing_t *slot;
			uint32_t k;
			float seconds;

			float pull[3], remaining, speed, release_distance;

			release[0] = stand[0] + direction[0] * clear * fractions[f];
			release[1] = stand[1] + direction[1] * clear * fractions[f];
			release[2] = stand[2] + direction[2] * clear * fractions[f];
			/* The pull at the release point: from the eye toward the bite,
			 * at the host's speed for that distance. */
			pull[0] = bite->point[0] - release[0];
			pull[1] = bite->point[1] - release[1];
			pull[2] = bite->point[2] - (release[2] + EYE_HEIGHT);
			remaining = sqrtf(pull[0] * pull[0] + pull[1] * pull[1] + pull[2] * pull[2]);
			speed = SG_RuneLawHookPullSpeed(build->law, remaining);
			if (remaining < 1.0f || speed <= 0.0f)
				continue;
			release_distance = remaining;
			velocity[0] = pull[0] / remaining * speed;
			velocity[1] = pull[1] / remaining * speed;
			velocity[2] = pull[2] / remaining * speed;
			start = SG_RuneLocate(&build->locator, release, 0U, 8.0f, NULL);
			if (start == SG_RUNE_CX_INDEX_NONE)
				continue;
			build->report.traces++;
			build->report.flights++;
			if (!SG_RuneFlightLandsRobustly(cx, build->law, start, release, velocity,
				RELEASE_TOLERANCE, 1, &flight) ||
				flight.outcome != SG_RUNE_FLIGHT_LANDED ||
				flight.landing_cell == cell)
				continue;
			/* What the ride costs a body that keeps running under the bolt
			 * (half the bolt's flight is not lost), is pulled, and flies:
			 * no fixed penalty.  The best players rope twenty to thirty
			 * times a minute; a route that charges the rope more than the
			 * walk never sees them. */
			seconds = 0.5f * bolt_seconds + clear * fractions[f] /
				build->law->hook_pull_speed + flight.seconds;
			/* A release at forward speed is worth more than its own time:
			 * the body carries it into the next crossing (the game drops
			 * the vertical part at release, not the forward part). */
			if (sqrtf(velocity[0] * velocity[0] + velocity[1] * velocity[1]) >= MOMENTUM_SPEED)
				seconds *= MOMENTUM_CREDIT;
			/* One record per landing cell: the cheapest ride there. */
			slot = NULL;
			for (k = 0U; k < *landing_count; k++)
				if (landings[k].cell == flight.landing_cell)
				{
					slot = &landings[k];
					break;
				}
			if (slot && slot->seconds <= seconds)
				continue;
			if (!slot)
			{
				if (*landing_count >= RECORDS_PER_CELL)
				{
					/* Replace the slowest when this one is faster. */
					uint32_t worst = 0U;

					for (k = 1U; k < *landing_count; k++)
						if (landings[k].seconds > landings[worst].seconds)
							worst = k;
					if (landings[worst].seconds <= seconds)
						continue;
					slot = &landings[worst];
				}
				else
					slot = &landings[(*landing_count)++];
			}
			slot->cell = flight.landing_cell;
			slot->seconds = seconds;
			memcpy(slot->bite, bite->point, sizeof(slot->bite));
			slot->release_distance = release_distance;
			memcpy(slot->velocity, velocity, sizeof(slot->velocity));
		}
	}
	return 1;
}

int SG_RuneHookEmit(const sg_rune_bsp_t *bsp,
	const sg_rune_cx_t *cx,
	const sg_rune_law_t *law, sg_rune_move_store_t *movement,
	sg_rune_hook_report_t *report_out)
{
	hook_build_t build;
	uint32_t cell;
	int ok = 0;

	if (report_out)
		memset(report_out, 0, sizeof(*report_out));
	if (!bsp || !cx || !law || !movement)
		return 0;
	memset(&build, 0, sizeof(build));
	if (!SG_RuneCxRead(cx, &build.view))
		return 0;
	build.bsp = bsp;
	build.law = law;
	build.movement = movement;
	build.artifact.complex = build.view;
	build.artifact.law = *law;
	if (!SG_RuneVisInit(&build.vis, bsp))
		return 0;
	build.cluster_count = build.vis.cluster_count;
	if (!SG_RuneLocatorBuild(&build.locator, &build.artifact) ||
		!CollectBites(&build))
		goto done;
	for (cell = 0U; cell < build.view.cell_count; cell++)
	{
		const sg_rune_cx_cell_t *record = &build.view.cells[cell];
		landing_t landings[RECORDS_PER_CELL];
		uint32_t landing_count = 0U, k;
		uint8_t stances;

		if (!(record->semantics & SG_RUNE_CX_CELL_SUPPORTED) ||
			(record->semantics & SG_RUNE_CX_CELL_WATER))
			continue;
		stances = 0U;
		if (record->valid_stances & SG_RUNE_CX_STANCE_STANDING)
			stances |= SG_RUNE_MOVE_STANDING;
		if (record->valid_stances & SG_RUNE_CX_STANCE_CROUCHING)
			stances |= SG_RUNE_MOVE_CROUCHING;
		if (!RidesFromCell(&build, cell, landings, &landing_count))
			goto done;
		for (k = 0U; k < landing_count; k++)
			if (!SG_RuneMoveAppendHook(movement, cell, landings[k].cell, stances,
				landings[k].bite, landings[k].velocity, landings[k].release_distance,
				landings[k].seconds))
				goto done;
		if (landing_count)
		{
			build.report.cells++;
			build.report.records += landing_count;
		}
	}
	ok = 1;
done:
	if (report_out)
		*report_out = build.report;
	SG_RuneLocatorFree(&build.locator);
	SG_RuneVisFree(&build.vis);
	free(build.bites);
	free(build.cluster_first);
	return ok;
}
