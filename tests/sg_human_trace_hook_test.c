#include <stdarg.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "g_local.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_human_trace.h"
#include "slipgate/sg_identity.h"

game_locals_t game;
level_locals_t level;
game_import_t gi;
game_export_t globals;
spawn_temp_t st;
sg_cvars_t sg_cv;

static edict_t entities[5];
static gclient_t clients[2];
static cvar_t enabled;
static cvar_t trace_directory;
static cvar_t game_directory;
static cvar_t airaccelerate;
static cvar_t gravity;
static cvar_t maxvelocity;
static cvar_t funky_gravity;
cvar_t *sv_gravity = &gravity;
cvar_t *sv_maxvelocity = &maxvelocity;
cvar_t *want_funky_gravity = &funky_gravity;
#ifdef SG_HUMAN_TRACE_WRAP_FWRITE
static int inject_write_failure;
static unsigned inject_write_failure_after;

size_t __real_fwrite(const void *pointer, size_t size, size_t count,
	FILE *stream);

size_t __wrap_fwrite(const void *pointer, size_t size, size_t count,
	FILE *stream)
{
	if (inject_write_failure && size && count)
	{
		if (inject_write_failure_after != 0U)
		{
			inject_write_failure_after--;
			return __real_fwrite(pointer, size, count, stream);
		}
		size_t partial = count / 2U;

		inject_write_failure = 0;
		(void)__real_fwrite(pointer, size, partial, stream);
		return partial;
	}
	return __real_fwrite(pointer, size, count, stream);
}
#endif

edict_t *g_edicts = entities;

static void SetupPlayer(edict_t *player, gclient_t *client,
	unsigned long generation);
static void SetupPmove(pmove_state_t *before, pmove_t *after);
static void CaptureCompleteTraversalFrames(edict_t *player, edict_t *hook,
	pmove_state_t *before, pmove_t *after, int first_frame,
	int landing_frame);
static void CaptureCompleteTraversal(edict_t *player, edict_t *hook,
	pmove_state_t *before, pmove_t *after, int first_frame);

