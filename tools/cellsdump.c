#define _POSIX_C_SOURCE 200809L
/* cellsdump MAP.bsp: run the era-4 cell builder and regions on one map and
 * print what came out and how long it took. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "slipgate/sg_bsp_world.h"
#include "slipgate/sg_host_collision.h"
#include "slipgate/sg_configuration_space.h"
#include "slipgate/sg_configuration_semantics.h"
#include "slipgate/sg_rune_compact_geometry.h"
#include "slipgate/sg_rune_movement.h"

static double Now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void Progress(void *context, uint32_t done, uint32_t total)
{
	(void)context;
	if (done == total || done % (total / 10U + 1U) == 0U)
		fprintf(stderr, "  carve %u/%u (%u%%)\n", (unsigned)done,
			(unsigned)total, (unsigned)(((uint64_t)done * 100U) / total));
}

int main(int argc, char **argv)
{
	sg_bsp_world_t *world = NULL;
	sg_bsp_error_t bsp_error;
	sg_host_collision_authority_t authority;
	sg_host_collision_error_t host_error;
	sg_rune_model_identity_t identity;
	sg_configuration_space_t *space = NULL;
	sg_configuration_error_t error;
	sg_configuration_semantics_t *semantics = NULL;
	sg_configuration_semantics_error_t semantics_error;
	sg_configuration_semantics_limits_t semantics_limits;
	uint32_t standing = 0U, cell, supported = 0U;
	double t0, t1, t2;

	if (argc != 2)
	{
		fprintf(stderr, "usage: cellsdump MAP.bsp\n");
		return 2;
	}
	if (!SG_BspWorldLoadFile(argv[1], &world, &bsp_error))
	{
		fprintf(stderr, "bsp load failed: code %d\n", (int)bsp_error.code);
		return 1;
	}
	memset(&identity, 0, sizeof(identity));
	memcpy(&identity.bsp_content_id, world->content_identity.bytes,
		sizeof(identity.bsp_content_id));
	identity.physics_abi_id = 1U;
	identity.source_set_identity = 1U;
	identity.standing_hull.mins.value[0] = -16.0f;
	identity.standing_hull.mins.value[1] = -16.0f;
	identity.standing_hull.mins.value[2] = -24.0f;
	identity.standing_hull.maxs.value[0] = 16.0f;
	identity.standing_hull.maxs.value[1] = 16.0f;
	identity.standing_hull.maxs.value[2] = 32.0f;
	identity.crouching_hull = identity.standing_hull;
	identity.crouching_hull.maxs.value[2] = 4.0f;
	identity.physics.gravity = 800.0f;
	identity.physics.ground_acceleration = 10.0f;
	identity.physics.air_acceleration = 1.0f;
	identity.physics.water_acceleration = 10.0f;
	identity.physics.hook_acceleration = 800.0f;
	identity.physics.external_acceleration = 1.0f;
	identity.physics.water_drag = 1.0f;
	identity.physics.max_velocity = 2000.0f;
	identity.physics.frame_ms = 100U;
	identity.physics.substep_ms = 25U;
	if (!SG_HostCollisionInit(&authority, world, &identity, &host_error))
	{
		fprintf(stderr, "host init failed\n");
		return 1;
	}
	printf("%s: %u leaves, %u brushes, %u planes\n", argv[1],
		(unsigned)world->leaf_count, (unsigned)world->brush_count,
		(unsigned)world->plane_count);
	t0 = Now();
	if (!SG_ConfigurationBuildWithProgress(&authority, NULL, Progress, NULL,
		&space, &error))
	{
		fprintf(stderr, "cell build failed: %s (source %u)\n",
			SG_ConfigurationErrorString(error.code),
			(unsigned)error.source_index);
		return 1;
	}
	t1 = Now();
	for (cell = 0U; cell < space->cell_count; cell++)
		if (space->cells[cell].stance == SG_RUNE_STANCE_STANDING)
			standing++;
	printf("cells: %u (%u crouching, %u standing), faces %u, vertices %u, "
		"portals %u, overlaps %u  [%.2fs]\n", (unsigned)space->cell_count,
		(unsigned)(space->cell_count - standing), (unsigned)standing,
		(unsigned)space->face_count, (unsigned)space->vertex_count,
		(unsigned)space->portal_count, (unsigned)space->stance_overlap_count,
		t1 - t0);
	{
		const sg_configuration_portal_stats_t *stats =
			SG_ConfigurationLastPortalStats();

		printf("portal pass: overlaps %llu, witnessed %llu, host refused %llu\n",
			(unsigned long long)stats->overlaps,
			(unsigned long long)stats->witnessed,
			(unsigned long long)stats->transition_failed);
	}
	SG_ConfigurationSemanticsDefaultLimits(&semantics_limits);
	if (!SG_ConfigurationSemanticsBuild(&authority, space, &semantics_limits,
		&semantics, &semantics_error))
	{
		fprintf(stderr, "semantics failed: %s (source %u)\n",
			SG_ConfigurationSemanticsErrorString(semantics_error.code),
			(unsigned)semantics_error.source_index);
		return 1;
	}
	t2 = Now();
	for (cell = 0U; cell < semantics->region_count; cell++)
		if (semantics->regions[cell].flags &
			SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED)
			supported++;
	printf("regions: %u (%u supported), boundaries %u, hook surfaces %u  "
		"[%.2fs]\n", (unsigned)semantics->region_count, (unsigned)supported,
		(unsigned)semantics->boundary_count,
		(unsigned)semantics->hook_surface_count, t2 - t1);
	{
		sg_rune_compact_geometry_t *geometry = NULL;
		sg_rune_compact_geometry_error_t geometry_error;
		sg_rune_compact_geometry_view_t view;
		sg_rune_compact_identity_t compact_identity;
		double t3;

		memset(&compact_identity, 0, sizeof(compact_identity));
		if (!SG_RuneCompactGeometryFromSpace(world, space, semantics,
			&compact_identity, NULL, &geometry, &geometry_error))
		{
			fprintf(stderr, "geometry failed: %s (domain %d record %u)\n",
				SG_RuneCompactGeometryErrorString(geometry_error.code),
				(int)geometry_error.domain, (unsigned)geometry_error.record);
			return 1;
		}
		t3 = Now();
		SG_RuneCompactGeometryRead(geometry, &view);
		printf("geometry: cells %u, facets %u, incidences %u, vertices %u, "
			"portals %u, source surfaces %u  [%.2fs]\n", (unsigned)view.cell_count,
			(unsigned)view.facet_count, (unsigned)view.incidence_count,
			(unsigned)view.vertex_count, (unsigned)view.portal_count,
			(unsigned)view.source_surface_count, t3 - t2);
		{
			sg_rune_move_store_t movement;
			sg_rune_move_law_t law;
			uint32_t counts[SG_RUNE_MOVE_KIND_COUNT];
			uint32_t index;
			double t4;

			law.gravity = identity.physics.gravity;
			law.frame_ms = identity.physics.frame_ms;
			law.substep_ms = identity.physics.substep_ms;
			if (!SG_RuneMoveStoreInit(&movement, &law) ||
				!SG_RuneMoveEmitGeometry(&movement, &view, semantics))
			{
				fprintf(stderr, "movement failed\n");
				return 1;
			}
			t4 = Now();
			memset(counts, 0, sizeof(counts));
			for (index = 0U; index < movement.capability_count; index++)
				counts[movement.capabilities[index].kind]++;
			printf("movement: %u capabilities  [%.2fs]\n",
				(unsigned)movement.capability_count, t4 - t3);
			for (index = 0U; index < SG_RUNE_MOVE_KIND_COUNT; index++)
				if (counts[index])
					printf("  %-16s %u\n", SG_RuneMoveKindString(
						(sg_rune_move_kind_t)index), (unsigned)counts[index]);
			SG_RuneMoveStoreFree(&movement);
		}
		SG_RuneCompactGeometryDestroy(geometry);
	}
	SG_ConfigurationSemanticsDestroy(semantics);
	SG_ConfigurationDestroy(space);
	SG_BspWorldDestroy(world);
	return 0;
}
