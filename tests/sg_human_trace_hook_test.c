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
#ifdef SG_HUMAN_TRACE_LEARNING_TEST
#include "slipgate/sg_human_trace_learning_spool_private.h"
#include "slipgate/sg_human_trace_learning_consumer.h"
#include "slipgate/sg_human_trace_learning_game_test.h"
#include "slipgate/sg_human_trace_learning_host_game.h"
#include "slipgate/sg_human_trace_learning_host_game_test.h"
#endif

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
static unsigned inject_receipt_write_failure;

size_t __real_fwrite(const void *pointer, size_t size, size_t count,
	FILE *stream);

size_t __wrap_fwrite(const void *pointer, size_t size, size_t count,
	FILE *stream)
{
	if (inject_receipt_write_failure && size == 1U && count > 64U)
	{
		inject_receipt_write_failure--;
		return __real_fwrite(pointer, size, count / 2U, stream);
	}
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

static int LearningSpoolPath(char *path, size_t size, const char *directory,
	unsigned segment)
{
	return snprintf(path, size,
		"%s/humantrace-tracehook-00000065-000000ca-%06u.learning",
		directory, segment) < (int)size;
}

static int LearningReceiptPath(char *path, size_t size,
	const sg_human_trace_v3_spool_ref_t *spool, uint32_t client_id,
	uint64_t spawn_generation)
{
	static const char digits[] = "0123456789abcdef";
	const char *separator;
	char terminal[65];
	unsigned index;
	int written;

	if (!path || !spool || client_id == 0U || spawn_generation == 0U ||
		!(separator = strrchr(spool->path, '/')) || separator == spool->path)
		return 0;
	for (index = 0U; index < SG_HUMAN_TRACE_SHA256_BYTES; index++)
	{
		terminal[index * 2U] = digits[
			spool->completion.terminal_sha256[index] >> 4];
		terminal[index * 2U + 1U] = digits[
			spool->completion.terminal_sha256[index] & 15U];
	}
	terminal[64] = '\0';
	written = snprintf(path, size,
		"%.*s/humantrace-learning-ack-%s-%" PRIu32 "-%" PRIu64 ".ack",
		(int)(separator - spool->path), spool->path, terminal, client_id,
		spawn_generation);
	return written >= 0 && (size_t)written < size;
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
	const unsigned char byte = 0xa5U;
	int ok;

	if (!file)
		return 0;
	ok = fwrite(&byte, 1U, 1U, file) == 1U && fclose(file) == 0;
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
	uint32_t previous_root;
	uint64_t count;
	uint8_t have_previous;
	uint8_t valid_order;
} spool_scan_t;

typedef struct spool_event_count_s
{
	uint64_t count;
} spool_event_count_t;

#ifdef SG_HUMAN_TRACE_LEARNING_TEST
typedef struct accepted_receipt_test_s
{
	const sg_human_trace_v3_spool_ref_t *target;
	uint32_t client_id;
	uint64_t spawn_generation;
	uint8_t target_root;
	uint8_t found;
	uint8_t mark;
	uint8_t consumed;
} accepted_receipt_test_t;
#endif

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
	scan->have_previous = 1U;
	scan->count++;
	return 1;
}

static int CountStoredSpoolEvent(void *opaque,
	const sg_human_trace_v3_event_t *event)
{
	spool_event_count_t *count = opaque;

	if (!count || !event)
		return 0;
	count->count++;
	return 1;
}

static int ScanCollectionEvent(void *opaque,
	const sg_human_trace_v3_scope_acceptance_t *scope,
	const sg_human_trace_v3_event_t *event)
{
	return opaque && scope && event;
}

static int ScanCollectionFinish(void *opaque)
{
	return opaque != NULL;
}

typedef struct stored_event_visit_s
{
	const sg_human_trace_v3_spool_ref_t *target;
	sg_human_trace_v3_event_visitor_fn visitor;
	void *context;
	uint8_t selected;
} stored_event_visit_t;

static int StoredEventBegin(void *opaque,
	const sg_human_trace_v3_spool_ref_t *spool)
{
	stored_event_visit_t *visit = opaque;

	if (!visit || !visit->target || !spool)
		return 0;
	visit->selected = spool->root_segment == visit->target->root_segment &&
		strcmp(spool->path, visit->target->path) == 0;
	return 1;
}

static int StoredEventVisit(void *opaque,
	const sg_human_trace_v3_scope_acceptance_t *scope,
	const sg_human_trace_v3_event_t *event)
{
	stored_event_visit_t *visit = opaque;

	return visit && scope && event && (!visit->selected ||
		visit->visitor(visit->context, event));
}

static int StoredEventFinish(void *opaque)
{
	return opaque != NULL;
}

