#include "../g_local.h"
#undef world
#include "sg_rune_game.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sg_rune_bsp.h"
#include "sg_rune_law.h"
#include "sg_rune_artifact.h"
#include "sg_rune_generate.h"
#include "sg_rune_hook.h"
#include "sg_rune_level.h"
#include "sg_bites.h"

/* Generation runs on its own thread: a map's first load builds its RUNE
 * while the server keeps its frames and the bots wait.  The worker never
 * touches the game: it gets copies of what it needs, keeps its report in a
 * buffer, and the frame poll prints the buffer and loads the result. */
typedef struct job_s
{
	char mapname[64];
	char bsp_path[MAX_OSPATH];
	char destination[MAX_OSPATH];
	char bites_path[MAX_OSPATH];
	char *entity_text;
	float gravity;
	char log[8192];
	size_t log_used;
	int generated;
	volatile int done;
#ifdef _WIN32
	HANDLE thread;
#else
	pthread_t thread;
#endif
	int started;
} job_t;

static job_t sg_job;

static void Log(job_t *job, const char *fmt, ...)
{
	va_list args;
	int n;

	if (job->log_used >= sizeof(job->log) - 2U)
		return;
	va_start(args, fmt);
	n = vsnprintf(job->log + job->log_used, sizeof(job->log) - job->log_used, fmt, args);
	va_end(args);
	if (n > 0)
		job->log_used += (size_t)n < sizeof(job->log) - job->log_used ? (size_t)n :
			sizeof(job->log) - job->log_used - 1U;
}

static void Progress(void *context, const char *stage, uint32_t done, uint32_t total)
{
	job_t *job = context;

	if (total == 0U)
		Log(job, "rune: generation map=%s stage=%s begin\n", job->mapname, stage);
	(void)done;
}

#ifdef _WIN32
static DWORD WINAPI Worker(LPVOID arg)
#else
static void *Worker(void *arg)
#endif
{
	job_t *job = arg;
	sg_rune_bsp_t bsp;
	sg_rune_bsp_fault_t bsp_fault;
	sg_rune_identity_t identity;
	sg_rune_law_t law;
	sg_rune_generate_report_t report;
	unsigned char *image = NULL;
	size_t image_size = 0U;
	sg_rune_artifact_status_t status;
	int os_error = 0;

	SG_RuneHookSetHumanBites(job->bites_path[0] ? job->bites_path : NULL);
	if (!SG_RuneBspLoadFile(job->bsp_path, &bsp, &bsp_fault))
	{
		Log(job, "rune: generation refused stage=bsp %s lump=%d record=%u\n",
			bsp_fault.what ? bsp_fault.what : "?", bsp_fault.lump,
			(unsigned int)bsp_fault.record);
		job->done = 1;
		return 0;
	}
	if (job->entity_text && !SG_RuneBspReplaceEntities(&bsp, job->entity_text))
	{
		Log(job, "rune: generation refused stage=entities\n");
		SG_RuneBspFree(&bsp);
		job->done = 1;
		return 0;
	}
	SG_RuneLawEngine(&law, job->gravity);
	memset(&identity, 0, sizeof(identity));
	identity.bsp_crc32 = bsp.file_crc32;
	identity.entity_crc32 = bsp.entity_crc32;
	identity.bsp_bytes = bsp.file_bytes;
	identity.law_crc32 = SG_RuneLawCrc(&law);
	identity.schema_id = SG_RUNE_ARTIFACT_SCHEMA_ID;
	Log(job, "rune: generation map=%s gravity=%g frame=%ums substep=%ums\n",
		job->mapname, (double)law.gravity, (unsigned int)law.frame_ms,
		(unsigned int)law.substep_ms);
	if (!SG_RuneGenerate(&bsp, &identity, &law, Progress, job, &image, &image_size, &report))
		Log(job, "rune: generation failed map=%s stage=%s error=%s\n", job->mapname,
			report.stage ? report.stage : "?", report.error ? report.error : "?");
	else
	{
		status = SG_RuneArtifactWriteFile(job->destination, image, image_size, &os_error);
		if (status != SG_RUNE_ARTIFACT_OK)
			Log(job, "rune: generation failed map=%s stage=publish error=%s os_error=%d\n",
				job->mapname, SG_RuneArtifactStatusString(status), os_error);
		else
		{
			Log(job, "rune: generation map=%s cells=%u portals=%u capabilities=%u "
				"surfaces=%u mechanisms=%u hooks=%u fires=%u seconds=%.1f\n",
				job->mapname, (unsigned int)report.cells, (unsigned int)report.portals,
				(unsigned int)report.capabilities, (unsigned int)report.surfaces,
				(unsigned int)report.mechanisms, (unsigned int)report.hooks,
				(unsigned int)report.fires, report.seconds);
			Log(job, "rune: generation published map=%s path=%s bytes=%lu\n",
				job->mapname, job->destination, (unsigned long)image_size);
			/* How many of the players' bites this rune was built with: the
			 * growth that triggers the next build is measured from here. */
			SG_BitesWriteCountFor(job->destination, job->bites_path);
			job->generated = 1;
		}
	}
	free(image);
	SG_RuneBspFree(&bsp);
	job->done = 1;
	return 0;
}

