/* Offline game-module command bridge for compact RUNE construction. */
#include "../g_local.h"
#undef world

#include "sg_rune_compact_game.h"

#include <stdio.h>
#include <string.h>

#include "sg_bsp_world.h"
#include "sg_host_law_owner.h"
#include "sg_rune_compact_generation.h"
#include "sg_rune_install.h"

static void GenerationProgress(void *context,
	sg_rune_compact_generation_stage_t stage,
	const sg_rune_compact_generation_counts_t *counts)
{
	const char *mapname = context;

	gi.dprintf("rune: compact generation map=%s stage=%s cells=%u "
		"portals=%u capabilities=%u fibers=%u weapons=%u bytes=%lu\n",
		mapname, SG_RuneCompactGenerationStageString(stage),
		(unsigned int)counts->composer_cells,
		(unsigned int)counts->composer_portals,
		(unsigned int)counts->composer_movement_capabilities,
		(unsigned int)counts->composer_movement_fibers,
		(unsigned int)counts->composer_weapon_kernels,
		(unsigned long)counts->encoded_bytes);
}

int SG_RuneCompactGameGenerate(const char *mapname)
{
	cvar_t *game_directory_cvar;
	const char *game_directory;
	char bsp_path[MAX_OSPATH];
	char destination[MAX_OSPATH];
	sg_bsp_world_t *bsp = NULL;
	sg_bsp_error_t bsp_error;
	sg_host_collision_authority_t collision_authority;
	sg_host_law_construction_t *construction = NULL;
	sg_host_law_result_t host_result;
	sg_host_collision_scene_t scene;
	sg_rune_compact_generation_input_t input;
	sg_rune_compact_generation_result_t result;
	int bsp_written;
	int generated;

	if (mapname == NULL || mapname[0] == '\0')
	{
		gi.dprintf("rune: compact generation refused stage=argument\n");
		return 0;
	}
	host_result = SG_HostLawProductionEnsureLevel(mapname);
	if (host_result.status != SG_HOST_LAW_OK)
	{
		gi.dprintf("rune: compact generation refused stage=host status=%s "
			"field=%s\n", SG_HostLawStatusString(host_result.status),
			SG_HostLawFieldString(host_result.field));
		return 0;
	}
	game_directory_cvar = gi.cvar("gamedir", "", 0);
	game_directory = game_directory_cvar && game_directory_cvar->string &&
		game_directory_cvar->string[0] ? game_directory_cvar->string : ".";
	bsp_written = snprintf(bsp_path, sizeof(bsp_path), "%s/maps/%s.bsp",
		game_directory, mapname);
	if (bsp_written < 0 || (size_t)bsp_written >= sizeof(bsp_path) ||
		!SG_RuneInstallDestinationPath(destination, sizeof(destination),
			game_directory, mapname))
	{
		gi.dprintf("rune: compact generation refused stage=path\n");
		return 0;
	}
	memset(&bsp_error, 0, sizeof(bsp_error));
	if (!SG_BspWorldLoadFile(bsp_path, &bsp, &bsp_error))
	{
		gi.dprintf("rune: compact generation refused stage=bsp code=%u "
			"lump=%u record=%u\n", (unsigned int)bsp_error.code,
			(unsigned int)bsp_error.lump, (unsigned int)bsp_error.record);
		return 0;
	}
	memset(&collision_authority, 0, sizeof(collision_authority));
	collision_authority.world = bsp;
	collision_authority.content_identity = bsp->content_identity;
	host_result = SG_HostLawProductionConstructionIssue(&collision_authority,
		&construction);
	if (host_result.status != SG_HOST_LAW_OK || construction == NULL)
	{
		gi.dprintf("rune: compact generation refused stage=construction "
			"status=%s field=%s element=%u\n",
			SG_HostLawStatusString(host_result.status),
			SG_HostLawFieldString(host_result.field),
			(unsigned int)host_result.element);
		SG_BspWorldDestroy(bsp);
		return 0;
	}
	memset(&scene, 0, sizeof(scene));
	memset(&input, 0, sizeof(input));
	input.builder_input.construction = construction;
	input.destination = destination;
	input.collision_scene = &scene;
	input.progress = GenerationProgress;
	input.progress_context = destination;
	memset(&result, 0, sizeof(result));
	generated = SG_RuneCompactGenerationRun(&input, &result);
	if (!generated)
	{
		gi.dprintf("rune: compact generation failed map=%s stage=%s "
			"error=%s builder=%u geometry=%u response=%u mechanisms=%u "
			"static=%u movement=%u relation=%u weapon=%u composer=%u "
			"wire_encode=%s wire_decode=%s publication=%u os_error=%d\n",
			mapname, SG_RuneCompactGenerationStageString(result.stage),
			SG_RuneCompactGenerationErrorString(result.error),
			(unsigned int)result.builder_error.code,
			(unsigned int)result.geometry_error.code,
			(unsigned int)result.response_error.code,
			(unsigned int)result.mechanisms_error.code,
			(unsigned int)result.static_error.code,
			(unsigned int)result.movement_error.code,
			(unsigned int)result.relation_error.code,
			(unsigned int)result.weapon_error.status,
			(unsigned int)result.composer_error.code,
			SG_RuneCompactWireErrorString(result.wire_encode_error.code),
			SG_RuneCompactWireErrorString(result.wire_decode_error.code),
			(unsigned int)result.publication.diagnostic,
			result.publication.os_error);
	}
	else
		gi.dprintf("rune: compact generation published map=%s path=%s "
			"bytes=%lu durable=%d\n", mapname, destination,
			(unsigned long)result.accepted.encoded_bytes, result.durable);
	SG_HostLawConstructionDestroy(construction);
	SG_BspWorldDestroy(bsp);
	return generated;
}
