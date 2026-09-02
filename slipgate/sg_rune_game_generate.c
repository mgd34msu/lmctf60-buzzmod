#include "../g_local.h"
#undef world
#include "sg_rune_game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sg_bsp_world.h"
#include "sg_host_law_owner.h"
#include "sg_rune_artifact.h"
#include "sg_rune_generate.h"

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

static void IdentityFromHost(const sg_host_law_construction_view_t *view,
	sg_rune_identity_t *identity, sg_rune_law_t *law)
{
	const sg_host_static_identity_t *host = &view->host_static_identity;

	memset(identity, 0, sizeof(*identity));
	memcpy(identity->bsp_sha256, host->bsp_identity.bytes,
		sizeof(identity->bsp_sha256));
	identity->bsp_bytes = host->bsp_bytes;
	identity->bsp_checksum = host->engine_checksum;
	identity->entity_crc32 = host->entity_crc32;
	identity->physics_abi_id = host->physics_abi_id;
	identity->collision_law_id = view->laws.collision_law_id;
	identity->pmove_law_id = view->laws.pmove_law_id;
	identity->gravity_law_id = view->laws.gravity_law_id;
	identity->hook_law_id = view->laws.hook_law_id;
	identity->mechanism_law_id = view->laws.mechanism_law_id;
	identity->schema_id = SG_RUNE_ARTIFACT_SCHEMA_ID;

	memset(law, 0, sizeof(*law));
	memcpy(law->standing_mins, host->standing_hull.mins.value,
		sizeof(law->standing_mins));
	memcpy(law->standing_maxs, host->standing_hull.maxs.value,
		sizeof(law->standing_maxs));
	memcpy(law->crouching_mins, host->crouching_hull.mins.value,
		sizeof(law->crouching_mins));
	memcpy(law->crouching_maxs, host->crouching_hull.maxs.value,
		sizeof(law->crouching_maxs));
	law->gravity = host->physics.gravity;
	law->ground_acceleration = host->physics.ground_acceleration;
	law->air_acceleration = host->physics.air_acceleration;
	law->water_acceleration = host->physics.water_acceleration;
	law->hook_acceleration = host->physics.hook_acceleration;
	law->water_drag = host->physics.water_drag;
	law->max_velocity = host->physics.max_velocity;
	law->frame_ms = host->physics.frame_ms;
	law->substep_ms = host->physics.substep_ms;
}

int SG_RuneGameGenerate(const char *mapname)
{
	cvar_t *game_directory_cvar;
	const char *game_directory;
	char bsp_path[MAX_OSPATH];
	char destination[MAX_OSPATH];
	sg_bsp_world_t *bsp = NULL;
	sg_bsp_error_t bsp_error;
	sg_host_collision_authority_t collision_authority;
	const sg_host_collision_authority_t *authority = NULL;
	sg_host_law_construction_t *construction = NULL;
	sg_host_law_construction_view_t view;
	sg_host_law_result_t host_result;
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
	host_result = SG_HostLawProductionEnsureLevel(mapname);
	if (host_result.status != SG_HOST_LAW_OK)
	{
		gi.dprintf("rune: generation refused stage=host status=%s field=%s\n",
			SG_HostLawStatusString(host_result.status),
			SG_HostLawFieldString(host_result.field));
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
	memset(&bsp_error, 0, sizeof(bsp_error));
	if (!SG_BspWorldLoadFile(bsp_path, &bsp, &bsp_error))
	{
		gi.dprintf("rune: generation refused stage=bsp code=%u lump=%u "
			"record=%u\n", (unsigned int)bsp_error.code,
			(unsigned int)bsp_error.lump, (unsigned int)bsp_error.record);
		return 0;
	}
	memset(&collision_authority, 0, sizeof(collision_authority));
	collision_authority.world = bsp;
	collision_authority.content_identity = bsp->content_identity;
	host_result = SG_HostLawProductionConstructionIssue(&collision_authority,
		&construction);
	if (host_result.status == SG_HOST_LAW_OK && construction)
		host_result = SG_HostLawConstructionRead(construction, &view);
	if (host_result.status == SG_HOST_LAW_OK)
		host_result = SG_HostLawProductionCollisionAuthority(&authority);
	if (host_result.status != SG_HOST_LAW_OK || !construction || !authority)
	{
		gi.dprintf("rune: generation refused stage=construction status=%s "
			"field=%s element=%u\n",
			SG_HostLawStatusString(host_result.status),
			SG_HostLawFieldString(host_result.field),
			(unsigned int)host_result.element);
		SG_HostLawConstructionDestroy(construction);
		SG_BspWorldDestroy(bsp);
		return 0;
	}
	IdentityFromHost(&view, &identity, &law);
	gi.dprintf("rune: generation map=%s gravity=%g frame=%ums substep=%ums\n",
		mapname, (double)law.gravity, (unsigned int)law.frame_ms,
		(unsigned int)law.substep_ms);
	if (!SG_RuneGenerate(bsp, authority, &identity, &law, Progress,
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
				"capabilities=%u surfaces=%u seconds=%.1f\n", mapname,
				(unsigned int)report.cells, (unsigned int)report.portals,
				(unsigned int)report.capabilities,
				(unsigned int)report.surfaces, report.seconds);
			gi.dprintf("rune: generation published map=%s path=%s bytes=%lu "
				"durable=1\n", mapname, destination,
				(unsigned long)image_size);
			generated = 1;
		}
	}
	free(image);
	SG_HostLawConstructionDestroy(construction);
	SG_BspWorldDestroy(bsp);
	return generated;
}