int SG_RuneGameGenerateBusy(void)
{
	return sg_job.started && !sg_job.done;
}

int SG_RuneGameGenerateStart(const char *mapname)
{
	cvar_t *game_directory_cvar;
	const char *game_directory;
	size_t n;

	if (SG_RuneGameGenerateBusy())
		return 0;
	if (mapname == NULL || mapname[0] == '\0')
	{
		gi.dprintf("rune: generation refused stage=argument\n");
		return 0;
	}
	if (sg_job.started)
	{
#ifdef _WIN32
		WaitForSingleObject(sg_job.thread, INFINITE);
		CloseHandle(sg_job.thread);
#else
		pthread_join(sg_job.thread, NULL);
#endif
		free(sg_job.entity_text);
	}
	memset(&sg_job, 0, sizeof(sg_job));
	strncpy(sg_job.mapname, mapname, sizeof(sg_job.mapname) - 1U);
	game_directory_cvar = gi.cvar("gamedir", "", 0);
	game_directory = game_directory_cvar && game_directory_cvar->string &&
		game_directory_cvar->string[0] ? game_directory_cvar->string : ".";
	if (snprintf(sg_job.bsp_path, sizeof(sg_job.bsp_path), "%s/maps/%s.bsp",
			game_directory, mapname) >= (int)sizeof(sg_job.bsp_path) ||
		!SG_RuneArtifactPath(sg_job.destination, sizeof(sg_job.destination),
			game_directory, mapname))
	{
		gi.dprintf("rune: generation refused stage=path\n");
		return 0;
	}
	n = strlen(sg_job.bsp_path);
	if (n > 4 && n < sizeof(sg_job.bites_path))
	{
		memcpy(sg_job.bites_path, sg_job.bsp_path, n - 4);
		strcpy(sg_job.bites_path + n - 4, ".bites");
	}
	if (SG_RuneLevelEntityText())
	{
		const char *text = SG_RuneLevelEntityText();

		sg_job.entity_text = malloc(strlen(text) + 1U);
		if (sg_job.entity_text)
			strcpy(sg_job.entity_text, text);
	}
	sg_job.gravity = sv_gravity ? sv_gravity->value : 800.0f;
#ifdef _WIN32
	sg_job.thread = CreateThread(NULL, 0, Worker, &sg_job, 0, NULL);
	if (sg_job.thread == NULL)
#else
	if (pthread_create(&sg_job.thread, NULL, Worker, &sg_job) != 0)
#endif
	{
		gi.dprintf("rune: generation refused stage=thread\n");
		free(sg_job.entity_text);
		sg_job.entity_text = NULL;
		return 0;
	}
	sg_job.started = 1;
	gi.dprintf("rune: generation started map=%s in the background\n", mapname);
	return 1;
}

int SG_RuneGameGeneratePoll(void)
{
	if (!sg_job.started || !sg_job.done)
		return 0;
#ifdef _WIN32
	WaitForSingleObject(sg_job.thread, INFINITE);
	CloseHandle(sg_job.thread);
#else
	pthread_join(sg_job.thread, NULL);
#endif
	sg_job.started = 0;
	if (sg_job.log_used)
		gi.dprintf("%s", sg_job.log);
	free(sg_job.entity_text);
	sg_job.entity_text = NULL;
	return sg_job.generated ? 1 : -1;
}

int SG_RuneGameGenerate(const char *mapname)
{
	return SG_RuneGameGenerateStart(mapname);
}