static int VisitStoredSpoolEvents(const sg_human_trace_v3_spool_ref_t *spool,
	sg_human_trace_v3_event_visitor_fn visitor, void *context)
{
	stored_event_visit_t visit;
	sg_human_trace_v3_collection_visitor_t collection;
	sg_level_identity_t identity;

	if (!spool || !visitor || !TraceIdentity(&identity))
		return 0;
	memset(&visit, 0, sizeof(visit));
	memset(&collection, 0, sizeof(collection));
	visit.target = spool;
	visit.visitor = visitor;
	visit.context = context;
	collection.begin_root = StoredEventBegin;
	collection.event = StoredEventVisit;
	collection.finish_root = StoredEventFinish;
	return SG_HumanTraceVisitAcceptedV3Collection(&identity, &collection,
		&visit);
}

#ifdef SG_HUMAN_TRACE_LEARNING_TEST
static int AcceptedReceiptBegin(void *opaque,
	const sg_human_trace_v3_spool_ref_t *spool)
{
	accepted_receipt_test_t *test = opaque;

	if (!test || !test->target || !spool)
		return 0;
	test->target_root = spool->root_segment == test->target->root_segment &&
		strcmp(spool->path, test->target->path) == 0;
	return 1;
}

static int AcceptedReceiptEvent(void *opaque,
	const sg_human_trace_v3_scope_acceptance_t *acceptance,
	const sg_human_trace_v3_event_t *event)
{
	accepted_receipt_test_t *test = opaque;
	const sg_human_trace_v3_spool_ref_t *spool;
	uint32_t client_id;
	uint64_t spawn_generation;

	if (!test || !acceptance || !event || !SG_HumanTraceAcceptedV3ScopeView(
		acceptance, &spool, &client_id, &spawn_generation))
		return 0;
	if (!test->target_root || test->found || spool->root_segment !=
		test->target->root_segment || client_id != test->client_id ||
		spawn_generation != test->spawn_generation)
		return 1;
	test->found = 1U;
	if (test->mark && !SG_HumanTraceMarkAcceptedV3ScopeConsumed(acceptance))
		return 0;
	test->consumed = SG_HumanTraceAcceptedV3ScopeConsumed(acceptance) ? 1U : 0U;
	return 1;
}

static int AcceptedReceiptFinish(void *opaque)
{
	accepted_receipt_test_t *test = opaque;

	return test != NULL;
}

static int AcceptedReceiptState(const sg_human_trace_v3_spool_ref_t *spool,
	uint32_t client_id, uint64_t spawn_generation, int mark,
	int *consumed_out)
{
	accepted_receipt_test_t test;
	sg_human_trace_v3_collection_visitor_t visitor;
	sg_level_identity_t identity;

	if (!spool || !consumed_out || !TraceIdentity(&identity))
		return 0;
	memset(&test, 0, sizeof(test));
	memset(&visitor, 0, sizeof(visitor));
	test.target = spool;
	test.client_id = client_id;
	test.spawn_generation = spawn_generation;
	test.mark = mark ? 1U : 0U;
	visitor.begin_root = AcceptedReceiptBegin;
	visitor.event = AcceptedReceiptEvent;
	visitor.finish_root = AcceptedReceiptFinish;
	if (!SG_HumanTraceVisitAcceptedV3Collection(&identity, &visitor, &test) ||
		!test.found)
		return 0;
	*consumed_out = test.consumed != 0U;
	return 1;
}
#endif

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
	visitor.event = ScanCollectionEvent;
	visitor.finish_root = ScanCollectionFinish;
	return SG_HumanTraceVisitAcceptedV3Collection(&identity, &visitor, scan);
}

static void RemoveTraceArtifact(const char *directory, unsigned segment)
{
	char path[1024];

	if (TracePath(path, sizeof(path), directory, segment))
		remove(path);
	if (LearningSpoolPath(path, sizeof(path), directory, segment))
		remove(path);
}

#ifdef SG_HUMAN_TRACE_LEARNING_TEST

typedef struct learning_host_fixture_s
{
	sg_rune_cell_t cells[1];
	sg_rune_phase_basis_t phases[1];
	sg_rune_capability_kernel_t kernels[1];
	sg_phase_coordinate_t coordinates[1];
	sg_rune_model_t model;
	sg_rune_runtime_snapshot_t snapshot;
	sg_human_trace_learning_kernel_key_t keys[1];
	uint64_t costs[1];
	uint64_t workspace_costs[1];
	sg_human_trace_learning_parameters_t parameters;
	sg_human_trace_learning_workspace_t workspace;
	sg_human_trace_learning_playthrough_t playthroughs[3];
	sg_human_trace_learning_runtime_t runtime;
} learning_host_fixture_t;

static sg_rune_stable_id_t LearningHostStableId(uint32_t domain,
	uint32_t ordinal)
{
	sg_rune_order_key_t order = {
		UINT64_C(0x4c4541524e494e47), domain, 7U, ordinal, 0U
	};

	return SG_RuneModelStableIdFromOrderKey(&order);
}

