#include "../g_local.h"
#undef world
#include "sg_rune_game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sg_rune_bsp.h"
#include "sg_rune_law.h"
#include "sg_rune_artifact.h"
#include "sg_rune_generate.h"
#include "sg_rune_hook.h"
#include "sg_rune_level.h"

static void Progress(void *context, const char *stage, uint32_t done,
	uint32_t total)
{
	const char *mapname = context;

	if (total == 0U)
		gi.dprintf("rune: generation map=%s stage=%s begin\n", mapname, stage);
	else
		gi.dprintf("rune: generation map=%s stage=%s %u/%u (%u%%)\n", mapname,
			stage, (unsigned int)done, (unsigned int)total,
			(unsigned int)(((uint64_t)done * 100U) / total));
}


int SG_RuneGameGenerate(const char *mapname)
{
	cvar_t *game_directory_cvar;
	const char *game_directory;
	char bsp_path[MAX_OSPATH];
	char destination[MAX_OSPATH];
	sg_rune_bsp_t bsp;
	sg_rune_bsp_fault_t bsp_fault;
	sg_rune_identity_t identity;
	sg_rune_law_t law;
	sg_rune_generate_report_t report;
	unsigned char *image = NULL;
	size_t image_size = 0U;
	sg_rune_artifact_status_t status;
	int os_error = 0;
	int generated = 0;

	if (mapname == NULL || mapname[0] == '\0')
	{
		gi.dprintf("rune: generation refused stage=argument\n");
		return 0;
	}
	game_directory_cvar = gi.cvar("gamedir", "", 0);
	game_directory = game_directory_cvar && game_directory_cvar->string &&
		game_directory_cvar->string[0] ? game_directory_cvar->string : ".";
	if (snprintf(bsp_path, sizeof(bsp_path), "%s/maps/%s.bsp", game_directory,
			mapname) >= (int)sizeof(bsp_path) ||
		!SG_RuneArtifactPath(destination, sizeof(destination), game_directory,
			mapname))
	{
		gi.dprintf("rune: generation refused stage=path\n");
		return 0;
	}
	{
		/* The players' bites for this map, beside the BSP as .bites. */
		char bites[1024];
		size_t n = strlen(bsp_path);

		if (n > 4 && n < sizeof(bites) && !strcmp(bsp_path + n - 4, ".bsp"))
		{
			memcpy(bites, bsp_path, n - 4);
			strcpy(bites + n - 4, ".bites");
			SG_RuneHookSetHumanBites(bites);
		}
		else
			SG_RuneHookSetHumanBites(NULL);
	}
	if (!SG_RuneBspLoadFile(bsp_path, &bsp, &bsp_fault))
	{
		gi.dprintf("rune: generation refused stage=bsp %s lump=%d record=%u\n",
			bsp_fault.what ? bsp_fault.what : "?", bsp_fault.lump,
			(unsigned int)bsp_fault.record);
		return 0;
	}
	if (SG_RuneLevelEntityText() &&
		!SG_RuneBspReplaceEntities(&bsp, SG_RuneLevelEntityText()))
	{
		gi.dprintf("rune: generation refused stage=entities\n");
		SG_RuneBspFree(&bsp);
		return 0;
	}
	SG_RuneLawEngine(&law, sv_gravity ? sv_gravity->value : 800.0f);
	memset(&identity, 0, sizeof(identity));
	identity.bsp_crc32 = bsp.file_crc32;
	identity.entity_crc32 = bsp.entity_crc32;
	identity.bsp_bytes = bsp.file_bytes;
	identity.law_crc32 = SG_RuneLawCrc(&law);
	identity.schema_id = SG_RUNE_ARTIFACT_SCHEMA_ID;
	gi.dprintf("rune: generation map=%s gravity=%g frame=%ums substep=%ums\n",
		mapname, (double)law.gravity, (unsigned int)law.frame_ms,
		(unsigned int)law.substep_ms);
	if (!SG_RuneGenerate(&bsp, &identity, &law, Progress,
		(void *)mapname, &image, &image_size, &report))
		gi.dprintf("rune: generation failed map=%s stage=%s error=%s\n",
			mapname, report.stage ? report.stage : "?",
			report.error ? report.error : "?");
	else
	{
		status = SG_RuneArtifactWriteFile(destination, image, image_size,
			&os_error);
		if (status != SG_RUNE_ARTIFACT_OK)
			gi.dprintf("rune: generation failed map=%s stage=publish error=%s "
				"os_error=%d\n", mapname, SG_RuneArtifactStatusString(status),
				os_error);
		else
		{
			gi.dprintf("rune: generation map=%s cells=%u portals=%u "
				"capabilities=%u surfaces=%u mechanisms=%u hooks=%u fires=%u "
				"seconds=%.1f\n",
				mapname, (unsigned int)report.cells, (unsigned int)report.portals,
				(unsigned int)report.capabilities,
				(unsigned int)report.surfaces, (unsigned int)report.mechanisms,
				(unsigned int)report.hooks, (unsigned int)report.fires,
				report.seconds);
			gi.dprintf("rune: generation published map=%s path=%s bytes=%lu "
				"durable=1\n", mapname, destination,
				(unsigned long)image_size);
			generated = 1;
		}
	}
	free(image);
	SG_RuneBspFree(&bsp);
	return generated;
}
