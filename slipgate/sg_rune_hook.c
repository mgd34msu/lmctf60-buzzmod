#include "sg_rune_hook.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "sg_bsp_world.h"
#include "sg_host_collision.h"
#include "sg_rune_artifact.h"
#include "sg_rune_cx_build.h"
#include "sg_rune_flight.h"
#include "sg_rune_locate.h"
#include "sg_rune_vis.h"
#include "sg_weapon_host_constants.h"

#define ROPE_RANGE 1000.0f
#define BITE_ABOVE_EYE 24.0f
#define CANDIDATES_PER_CELL 24U
#define RECORDS_PER_CELL 8U
#define EYE_HEIGHT 22.0f
#define BODY_ORIGIN 24.0f
#define NEAR_BITE_STOP 40.0f
#define MIN_PULL 64.0f

typedef struct bite_s
{
	float point[3];
	float normal[3];
	int32_t cluster;
	uint32_t next;            /* next bite in the same cluster */
} bite_t;

typedef struct hook_build_s
{
	const sg_bsp_world_t *bsp;
	const sg_host_collision_authority_t *authority;
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

static int CollectBites(hook_build_t *build)
{
	const sg_rune_cx_view_t *cx = &build->view;
	uint32_t index;

	build->bites = malloc((size_t)(cx->surface_count ? cx->surface_count : 1U) *
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
	build->report.bites = build->bite_count;
	return 1;
}

/* ---- traces ------------------------------------------------------------------------ */

static int LineClear(hook_build_t *build, const float from[3], const float to[3])
{
	sg_host_collision_trace_t trace;
	float dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
	float length = sqrtf(dx * dx + dy * dy + dz * dz);

	static const float point[3] = { 0.0f, 0.0f, 0.0f };

	build->report.traces++;
	if (!SG_HostCollisionTrace(build->authority, NULL, from, point, point, to,
		SG_HOST_MASK_PLAYER_SOLID, &trace))
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
	sg_host_collision_trace_t trace;
	float end[3];

	end[0] = start[0] + direction[0] * length;
	end[1] = start[1] + direction[1] * length;
	end[2] = start[2] + direction[2] * length;
	build->report.traces++;
	if (!SG_HostCollisionTrace(build->authority, NULL, start,
		build->law->crouching_mins, build->law->crouching_maxs, end,
		SG_HOST_MASK_PLAYER_SOLID, &trace))
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
	/* The nearest bites in view, above the eye, facing it. */
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

			if (dz < BITE_ABOVE_EYE || distance > ROPE_RANGE || distance < 48.0f)
				continue;
			if (bite->normal[0] * dx + bite->normal[1] * dy + bite->normal[2] * dz > 0.0f)
				continue;   /* the surface faces away */
			/* Insert by distance, keeping the nearest few. */
			for (slot = candidate_count; slot > 0U &&
				candidate_distance[slot - 1U] > distance; slot--)
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
		static const float fractions[] = { 0.5f, 0.75f, 1.0f };
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
		bolt_seconds = candidate_distance[i] / (float)SG_HOST_HOOK_FIRE_SPEED;
		clear = PullClear(build, stand, direction, length - NEAR_BITE_STOP);
		if (clear < MIN_PULL)
			continue;
		build->report.pull_clear++;
		for (f = 0U; f < sizeof(fractions) / sizeof(fractions[0]); f++)
		{
			float release[3], velocity[3];
			uint32_t start;
			sg_rune_flight_t flight;
			landing_t *slot;
			uint32_t k;
			float seconds;

			release[0] = stand[0] + direction[0] * clear * fractions[f];
			release[1] = stand[1] + direction[1] * clear * fractions[f];
			release[2] = stand[2] + direction[2] * clear * fractions[f];
			velocity[0] = direction[0] * (float)SG_HOST_HOOK_PULL_SPEED;
			velocity[1] = direction[1] * (float)SG_HOST_HOOK_PULL_SPEED;
			velocity[2] = direction[2] * (float)SG_HOST_HOOK_PULL_SPEED;
			start = SG_RuneLocate(&build->locator, release, 0U, 8.0f, NULL);
			if (start == SG_RUNE_CX_INDEX_NONE)
				continue;
			build->report.traces++;
			build->report.flights++;
			if (!SG_RuneFlightTrace(cx, build->law, start, release, velocity, &flight) ||
				flight.outcome != SG_RUNE_FLIGHT_LANDED ||
				flight.landing_cell == cell ||
				!(cx->cells[flight.landing_cell].semantics & SG_RUNE_CX_CELL_SUPPORTED))
				continue;
			seconds = bolt_seconds + clear * fractions[f] /
				(float)SG_HOST_HOOK_PULL_SPEED + flight.seconds + 0.3f;
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
			memcpy(slot->velocity, velocity, sizeof(slot->velocity));
		}
	}
	return 1;
}

int SG_RuneHookEmit(const sg_bsp_world_t *bsp,
	const sg_host_collision_authority_t *authority, const sg_rune_cx_t *cx,
	const sg_rune_law_t *law, sg_rune_move_store_t *movement,
	sg_rune_hook_report_t *report_out)
{
	hook_build_t build;
	uint32_t cell;
	int ok = 0;

	if (report_out)
		memset(report_out, 0, sizeof(*report_out));
	if (!bsp || !authority || !cx || !law || !movement)
		return 0;
	memset(&build, 0, sizeof(build));
	if (!SG_RuneCxRead(cx, &build.view))
		return 0;
	build.bsp = bsp;
	build.authority = authority;
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
				landings[k].bite, landings[k].velocity, landings[k].seconds))
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