static int LearningHostFixtureInit(learning_host_fixture_t *fixture)
{
	sg_human_trace_learning_domain_t domain;
	sg_human_trace_learning_storage_t storage;

	if (!fixture)
		return 0;
	memset(fixture, 0, sizeof(*fixture));
	fixture->kernels[0].id.value = LearningHostStableId(SG_RUNE_ORDER_KERNEL,
		0U);
	fixture->kernels[0].family = SG_RUNE_CAPABILITY_HOOK_TRAJECTORY;
	fixture->keys[0].kernel = fixture->kernels[0].id;
	fixture->keys[0].control.value = LearningHostStableId(
		SG_RUNE_ORDER_CONTROL_FIBER, 0U);
	fixture->model = (sg_rune_model_t){
		.version = SG_RUNE_MODEL_VERSION,
		.schema_tag = SG_RUNE_MODEL_SCHEMA_TAG,
		.flags = SG_RUNE_MODEL_IMMUTABLE | SG_RUNE_MODEL_EXACT_BOUND |
			SG_RUNE_MODEL_NO_RUNTIME_ACTORS,
		.identity = {
			.bsp_content_id = UINT64_C(101),
			.physics_abi_id = UINT64_C(202)
		},
		.completeness = { .state = SG_RUNE_COMPLETENESS_COMPLETE },
		.cells = fixture->cells,
		.cell_count = 1U,
		.phases = fixture->phases,
		.phase_count = 1U,
		.kernels = fixture->kernels,
		.kernel_count = 1U
	};
	fixture->coordinates[0] = (sg_phase_coordinate_t){ 0U, 0U };
	fixture->snapshot = (sg_rune_runtime_snapshot_t){
		.identity = UINT64_C(303),
		.topology_revision = UINT64_C(404),
		.cell_count = 1U,
		.phase_count = 1U,
		.region_count = 1U,
		.model = &fixture->model,
		.phases = fixture->coordinates
	};
	domain = (sg_human_trace_learning_domain_t){
		.identity = {
			.rune_identity = fixture->snapshot.identity,
			.topology_revision = fixture->snapshot.topology_revision,
			.bsp_identity = fixture->model.identity.bsp_content_id,
			.physics_identity = fixture->model.identity.physics_abi_id
		},
		.snapshot = &fixture->snapshot,
		.kernel_keys = fixture->keys,
		.kernel_key_count = 1U
	};
	storage = (sg_human_trace_learning_storage_t){
		.effective_cost_us = fixture->costs,
		.effective_cost_capacity = 1U
	};
	fixture->workspace = (sg_human_trace_learning_workspace_t){
		.effective_cost_us = fixture->workspace_costs,
		.effective_cost_capacity = 1U
	};
	return SG_RuneRuntimeSnapshotValid(&fixture->snapshot) &&
		SG_HumanTraceLearningParametersInit(&fixture->parameters, &domain,
			&storage) && SG_HumanTraceLearningTestRuntimeInit(&fixture->runtime,
			&fixture->parameters, &fixture->workspace, fixture->playthroughs,
			1U, 1U);
}

static int LearningHostLocateKernel(void *context,
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_human_trace_v3_event_t *fire,
	const sg_human_trace_v3_event_t *attach,
	sg_human_trace_learning_kernel_key_t *key_out)
{
	learning_host_fixture_t *fixture = context;

	if (!fixture || snapshot != &fixture->snapshot || !fire || !attach ||
		!key_out || fire->kind != SG_HUMAN_TRACE_V3_EVENT_HOOK_FIRE ||
		attach->kind != SG_HUMAN_TRACE_V3_EVENT_HOOK_ATTACH ||
		fire->client_id != attach->client_id ||
		fire->spawn_generation != attach->spawn_generation ||
		fire->order >= attach->order)
		return 0;
	*key_out = fixture->keys[0];
	return 1;
}

static void LearningHostPublishedFixture(learning_host_fixture_t *fixture,
	sg_human_trace_learning_test_published_runtime_t *published)
{
	memset(published, 0, sizeof(*published));
	published->runtime = &fixture->runtime;
	published->snapshot = &fixture->snapshot;
	published->level_identity.bsp_checksum = 101U;
	published->level_identity.entity_crc32 = 202U;
	published->level_identity.host_physics_id = 1U;
	strcpy(published->level_identity.mapname, "tracehook");
	published->evidence_context = fixture;
	published->locate_hook_kernel = LearningHostLocateKernel;
}

