/* Lossless human command and fixed-point Pmove trace writer. */
#include "../g_local.h"
#include "sg_cvars.h"
#include "sg_human_trace.h"
#include "sg_identity.h"

#include <stdint.h>
#include <stdio.h>

#define SG_HUMAN_TRACE_FORMAT "lmctf-human-trace-v1"

static FILE *sg_human_trace_file;
static unsigned long sg_human_trace_sequence;
static qboolean sg_human_trace_open_failed;

static int HumanTraceEntityKey(const edict_t *entity)
{
	uintptr_t address;
	uintptr_t base;
	uintptr_t offset;
	size_t extent;

	if (!entity || !g_edicts)
		return 0;
	address = (uintptr_t)entity;
	base = (uintptr_t)g_edicts;
	extent = (size_t)globals.num_edicts * sizeof(*g_edicts);
	if (address < base)
		return -1;
	offset = address - base;
	if (offset >= extent || offset % sizeof(*g_edicts))
		return -1;
	return (int)(offset / sizeof(*g_edicts));
}

static void HumanTraceState(FILE *file, const pmove_state_t *state)
{
	fprintf(file,
		"{\"type\":%d,\"origin\":[%d,%d,%d],"
		"\"velocity\":[%d,%d,%d],\"flags\":%u,\"time\":%u,"
		"\"gravity\":%d,\"delta_angles\":[%d,%d,%d]}",
		(int)state->pm_type,
		(int)state->origin[0], (int)state->origin[1],
		(int)state->origin[2], (int)state->velocity[0],
		(int)state->velocity[1], (int)state->velocity[2],
		(unsigned int)state->pm_flags, (unsigned int)state->pm_time,
		(int)state->gravity, (int)state->delta_angles[0],
		(int)state->delta_angles[1], (int)state->delta_angles[2]);
}

static qboolean HumanTraceOpen(void)
{
	sg_level_identity_t identity;
	cvar_t *game_directory;
	const char *directory;
	char path[512];

	if (sg_human_trace_file)
		return true;
	if (sg_human_trace_open_failed)
		return false;
	if (SG_LevelIdentitySnapshot(level.mapname, &identity) != SG_IDENTITY_OK)
	{
		gi.dprintf("humantrace: level identity is unavailable\n");
		sg_human_trace_open_failed = true;
		return false;
	}
	game_directory = gi.cvar("gamedir", "", 0);
	directory = sg_cv.humantracedir->string;
	if (!directory[0])
		directory = game_directory && game_directory->string[0]
			? game_directory->string : ".";
	if (snprintf(path, sizeof(path), "%s/humantrace-%s.jsonl",
	        directory, identity.mapname) >= (int)sizeof(path))
	{
		gi.dprintf("humantrace: output path is too long\n");
		sg_human_trace_open_failed = true;
		return false;
	}
	sg_human_trace_file = fopen(path, "a");
	if (!sg_human_trace_file)
	{
		gi.dprintf("humantrace: could not open %s\n", path);
		sg_human_trace_open_failed = true;
		return false;
	}
	fprintf(sg_human_trace_file,
		"{\"format\":\"%s\",\"kind\":\"header\","
		"\"map\":\"%s\",\"bsp_checksum\":%u,\"entity_crc32\":%u,"
		"\"physics_id\":%u,\"module_revision\":%d,"
		"\"module_version\":\"%s\"}\n",
		SG_HUMAN_TRACE_FORMAT, identity.mapname, identity.bsp_checksum,
		identity.entity_crc32, identity.host_physics_id,
		LMCTF_REVISION, LMCTF_VERSION);
	fflush(sg_human_trace_file);
	gi.dprintf("humantrace: recording exact Pmove evidence to %s\n", path);
	return true;
}

void SG_HumanTraceNewLevel(void)
{
	if (sg_human_trace_file)
	{
		fclose(sg_human_trace_file);
		sg_human_trace_file = NULL;
	}
	sg_human_trace_sequence = 0;
	sg_human_trace_open_failed = false;
}

void SG_HumanTracePmove(edict_t *entity,
	const pmove_state_t *before, const pmove_t *after)
{
	const usercmd_t *command;
	int client_key;
	int i;

	SG_CvarsInit();
	if (!sg_cv.humantrace->value || !entity || !entity->client ||
	    !entity->inuse || (entity->flags & FL_BOT) || !before || !after ||
	    before->pm_type != PM_NORMAL || after->numtouch < 0 ||
	    after->numtouch > MAXTOUCH)
		return;
	client_key = HumanTraceEntityKey(entity);
	if (client_key <= 0 || !HumanTraceOpen())
		return;
	command = &after->cmd;
	fprintf(sg_human_trace_file,
		"{\"format\":\"%s\",\"kind\":\"step\",\"seq\":%lu,"
		"\"client\":%d,\"frame\":%d,\"snapinitial\":%d,"
		"\"cmd\":{\"msec\":%u,\"buttons\":%u,"
		"\"angles\":[%d,%d,%d],\"forward\":%d,\"side\":%d,"
		"\"up\":%d,\"impulse\":%u,\"light\":%u},\"before\":",
		SG_HUMAN_TRACE_FORMAT, ++sg_human_trace_sequence, client_key,
		level.framenum, after->snapinitial ? 1 : 0,
		(unsigned int)command->msec, (unsigned int)command->buttons,
		(int)command->angles[0], (int)command->angles[1],
		(int)command->angles[2], (int)command->forwardmove,
		(int)command->sidemove, (int)command->upmove,
		(unsigned int)command->impulse, (unsigned int)command->lightlevel);
	HumanTraceState(sg_human_trace_file, before);
	fputs(",\"after\":", sg_human_trace_file);
	HumanTraceState(sg_human_trace_file, &after->s);
	fprintf(sg_human_trace_file,
		",\"ground\":%d,\"waterlevel\":%d,\"watertype\":%d,"
		"\"touches\":[", HumanTraceEntityKey(after->groundentity),
		after->waterlevel, after->watertype);
	for (i = 0; i < after->numtouch; i++)
		fprintf(sg_human_trace_file, "%s%d", i ? "," : "",
			HumanTraceEntityKey(after->touchents[i]));
	fputs("]}\n", sg_human_trace_file);
	fflush(sg_human_trace_file);
}
