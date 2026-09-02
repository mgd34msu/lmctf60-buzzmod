#define _POSIX_C_SOURCE 200809L
/* cellsdump MAP.bsp: run the era-4 cell builder and regions on one map and
 * print what came out and how long it took. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "slipgate/sg_rune_bsp.h"
#include "slipgate/sg_rune_law.h"
#include "slipgate/sg_configuration_space.h"
#include "slipgate/sg_configuration_semantics.h"
#include "slipgate/sg_rune_cx_build.h"
#include "slipgate/sg_rune_artifact.h"
#include "slipgate/sg_rune_mechanisms.h"
#include "slipgate/sg_rune_hook.h"
#include "slipgate/sg_rune_fire.h"
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

static void FireProgress(void *context, uint32_t done, uint32_t total)
{
	fprintf(stderr, "fire: %u/%u sources [%.1fs]\n", (unsigned)done, (unsigned)total,
		Now() - *(double *)context);
}

int main(int argc, char **argv)
{
	sg_rune_bsp_t bsp_store, *world = &bsp_store;
	sg_rune_bsp_fault_t bsp_fault;
	sg_rune_law_t law4;
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
	if (!SG_RuneBspLoadFile(argv[1], world, &bsp_fault))
	{
		fprintf(stderr, "bsp load failed: %s lump %d record %u\n",
			bsp_fault.what ? bsp_fault.what : "?", bsp_fault.lump,
			(unsigned)bsp_fault.record);
		return 1;
	}
	SG_RuneLawEngine(&law4, 800.0f);
	printf("%s: %u leaves, %u brushes, %u planes\n", argv[1],
		(unsigned)world->leaf_count, (unsigned)world->brush_count,
		(unsigned)world->plane_count);
	t0 = Now();
	if (!SG_ConfigurationBuildWithProgress(world, &law4, NULL, Progress, NULL,
		&space, &error))
	{
		fprintf(stderr, "cell build failed: %s (source %u)\n",
			SG_ConfigurationErrorString(error.code),
			(unsigned)error.source_index);
		return 1;
	}
	t1 = Now();
	for (cell = 0U; cell < space->cell_count; cell++)
		if (space->cells[cell].stance == SG_CFG_STANDING)
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
	if (!SG_ConfigurationSemanticsBuild(world, &law4, space, &semantics_limits,
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
		if (!SG_RuneMechMarkCells(world, geometry))
		{
			fprintf(stderr, "mechanism marking failed\n");
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
			sg_rune_mech_store_t mechanisms;
			sg_rune_fire_store_t fires;
			uint32_t counts[SG_RUNE_MOVE_KIND_COUNT];
			uint32_t index;
			double t4;

			if (!SG_RuneMoveStoreInit(&movement, &law4) ||
				!SG_RuneMoveEmitComplex(&movement, &view, &law4))
			{
				fprintf(stderr, "movement failed\n");
				return 1;
			}
			SG_RuneMechStoreInit(&mechanisms);
			if (!SG_RuneMechEmit(world, geometry, &law4, &movement,
				&mechanisms))
			{
				fprintf(stderr, "mechanisms failed\n");
				return 1;
			}
			{
				sg_rune_hook_report_t hooks;
				double th = Now();

				if (!SG_RuneHookEmit(world, geometry, &law4, &movement,
					&hooks))
				{
					fprintf(stderr, "hook reach failed\n");
					return 1;
				}
				printf("hook: %u bites, %u cells with rides, %u records, %u traces "
					"[%.2fs]; candidates %u bolt clear %u pull clear %u flights %u\n",
					(unsigned)hooks.bites, (unsigned)hooks.cells,
					(unsigned)hooks.records, (unsigned)hooks.traces, Now() - th,
					(unsigned)hooks.candidates, (unsigned)hooks.bolt_clear,
					(unsigned)hooks.pull_clear, (unsigned)hooks.flights);
			}
			{
				sg_rune_fire_report_t fire;
				double tf = Now();

				SG_RuneFireStoreInit(&fires);
				if (!SG_RuneFireEmit(world, geometry, &law4, &fires,
					FireProgress, &tf, &fire))
				{
					fprintf(stderr, "fire relations failed\n");
					return 1;
				}
				printf("fire: %u sources, %u pairs, %u records (line %u corridor %u "
					"blast %u lob %u), %u traces, %u arcs [%.2fs]; %.1f MB\n",
					(unsigned)fire.sources, (unsigned)fire.pairs, (unsigned)fire.records,
					(unsigned)fire.line, (unsigned)fire.corridor, (unsigned)fire.blast,
					(unsigned)fire.lob, (unsigned)fire.traces, (unsigned)fire.arcs,
					Now() - tf, (double)fires.record_count * 8.0 / 1048576.0);
			}
			printf("mechanisms: %u records, %u gate cells\n",
				(unsigned)mechanisms.record_count, (unsigned)mechanisms.cell_count);
			{
				uint32_t m, kinds[SG_RUNE_MECH_KIND_COUNT] = { 0 };

				for (m = 0U; m < mechanisms.record_count; m++)
					kinds[mechanisms.records[m].kind]++;
				for (m = 0U; m < SG_RUNE_MECH_KIND_COUNT; m++)
					if (kinds[m])
						printf("  %-12s %u\n", SG_RuneMechKindString(
							(sg_rune_mech_kind_t)m), kinds[m]);
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
				source.identity.bsp_crc32 = world->file_crc32;
				source.identity.entity_crc32 = world->entity_crc32;
				source.identity.bsp_bytes = world->file_bytes;
				source.identity.law_crc32 = SG_RuneLawCrc(&law4);
				source.law = law4;
				source.complex = view;
				SG_RuneMoveStoreView(&movement, &source.movement);
				SG_RuneMechStoreView(&mechanisms, &source.mechanisms);
				SG_RuneFireStoreView(&fires, &source.fires);
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
			SG_RuneMechStoreFree(&mechanisms);
			SG_RuneFireStoreFree(&fires);
			SG_RuneMoveStoreFree(&movement);
		}
		SG_RuneCxDestroy(geometry);
	}
	SG_ConfigurationSemanticsDestroy(semantics);
	SG_ConfigurationDestroy(space);
	SG_RuneBspFree(world);
	return 0;
}