static int RunLearningHostSequentialRoots(void)
{
	learning_host_fixture_t fixture;
	sg_human_trace_learning_test_published_runtime_t published;
	sg_human_trace_learning_host_report_t report;
	edict_t *player = &entities[1];
	edict_t *hook = &entities[3];
	pmove_state_t before;
	pmove_t after;
	unsigned root;

	if (!LearningHostFixtureInit(&fixture))
		return 95;
	LearningHostPublishedFixture(&fixture, &published);
	if (!SG_HumanTraceLearningHostGameTestPublishRuntime(&published, &report))
		return 96;
	SetupPmove(&before, &after);
	for (root = 0U; root < 3U; root++)
	{
		SetupPlayer(player, &clients[0], (unsigned long)(11U + root));
		SG_HumanTraceNewLevel();
		CaptureCompleteTraversalFrames(player, hook, &before, &after,
			(int)(1U + root * 8U), (int)(4U + root * 9U));
		SG_HumanTraceMatchEnd();
		memset(&report, 0, sizeof(report));
		SG_HumanTraceLearningHostGamePostMatch(&report);
		if (!report.trace_authenticated || !report.runtime_published ||
			report.committed_batches != 1U || report.rejected_batches != 0U ||
			fixture.playthroughs[0].used != 0U ||
			fixture.parameters.generation != (uint64_t)(2U + root) ||
			fixture.costs[0] != (uint64_t)(300000U + root * 100000U))
			return 97;
	}
	SG_HumanTraceLearningHostGameTestWithdrawRuntime(&fixture.runtime,
		&fixture.snapshot);
	SG_HumanTraceLearningHostGameReset();
	return 0;
}

#ifdef SG_HUMAN_TRACE_WRAP_FWRITE
static int RunLearningHostReceiptFailure(void)
{
	learning_host_fixture_t fixture;
	sg_human_trace_learning_test_published_runtime_t published;
	sg_human_trace_learning_host_report_t first;
	sg_human_trace_learning_host_report_t retry;
	edict_t *player = &entities[1];
	edict_t *hook = &entities[3];
	pmove_state_t before;
	pmove_t after;
	uint64_t generation;

	if (!LearningHostFixtureInit(&fixture))
		return 98;
	LearningHostPublishedFixture(&fixture, &published);
	if (!SG_HumanTraceLearningHostGameTestPublishRuntime(&published, NULL))
		return 99;
	SetupPlayer(player, &clients[0], 11UL);
	SetupPmove(&before, &after);
	SG_HumanTraceNewLevel();
	CaptureCompleteTraversal(player, hook, &before, &after, 1);
	SG_HumanTraceMatchEnd();
	/* The terminal-bound receipt tears after the parameter commit. The exact
	 * scope cursor must remain available for recovery. */
	inject_receipt_write_failure = 1U;
	SG_HumanTraceLearningHostGamePostMatch(&first);
	if (!first.trace_authenticated || first.committed_batches != 1U ||
		first.rejected_batches != 0U || fixture.parameters.generation != 2U ||
		fixture.playthroughs[0].used != 1U)
		return 100;
	generation = fixture.parameters.generation;
	/* Queue the next root before retry. One discovery pass must repair root
	 * zero's receipt, release the only cursor, and commit root one with it. */
	SetupPlayer(player, &clients[0], 12UL);
	SG_HumanTraceNewLevel();
	CaptureCompleteTraversalFrames(player, hook, &before, &after, 9, 13);
	SG_HumanTraceMatchEnd();
	SG_HumanTraceLearningHostGameReset();
	if (!SG_HumanTraceLearningHostGameTestPublishRuntime(&published, &retry) ||
		retry.committed_batches != 1U || retry.rejected_batches != 0U ||
		fixture.parameters.generation != generation + 1U ||
		fixture.costs[0] != UINT64_C(400000) ||
		fixture.playthroughs[0].used != 0U)
		return 101;
	SG_HumanTraceLearningHostGameTestWithdrawRuntime(&fixture.runtime,
		&fixture.snapshot);
	SG_HumanTraceLearningHostGameReset();
	return 0;
}

