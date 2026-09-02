#include "../g_local.h"
#undef world
#include "sg_rune_level.h"

#include <string.h>

#include "sg_host_law_owner.h"

sg_rune_level_t sg_rune_level;

static void IdentityFromAuthority(const sg_host_law_runtime_authority_t *view,
	sg_rune_identity_t *identity, sg_rune_law_t *law)
{
	const sg_host_law_view_t *laws = &view->view;
	const sg_host_static_identity_t *host = &laws->static_identity;

	memset(identity, 0, sizeof(*identity));
	memcpy(identity->bsp_sha256, host->bsp_identity.bytes,
		sizeof(identity->bsp_sha256));
	identity->bsp_bytes = host->bsp_bytes;
	identity->bsp_checksum = host->engine_checksum;
	identity->entity_crc32 = host->entity_crc32;
	identity->physics_abi_id = host->physics_abi_id;
	identity->collision_law_id = laws->collision_law_id;
	identity->pmove_law_id = laws->pmove_law_id;
	identity->gravity_law_id = laws->gravity_law_id;
	identity->hook_law_id = laws->hook_law_id;
	identity->mechanism_law_id = laws->mechanism_law_id;
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

void SG_RuneLevelClear(void)
{
	uint32_t index;

	for (index = 0U; index < SG_RUNE_LEVEL_FIELDS; index++)
		SG_RuneFieldFree(&sg_rune_level.fields[index].field);
	SG_RuneRouterFree(&sg_rune_level.router);
	SG_RuneLocatorFree(&sg_rune_level.locator);
	SG_RuneArtifactRelease(&sg_rune_level.artifact);
	memset(&sg_rune_level, 0, sizeof(sg_rune_level));
	for (index = 0U; index < SG_RUNE_LEVEL_FIELDS; index++)
		sg_rune_level.fields[index].destination_cell = SG_RUNE_CX_INDEX_NONE;
}

int SG_RuneLevelCurrent(void)
{
	return sg_rune_level.current;
}

int SG_RuneLevelBegin(const char *mapname)
{
	cvar_t *game_directory_cvar;
	const char *game_directory;
	char path[MAX_OSPATH];
	sg_rune_artifact_status_t status;
	sg_rune_fault_t fault;
	sg_host_law_runtime_authority_t authority;
	sg_host_law_result_t host_result;
	sg_rune_identity_t identity;
	sg_rune_law_t law;
	int os_error = 0;

	SG_RuneLevelClear();
	if (!mapname || !mapname[0])
		return 0;
	game_directory_cvar = gi.cvar("gamedir", "", 0);
	game_directory = game_directory_cvar && game_directory_cvar->string &&
		game_directory_cvar->string[0] ? game_directory_cvar->string : ".";
	if (!SG_RuneArtifactPath(path, sizeof(path), game_directory, mapname))
	{
		gi.dprintf("slipgate: rune refused map=%s: path\n", mapname);
		return 0;
	}
	status = SG_RuneArtifactLoadFile(path, &sg_rune_level.artifact, &os_error,
		&fault);
	if (status != SG_RUNE_ARTIFACT_OK)
	{
		if (status == SG_RUNE_ARTIFACT_FILE_ERROR)
			gi.dprintf("slipgate: no rune for %s (%s): bots hold\n", mapname,
				path);
		else if (fault.array)
			gi.dprintf("slipgate: rune refused map=%s: %s at %s[%u] %s\n",
				mapname, SG_RuneArtifactStatusString(status), fault.array,
				(unsigned int)fault.record, fault.reason);
		else
			gi.dprintf("slipgate: rune refused map=%s: %s\n", mapname,
				SG_RuneArtifactStatusString(status));
		SG_RuneLevelClear();
		return 0;
	}
	host_result = SG_HostLawProductionAcquire(&authority);
	if (host_result.status != SG_HOST_LAW_OK)
	{
		gi.dprintf("slipgate: rune refused map=%s: host %s\n", mapname,
			SG_HostLawStatusString(host_result.status));
		SG_RuneLevelClear();
		return 0;
	}
	IdentityFromAuthority(&authority, &identity, &law);
	if (!SG_RuneIdentityMatches(&sg_rune_level.artifact.identity, &identity))
	{
		gi.dprintf("slipgate: rune refused map=%s: identity differs from the "
			"live host (map bytes or laws changed); regenerate with sv rune\n",
			mapname);
		SG_RuneLevelClear();
		return 0;
	}
	if (!SG_RuneLawMatches(&sg_rune_level.artifact.law, &law))
	{
		gi.dprintf("slipgate: rune refused map=%s: law differs from the live "
			"host (gravity, hulls, or frame); regenerate with sv rune\n",
			mapname);
		SG_RuneLevelClear();
		return 0;
	}
	if (!SG_RuneLocatorBuild(&sg_rune_level.locator, &sg_rune_level.artifact) ||
		!SG_RuneRouterBuild(&sg_rune_level.router, &sg_rune_level.artifact))
	{
		gi.dprintf("slipgate: rune refused map=%s: out of memory indexing\n",
			mapname);
		SG_RuneLevelClear();
		return 0;
	}
	strncpy(sg_rune_level.mapname, mapname, sizeof(sg_rune_level.mapname) - 1U);
	sg_rune_level.mapname[sizeof(sg_rune_level.mapname) - 1U] = '\0';
	sg_rune_level.current = 1;
	gi.dprintf("slipgate: rune ready %s, cells %u portals %u capabilities %u "
		"bytes %lu\n", mapname,
		(unsigned int)sg_rune_level.artifact.complex.cell_count,
		(unsigned int)sg_rune_level.artifact.complex.portal_count,
		(unsigned int)sg_rune_level.artifact.movement.capability_count,
		(unsigned long)sg_rune_level.artifact.image_size);
	return 1;
}

const sg_rune_field_t *SG_RuneLevelField(uint32_t destination_cell)
{
	uint32_t index, victim = 0U;
	uint64_t oldest = UINT64_MAX;

	if (!sg_rune_level.current ||
		destination_cell >= sg_rune_level.artifact.complex.cell_count)
		return NULL;
	sg_rune_level.frame++;
	for (index = 0U; index < SG_RUNE_LEVEL_FIELDS; index++)
	{
		sg_rune_level_field_t *slot = &sg_rune_level.fields[index];

		if (slot->destination_cell == destination_cell)
		{
			slot->last_used_frame = sg_rune_level.frame;
			return &slot->field;
		}
		if (slot->last_used_frame < oldest)
		{
			oldest = slot->last_used_frame;
			victim = index;
		}
	}
	{
		sg_rune_level_field_t *slot = &sg_rune_level.fields[victim];

		if (!SG_RuneFieldBuild(&slot->field, &sg_rune_level.router,
			destination_cell))
		{
			slot->destination_cell = SG_RUNE_CX_INDEX_NONE;
			return NULL;
		}
		slot->destination_cell = destination_cell;
		slot->last_used_frame = sg_rune_level.frame;
		return &slot->field;
	}
}

uint32_t SG_RuneLevelLocate(const float origin[3], int crouching,
	float *violation_out)
{
	if (!sg_rune_level.current)
	{
		if (violation_out)
			*violation_out = 0.0f;
		return SG_RUNE_CX_INDEX_NONE;
	}
	return SG_RuneLocate(&sg_rune_level.locator, origin,
		crouching ? SG_RUNE_MOVE_CROUCHING : SG_RUNE_MOVE_STANDING, 8.0f,
		violation_out);
}
