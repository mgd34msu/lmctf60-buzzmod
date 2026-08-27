/* Lossless human command and fixed-point Pmove trace writer. */
#include "../g_local.h"
#include "sg_cvars.h"
#include "sg_human_trace.h"
#include "sg_identity.h"
#include "sg_local.h"

#include <stdint.h>
#include <stdio.h>
#include <math.h>

#define SG_HUMAN_TRACE_FORMAT "lmctf-human-trace-v2"

static FILE *sg_human_trace_file;
static unsigned long sg_human_trace_sequence;
static unsigned long sg_human_trace_hook_event;
static qboolean sg_human_trace_open_failed;
static qboolean sg_human_trace_match_ended;
static qboolean sg_human_trace_rune_bound;
static qboolean sg_human_trace_rune_bind_attempted;

static qboolean HumanTraceVectorQ8(const vec3_t vector, int32_t out[3])
{
	int i;

	for (i = 0; i < 3; i++)
	{
		double scaled = (double)vector[i] * 8.0;

		if (!isfinite(scaled) || scaled < (double)INT32_MIN ||
		    scaled > (double)INT32_MAX)
			return false;
		out[i] = (int32_t)lround(scaled);
	}
	return true;
}

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
	if (sg_human_trace_open_failed || sg_human_trace_match_ended)
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

static qboolean HumanTraceBindRune(qboolean allow_transient_load)
{
	const rune_t *rune;
	rune_t *transient_rune = NULL;
	const rune_artifact_t *artifact;
	const rune_identity_t *identity;
	int written;
	qboolean result = true;

	if (sg_human_trace_rune_bound)
		return true;
	rune = SG_Rune();
	if (!rune && allow_transient_load)
	{
		if (sg_human_trace_rune_bind_attempted)
			goto cleanup;
		sg_human_trace_rune_bind_attempted = true;
		transient_rune = Rune_Load(level.mapname);
		rune = transient_rune;
	}
	if (!rune || rune->artifact.route_contract !=
	    RUNE_ROUTE_CONTRACT_LOCAL_ONLY || !rune->encoded_sha256[0])
		goto cleanup;
	artifact = &rune->artifact;
	identity = &artifact->identity;
	written = fprintf(sg_human_trace_file,
		"{\"format\":\"%s\",\"kind\":\"rune-bind\","
		"\"start_sequence\":%lu,\"start_hook_event\":%lu,"
		"\"frame\":%d,\"map\":\"%s\","
		"\"bsp_checksum\":%u,\"entity_crc32\":%u,"
		"\"physics_flags\":%u,\"gravity\":%.9g,"
		"\"airaccelerate\":%.9g,\"maxvelocity\":%.9g,"
		"\"pmove_substep_ms\":%u,\"server_frame_ms\":%u,"
		"\"host_physics_id\":%u,\"route_contract\":%u,"
		"\"payload_crc32\":%u,\"header_crc32\":%u,"
		"\"action_contract_crc32\":%u,"
		"\"mechanism_contract_crc32\":%u,"
		"\"num_seeds\":%u,\"num_links\":%u,"
		"\"num_mechanism_nodes\":%u,"
		"\"num_mechanism_edges\":%u,"
		"\"num_inventory_edges\":%u,"
		"\"num_mechanism_plans\":%u,\"string_bytes\":%u,"
		"\"rune_sha256\":\"%s\"}\n",
		SG_HUMAN_TRACE_FORMAT, sg_human_trace_sequence + 1UL,
		sg_human_trace_hook_event + 1UL,
		level.framenum, identity->map_name, identity->bsp_checksum,
		identity->entity_crc32, identity->physics_flags,
		(double)identity->gravity, (double)identity->airaccelerate,
		(double)identity->maxvelocity,
		(unsigned int)identity->pmove_substep_ms,
		(unsigned int)identity->server_frame_ms,
		identity->host_physics_id,
		(unsigned int)artifact->route_contract, artifact->payload_crc32,
		artifact->header_crc32, artifact->action_contract_crc32,
		artifact->mechanism_contract_crc32, artifact->num_seeds,
		artifact->num_links, artifact->num_mechanism_nodes,
		artifact->num_mechanism_edges, artifact->num_inventory_edges,
		artifact->num_mechanism_plans, artifact->string_bytes,
		rune->encoded_sha256);
	if (written < 0 || fflush(sg_human_trace_file) != 0)
	{
		gi.dprintf("humantrace: could not bind LOCAL_ONLY RUNE evidence\n");
		fclose(sg_human_trace_file);
		sg_human_trace_file = NULL;
		sg_human_trace_open_failed = true;
		result = false;
		goto cleanup;
	}
	sg_human_trace_rune_bound = true;
cleanup:
	Rune_Free(transient_rune);
	return result;
}

void SG_HumanTraceNewLevel(void)
{
	if (sg_human_trace_file)
	{
		fclose(sg_human_trace_file);
		sg_human_trace_file = NULL;
	}
	sg_human_trace_sequence = 0;
	sg_human_trace_hook_event = 0;
	sg_human_trace_open_failed = false;
	sg_human_trace_match_ended = false;
	sg_human_trace_rune_bound = false;
	sg_human_trace_rune_bind_attempted = false;
}