static cvar_t *TestCvar(char *name, char *value, int flags)
{
	(void)value;
	(void)flags;
	if (strcmp(name, "gamedir") == 0)
		return &game_directory;
	if (strcmp(name, "sv_airaccelerate") == 0)
		return &airaccelerate;
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

static int TracePath(char *path, size_t size, const char *directory,
	unsigned segment)
{
	return snprintf(path, size,
		"%s/humantrace-tracehook-00000065-000000ca-%06u.jsonl",
		directory, segment) < (int)size;
}

static int SpoolPath(char *path, size_t size, const char *directory,
	unsigned segment)
{
	return snprintf(path, size,
		"%s/humantrace-tracehook-00000065-000000ca-%06u.spool",
		directory, segment) < (int)size;
}

static int CountRecords(const char *path, const char *kind)
{
	FILE *file = fopen(path, "rb");
	char line[16384];
	int count = 0;

	if (!file)
		return -1;
	while (fgets(line, sizeof(line), file))
		if (!kind || strstr(line, kind))
			count++;
	fclose(file);
	return count;
}

static int FileContains(const char *path, const char *text)
{
	FILE *file = fopen(path, "rb");
	char line[16384];
	int found = 0;

	if (!file)
		return 0;
	while (!found && fgets(line, sizeof(line), file))
		found = strstr(line, text) != NULL;
	fclose(file);
	return found;
}

static int AppendPartial(const char *path)
{
	FILE *file = fopen(path, "ab");
	static const char partial[] = "{\"partial\"";
	int ok;

	if (!file)
		return 0;
	ok = fwrite(partial, 1U, sizeof(partial) - 1U, file) ==
		sizeof(partial) - 1U && fclose(file) == 0;
	return ok;
}

static int AppendSpoolPartial(const char *path)
{
	FILE *file = fopen(path, "ab");
	const unsigned char marker = 0xa5U;
	int ok;

	if (!file)
		return 0;
	ok = fwrite(&marker, 1U, 1U, file) == 1U && fclose(file) == 0;
	return ok;
}

static int FlipSpoolByte(const char *path)
{
	FILE *file = fopen(path, "r+b");
	int value;
	int ok;

	if (!file || fseek(file, 80L, SEEK_SET) != 0)
	{
		if (file)
			fclose(file);
		return 0;
	}
	value = fgetc(file);
	if (value == EOF || fseek(file, 80L, SEEK_SET) != 0)
	{
		fclose(file);
		return 0;
	}
	if (fputc(value ^ 1, file) == EOF || fflush(file) != 0)
	{
		fclose(file);
		return 0;
	}
	ok = fclose(file) == 0;
	return ok;
}

static int TraceIdentity(sg_level_identity_t *identity)
{
	if (!identity)
		return 0;
	memset(identity, 0, sizeof(*identity));
	identity->bsp_checksum = 101U;
	identity->entity_crc32 = 202U;
	identity->host_physics_id = 1U;
	strcpy(identity->mapname, "tracehook");
	return 1;
}

typedef struct spool_scan_s
{
	sg_human_trace_v3_spool_ref_t first;
	uint32_t scope_client[64];
	uint64_t scope_generation[64];
	uint32_t segment_number[64];
	uint32_t segment_gravity_bits[64];
	const sg_human_trace_v3_scope_acceptance_t *saved_scope;
	uint32_t previous_root;
	uint32_t previous_segment;
	uint64_t count;
	uint64_t event_count;
	uint64_t last_event_order;
	size_t root_scope_start;
	size_t scope_count;
	size_t segment_count;
	uint8_t have_previous;
	uint8_t have_previous_segment;
	uint8_t valid_order;
	uint8_t forged_scope_accepted;
} spool_scan_t;

typedef struct scope_search_s
{
	const sg_human_trace_v3_spool_ref_t *target;
	uint32_t client_id;
	uint64_t spawn_generation;
	uint8_t target_root;
	uint8_t found;
} scope_search_t;

static int ScanSpool(void *opaque, const sg_human_trace_v3_spool_ref_t *spool)
{
	spool_scan_t *scan = opaque;

	if (!scan || !spool)
		return 0;
	if (!scan->have_previous)
		scan->first = *spool;
	else if (spool->root_segment <= scan->previous_root)
		scan->valid_order = 0U;
	if (spool->completion.session != spool->root_segment)
		scan->valid_order = 0U;
	scan->previous_root = spool->root_segment;
	scan->last_event_order = 0U;
	scan->root_scope_start = scan->scope_count;
	scan->have_previous_segment = 0U;
	scan->have_previous = 1U;
	scan->count++;
	return 1;
}

static int ScanCollectionSegment(void *opaque,
	const sg_human_trace_v3_segment_ref_t *segment)
{
	spool_scan_t *scan = opaque;

	if (!scan || !segment || segment->session != scan->previous_root ||
		segment->segment < segment->session ||
		segment->identity.host_physics_id != 1U)
		return 0;
	if (scan->have_previous_segment &&
		segment->segment <= scan->previous_segment)
		scan->valid_order = 0U;
	scan->previous_segment = segment->segment;
	scan->have_previous_segment = 1U;
	if (scan->segment_count < sizeof(scan->segment_number) /
		sizeof(scan->segment_number[0]))
	{
		scan->segment_number[scan->segment_count] = segment->segment;
		scan->segment_gravity_bits[scan->segment_count] = segment->gravity_bits;
	}
	scan->segment_count++;
	return 1;
}

static int ScanCollectionEvent(void *opaque,
	const sg_human_trace_v3_scope_acceptance_t *scope,
	const sg_human_trace_v3_segment_ref_t *segment,
	const sg_human_trace_v3_event_t *event)
{
	spool_scan_t *scan = opaque;
	const sg_human_trace_v3_spool_ref_t *root;
	uint64_t spawn_generation;
	uint32_t client_id;

	if (!scan || !scope || !segment || !event ||
		!SG_HumanTraceAcceptedV3ScopeView(scope, &root, &client_id,
			&spawn_generation) || !root || root->root_segment != scan->previous_root ||
		event->client_id != client_id ||
		event->spawn_generation != spawn_generation ||
		segment->segment != scan->previous_segment ||
		event->order <= scan->last_event_order)
		return 0;
	scan->last_event_order = event->order;
	scan->event_count++;
	return 1;
}

static int ScanCollectionScope(void *opaque,
	const sg_human_trace_v3_scope_acceptance_t *scope)
{
	spool_scan_t *scan = opaque;
	const sg_human_trace_v3_spool_ref_t *root;
	uint64_t spawn_generation;
	uint32_t client_id;
	uint64_t forged[16];
	size_t index;

	if (!scan || !scope)
		return 0;
	memset(forged, 0, sizeof(forged));
	if (SG_HumanTraceAcceptedV3ScopeView(
		(const sg_human_trace_v3_scope_acceptance_t *)forged,
		NULL, NULL, NULL))
		scan->forged_scope_accepted = 1U;
	if (!SG_HumanTraceAcceptedV3ScopeView(scope, &root,
		&client_id, &spawn_generation) || !root ||
		root->root_segment != scan->previous_root || client_id == 0U ||
		spawn_generation == 0U ||
		scan->scope_count >= sizeof(scan->scope_client) /
			sizeof(scan->scope_client[0]))
		return 0;
	for (index = scan->root_scope_start; index < scan->scope_count; index++)
		if (scan->scope_client[index] == client_id &&
			scan->scope_generation[index] == spawn_generation)
			return 0;
	scan->scope_client[scan->scope_count] = client_id;
	scan->scope_generation[scan->scope_count] = spawn_generation;
	if (!scan->saved_scope)
		scan->saved_scope = scope;
	scan->scope_count++;
	return 1;
}

static int ScanCollectionFinish(void *opaque)
{
	return opaque != NULL;
}

static int CollectionSegmentPass(void *opaque,
	const sg_human_trace_v3_segment_ref_t *segment)
{
	return opaque && segment;
}

static int ScopeSearchBegin(void *opaque,
	const sg_human_trace_v3_spool_ref_t *spool)
{
	scope_search_t *test = opaque;

	if (!test || !test->target || !spool)
		return 0;
	test->target_root = spool->root_segment == test->target->root_segment &&
		strcmp(spool->path, test->target->path) == 0;
	return 1;
}

static int ScopeSearchScope(void *opaque,
	const sg_human_trace_v3_scope_acceptance_t *scope)
{
	scope_search_t *test = opaque;
	const sg_human_trace_v3_spool_ref_t *root;
	uint64_t spawn_generation;
	uint32_t client_id;

	if (!test || !scope || !SG_HumanTraceAcceptedV3ScopeView(scope, &root,
		&client_id, &spawn_generation) || !root)
		return 0;
	if (!test->target_root || test->found || client_id != test->client_id ||
		spawn_generation != test->spawn_generation)
		return 1;
	test->found = 1U;
	return 1;
}

static int ScopeSearchEvent(void *opaque,
	const sg_human_trace_v3_scope_acceptance_t *scope,
	const sg_human_trace_v3_segment_ref_t *segment,
	const sg_human_trace_v3_event_t *event)
{
	return opaque && scope && segment && event;
}

static int ScopeSearchFinish(void *opaque)
{
	scope_search_t *test = opaque;

	return test != NULL;
}

static int ScopeExists(const sg_human_trace_v3_spool_ref_t *spool,
	uint32_t client_id, uint64_t spawn_generation)
{
	scope_search_t test;
	sg_human_trace_v3_collection_visitor_t visitor;
	sg_level_identity_t identity;

	if (!spool || !TraceIdentity(&identity))
		return 0;
	memset(&test, 0, sizeof(test));
	memset(&visitor, 0, sizeof(visitor));
	test.target = spool;
	test.client_id = client_id;
	test.spawn_generation = spawn_generation;
	visitor.begin_root = ScopeSearchBegin;
	visitor.segment = CollectionSegmentPass;
	visitor.scope = ScopeSearchScope;
	visitor.event = ScopeSearchEvent;
	visitor.finish_root = ScopeSearchFinish;
	if (!SG_HumanTraceVisitAcceptedV3Collection(&identity, &visitor, &test) ||
		!test.found)
		return 0;
	return 1;
}

static int ScanCompletedSpools(spool_scan_t *scan)
{
	sg_level_identity_t identity;
	sg_human_trace_v3_collection_visitor_t visitor;

	if (!scan || !TraceIdentity(&identity))
		return 0;
	memset(scan, 0, sizeof(*scan));
	memset(&visitor, 0, sizeof(visitor));
	scan->valid_order = 1U;
	visitor.begin_root = ScanSpool;
	visitor.segment = ScanCollectionSegment;
	visitor.scope = ScanCollectionScope;
	visitor.event = ScanCollectionEvent;
	visitor.finish_root = ScanCollectionFinish;
	if (!SG_HumanTraceVisitAcceptedV3Collection(&identity, &visitor, scan) ||
		scan->forged_scope_accepted ||
		SG_HumanTraceAcceptedV3ScopeView(scan->saved_scope, NULL, NULL, NULL))
		return 0;
	return 1;
}

static void RemoveTraceArtifact(const char *directory, unsigned segment)
{
	char path[1024];

	if (TracePath(path, sizeof(path), directory, segment))
		remove(path);
	if (SpoolPath(path, sizeof(path), directory, segment))
		remove(path);
}


static void SetupPlayer(edict_t *player, gclient_t *client,
	unsigned long generation)
{
	memset(player, 0, sizeof(*player));
	memset(client, 0, sizeof(*client));
	player->inuse = true;
	player->client = client;
	player->viewheight = 22;
	player->s.origin[0] = 1.1f;
	player->velocity[0] = 2.2f;
	player->mins[0] = -16.0f;
	player->mins[1] = -16.0f;
	player->mins[2] = -24.0f;
	player->maxs[0] = 16.0f;
	player->maxs[1] = 16.0f;
	player->maxs[2] = 32.0f;
	client->ctf.ctfid = generation;
	client->pers.hand = RIGHT_HANDED;
	client->v_angle[YAW] = 90.0f;
}

static void SetupPmove(pmove_state_t *before, pmove_t *after)
{
	memset(before, 0, sizeof(*before));
	memset(after, 0, sizeof(*after));
	before->pm_type = PM_NORMAL;
	before->origin[0] = 8;
	before->origin[1] = -16;
	before->origin[2] = 24;
	before->velocity[0] = 80;
	before->velocity[1] = -96;
	before->velocity[2] = 112;
	before->pm_flags = 5;
	before->pm_time = 7;
	before->gravity = 777;
	before->delta_angles[0] = 101;
	before->delta_angles[1] = -202;
	before->delta_angles[2] = 303;
	after->s = *before;
	after->s.origin[0] = 16;
	after->s.origin[1] = -24;
	after->s.origin[2] = 32;
	after->s.velocity[0] = 120;
	after->s.velocity[1] = -136;
	after->s.velocity[2] = 152;
	after->s.pm_flags = 9;
	after->s.pm_time = 11;
	after->s.gravity = 333;
	after->s.delta_angles[0] = 404;
	after->s.delta_angles[1] = -505;
	after->s.delta_angles[2] = 606;
	after->snapinitial = true;
	after->cmd.msec = 25;
	after->cmd.buttons = BUTTON_ATTACK | BUTTON_USE;
	after->cmd.angles[0] = 1234;
	after->cmd.angles[1] = -2345;
	after->cmd.angles[2] = 3456;
	after->cmd.forwardmove = 400;
	after->cmd.sidemove = -300;
	after->cmd.upmove = 200;
	after->cmd.impulse = 17;
	after->cmd.lightlevel = 91;
	after->viewangles[0] = 1.1f;
	after->viewangles[1] = 2.2f;
	after->viewangles[2] = 3.3f;
	after->viewheight = 22.0f;
	after->mins[0] = -16.0f;
	after->mins[1] = -16.0f;
	after->mins[2] = -24.0f;
	after->maxs[0] = 16.0f;
	after->maxs[1] = 16.0f;
	after->maxs[2] = 32.0f;
	after->waterlevel = 2;
	after->watertype = CONTENTS_WATER;
	after->numtouch = 2;
	after->touchents[0] = &entities[0];
	after->touchents[1] = &entities[2];
}

static int ObserveLifecycle(edict_t *player, edict_t *hook,
	pmove_state_t *before, pmove_t *after)
{
	edict_t player_copy = *player;
	edict_t hook_copy = *hook;
	gclient_t client_copy = *player->client;
	pmove_state_t before_copy = *before;
	pmove_t after_copy = *after;

	SG_HumanTracePmove(player, before, after);
	SG_HumanTraceHookFire(player, hook);
	SG_HumanTraceHookAttach(player, hook, &entities[0]);
	SG_HumanTraceHookRelease(player);
	SG_HumanTraceHookReset(player, hook);
	return memcmp(player, &player_copy, sizeof(*player)) == 0 &&
		memcmp(hook, &hook_copy, sizeof(*hook)) == 0 &&
		memcmp(player->client, &client_copy, sizeof(*player->client)) == 0 &&
		memcmp(before, &before_copy, sizeof(*before)) == 0 &&
		memcmp(after, &after_copy, sizeof(*after)) == 0;
}

static void CaptureCompleteTraversalFrames(edict_t *player, edict_t *hook,
	pmove_state_t *before, pmove_t *after, int first_frame,
	int landing_frame)
{
	memset(hook, 0, sizeof(*hook));
	hook->inuse = true;
	hook->owner = player;
	hook->hook_target = &entities[0];
	player->client->hook = hook;
	after->groundentity = NULL;
	level.framenum = first_frame;
	SG_HumanTracePmove(player, before, after);
	SG_HumanTraceHookFire(player, hook);
	SG_HumanTraceHookAttach(player, hook, &entities[0]);
	level.framenum = first_frame + 1;
	SG_HumanTracePmove(player, before, after);
	level.framenum = first_frame + 2;
	SG_HumanTraceHookRelease(player);
	SG_HumanTraceHookReset(player, hook);
	after->groundentity = &entities[0];
	level.framenum = landing_frame;
	SG_HumanTracePmove(player, before, after);
	after->groundentity = NULL;
}

static void CaptureCompleteTraversal(edict_t *player, edict_t *hook,
	pmove_state_t *before, pmove_t *after, int first_frame)
{
	CaptureCompleteTraversalFrames(player, hook, before, after, first_frame,
		first_frame + 3);
}

static int RunCapacityFailure(const char *directory)
{
	char path[1024];
	FILE *occupied;
	edict_t *player = &entities[1];
	edict_t *hook = &entities[3];
	pmove_state_t before;
	pmove_t after;
	int unchanged;

	if (!TracePath(path, sizeof(path), directory, 0U))
		return 20;
	occupied = fopen(path, "wb");
	if (!occupied || fputs("occupied\n", occupied) < 0 ||
	    fclose(occupied) != 0)
		return 21;
	SetupPlayer(player, &clients[0], 11UL);
	SetupPmove(&before, &after);
	hook->inuse = true;
	hook->owner = player;
	hook->hook_target = &entities[0];
	player->client->hook = hook;
	SG_HumanTraceNewLevel();
	unchanged = ObserveLifecycle(player, hook, &before, &after);
	SG_HumanTraceMatchEnd();
	remove(path);
	return unchanged ? 0 : 22;
}

static int RunRotation(const char *directory)
{
	char path[1024];
	edict_t *player = &entities[1];
	pmove_state_t before;
	pmove_t after;
	unsigned segment;
	int files = 0;

	SetupPlayer(player, &clients[0], 11UL);
	SetupPmove(&before, &after);
	SG_HumanTraceNewLevel();
	SG_HumanTracePmove(player, &before, &after);
	SG_HumanTraceMatchEnd();
	for (segment = 0U; segment < 16U; segment++)
	{
		if (!TracePath(path, sizeof(path), directory, segment) ||
		    CountRecords(path, NULL) < 0)
			break;
		if (segment > 0U &&
		    !FileContains(path, "\"continuation\":1"))
			return 30;
		files++;
		remove(path);
	}
	if (SpoolPath(path, sizeof(path), directory, 0U))
		remove(path);
	return files >= 2 ? 0 : 31;
}

static int RunSpoolRejection(const char *directory, int tamper)
{
	char spool_path[1024];
	edict_t *player = &entities[1];
	pmove_state_t before;
	pmove_t after;
	spool_scan_t scan;
	int result = 0;

	if (!SpoolPath(spool_path, sizeof(spool_path), directory, 0U))
		return 32;
	SetupPlayer(player, &clients[0], 11UL);
	SetupPmove(&before, &after);
	level.framenum = 1;
	SG_HumanTraceNewLevel();
	SG_HumanTracePmove(player, &before, &after);
	SG_HumanTraceMatchEnd();
	if (!ScanCompletedSpools(&scan) || scan.count != 1U ||
		!scan.valid_order || strcmp(scan.first.path, spool_path) != 0)
		result = 33;
	if (!result && !(tamper ? FlipSpoolByte(spool_path) :
		AppendSpoolPartial(spool_path)))
		result = 34;
	if (!result && (!ScanCompletedSpools(&scan) || scan.count != 0U))
		result = 35;
	RemoveTraceArtifact(directory, 0U);
	return result;
}

static int RunLongStream(const char *directory, int finish)
{
	/* This deliberately crosses the historical 16,384-event refusal point.
	 * It is a regression witness, not a recorder work limit. */
	const unsigned long event_count = 16385UL;
	edict_t *player = &entities[1];
	pmove_state_t before;
	pmove_t after;
	spool_scan_t scan;
	unsigned long index;
	int result = 0;

	SetupPlayer(player, &clients[0], 11UL);
	SetupPmove(&before, &after);
	after.groundentity = &entities[0];
	SG_HumanTraceNewLevel();
	for (index = 0UL; index < event_count; index++)
	{
		level.framenum = (int)(index + 1UL);
		SG_HumanTracePmove(player, &before, &after);
	}
	if (!finish)
		return 0;
	SG_HumanTraceMatchEnd();
	if (!ScanCompletedSpools(&scan) || scan.count != 1U ||
		!scan.valid_order || scan.event_count != event_count)
		result = 36;
	RemoveTraceArtifact(directory, 0U);
	return result;
}

static int RunConsumerVisitCount(const char *directory, unsigned count,
	int visit, int clean)
{
	edict_t *player = &entities[1];
	edict_t *hook = &entities[3];
	pmove_state_t before;
	pmove_t after;
	spool_scan_t scan;
	unsigned index;
	int result = 0;

	SetupPlayer(player, &clients[0], 11UL);
	SetupPmove(&before, &after);
	SG_HumanTraceNewLevel();
	for (index = 0U; index < count; index++)
		CaptureCompleteTraversal(player, hook, &before, &after,
			(int)(index * 4U + 1U));
	SG_HumanTraceMatchEnd();
	if (visit && (!ScanCompletedSpools(&scan) || scan.count != 1U ||
		!scan.valid_order || scan.scope_count != 1U ||
		scan.event_count != (uint64_t)count * UINT64_C(7)))
		result = 122;
	if (clean)
		RemoveTraceArtifact(directory, 0U);
	return result;
}

static int RunConsumerRoots(const char *directory, unsigned root_count,
	int visit, int clean)
{
	edict_t *player = &entities[1];
	edict_t *hook = &entities[3];
	pmove_state_t before;
	pmove_t after;
	spool_scan_t scan;
	unsigned root;
	int result = 0;

	SetupPlayer(player, &clients[0], 11UL);
	SetupPmove(&before, &after);
	for (root = 0U; root < root_count; root++)
	{
		SG_HumanTraceNewLevel();
		CaptureCompleteTraversal(player, hook, &before, &after,
			(int)(root * 4U + 1U));
		SG_HumanTraceMatchEnd();
	}
	if (visit && (!ScanCompletedSpools(&scan) || scan.count != root_count ||
		!scan.valid_order || scan.event_count !=
			(uint64_t)root_count * UINT64_C(7) ||
		scan.scope_count != root_count))
		result = 123;
	if (clean)
		for (root = 0U; root < root_count; root++)
			RemoveTraceArtifact(directory, root);
	return result;
}

static int RunConsumerRestartRead(const char *directory)
{
	spool_scan_t scan;
	unsigned root;

	if (!ScanCompletedSpools(&scan) || scan.count != 3U ||
		!scan.valid_order || scan.first.root_segment != 0U ||
		scan.previous_root != 2U || scan.event_count != UINT64_C(21) ||
		scan.scope_count != 3U)
		return 124;
	for (root = 0U; root < 3U; root++)
		RemoveTraceArtifact(directory, root);
	return 0;
}

static int RunConsumerFirstOccurrence(const char *directory)
{
	edict_t *player1 = &entities[1];
	edict_t *player2 = &entities[2];
	pmove_state_t before1, before2;
	pmove_t after1, after2;
	spool_scan_t scan;
	int result = 0;

	SetupPlayer(player1, &clients[0], 11UL);
	SetupPlayer(player2, &clients[1], 22UL);
	SetupPmove(&before1, &after1);
	SetupPmove(&before2, &after2);
	level.framenum = 1;
	SG_HumanTraceNewLevel();
	SG_HumanTracePmove(player1, &before1, &after1);
	level.framenum = 2;
	SG_HumanTracePmove(player2, &before2, &after2);
	player2->client->ctf.ctfid = 23UL;
	level.framenum = 3;
	SG_HumanTracePmove(player2, &before2, &after2);
	SG_HumanTraceMatchEnd();
	if (!ScanCompletedSpools(&scan) || scan.count != 1U ||
		scan.event_count != 3U || scan.scope_count != 3U ||
		scan.scope_client[0] != 1U || scan.scope_generation[0] != UINT64_C(11) ||
		scan.scope_client[1] != 2U || scan.scope_generation[1] != UINT64_C(22) ||
		scan.scope_client[2] != 2U || scan.scope_generation[2] != UINT64_C(23))
		result = 125;
	RemoveTraceArtifact(directory, 0U);
	return result;
}

static int RunSpoolOrder(const char *directory)
{
	edict_t *player = &entities[1];
	pmove_state_t before;
	pmove_t after;
	spool_scan_t scan;
	unsigned segment;
	int result = 0;

	SetupPlayer(player, &clients[0], 11UL);
	SetupPmove(&before, &after);
	for (segment = 0U; segment < 3U; segment++)
	{
		level.framenum = (int)(segment + 1U);
		SG_HumanTraceNewLevel();
		SG_HumanTracePmove(player, &before, &after);
		SG_HumanTraceMatchEnd();
	}
	if (!ScanCompletedSpools(&scan) || scan.count != 3U ||
		!scan.valid_order || scan.first.root_segment != 0U ||
		scan.previous_root != 2U)
		result = 38;
	for (segment = 0U; segment < 3U; segment++)
		RemoveTraceArtifact(directory, segment);
	return result;
}

static int RunSpoolQuarantine(const char *directory)
{
	char spool_path[1024];
	char json_path[1024];
	FILE *malformed;
	edict_t *player = &entities[1];
	pmove_state_t before;
	pmove_t after;
	spool_scan_t scan;
	unsigned segment;
	int result = 0;

	SetupPlayer(player, &clients[0], 11UL);
	SetupPmove(&before, &after);
	for (segment = 0U; segment < 3U; segment++)
	{
		level.framenum = (int)(segment + 1U);
		SG_HumanTraceNewLevel();
		SG_HumanTracePmove(player, &before, &after);
		SG_HumanTraceMatchEnd();
	}
	/* Root one is a torn completed spool. Roots three and four are unrelated
	 * collision garbage. A canonical-name JSON sibling whose header cannot be
	 * assigned to any session is quarantined before grouping. Valid roots zero
	 * and two must still be visited FIFO. */
	if (!SpoolPath(spool_path, sizeof(spool_path), directory, 1U) ||
		!AppendSpoolPartial(spool_path))
		result = 91;
	if (!result && (!SpoolPath(spool_path, sizeof(spool_path),
		directory, 3U) || !AppendSpoolPartial(spool_path)))
		result = 92;
	if (!result && (!SpoolPath(spool_path, sizeof(spool_path),
		directory, 4U) || !AppendSpoolPartial(spool_path)))
		result = 93;
	if (!result && (!TracePath(json_path, sizeof(json_path), directory, 3U) ||
		!(malformed = fopen(json_path, "wb"))))
		result = 93;
	if (!result && (fputs("{\n", malformed) < 0 || fclose(malformed) != 0))
		result = 93;
	if (!result && (!ScanCompletedSpools(&scan) || scan.count != 2U ||
		!scan.valid_order || scan.first.root_segment != 0U ||
		scan.previous_root != 2U))
		result = 94;
	for (segment = 0U; segment < 5U; segment++)
		RemoveTraceArtifact(directory, segment);
	return result;
}

static int RunSegmentNames(const char *directory)
{
	static const uint32_t segments[] = { 999999U, 1000000U };
	static const char *const suffixes[] = { "999999.jsonl", "1000000.jsonl" };
	sg_level_identity_t identity;
	char path[SG_HUMAN_TRACE_SPOOL_PATH_BYTES];
	const char *name;
	uint32_t parsed;
	size_t index;

	if (!TraceIdentity(&identity))
		return 112;
	for (index = 0U; index < sizeof(segments) / sizeof(segments[0]); index++)
	{
		if (!SG_HumanTraceTestFormatJsonPath(directory, &identity,
			segments[index], path))
			return 113;
		name = strrchr(path, '/');
		name = name ? name + 1 : path;
		if (strlen(name) < strlen(suffixes[index]) || strcmp(name + strlen(name) -
			strlen(suffixes[index]), suffixes[index]) != 0 ||
			!SG_HumanTraceTestJsonNameSegment(name, &identity,
				&parsed) || parsed != segments[index])
			return 114;
	}
	if (SG_HumanTraceTestJsonNameSegment(
		"humantrace-tracehook-00000065-000000ca-0000000.jsonl", &identity,
		&parsed))
		return 115;
	return 0;
}

static int RunStoredSpoolCoverage(void)
{
	spool_scan_t scan;

	if (!ScanCompletedSpools(&scan) || scan.count != 1U ||
		!scan.valid_order || scan.first.root_segment != 0U ||
		scan.event_count != 12U || scan.scope_count != 2U ||
		scan.scope_client[0] != 1U || scan.scope_generation[0] != UINT64_C(11) ||
		scan.scope_client[1] != 2U || scan.scope_generation[1] != UINT64_C(22))
		return 39;
	return 0;
}

static int RunScopeIsolation(const char *directory)
{
	edict_t *player = &entities[1];
	pmove_state_t before;
	pmove_t after;
	spool_scan_t scan;
	int absent_accepted;
	int typo_accepted;
	int valid_accepted;

	(void)directory;
	SetupPlayer(player, &clients[0], 11UL);
	SetupPmove(&before, &after);
	level.framenum = 1;
	SG_HumanTraceNewLevel();
	SG_HumanTracePmove(player, &before, &after);
	SG_HumanTraceMatchEnd();
	if (!ScanCompletedSpools(&scan) || scan.count != 1U)
		return 120;
	absent_accepted = ScopeExists(&scan.first, 2U, UINT64_C(11));
	typo_accepted = ScopeExists(&scan.first, 1U, UINT64_C(12));
	valid_accepted = ScopeExists(&scan.first, 1U, UINT64_C(11));
	if (absent_accepted || typo_accepted || !valid_accepted)
	{
		fprintf(stderr,
			"absent_scope_accepted=%d typo_scope_accepted=%d valid_scope_accepted=%d\n",
			absent_accepted, typo_accepted, valid_accepted);
		return 121;
	}
	return 0;
}

#ifdef SG_HUMAN_TRACE_WRAP_FWRITE
static int RunWriteFailure(const char *directory)
{
	char path0[1024], path1[1024];
	edict_t *player = &entities[1];
	pmove_state_t before;
	pmove_t after;
	int unchanged;

	if (!TracePath(path0, sizeof(path0), directory, 0U) ||
	    !TracePath(path1, sizeof(path1), directory, 1U))
		return 40;
	SetupPlayer(player, &clients[0], 11UL);
	SetupPmove(&before, &after);
	SG_HumanTraceNewLevel();
	inject_write_failure = 1;
	unchanged = ObserveLifecycle(player, &entities[3], &before, &after);
	SG_HumanTraceMatchEnd();
	if (!unchanged || CountRecords(path0, NULL) < 1)
		return 41;
	SG_HumanTraceNewLevel();
	SG_HumanTracePmove(player, &before, &after);
	SG_HumanTraceMatchEnd();
	if (CountRecords(path1, NULL) != 3)
		return 42;
	remove(path0);
	remove(path1);
	if (SpoolPath(path0, sizeof(path0), directory, 0U))
		remove(path0);
	if (SpoolPath(path1, sizeof(path1), directory, 1U))
		remove(path1);
	return 0;
}
#endif

#ifndef _WIN32
static int RunFileSizeFailure(const char *directory)
{
	struct rlimit original, limited;
	edict_t *player = &entities[1];
	edict_t *hook = &entities[3];
	pmove_state_t before;
	pmove_t after;
	int unchanged;
	(void)directory;

	if (getrlimit(RLIMIT_FSIZE, &original) != 0)
		return 50;
	limited = original;
	limited.rlim_cur = 1;
	if (setrlimit(RLIMIT_FSIZE, &limited) != 0)
		return 51;
	SetupPlayer(player, &clients[0], 11UL);
	SetupPmove(&before, &after);
	memset(hook, 0, sizeof(*hook));
	hook->inuse = true;
	hook->owner = player;
	hook->hook_target = &entities[0];
	player->client->hook = hook;
	SG_HumanTraceNewLevel();
	unchanged = ObserveLifecycle(player, hook, &before, &after);
	SG_HumanTraceMatchEnd();
	if (setrlimit(RLIMIT_FSIZE, &original) != 0)
		return 52;
	return unchanged ? 0 : 53;
}
#endif

static int RunCollision(const char *directory)
{
	char path[1024];
	FILE *occupied;
	edict_t *player = &entities[1];
	pmove_state_t before;
	pmove_t after;

	if (!TracePath(path, sizeof(path), directory, 0U))
		return 60;
	occupied = fopen(path, "wb");
	if (!occupied || fputs("occupied\n", occupied) < 0 ||
	    fclose(occupied) != 0)
		return 61;
	SetupPlayer(player, &clients[0], 11UL);
	SetupPmove(&before, &after);
	SG_HumanTraceNewLevel();
	SG_HumanTracePmove(player, &before, &after);
	SG_HumanTraceMatchEnd();
	return CountRecords(path, NULL) == 1 ? 0 : 62;
}

static int RunPhysicsDrift(const char *directory, int occupied_gap)
{
	char path0[1024], path1[1024], path2[1024];
	FILE *occupied = NULL;
	edict_t *player = &entities[1];
	pmove_state_t before;
	pmove_t after;
	spool_scan_t scan;
	unsigned terminal_segment = occupied_gap ? 2U : 1U;

	if (!TracePath(path0, sizeof(path0), directory, 0U) ||
	    !TracePath(path1, sizeof(path1), directory, 1U) ||
	    !TracePath(path2, sizeof(path2), directory, terminal_segment))
		return 65;
	if (occupied_gap)
	{
		occupied = fopen(path1, "wb");
		if (!occupied || fputs("occupied\n", occupied) < 0 ||
			fclose(occupied) != 0)
			return 65;
	}
	SetupPlayer(player, &clients[0], 11UL);
	SetupPmove(&before, &after);
	SG_HumanTraceNewLevel();
	SG_HumanTracePmove(player, &before, &after);
	gravity.value = 100.0f;
	SG_HumanTracePmove(player, &before, &after);
	SG_HumanTraceMatchEnd();
	if (CountRecords(path0, NULL) != 2 ||
		CountRecords(path2, NULL) != 3 || !ScanCompletedSpools(&scan) ||
		scan.count != 1U || scan.event_count != 2U ||
		scan.segment_count != 2U || scan.segment_number[0] != 0U ||
		scan.segment_number[1] != terminal_segment ||
		scan.segment_gravity_bits[0] != UINT32_C(0x44480000) ||
		scan.segment_gravity_bits[1] != UINT32_C(0x42c80000) ||
		scan.first.completion.segment != terminal_segment ||
		scan.first.completion.gravity_bits != UINT32_C(0x42c80000))
		return 66;
	return 0;
}

#ifndef _WIN32
static int RunConcurrentCollision(const char *directory)
{
	pid_t children[8];
	int gate[2];
	int child;
	int status;
	edict_t *player = &entities[1];
	pmove_state_t before;
	pmove_t after;

	if (pipe(gate) != 0)
		return 70;
	for (child = 0; child < 8; child++)
	{
		children[child] = fork();
		if (children[child] < 0)
		{
			int started = child;

			close(gate[0]);
			close(gate[1]);
			while (started-- > 0)
				(void)waitpid(children[started], NULL, 0);
			return 71;
		}
		if (children[child] == 0)
		{
			char released;

			close(gate[1]);
			if (read(gate[0], &released, 1U) != 0)
				_exit(72);
			close(gate[0]);
			SetupPlayer(player, &clients[0], (unsigned long)(11 + child));
			SetupPmove(&before, &after);
			SG_HumanTraceNewLevel();
			SG_HumanTracePmove(player, &before, &after);
			SG_HumanTraceMatchEnd();
			_exit(0);
		}
	}
	close(gate[0]);
	close(gate[1]);
	for (child = 0; child < 8; child++)
	{
		if (waitpid(children[child], &status, 0) != children[child] ||
		    !WIFEXITED(status) || WEXITSTATUS(status) != 0)
			return 73;
	}
	for (child = 0; child < 8; child++)
	{
		char path[1024];

		if (!TracePath(path, sizeof(path), directory, (unsigned)child) ||
		    CountRecords(path, NULL) != 3)
			return 74;
	}
	return 0;
}
#endif

int main(int argc, char **argv)
{
	char path0[1024], path1[1024], path2[1024];
	edict_t *player1 = &entities[1];
	edict_t *hook1 = &entities[3];
	edict_t *player2 = &entities[2];
	edict_t *hook2 = &entities[4];
	edict_t player1_copy;
	edict_t hook1_copy;
	gclient_t client1_copy;
	pmove_state_t before1, before2;
	pmove_t after1, after2;
	pmove_state_t before1_copy;
	pmove_t after1_copy;
	int result = 0;

	if ((argc != 2 && argc != 3) ||
	    !TracePath(path0, sizeof(path0), argv[1], 0U) ||
	    !TracePath(path1, sizeof(path1), argv[1], 1U) ||
	    !TracePath(path2, sizeof(path2), argv[1], 2U))
		return 2;
	enabled.value = 1.0f;
	airaccelerate.value = 1.5f;
	gravity.value = 800.0f;
	maxvelocity.value = 2000.0f;
	funky_gravity.value = 0.0f;
	trace_directory.string = argv[1];
	game_directory.string = argv[1];
	gi.cvar = TestCvar;
	gi.dprintf = TestDprintf;
	globals.num_edicts = 5;
	strcpy(level.mapname, "tracehook");
	if (argc == 3 && strcmp(argv[2], "capacity") == 0)
		return RunCapacityFailure(argv[1]);
	if (argc == 3 && strcmp(argv[2], "rotation") == 0)
		return RunRotation(argv[1]);
	if (argc == 3 && strcmp(argv[2], "spool-truncated") == 0)
		return RunSpoolRejection(argv[1], 0);
	if (argc == 3 && strcmp(argv[2], "spool-tampered") == 0)
		return RunSpoolRejection(argv[1], 1);
	if (argc == 3 && strcmp(argv[2], "long-stream") == 0)
		return RunLongStream(argv[1], 1);
	if (argc == 3 && strcmp(argv[2], "long-stream-live") == 0)
		return RunLongStream(argv[1], 0);
	if (argc == 3 && strcmp(argv[2], "consumer-io-64") == 0)
		return RunConsumerVisitCount(argv[1], 64U, 1, 0);
	if (argc == 3 && strcmp(argv[2], "consumer-io-128") == 0)
		return RunConsumerVisitCount(argv[1], 128U, 1, 0);
	if (argc == 3 && strcmp(argv[2], "consumer-io-256") == 0)
		return RunConsumerVisitCount(argv[1], 256U, 1, 0);
	if (argc == 3 && strcmp(argv[2], "consumer-io-512") == 0)
		return RunConsumerVisitCount(argv[1], 512U, 1, 0);
	if (argc == 3 && strcmp(argv[2], "consumer-io-roots") == 0)
		return RunConsumerRoots(argv[1], 8U, 1, 0);
	if (argc == 3 && strcmp(argv[2], "consumer-restart-write") == 0)
		return RunConsumerRoots(argv[1], 3U, 0, 0);
	if (argc == 3 && strcmp(argv[2], "consumer-restart-read") == 0)
		return RunConsumerRestartRead(argv[1]);
	if (argc == 3 && strcmp(argv[2], "consumer-produce-only") == 0)
		return RunConsumerVisitCount(argv[1], 1U, 0, 0);
	if (argc == 3 && strcmp(argv[2], "consumer-boundary-source") == 0)
		return RunConsumerVisitCount(argv[1], 1U, 0, 0);
	if (argc == 3 && strcmp(argv[2], "consumer-first-occurrence") == 0)
		return RunConsumerFirstOccurrence(argv[1]);
	if (argc == 3 && strcmp(argv[2], "spool-order") == 0)
		return RunSpoolOrder(argv[1]);
	if (argc == 3 && strcmp(argv[2], "spool-quarantine") == 0)
		return RunSpoolQuarantine(argv[1]);
	if (argc == 3 && strcmp(argv[2], "segment-names") == 0)
		return RunSegmentNames(argv[1]);
	if (argc == 3 && strcmp(argv[2], "consumer-scope-isolation") == 0)
		return RunScopeIsolation(argv[1]);
#ifndef _WIN32
	if (argc == 3 && strcmp(argv[2], "fsize") == 0)
		return RunFileSizeFailure(argv[1]);
#endif
	if (argc == 3 && strcmp(argv[2], "collision") == 0)
		return RunCollision(argv[1]);
	if (argc == 3 && strcmp(argv[2], "physics") == 0)
		return RunPhysicsDrift(argv[1], 0);
	if (argc == 3 && strcmp(argv[2], "physics-gap") == 0)
		return RunPhysicsDrift(argv[1], 1);
#ifndef _WIN32
	if (argc == 3 && strcmp(argv[2], "concurrent") == 0)
		return RunConcurrentCollision(argv[1]);
#endif
#ifdef SG_HUMAN_TRACE_WRAP_FWRITE
	if (argc == 3 && strcmp(argv[2], "writefail") == 0)
		return RunWriteFailure(argv[1]);
#endif

	SetupPlayer(player1, &clients[0], 11UL);
	SetupPlayer(player2, &clients[1], 22UL);
	SetupPmove(&before1, &after1);
	SetupPmove(&before2, &after2);
	after1.groundentity = &entities[0];
	hook1->inuse = true;
	hook1->owner = player1;
	hook1->hook_target = &entities[0];
	hook1->s.origin[0] = 64.1f;
	hook1->velocity[0] = 5.2f;
	player1->client->hook = hook1;
	hook2->inuse = true;
	hook2->owner = player2;
	hook2->hook_target = &entities[0];
	hook2->s.origin[0] = 96.0f;
	player2->client->hook = hook2;

	player1_copy = *player1;
	hook1_copy = *hook1;
	client1_copy = *player1->client;
	before1_copy = before1;
	after1_copy = after1;
	level.framenum = 17;
	SG_HumanTraceNewLevel();
	SG_HumanTracePmove(player1, &before1, &after1);
	SG_HumanTraceHookFire(player1, hook1);
	SG_HumanTraceHookAttach(player1, hook1, &entities[0]);
	level.framenum = 18;
	SG_HumanTracePmove(player1, &before1, &after1);
	level.framenum = 19;
	SG_HumanTraceHookRelease(player1);
	SG_HumanTraceHookReset(player1, hook1);
	level.framenum = 20;
	SG_HumanTracePmove(player1, &before1, &after1);
	level.framenum = 25;
	SG_HumanTracePmove(player2, &before2, &after2);
	SG_HumanTraceHookFire(player2, hook2);
	SG_HumanTraceHookAttach(player2, hook2, &entities[0]);
	level.framenum = 26;
	SG_HumanTracePmove(player2, &before2, &after2);
	level.framenum = 27;
	SG_HumanTraceHookRelease(player2);
	if (memcmp(player1, &player1_copy, sizeof(*player1)) != 0 ||
	    memcmp(hook1, &hook1_copy, sizeof(*hook1)) != 0 ||
	    memcmp(player1->client, &client1_copy, sizeof(*player1->client)) != 0 ||
	    memcmp(&before1, &before1_copy, sizeof(before1)) != 0 ||
	    memcmp(&after1, &after1_copy, sizeof(after1)) != 0)
		result = 3;
	SG_HumanTraceMatchEnd();
#ifdef SG_HUMAN_TRACE_TEST
	if (!result && RunStoredSpoolCoverage() != 0)
		result = 9;
#endif
	enabled.value = 0.0f;
	if (!ObserveLifecycle(player1, hook1, &before1, &after1))
		result = 4;
	enabled.value = 1.0f;

	level.framenum = 31;
	SG_HumanTraceNewLevel();
	if (!ObserveLifecycle(player2, hook2, &before2, &after2))
		result = 5;
	SG_HumanTraceMatchEnd();

	if (CountRecords(path0, NULL) != 14 ||
	    CountRecords(path1, NULL) != 7 ||
	    CountRecords(path0, "\"kind\":\"step\"") != 5 ||
	    CountRecords(path0, "\"kind\":\"hook-fire\"") != 2 ||
	    CountRecords(path0, "\"kind\":\"hook-attach\"") != 2 ||
	    CountRecords(path0, "\"kind\":\"hook-release\"") != 2 ||
	    CountRecords(path0, "\"kind\":\"hook-reset\"") != 1 ||
	    CountRecords(path0, "\"sha256\":") != 14 ||
	    !FileContains(path0, "\"client\":1,\"spawn_generation\":11") ||
	    !FileContains(path0, "\"frame\":20") ||
	    !FileContains(path0, "\"client\":2,\"spawn_generation\":22") ||
	    !FileContains(path0, "\"frame\":27") ||
	    !FileContains(path0, "\"order\":1,\"command\":1") ||
	    !FileContains(path0, "\"order\":6,\"hook_event\":4") ||
	    !FileContains(path1, "\"client\":2,\"spawn_generation\":22") ||
	    !FileContains(path1, "\"frame\":31"))
		result = 6;

	if (!AppendPartial(path1))
		result = 7;
	level.framenum = 32;
	SG_HumanTraceNewLevel();
	SG_HumanTracePmove(player2, &before2, &after2);
	SG_HumanTraceMatchEnd();
	if (CountRecords(path2, NULL) != 3 ||
	    !FileContains(path2, "\"segment\":2") ||
	    !FileContains(path2, "\"client\":2,\"spawn_generation\":22"))
		result = 8;

	if (!getenv("SG_HUMAN_TRACE_KEEP"))
	{
		RemoveTraceArtifact(argv[1], 0U);
		RemoveTraceArtifact(argv[1], 1U);
		RemoveTraceArtifact(argv[1], 2U);
	}
	if (result)
		return result;
	puts("sg_human_trace_hook_test: ok");
	return 0;
}
