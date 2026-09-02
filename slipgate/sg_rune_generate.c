#define _POSIX_C_SOURCE 200809L
#include "sg_rune_generate.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sg_rune_bsp.h"
#include "sg_configuration_semantics.h"
#include "sg_configuration_space.h"
#include "sg_rune_trace.h"
#include "sg_rune_law.h"
#include "sg_rune_cx_build.h"
#include "sg_rune_fire.h"
#include "sg_rune_hook.h"
#include "sg_rune_mechanisms.h"
#include "sg_rune_movement.h"

typedef struct progress_link_s
{
	sg_rune_generate_progress_fn fn;
	void *context;
} progress_link_t;

static void CellProgress(void *context, uint32_t done, uint32_t total)
{
	const progress_link_t *link = context;

	if (link->fn)
		link->fn(link->context, "cells", done, total);
}

static void FireProgress(void *context, uint32_t done, uint32_t total)
{
	const progress_link_t *link = context;

	if (link->fn)
		link->fn(link->context, "fire", done, total);
}

static void Begin(const progress_link_t *link, const char *stage,
	sg_rune_generate_report_t *report)
{
	report->stage = stage;
	if (link->fn)
		link->fn(link->context, stage, 0U, 0U);
}

static double Now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int Fail(sg_rune_generate_report_t *report, const char *error)
{
	report->error = error;
	return 0;
}

int SG_RuneGenerate(const sg_rune_bsp_t *world,
	const sg_rune_identity_t *identity, const sg_rune_law_t *law,
	sg_rune_generate_progress_fn progress, void *progress_context,
	unsigned char **image_out, size_t *image_size_out,
	sg_rune_generate_report_t *report_out)
{
	sg_rune_generate_report_t report;
	progress_link_t link;
	sg_configuration_space_t *space = NULL;
	sg_configuration_error_t space_error;
	sg_configuration_semantics_t *semantics = NULL;
	sg_configuration_semantics_limits_t semantics_limits;
	sg_configuration_semantics_error_t semantics_error;
	sg_rune_cx_t *complex = NULL;
	sg_rune_cx_error_t complex_error;
	sg_rune_move_store_t movement;
	sg_rune_mech_store_t mechanisms;
	sg_rune_hook_report_t hooks;
	sg_rune_fire_store_t fires;
	sg_rune_fire_report_t fire_report;
	sg_rune_artifact_t source;
	sg_rune_artifact_status_t status;
	double started = Now();
	int ok = 0;

	memset(&report, 0, sizeof(report));
	memset(&movement, 0, sizeof(movement));
	SG_RuneMechStoreInit(&mechanisms);
	SG_RuneFireStoreInit(&fires);
	if (image_out)
		*image_out = NULL;
	if (image_size_out)
		*image_size_out = 0U;
	link.fn = progress;
	link.context = progress_context;
	if (!world || !identity || !law || !image_out ||
		!image_size_out)
	{
		Fail(&report, "invalid argument");
		goto done;
	}

	Begin(&link, "cells", &report);
	if (!SG_ConfigurationBuildWithProgress(world, law, NULL, CellProgress,
		&link, &space, &space_error))
	{
		Fail(&report, SG_ConfigurationErrorString(space_error.code));
		goto done;
	}
	report.cells = space->cell_count;

	Begin(&link, "regions", &report);
	SG_ConfigurationSemanticsDefaultLimits(&semantics_limits);
	if (!SG_ConfigurationSemanticsBuild(world, law, space, &semantics_limits,
		&semantics, &semantics_error))
	{
		Fail(&report, SG_ConfigurationSemanticsErrorString(
			semantics_error.code));
		goto done;
	}

	Begin(&link, "complex", &report);
	if (!SG_RuneCxFromSpace(world, space, semantics, NULL, &complex,
		&complex_error))
	{
		Fail(&report, SG_RuneCxErrorString(complex_error.code));
		goto done;
	}
	Begin(&link, "mechanisms", &report);
	if (!SG_RuneMechMarkCells(world, complex))
	{
		Fail(&report, "mechanism marking failed");
		goto done;
	}
	memset(&source, 0, sizeof(source));
	source.identity = *identity;
	source.law = *law;
	SG_RuneCxRead(complex, &source.complex);
	report.portals = source.complex.portal_count;
	report.surfaces = source.complex.surface_count;

	Begin(&link, "movement", &report);
	if (!SG_RuneMoveStoreInit(&movement, law) ||
		!SG_RuneMoveEmitComplex(&movement, &source.complex, law))
	{
		Fail(&report, "movement emission failed");
		goto done;
	}
	if (!SG_RuneMechEmit(world, complex, law, &movement, &mechanisms))
	{
		Fail(&report, "mechanism emission failed");
		goto done;
	}
	Begin(&link, "hook", &report);
	if (!SG_RuneHookEmit(world, complex, law, &movement, &hooks))
	{
		Fail(&report, "hook emission failed");
		goto done;
	}
	report.hooks = hooks.records;
	Begin(&link, "fire", &report);
	if (!SG_RuneFireEmit(world, complex, law, &fires, FireProgress, &link,
		&fire_report))
	{
		Fail(&report, "fire relations failed");
		goto done;
	}
	report.fires = fire_report.records;
	SG_RuneFireStoreView(&fires, &source.fires);
	SG_RuneMoveStoreView(&movement, &source.movement);
	SG_RuneMechStoreView(&mechanisms, &source.mechanisms);
	report.capabilities = source.movement.capability_count;
	report.mechanisms = source.mechanisms.record_count;

	Begin(&link, "encode", &report);
	status = SG_RuneArtifactEncode(&source, image_out, image_size_out);
	if (status != SG_RUNE_ARTIFACT_OK)
	{
		Fail(&report, SG_RuneArtifactStatusString(status));
		goto done;
	}
	report.image_bytes = *image_size_out;
	ok = 1;

done:
	report.seconds = Now() - started;
	SG_RuneFireStoreFree(&fires);
	SG_RuneMechStoreFree(&mechanisms);
	SG_RuneMoveStoreFree(&movement);
	SG_RuneCxDestroy(complex);
	SG_ConfigurationSemanticsDestroy(semantics);
	SG_ConfigurationDestroy(space);
	if (report_out)
		*report_out = report;
	return ok;
}
