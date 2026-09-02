#define _POSIX_C_SOURCE 200809L
/* cellsdump MAP.bsp: run the era-4 cell builder and regions on one map and
 * print what came out and how long it took. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "slipgate/sg_bsp_world.h"
#include "slipgate/sg_host_collision.h"
#include "slipgate/sg_configuration_space.h"
#include "slipgate/sg_configuration_semantics.h"
#include "slipgate/sg_rune_cx_build.h"
#include "slipgate/sg_rune_artifact.h"
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

	if (argc < 2 || argc > 3)
	{
		fprintf(stderr, "usage: cellsdump MAP.bsp [OUT.rune]\n");
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
		sg_rune_cx_t *geometry = NULL;
		sg_rune_cx_error_t geometry_error;
		sg_rune_cx_view_t view;
		double t3;

		if (!SG_RuneCxFromSpace(world, space, semantics, NULL, &geometry, &geometry_error))
		{
			fprintf(stderr, "geometry failed: %s (domain %d record %u)\n",
				SG_RuneCxErrorString(geometry_error.code),
				(int)geometry_error.domain, (unsigned)geometry_error.record);
			return 1;
		}
		t3 = Now();
		SG_RuneCxRead(geometry, &view);
		printf("geometry: cells %u, facets %u, incidences %u, vertices %u, "
			"portals %u, source surfaces %u  [%.2fs]\n", (unsigned)view.cell_count,
			(unsigned)view.facet_count, (unsigned)view.incidence_count,
			(unsigned)view.vertex_count, (unsigned)view.portal_count,
			(unsigned)view.surface_count, t3 - t2);
		{
			sg_rune_move_store_t movement;
			sg_rune_move_law_t law;
			sg_rune_law_t law4;
			uint32_t counts[SG_RUNE_MOVE_KIND_COUNT];
			uint32_t index;
			double t4;

			law.gravity = identity.physics.gravity;
			law.frame_ms = identity.physics.frame_ms;
			law.substep_ms = identity.physics.substep_ms;
			memset(&law4, 0, sizeof(law4));
			law4.gravity = identity.physics.gravity;
			law4.frame_ms = identity.physics.frame_ms;
			law4.substep_ms = identity.physics.substep_ms;
			if (!SG_RuneMoveStoreInit(&movement, &law) ||
				!SG_RuneMoveEmitComplex(&movement, &view, &law4))
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
			{
				sg_rune_artifact_t source, loaded;
				unsigned char *image = NULL;
				size_t image_size = 0U;
				sg_rune_artifact_status_t status;
				const char *rune_path = argc > 2 ? argv[2] : "cellsdump.rune";
				int os_error = 0;
				sg_rune_fault_t fault;
				double t5, t6, t7;

				memset(&source, 0, sizeof(source));
				source.identity.schema_id = SG_RUNE_ARTIFACT_SCHEMA_ID;
				memcpy(source.identity.bsp_sha256, world->content_identity.bytes,
					sizeof(source.identity.bsp_sha256));
				memcpy(source.law.standing_mins, identity.standing_hull.mins.value,
					sizeof(source.law.standing_mins));
				memcpy(source.law.standing_maxs, identity.standing_hull.maxs.value,
					sizeof(source.law.standing_maxs));
				memcpy(source.law.crouching_mins, identity.crouching_hull.mins.value,
					sizeof(source.law.crouching_mins));
				memcpy(source.law.crouching_maxs, identity.crouching_hull.maxs.value,
					sizeof(source.law.crouching_maxs));
				source.law.gravity = identity.physics.gravity;
				source.law.ground_acceleration = identity.physics.ground_acceleration;
				source.law.air_acceleration = identity.physics.air_acceleration;
				source.law.water_acceleration = identity.physics.water_acceleration;
				source.law.hook_acceleration = identity.physics.hook_acceleration;
				source.law.water_drag = identity.physics.water_drag;
				source.law.max_velocity = identity.physics.max_velocity;
				source.law.frame_ms = identity.physics.frame_ms;
				source.law.substep_ms = identity.physics.substep_ms;
				source.complex = view;
				SG_RuneMoveStoreView(&movement, &source.movement);
				t5 = Now();
				status = SG_RuneArtifactEncode(&source, &image, &image_size);
				t6 = Now();
				if (status != SG_RUNE_ARTIFACT_OK)
				{
					fprintf(stderr, "encode failed: %s\n",
						SG_RuneArtifactStatusString(status));
					return 1;
				}
				status = SG_RuneArtifactWriteFile(rune_path, image, image_size,
					&os_error);
				if (status != SG_RUNE_ARTIFACT_OK)
				{
					fprintf(stderr, "write failed: %s (%d)\n",
						SG_RuneArtifactStatusString(status), os_error);
					return 1;
				}
				t7 = Now();
				printf("artifact: %zu bytes  [encode %.2fs, write %.2fs] -> %s\n",
					image_size, t6 - t5, t7 - t6, rune_path);
				status = SG_RuneArtifactLoadFile(rune_path, &loaded, &os_error,
					&fault);
				printf("reload: %s  [%.2fs]  cells %u portals %u capabilities %u\n",
					SG_RuneArtifactStatusString(status), Now() - t7,
					(unsigned)loaded.complex.cell_count,
					(unsigned)loaded.complex.portal_count,
					(unsigned)loaded.movement.capability_count);
				if (status != SG_RUNE_ARTIFACT_OK && fault.array)
					printf("  fault: %s[%u] %s\n", fault.array,
						(unsigned)fault.record, fault.reason);
				SG_RuneArtifactRelease(&loaded);
				free(image);
			}
			SG_RuneMoveStoreFree(&movement);
		}
		SG_RuneCxDestroy(geometry);
	}
	SG_ConfigurationSemanticsDestroy(semantics);
	SG_ConfigurationDestroy(space);
	SG_BspWorldDestroy(world);
	return 0;
}