static int RunLearningHostFirstOccurrenceOrder(void)
{
	learning_host_fixture_t fixture;
	sg_human_trace_learning_test_published_runtime_t published;
	sg_human_trace_learning_host_report_t report;
	edict_t *first = &entities[1];
	edict_t *second = &entities[2];
	edict_t *hook = &entities[3];
	pmove_state_t before;
	pmove_t after;

	if (!LearningHostFixtureInit(&fixture))
		return 116;
	fixture.runtime.playthrough_capacity = 3U;
	LearningHostPublishedFixture(&fixture, &published);
	if (!SG_HumanTraceLearningHostGameTestPublishRuntime(&published, NULL))
		return 117;
	SetupPmove(&before, &after);
	SG_HumanTraceNewLevel();
	SetupPlayer(first, &clients[0], 11UL);
	memset(hook, 0, sizeof(*hook));
	hook->inuse = true;
	hook->owner = first;
	hook->hook_target = &entities[0];
	first->client->hook = hook;
	after.groundentity = NULL;
	level.framenum = 1;
	SG_HumanTracePmove(first, &before, &after);
	SG_HumanTraceHookFire(first, hook);
	SG_HumanTraceHookAttach(first, hook, &entities[0]);
	level.framenum = 2;
	SG_HumanTracePmove(first, &before, &after);
	level.framenum = 3;
	SG_HumanTraceHookRelease(first);
	SG_HumanTraceHookReset(first, hook);
	SetupPlayer(second, &clients[1], 22UL);
	CaptureCompleteTraversal(second, hook, &before, &after, 4);
	SetupPlayer(second, &clients[1], 23UL);
	CaptureCompleteTraversal(second, hook, &before, &after, 8);
	after.groundentity = &entities[0];
	level.framenum = 12;
	SG_HumanTracePmove(first, &before, &after);
	SG_HumanTraceMatchEnd();
	inject_receipt_write_failure = 3U;
	SG_HumanTraceLearningHostGamePostMatch(&report);
	if (!report.trace_authenticated || report.committed_batches != 3U ||
		report.rejected_batches != 0U ||
		fixture.playthroughs[0].used != 1U ||
		fixture.playthroughs[0].client_id != 1U ||
		fixture.playthroughs[0].spawn_generation != UINT64_C(11) ||
		fixture.playthroughs[1].used != 1U ||
		fixture.playthroughs[1].client_id != 2U ||
		fixture.playthroughs[1].spawn_generation != UINT64_C(22) ||
		fixture.playthroughs[2].used != 1U ||
		fixture.playthroughs[2].client_id != 2U ||
		fixture.playthroughs[2].spawn_generation != UINT64_C(23))
	{
		fprintf(stderr,
			"actual_scope_order=%" PRIu32 "/%" PRIu64 ",%" PRIu32 "/%" PRIu64
			",%" PRIu32 "/%" PRIu64 "\n",
			fixture.playthroughs[0].client_id,
			fixture.playthroughs[0].spawn_generation,
			fixture.playthroughs[1].client_id,
			fixture.playthroughs[1].spawn_generation,
			fixture.playthroughs[2].client_id,
			fixture.playthroughs[2].spawn_generation);
		return 119;
	}
	SG_HumanTraceLearningHostGameTestWithdrawRuntime(&fixture.runtime,
		&fixture.snapshot);
	SG_HumanTraceLearningHostGameReset();
	return 0;
}
#endif

static int RunLearningHostVisitCount(const char *directory, unsigned count,
	int clean)
{
	learning_host_fixture_t fixture;
	sg_human_trace_learning_test_published_runtime_t published;
	sg_human_trace_learning_host_report_t report;
	edict_t *player = &entities[1];
	edict_t *hook = &entities[3];
	pmove_state_t before;
	pmove_t after;
	spool_scan_t scan;
	char receipt_path[1024];
	unsigned index;
	uint64_t expected;
	int result = 0;

	if (!LearningHostFixtureInit(&fixture))
		return 102;
	LearningHostPublishedFixture(&fixture, &published);
	if (!SG_HumanTraceLearningHostGameTestPublishRuntime(&published, NULL))
		return 103;
	SetupPlayer(player, &clients[0], 11UL);
	SetupPmove(&before, &after);
	SG_HumanTraceNewLevel();
	for (index = 0U; index < count; index++)
		CaptureCompleteTraversal(player, hook, &before, &after,
			(int)(1U + index * 4U));
	SG_HumanTraceMatchEnd();
	SG_HumanTraceLearningHostGameTestResetVisitCount();
	SG_HumanTraceLearningHostGamePostMatch(&report);
	expected = (uint64_t)count * UINT64_C(7);
	if (!report.trace_authenticated || report.committed_batches != 1U ||
		report.rejected_batches != 0U || fixture.parameters.generation !=
		(UINT64_C(1) + (uint64_t)count) ||
		SG_HumanTraceLearningHostGameTestVisitCount() != expected)
		result = 104;
	if (clean)
	{
		if (ScanCompletedSpools(&scan) && scan.count == 1U &&
			LearningReceiptPath(receipt_path, sizeof(receipt_path), &scan.first,
				1U, UINT64_C(11)))
			remove(receipt_path);
		RemoveTraceArtifact(directory, 0U);
	}
	SG_HumanTraceLearningHostGameTestWithdrawRuntime(&fixture.runtime,
		&fixture.snapshot);
	SG_HumanTraceLearningHostGameReset();
	return result;
}

static int RunLearningHostLinearity(const char *directory)
{
	static const unsigned counts[] = { 64U, 128U, 256U, 512U };
	unsigned index;

	for (index = 0U; index < sizeof(counts) / sizeof(counts[0]); index++)
		if (RunLearningHostVisitCount(directory, counts[index], 1) != 0)
			return 105 + (int)index;
	return 0;
}

