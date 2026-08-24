#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_human_trace.h"
#include "slipgate/sg_identity.h"
#include "slipgate/sg_local.h"

game_locals_t game;
level_locals_t level;
game_import_t gi;
game_export_t globals;
spawn_temp_t st;
sg_cvars_t sg_cv;

static edict_t entities[3];
static gclient_t client;
static rune_t source_rune;
static cvar_t enabled;
static cvar_t trace_directory;
static cvar_t game_directory;
static int load_count;
static int free_count;

edict_t *g_edicts = entities;

static cvar_t *TestCvar(char *name, char *value, int flags)
{
	(void)value;
	(void)flags;
	if (strcmp(name, "gamedir") == 0)
		return &game_directory;
	return NULL;
}

static void TestDprintf(char *format, ...)
{
	(void)format;
}

void SG_CvarsInit(void)
{
	sg_cv.humantrace = &enabled;
	sg_cv.humantracedir = &trace_directory;
}

sg_identity_status_t SG_LevelIdentitySnapshot(const char *mapname,
	sg_level_identity_t *out)
{
	if (!mapname || strcmp(mapname, "tracehook") != 0 || !out)
		return SG_IDENTITY_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	out->bsp_checksum = 101;
	out->entity_crc32 = 202;
	out->host_physics_id = 1;
	strcpy(out->mapname, mapname);
	return SG_IDENTITY_OK;
}

rune_t *SG_Rune(void)
{
	return NULL;
}

rune_t *Rune_Load(const char *mapname)
{
	if (!mapname || strcmp(mapname, "tracehook") != 0)
		return NULL;
	load_count++;
	return &source_rune;
}

void Rune_Free(rune_t *rune)
{
	if (rune == &source_rune)
		free_count++;
}

static int CountRecords(const char *path, const char *kind)
{
	FILE *file = fopen(path, "r");
	char line[4096];
	int count = 0;

	if (!file)
		return -1;
	while (fgets(line, sizeof(line), file))
		if (strstr(line, kind))
			count++;
	fclose(file);
	return count;
}

int main(int argc, char **argv)
{
	char path[1024];
	edict_t *player = &entities[1];
	edict_t *hook = &entities[2];

	if (argc != 2)
		return 2;
	memset(&source_rune, 0, sizeof(source_rune));
	source_rune.artifact.route_contract = RUNE_ROUTE_CONTRACT_LOCAL_ONLY;
	source_rune.artifact.identity.bsp_checksum = 101;
	source_rune.artifact.identity.entity_crc32 = 202;
	source_rune.artifact.identity.gravity = 800.0f;
	source_rune.artifact.identity.maxvelocity = 2000.0f;
	source_rune.artifact.identity.pmove_substep_ms = 25;
	source_rune.artifact.identity.server_frame_ms = 100;
	source_rune.artifact.identity.host_physics_id = 1;
	strcpy(source_rune.artifact.identity.map_name, "tracehook");
	source_rune.artifact.num_seeds = 1;
	source_rune.artifact.string_bytes = 1;
	memset(source_rune.encoded_sha256, 'a', 64);
	source_rune.encoded_sha256[64] = '\0';

	enabled.value = 1.0f;
	trace_directory.string = argv[1];
	game_directory.string = argv[1];
	gi.cvar = TestCvar;
	gi.dprintf = TestDprintf;
	globals.num_edicts = 3;
	strcpy(level.mapname, "tracehook");
	level.framenum = 17;

	player->inuse = true;
	player->client = &client;
	player->viewheight = 22;
	player->client->pers.hand = RIGHT_HANDED;
	player->client->v_angle[YAW] = 90.0f;
	hook->inuse = true;
	hook->owner = player;
	hook->s.origin[0] = 64.0f;

	SG_HumanTraceNewLevel();
	SG_HumanTraceHookFire(player, hook);
	hook->hook_target = &entities[0];
	SG_HumanTraceHookAttach(player, hook, &entities[0]);
	player->client->hook = hook;
	SG_HumanTraceHookRelease(player);
	SG_HumanTraceMatchEnd();

	if (snprintf(path, sizeof(path), "%s/humantrace-tracehook.jsonl",
	    argv[1]) >= (int)sizeof(path))
		return 3;
	if (load_count != 1 || free_count != 1 ||
	    CountRecords(path, "\"kind\":\"rune-bind\"") != 1 ||
	    CountRecords(path, "\"kind\":\"hook-fire\"") != 1 ||
	    CountRecords(path, "\"kind\":\"hook-attach\"") != 1 ||
	    CountRecords(path, "\"kind\":\"hook-release\"") != 1)
		return 4;
	puts("sg_human_trace_hook_test: ok");
	return 0;
}
