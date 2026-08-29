#include <stdarg.h>
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

size_t __real_fwrite(const void *pointer, size_t size, size_t count,
	FILE *stream);

size_t __wrap_fwrite(const void *pointer, size_t size, size_t count,
	FILE *stream)
{
	if (inject_write_failure && size && count)
	{
		size_t partial = count / 2U;

		inject_write_failure = 0;
		(void)__real_fwrite(pointer, size, partial, stream);
		return partial;
	}
	return __real_fwrite(pointer, size, count, stream);
}
#endif

edict_t *g_edicts = entities;

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
	return files >= 2 ? 0 : 31;
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

static int RunPhysicsDrift(const char *directory)
{
	char path0[1024], path1[1024];
	edict_t *player = &entities[1];
	pmove_state_t before;
	pmove_t after;

	if (!TracePath(path0, sizeof(path0), directory, 0U) ||
	    !TracePath(path1, sizeof(path1), directory, 1U))
		return 65;
	SetupPlayer(player, &clients[0], 11UL);
	SetupPmove(&before, &after);
	SG_HumanTraceNewLevel();
	SG_HumanTracePmove(player, &before, &after);
	gravity.value = 100.0f;
	SG_HumanTracePmove(player, &before, &after);
	SG_HumanTraceMatchEnd();
	if (CountRecords(path0, NULL) != 2 ||
	    CountRecords(path1, NULL) != 3)
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
	pmove_state_t before1, before2;
	pmove_t after1, after2;
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
#ifndef _WIN32
	if (argc == 3 && strcmp(argv[2], "fsize") == 0)
		return RunFileSizeFailure(argv[1]);
#endif
	if (argc == 3 && strcmp(argv[2], "collision") == 0)
		return RunCollision(argv[1]);
	if (argc == 3 && strcmp(argv[2], "physics") == 0)
		return RunPhysicsDrift(argv[1]);
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

	level.framenum = 17;
	SG_HumanTraceNewLevel();
	if (!ObserveLifecycle(player1, hook1, &before1, &after1))
		result = 3;
	SG_HumanTraceMatchEnd();
	enabled.value = 0.0f;
	if (!ObserveLifecycle(player1, hook1, &before1, &after1))
		result = 4;
	enabled.value = 1.0f;

	level.framenum = 31;
	SG_HumanTraceNewLevel();
	if (!ObserveLifecycle(player2, hook2, &before2, &after2))
		result = 5;
	SG_HumanTraceMatchEnd();

	if (CountRecords(path0, NULL) != 7 ||
	    CountRecords(path1, NULL) != 7 ||
	    CountRecords(path0, "\"kind\":\"step\"") != 1 ||
	    CountRecords(path0, "\"kind\":\"hook-fire\"") != 1 ||
	    CountRecords(path0, "\"kind\":\"hook-attach\"") != 1 ||
	    CountRecords(path0, "\"kind\":\"hook-release\"") != 1 ||
	    CountRecords(path0, "\"kind\":\"hook-reset\"") != 1 ||
	    CountRecords(path0, "\"sha256\":") != 7 ||
	    !FileContains(path0, "\"client\":1,\"spawn_generation\":11") ||
	    !FileContains(path0, "\"frame\":17") ||
	    !FileContains(path0, "\"order\":1,\"command\":1") ||
	    !FileContains(path0, "\"order\":5,\"hook_event\":4") ||
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
		remove(path0);
		remove(path1);
		remove(path2);
	}
	if (result)
		return result;
	puts("sg_human_trace_hook_test: ok");
	return 0;
}