static int RunLearningHostIORoots(unsigned root_count)
{
	learning_host_fixture_t fixture;
	sg_human_trace_learning_test_published_runtime_t published;
	sg_human_trace_learning_host_report_t report;
	edict_t *player = &entities[1];
	edict_t *hook = &entities[3];
	pmove_state_t before;
	pmove_t after;
	unsigned root;

	if (!LearningHostFixtureInit(&fixture))
		return 120;
	LearningHostPublishedFixture(&fixture, &published);
	if (!SG_HumanTraceLearningHostGameTestPublishRuntime(&published, NULL))
		return 121;
	SetupPmove(&before, &after);
	for (root = 0U; root < root_count; root++)
	{
		SetupPlayer(player, &clients[0], (unsigned long)(11U + root));
		SG_HumanTraceNewLevel();
		CaptureCompleteTraversal(player, hook, &before, &after,
			(int)(1U + root * 8U));
		SG_HumanTraceMatchEnd();
	}
	SG_HumanTraceLearningHostGameTestResetVisitCount();
	SG_HumanTraceLearningHostGamePostMatch(&report);
	if (!report.trace_authenticated ||
		report.committed_batches != root_count ||
		report.rejected_batches != 0U ||
		fixture.parameters.generation != UINT64_C(1) + root_count ||
		SG_HumanTraceLearningHostGameTestVisitCount() !=
			(uint64_t)root_count * UINT64_C(7))
		return 122;
	SG_HumanTraceLearningHostGameTestWithdrawRuntime(&fixture.runtime,
		&fixture.snapshot);
	SG_HumanTraceLearningHostGameReset();
	return 0;
}

static int RunLearningHostLongPostMatch(void)
{
	const unsigned long event_count = 16385UL;
	learning_host_fixture_t fixture;
	sg_human_trace_learning_test_published_runtime_t published;
	sg_human_trace_learning_host_report_t report;
	edict_t *player = &entities[1];
	edict_t *hook = &entities[3];
	pmove_state_t before;
	pmove_t after;
	unsigned long index;

	if (!LearningHostFixtureInit(&fixture))
		return 109;
	LearningHostPublishedFixture(&fixture, &published);
	if (!SG_HumanTraceLearningHostGameTestPublishRuntime(&published, NULL))
		return 110;
	SetupPlayer(player, &clients[0], 11UL);
	SetupPmove(&before, &after);
	SG_HumanTraceNewLevel();
	CaptureCompleteTraversal(player, hook, &before, &after, 1);
	after.groundentity = &entities[0];
	for (index = 7UL; index < event_count; index++)
	{
		level.framenum = (int)(index + 1UL);
		SG_HumanTracePmove(player, &before, &after);
	}
	SG_HumanTraceMatchEnd();
	SG_HumanTraceLearningHostGameTestResetVisitCount();
	SG_HumanTraceLearningHostGamePostMatch(&report);
	if (!report.trace_authenticated || report.committed_batches != 1U ||
		report.rejected_batches != 0U || fixture.parameters.generation != 2U ||
		SG_HumanTraceLearningHostGameTestVisitCount() != event_count ||
		fixture.playthroughs[0].used != 0U)
		return 111;
	SG_HumanTraceLearningHostGameTestWithdrawRuntime(&fixture.runtime,
		&fixture.snapshot);
	SG_HumanTraceLearningHostGameReset();
	return 0;
}

static int RunLearningHostIntegration(void)
{
	learning_host_fixture_t fixture;
	sg_human_trace_learning_test_published_runtime_t published;
	sg_human_trace_learning_host_report_t pending;
	sg_human_trace_learning_host_report_t committed;
	spool_scan_t scan;
	char receipt_path[1024];
	uint64_t effective_cost_us;
	int consumed;

	if (!LearningHostFixtureInit(&fixture))
		return 80;
	memset(&published, 0, sizeof(published));
	published.runtime = &fixture.runtime;
	published.snapshot = &fixture.snapshot;
	published.level_identity.bsp_checksum = 101U;
	published.level_identity.entity_crc32 = 202U;
	published.level_identity.host_physics_id = 1U;
	strcpy(published.level_identity.mapname, "tracehook");
	published.evidence_context = &fixture;
	published.locate_hook_kernel = LearningHostLocateKernel;
	/* A valid receipt is then damaged as though its final sector tore after a
	 * crash.  It must not suppress the real host-only replay below: the
	 * immutable spool remains discoverable and the host repairs the sidecar
	 * only after its atomic parameter commit. */
	if (!ScanCompletedSpools(&scan) || scan.count != 1U ||
		!LearningReceiptPath(receipt_path, sizeof(receipt_path), &scan.first,
			1U, UINT64_C(11)) || !AcceptedReceiptState(&scan.first, 1U,
			UINT64_C(11), 1, &consumed) || !consumed ||
		!FlipSpoolByte(receipt_path) || !AcceptedReceiptState(&scan.first, 1U,
			UINT64_C(11), 0, &consumed) || consumed)
		return 81;
	SG_HumanTraceLearningHostGamePostMatch(&pending);
	if (!pending.trace_authenticated || pending.derived_batches != 1U ||
		pending.derived_records != 1U || pending.committed_batches != 0U ||
		pending.pending_batches != 1U || fixture.parameters.generation != 1U ||
		fixture.costs[0] != 0U)
		return 82;
	/* A level transition and recorder teardown retire all in-process views, but
	 * not the authenticated spool; a matching next publication must rediscover
	 * and consume the same batch atomically. */
	SG_HumanTraceLearningHostGameReset();
	SG_HumanTraceNewLevel();
	if (!SG_HumanTraceLearningHostGameTestPublishRuntime(&published,
		&committed) || committed.committed_batches != 1U ||
		committed.rejected_batches != 0U || committed.pending_batches != 0U ||
		fixture.parameters.generation != 2U || fixture.costs[0] !=
		UINT64_C(300000) || fixture.playthroughs[0].used != 0U)
		return 83;
	if (!SG_HumanTraceLearningConsumerEffectiveKernelCost(
		&fixture.parameters, &fixture.keys[0], UINT64_C(900000),
		&effective_cost_us) || effective_cost_us != UINT64_C(300000))
		return 84;
	if (!ScanCompletedSpools(&scan))
		return 85;
	if (LearningReceiptPath(receipt_path, sizeof(receipt_path), &scan.first,
		1U, UINT64_C(11)))
		remove(receipt_path);
	if (LearningReceiptPath(receipt_path, sizeof(receipt_path), &scan.first,
		2U, UINT64_C(22)))
		remove(receipt_path);
	SG_HumanTraceLearningHostGameTestWithdrawRuntime(&fixture.runtime,
		&fixture.snapshot);
	SG_HumanTraceLearningHostGameReset();
	return 0;
}