void SG_HumanTraceMatchEnd(void)
{
	if (sg_human_trace_file)
	{
		int flush_failed = fflush(sg_human_trace_file) != 0;
		int close_failed = fclose(sg_human_trace_file) != 0;

		if (flush_failed || close_failed)
			gi.dprintf("humantrace: match-end close failed\n");
		sg_human_trace_file = NULL;
	}
	sg_human_trace_match_ended = true;
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
	if (client_key <= 0 || !HumanTraceOpen() || !HumanTraceBindRune(false))
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

static qboolean HumanTraceHookReady(edict_t *entity)
{
	SG_CvarsInit();
	if (!sg_cv.humantrace->value || !entity || !entity->client ||
	    !entity->inuse || (entity->flags & FL_BOT))
		return false;
	/* A missing RUNE is why human evidence may be needed.  Bind immediately
	 * when an exact LOCAL_ONLY source exists, but still retain hook lifecycle
	 * evidence when it does not.  The offline recovery path later requires an
	 * exact map/BSP/entity/physics match before it may nominate any edge. */
	return HumanTraceOpen() && HumanTraceBindRune(true);
}

static void HumanTraceHookCommit(int written)
{
	if (written >= 0 && fflush(sg_human_trace_file) == 0)
		return;
	gi.dprintf("humantrace: hook telemetry write failed\n");
	if (sg_human_trace_file)
		fclose(sg_human_trace_file);
	sg_human_trace_file = NULL;
	sg_human_trace_open_failed = true;
}

void SG_HumanTraceHookFire(edict_t *entity, edict_t *hook)
{
	int32_t origin[3], velocity[3];
	int written;
	int client_key, hook_key;

	if (!HumanTraceHookReady(entity) || !hook || hook->owner != entity ||
	    !HumanTraceVectorQ8(entity->s.origin, origin) ||
	    !HumanTraceVectorQ8(entity->velocity, velocity))
		return;
	client_key = HumanTraceEntityKey(entity);
	hook_key = HumanTraceEntityKey(hook);
	if (client_key <= 0 || hook_key <= 0)
		return;
	written = fprintf(sg_human_trace_file,
		"{\"format\":\"%s\",\"kind\":\"hook-fire\","
		"\"event\":%lu,\"after_step\":%lu,\"client\":%d,"
		"\"frame\":%d,\"hook\":%d,"
		"\"origin_q8\":[%d,%d,%d],\"velocity_q8\":[%d,%d,%d],"
		"\"view_short\":[%d,%d],\"hand\":%d}\n",
		SG_HUMAN_TRACE_FORMAT, ++sg_human_trace_hook_event,
		sg_human_trace_sequence, client_key, level.framenum, hook_key,
		origin[0], origin[1], origin[2], velocity[0], velocity[1],
		velocity[2], (short)ANGLE2SHORT(entity->client->v_angle[PITCH]),
		(short)ANGLE2SHORT(entity->client->v_angle[YAW]),
		entity->client->pers.hand);
	HumanTraceHookCommit(written);
}

void SG_HumanTraceHookAttach(edict_t *entity, edict_t *hook,
	edict_t *target)
{
	int32_t bite[3];
	int written;
	int client_key, hook_key, target_key;

	if (!HumanTraceHookReady(entity) || !hook || !target ||
	    hook->owner != entity || hook->hook_target != target ||
	    !HumanTraceVectorQ8(hook->s.origin, bite))
		return;
	client_key = HumanTraceEntityKey(entity);
	hook_key = HumanTraceEntityKey(hook);
	target_key = HumanTraceEntityKey(target);
	if (client_key <= 0 || hook_key <= 0 || target_key < 0)
		return;
	written = fprintf(sg_human_trace_file,
		"{\"format\":\"%s\",\"kind\":\"hook-attach\","
		"\"event\":%lu,\"after_step\":%lu,\"client\":%d,"
		"\"frame\":%d,\"hook\":%d,\"bite_q8\":[%d,%d,%d],"
		"\"target\":%d,\"world\":%d}\n",
		SG_HUMAN_TRACE_FORMAT, ++sg_human_trace_hook_event,
		sg_human_trace_sequence, client_key, level.framenum, hook_key,
		bite[0], bite[1], bite[2], target_key, target == g_edicts ? 1 : 0);
	HumanTraceHookCommit(written);
}

void SG_HumanTraceHookRelease(edict_t *entity)
{
	edict_t *hook;
	int32_t origin[3], velocity[3];
	int written;
	int client_key, hook_key;

	if (!HumanTraceHookReady(entity))
		return;
	hook = entity->client->hook;
	if (!hook || hook->owner != entity ||
	    !HumanTraceVectorQ8(entity->s.origin, origin) ||
	    !HumanTraceVectorQ8(entity->velocity, velocity))
		return;
	client_key = HumanTraceEntityKey(entity);
	hook_key = HumanTraceEntityKey(hook);
	if (client_key <= 0 || hook_key <= 0)
		return;
	written = fprintf(sg_human_trace_file,
		"{\"format\":\"%s\",\"kind\":\"hook-release\","
		"\"event\":%lu,\"after_step\":%lu,\"client\":%d,"
		"\"frame\":%d,\"hook\":%d,"
		"\"origin_q8\":[%d,%d,%d],\"velocity_q8\":[%d,%d,%d]}\n",
		SG_HUMAN_TRACE_FORMAT, ++sg_human_trace_hook_event,
		sg_human_trace_sequence, client_key, level.framenum, hook_key,
		origin[0], origin[1], origin[2], velocity[0], velocity[1],
		velocity[2]);
	HumanTraceHookCommit(written);
}