#endif /* SG_HUMAN_TRACE_LEARNING_TEST */

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
	if (LearningSpoolPath(path, sizeof(path), directory, 0U))
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

	if (!LearningSpoolPath(spool_path, sizeof(spool_path), directory, 0U))
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

static int RunAckRecovery(const char *directory, int tamper)
{
	char receipt_path[1024] = { 0 };
	edict_t *player = &entities[1];
	pmove_state_t before;
	pmove_t after;
	spool_scan_t scan;
	int consumed;
	int result = 0;

	SetupPlayer(player, &clients[0], 11UL);
	SetupPmove(&before, &after);
	level.framenum = 1;
	SG_HumanTraceNewLevel();
	SG_HumanTracePmove(player, &before, &after);
	SG_HumanTraceMatchEnd();
	if (!ScanCompletedSpools(&scan) || scan.count != 1U ||
		!LearningReceiptPath(receipt_path, sizeof(receipt_path), &scan.first,
			1U, UINT64_C(11)) || !AcceptedReceiptState(&scan.first, 1U,
			UINT64_C(11), 1, &consumed) || !consumed)
		result = 42;
	if (!result && !(tamper ? FlipSpoolByte(receipt_path) :
		AppendSpoolPartial(receipt_path)))
		result = 43;
	/* A torn or modified receipt is ignored.  It cannot poison the immutable
	 * evidence spool or suppress a later replay of this scope. */
	if (!result && (!ScanCompletedSpools(&scan) || scan.count != 1U ||
		!AcceptedReceiptState(&scan.first, 1U, UINT64_C(11), 0,
			&consumed) || consumed))
		result = 44;
	if (!result && (!AcceptedReceiptState(&scan.first, 1U, UINT64_C(11), 1,
		&consumed) || !consumed))
		result = 45;
	if (receipt_path[0])
		remove(receipt_path);
	RemoveTraceArtifact(directory, 0U);
	return result;
}

static int RunLongStream(const char *directory)
{
	/* This deliberately crosses the historical 16,384-event refusal point.
	 * It is a regression witness, not a recorder work limit. */
	const unsigned long event_count = 16385UL;
	edict_t *player = &entities[1];
	pmove_state_t before;
	pmove_t after;
	spool_scan_t scan;
	spool_event_count_t events;
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
	SG_HumanTraceMatchEnd();
	if (!ScanCompletedSpools(&scan) || scan.count != 1U ||
		!scan.valid_order)
		result = 36;
	memset(&events, 0, sizeof(events));
	if (!result && (!VisitStoredSpoolEvents(&scan.first,
		CountStoredSpoolEvent, &events) || events.count != event_count))
		result = 37;
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
	 * collision garbage. Valid roots zero and two must still be visited FIFO. */
	if (!LearningSpoolPath(spool_path, sizeof(spool_path), directory, 1U) ||
		!AppendSpoolPartial(spool_path))
		result = 91;
	if (!result && (!LearningSpoolPath(spool_path, sizeof(spool_path),
		directory, 3U) || !AppendSpoolPartial(spool_path)))
		result = 92;
	if (!result && (!LearningSpoolPath(spool_path, sizeof(spool_path),
		directory, 4U) || !AppendSpoolPartial(spool_path)))
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
		if (!SG_HumanTraceLearningSpoolTestFormatJsonPath(directory, &identity,
			segments[index], path))
			return 113;
		name = strrchr(path, '/');
		name = name ? name + 1 : path;
		if (strlen(name) < strlen(suffixes[index]) || strcmp(name + strlen(name) -
			strlen(suffixes[index]), suffixes[index]) != 0 ||
			!SG_HumanTraceLearningSpoolTestJsonNameSegment(name, &identity,
				&parsed) || parsed != segments[index])
			return 114;
	}
	if (SG_HumanTraceLearningSpoolTestJsonNameSegment(
		"humantrace-tracehook-00000065-000000ca-0000000.jsonl", &identity,
		&parsed))
		return 115;
	return 0;
}

static int RunStoredSpoolCoverage(void)
{
	spool_scan_t scan;
	spool_event_count_t events;
	int consumed;

	if (!ScanCompletedSpools(&scan) || scan.count != 1U ||
		!scan.valid_order || scan.first.root_segment != 0U)
		return 39;
	memset(&events, 0, sizeof(events));
	if (!VisitStoredSpoolEvents(&scan.first, CountStoredSpoolEvent,
		&events) || events.count != 12U)
		return 40;
	/* Acknowledging the incomplete second life must not hide the complete first
	 * life.  The later publisher therefore still sees and commits client one. */
	if (!AcceptedReceiptState(&scan.first, 2U, UINT64_C(22), 1,
		&consumed) || !consumed || !AcceptedReceiptState(&scan.first, 1U,
		UINT64_C(11), 0, &consumed) || consumed)
		return 41;
	return 0;
}

static int RunAcceptedScopeDenial(const char *directory)
{
	edict_t *player = &entities[1];
	pmove_state_t before;
	pmove_t after;
	spool_scan_t scan;
	int absent_accepted;
	int consumed;
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
	absent_accepted = AcceptedReceiptState(&scan.first, 2U, UINT64_C(11), 1,
		&consumed);
	typo_accepted = AcceptedReceiptState(&scan.first, 1U, UINT64_C(12), 1,
		&consumed);
	valid_accepted = AcceptedReceiptState(&scan.first, 1U, UINT64_C(11), 1,
		&consumed);
	if (absent_accepted || typo_accepted || !valid_accepted || !consumed)
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
	if (LearningSpoolPath(path0, sizeof(path0), directory, 0U))
		remove(path0);
	if (LearningSpoolPath(path1, sizeof(path1), directory, 1U))
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
	spool_scan_t scan;

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
		CountRecords(path1, NULL) != 3 || !ScanCompletedSpools(&scan) ||
		scan.count != 1U || !scan.valid_order ||
		scan.first.root_segment != 0U || scan.first.completion.session != 0U ||
		scan.first.completion.segment != 1U ||
		scan.first.completion.continuation != 1U)
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
	if (argc == 3 && strcmp(argv[2], "ack-truncated") == 0)
		return RunAckRecovery(argv[1], 0);
	if (argc == 3 && strcmp(argv[2], "ack-tampered") == 0)
		return RunAckRecovery(argv[1], 1);
	if (argc == 3 && strcmp(argv[2], "long-stream") == 0)
		return RunLongStream(argv[1]);
	if (argc == 3 && strcmp(argv[2], "spool-order") == 0)
		return RunSpoolOrder(argv[1]);
	if (argc == 3 && strcmp(argv[2], "spool-quarantine") == 0)
		return RunSpoolQuarantine(argv[1]);
	if (argc == 3 && strcmp(argv[2], "segment-names") == 0)
		return RunSegmentNames(argv[1]);
#ifdef SG_HUMAN_TRACE_LEARNING_TEST
	if (argc == 3 && strcmp(argv[2], "host-sequential") == 0)
		return RunLearningHostSequentialRoots();
	if (argc == 3 && strcmp(argv[2], "host-linearity") == 0)
		return RunLearningHostLinearity(argv[1]);
	if (argc == 3 && strcmp(argv[2], "host-io-64") == 0)
		return RunLearningHostVisitCount(argv[1], 64U, 0);
	if (argc == 3 && strcmp(argv[2], "host-io-128") == 0)
		return RunLearningHostVisitCount(argv[1], 128U, 0);
	if (argc == 3 && strcmp(argv[2], "host-io-roots") == 0)
		return RunLearningHostIORoots(8U);
	if (argc == 3 && strcmp(argv[2], "host-long-postmatch") == 0)
		return RunLearningHostLongPostMatch();
#ifdef SG_HUMAN_TRACE_WRAP_FWRITE
	if (argc == 3 && strcmp(argv[2], "host-receipt-failure") == 0)
		return RunLearningHostReceiptFailure();
	if (argc == 3 && strcmp(argv[2], "host-first-occurrence") == 0)
		return RunLearningHostFirstOccurrenceOrder();
#endif
#endif
	if (argc == 3 && strcmp(argv[2], "accepted-scope-denial") == 0)
		return RunAcceptedScopeDenial(argv[1]);
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
#ifdef SG_HUMAN_TRACE_LEARNING_TEST
	if (!result && RunStoredSpoolCoverage() != 0)
		result = 9;
	if (!result && RunLearningHostIntegration() != 0)
		result = 10;
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
