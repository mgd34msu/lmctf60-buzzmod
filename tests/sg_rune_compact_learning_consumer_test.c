#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../g_local.h"
#undef world
#include "../g_tourney.h"
#include "../slipgate/sg_crc32.h"
#include "../slipgate/sg_rune_compact_learning_game.h"
#include "../slipgate/sg_rune_compact_weapon_catalog.h"
#include "../slipgate/sg_rune_source_authority_owner.h"

#if defined(SG_RUNE_COMPACT_LEARNING_HOST_LIFECYCLE_TEST)
#include "../slipgate/sg_local.h"
#include "../slipgate/sg_bot.h"
#include "../slipgate/sg_compound_guard_game.h"
#include "../slipgate/sg_hook_diagnostics.h"
#include "../slipgate/sg_hooks.h"
#include "../slipgate/sg_strategy_caller.h"
#endif

sg_rune_compact_model_t *SG_TestCompactLearningConsumerModel(void);
sg_rune_compact_model_t *
	SG_TestCompactLearningConsumerDisconnectedModel(void);
int SG_TestCompactLearningConsumerParseModelIdentity(
	const sg_rune_compact_model_t *model, sg_level_identity_t *identity_out);

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

struct sg_human_trace_v3_scope_acceptance_s
{
	uint32_t token;
};

typedef struct trace_fixture_s
{
	sg_level_identity_t identity;
	sg_human_trace_v3_spool_ref_t root;
	sg_human_trace_v3_segment_ref_t segment;
	struct sg_human_trace_v3_scope_acceptance_s scopes[2];
	sg_human_trace_v3_event_t events[4];
	uint8_t event_scope[4];
	uint32_t event_count;
	int visit_result;
	sg_human_trace_visit_status_t visit_status;
	int scope_view_result;
} trace_fixture_t;

static trace_fixture_t *active_trace;
static sg_identity_status_t identity_snapshot_status = SG_IDENTITY_OK;
static const sg_rune_compact_model_t *production_model;
static sg_compact_localization_observation_owner_t production_observation;
static const sg_level_identity_t *identity_snapshot_override;
static cvar_t source_ctfflags = {
	.name = "ctfflags", .string = "0", .value = 0.0f
};
static cvar_t source_deathmatch = {
	.name = "deathmatch", .string = "1", .value = 1.0f
};
static cvar_t source_fastswitch = {
	.name = "fastswitch", .string = "0", .value = 0.0f
};
cvar_t *ctfflags = &source_ctfflags;
cvar_t *deathmatch = &source_deathmatch;
cvar_t *fastswitch = &source_fastswitch;
int matchstate = MATCH_NONE;
static int source_host_current = 1;
static uint64_t source_host_epoch = UINT64_C(1);

#if !defined(SG_RUNE_COMPACT_LEARNING_HOST_LIFECYCLE_TEST)
/* The isolated consumer test does not link engine lifecycle ownership.  Its
 * compact-production seam carries only the sealed snapshot it is given. */
struct sg_rune_source_authority_s { uint32_t fixture; };
static struct sg_rune_source_authority_s source_fixture_authority;
static sg_rune_source_snapshot_t source_fixture_snapshot;

sg_rune_source_status_t SG_RuneSourceAuthorityAcquire(
	sg_rune_source_authority_t **authority_out)
{
	if (authority_out == NULL)
		return SG_RUNE_SOURCE_INVALID_ARGUMENT;
	*authority_out = (sg_rune_source_authority_t *)(void *)
		&source_fixture_authority;
	return SG_RUNE_SOURCE_OK;
}

sg_rune_source_status_t SG_RuneSourceAuthoritySnapshot(
	const sg_rune_source_authority_t *authority,
	sg_rune_source_snapshot_t *snapshot_out)
{
	if (authority != (const sg_rune_source_authority_t *)(const void *)
		&source_fixture_authority || snapshot_out == NULL)
		return SG_RUNE_SOURCE_INVALID_ARGUMENT;
	*snapshot_out = source_fixture_snapshot;
	return SG_RUNE_SOURCE_OK;
}

sg_rune_source_status_t SG_RuneSourceAuthorityCurrent(
	const sg_rune_source_authority_t *authority)
{
	return authority == (const sg_rune_source_authority_t *)(const void *)
		&source_fixture_authority ? SG_RUNE_SOURCE_OK :
		SG_RUNE_SOURCE_INVALID_ARGUMENT;
}

void SG_RuneSourceAuthorityDestroy(sg_rune_source_authority_t *authority)
{
	(void)authority;
}
#endif

#if defined(SG_RUNE_COMPACT_LEARNING_HOST_LIFECYCLE_TEST)
enum
{
	HOST_EVENT_TRACE_END = 1,
	HOST_EVENT_POST_MATCH,
	HOST_EVENT_SAVE_CLIENTS,
	HOST_EVENT_SOURCE_RESET,
	HOST_EVENT_IDENTITY_RESET
};

static int host_events[16];
static uint32_t host_event_count;
static uint32_t host_expected_priors_at_save;
static int host_trace_terminal;
static int host_lifecycle_active;
game_import_t gi;
game_locals_t game;
level_locals_t level;
static edict_t host_edicts[1];
edict_t *g_edicts = host_edicts;
gitem_t itemlist[MAX_ITEMS];
sg_bot_t sg_bots[SG_MAXBOTS];
sg_fields_t sg_fields;
static cvar_t host_coop = {
	.name = "coop", .string = "0", .value = 0.0f
};
static cvar_t host_maxclients = {
	.name = "maxclients", .string = "0", .value = 0.0f
};
static cvar_t host_game_directory = {
	.name = "gamedir", .string = "base", .value = 0.0f
};
cvar_t *coop = &host_coop;
cvar_t *maxclients = &host_maxclients;
sg_host_t sg_host;

qboolean SG_LevelSetup(void);
void SG_LevelChange(void);
uint32_t SG_CompactProductionLearningPriorCount(void);

static void HostEvent(int event)
{
	if (host_event_count < (uint32_t)(sizeof(host_events) /
		sizeof(host_events[0])))
		host_events[host_event_count++] = event;
}

static void HostResetEvents(void)
{
	memset(host_events, 0, sizeof(host_events));
	host_event_count = 0U;
	host_trace_terminal = 0;
}

static void HostDPrint(const char *format, ...)
{
	(void)format;
}

static void HostFlush(void)
{
}

static cvar_t *HostCvar(const char *name, const char *value, int flags)
{
	(void)value;
	(void)flags;
	return name != NULL && strcmp(name, "gamedir") == 0 ?
		&host_game_directory : NULL;
}

void SG_HooksInit(void)
{
}

sg_host_law_result_t SG_HostLawProductionEnsureLevel(const char *mapname)
{
	sg_host_law_result_t result;

	memset(&result, 0, sizeof(result));
	result.status = mapname != NULL && strcmp(mapname, "lmctf-test") == 0 ?
		SG_HOST_LAW_OK : SG_HOST_LAW_INVALID_ARGUMENT;
	return result;
}

int SG_RuneInstallDestinationPath(char *output, size_t output_size,
	const char *game_directory, const char *map_name)
{
	if (output == NULL || output_size < sizeof("model.rune") ||
		game_directory == NULL || map_name == NULL)
		return 0;
	(void)snprintf(output, output_size, "%s", "model.rune");
	return 1;
}

void Caco_Reset(void)
{
}

const char *SG_HostLawStatusString(sg_host_law_status_t status)
{
	(void)status;
	return "test";
}

const char *SG_HostLawFieldString(sg_host_law_field_t field)
{
	(void)field;
	return "test";
}

const char *SG_CompactRuntimeLevelStatusString(
	sg_compact_runtime_level_status_t status)
{
	(void)status;
	return "test";
}

const char *SG_RuneCompactWireErrorString(sg_rune_compact_wire_error_code_t code)
{
	(void)code;
	return "test";
}

const char *SG_RuneCompactArtifactLoadDiagnosticString(
	sg_rune_compact_artifact_load_diagnostic_t diagnostic)
{
	(void)diagnostic;
	return "test";
}

void SG_StrikeAdapterReset(void *adapter)
{
	(void)adapter;
}

void SG_HumanTraceMatchEnd(void)
{
	CHECK(active_trace != NULL);
	host_trace_terminal = 1;
	HostEvent(HOST_EVENT_TRACE_END);
}

void Victory(void)
{
}

void SG_ChatLevelEnd(void)
{
}

int DB_SessionRecord(void)
{
	return 1;
}

void UI_Boards_MatchEnd(void)
{
}

void CTF_MatchReport(void)
{
}

edict_t *G_Find(edict_t *from, int fieldofs, char *match)
{
	static edict_t spot;

	(void)from;
	(void)fieldofs;
	(void)match;
	return &spot;
}

void SaveClientData(void)
{
	HostEvent(HOST_EVENT_SAVE_CLIENTS);
	CHECK(SG_CompactProductionLearningPriorCount() ==
		host_expected_priors_at_save);
}

void SG_LevelIdentityReset(void)
{
	sg_rune_source_authority_t *authority = NULL;

	CHECK(SG_RuneSourceAuthorityAcquire(&authority) ==
		SG_RUNE_SOURCE_INVALID_STATE);
	CHECK(authority == NULL);
	HostEvent(HOST_EVENT_SOURCE_RESET);
	HostEvent(HOST_EVENT_IDENTITY_RESET);
	CHECK(SG_CompactProductionLearningPriorCount() == 0U);
}

#if !defined(SG_RUNE_COMPACT_LEARNING_HOST_RUNTIME_TEST)
void SG_StrategyCallerDestroy(sg_strategy_caller_t *caller)
{
	(void)caller;
}
#endif

int SG_HookDiagnosticsFinish(sg_hook_diagnostic_state_t *state,
	const char *terminal, const char *detail)
{
	(void)state;
	(void)terminal;
	(void)detail;
	return 1;
}

void SG_HostLawProductionReset(void)
{
}

void SG_ButtonExecutionLevelReset(void)
{
}

void SG_TimedVaultEgressScopeEnd(void)
{
}

sg_compound_guard_result_t SG_CompoundGuardGameLevelReset(void)
{
	return SG_COMPOUND_GUARD_OK;
}

int SG_RemoveBots(void)
{
	return 0;
}

void SG_CollectibleArmorTargetLevelReset(void)
{
}

void Botfill_Reset(void)
{
}

void Combat_ResetLevel(void)
{
}

void Clock_LevelReset(void)
{
}

void Tilt_LevelReset(void)
{
}
#endif

int SG_RuneCompactArtifactLoaderInit(
	sg_rune_compact_artifact_loader_t *loader)
{
	if (loader == NULL)
		return 0;
	loader->state = 1U;
	return 1;
}

void SG_RuneCompactArtifactLoaderReset(
	sg_rune_compact_artifact_loader_t *loader)
{
	if (loader != NULL)
		loader->published = NULL;
}

void SG_RuneCompactArtifactLoaderDestroy(
	sg_rune_compact_artifact_loader_t *loader)
{
	if (loader != NULL)
		memset(loader, 0, sizeof(*loader));
}

const sg_rune_compact_model_t *SG_RuneCompactArtifactLoaderSnapshot(
	const sg_rune_compact_artifact_loader_t *loader)
{
	return loader != NULL && loader->published != NULL ? production_model : NULL;
}

int SG_RuneCompactArtifactLoaderSnapshotInfo(
	const sg_rune_compact_artifact_loader_t *loader,
	sg_rune_compact_wire_info_t *info_out)
{
	if (info_out != NULL)
		memset(info_out, 0, sizeof(*info_out));
	if (loader == NULL || loader->published == NULL || info_out == NULL)
		return 0;
	*info_out = loader->published_info;
	return 1;
}

sg_rune_compact_artifact_load_result_t
SG_RuneCompactArtifactLoaderLoadAcceptedFileWithInfo(
	sg_rune_compact_artifact_loader_t *loader, const char *path,
	sg_rune_compact_identity_t *identity_out,
	sg_rune_compact_wire_info_t *info_out)
{
	sg_rune_compact_artifact_load_result_t result;

	memset(&result, 0, sizeof(result));
	if (info_out != NULL)
		memset(info_out, 0, sizeof(*info_out));
	if (loader == NULL || path == NULL || identity_out == NULL ||
		info_out == NULL || production_model == NULL)
	{
		result.diagnostic = SG_RUNE_COMPACT_ARTIFACT_LOAD_INVALID_ARGUMENT;
		return result;
	}
	loader->published = (sg_rune_compact_wire_decoded_t *)(uintptr_t)1U;
	*identity_out = production_model->identity;
	info_out->wire_version = SG_RUNE_COMPACT_WIRE_VERSION;
	info_out->model_version = SG_RUNE_COMPACT_MODEL_VERSION;
	info_out->analytic_version = SG_RUNE_COMPACT_ANALYTIC_VERSION;
	info_out->schema_tag = SG_RUNE_COMPACT_MODEL_SCHEMA_TAG;
	info_out->image_bytes = 1U;
	info_out->checksum = 1U;
	info_out->identity = production_model->identity;
	loader->published_info = *info_out;
	result.diagnostic = SG_RUNE_COMPACT_ARTIFACT_LOAD_OK;
	return result;
}

sg_host_law_result_t SG_HostLawProductionAcquire(
	sg_host_law_runtime_authority_t *authority_out)
{
	sg_host_law_result_t result;

	memset(&result, 0, sizeof(result));
	if (authority_out == NULL) {
		result.status = SG_HOST_LAW_INVALID_ARGUMENT;
		return result;
	}
	authority_out->version = SG_HOST_LAW_RUNTIME_AUTHORITY_VERSION;
	authority_out->epoch = source_host_epoch;
	authority_out->epoch_complement = ~source_host_epoch;
	if (production_model != NULL)
		authority_out->view.static_identity.physics_abi_id =
			production_model->identity.physics_abi_id;
	result.status = SG_HOST_LAW_OK;
	return result;
}

sg_host_law_result_t SG_HostLawProductionAuthorityCurrent(
	const sg_host_law_runtime_authority_t *authority)
{
	sg_host_law_result_t result;

	memset(&result, 0, sizeof(result));
	if (!source_host_current || authority == NULL ||
		authority->version != SG_HOST_LAW_RUNTIME_AUTHORITY_VERSION ||
		authority->epoch != source_host_epoch ||
		authority->epoch_complement != ~source_host_epoch)
	{
		result.status = SG_HOST_LAW_PRODUCTION_DRIFT;
		return result;
	}
	result.status = SG_HOST_LAW_OK;
	return result;
}

int SG_RuneCompactSpatialIndexBuildTopology(
	const sg_rune_compact_spatial_topology_input_t *topology,
	const sg_rune_compact_spatial_allocator_t *allocator,
	sg_rune_compact_spatial_index_t **index_out,
	sg_rune_compact_spatial_error_t *error_out)
{
	(void)allocator;
	if (error_out != NULL)
		memset(error_out, 0, sizeof(*error_out));
	if (topology == NULL || topology->cell_count == 0U || index_out == NULL ||
		*index_out != NULL)
		return 0;
	*index_out = (sg_rune_compact_spatial_index_t *)(uintptr_t)1U;
	return 1;
}

void SG_RuneCompactSpatialIndexDestroy(sg_rune_compact_spatial_index_t *index)
{
	(void)index;
}

const sg_compact_localization_observation_owner_t *
SG_BotLocalizationObservationOwner(void)
{
	return &production_observation;
}

sg_compact_runtime_level_status_t SG_CompactRuntimeLevelInstall(
	sg_compact_runtime_level_t *runtime,
	const sg_rune_compact_model_t *accepted_model,
	const sg_rune_compact_identity_t *expected_identity,
	const sg_rune_compact_spatial_index_t *spatial_index,
	const sg_compact_localization_observation_owner_t *candidate_owner,
	const sg_host_law_runtime_authority_t *host_authority,
	uint64_t rune_identity, uint64_t topology_revision)
{
	if (runtime == NULL || accepted_model != production_model ||
		expected_identity == NULL || spatial_index == NULL ||
		candidate_owner != &production_observation || host_authority == NULL ||
		rune_identity == 0U || topology_revision == 0U)
		return SG_COMPACT_RUNTIME_LEVEL_INVALID_ARGUMENT;
	runtime->active = 1U;
	runtime->field_service =
		(sg_rune_compact_field_service_t *)(uintptr_t)1U;
	return SG_COMPACT_RUNTIME_LEVEL_OK;
}

void SG_CompactRuntimeLevelClear(sg_compact_runtime_level_t *runtime)
{
	if (runtime != NULL)
		memset(runtime, 0, sizeof(*runtime));
}

int SG_CompactRuntimeLevelCurrent(const sg_compact_runtime_level_t *runtime)
{
	return runtime != NULL && runtime->active == 1U;
}

const sg_rune_compact_field_service_t *SG_CompactRuntimeLevelFieldService(
	const sg_compact_runtime_level_t *runtime)
{
	return runtime != NULL && runtime->active == 1U ?
		runtime->field_service : NULL;
}

int SG_RuneCompactPortalSnapshotSourcePrepare(
	const sg_rune_compact_model_t *accepted_model,
	sg_rune_compact_portal_snapshot_source_t **source_out)
{
	if (accepted_model != production_model || source_out == NULL ||
		*source_out != NULL)
		return 0;
	*source_out = (sg_rune_compact_portal_snapshot_source_t *)(uintptr_t)1U;
	return 1;
}

int SG_RuneCompactPortalSnapshotSourceCurrent(
	const sg_rune_compact_portal_snapshot_source_t *source,
	const sg_rune_compact_model_t *accepted_model)
{
	return source != NULL && accepted_model == production_model;
}

const sg_bsp_entity_semantics_t *
SG_RuneCompactPortalSnapshotSourceEffectiveSemantics(
	const sg_rune_compact_portal_snapshot_source_t *source)
{
	(void)source;
	return NULL;
}

void SG_RuneCompactPortalSnapshotSourceDestroy(
	sg_rune_compact_portal_snapshot_source_t *source)
{
	(void)source;
}

int SG_RuneCompactPortalSnapshotCreate(const sg_rune_compact_model_t *model,
	const sg_rune_compact_field_service_t *service,
	const sg_bsp_entity_semantics_t *effective,
	sg_rune_compact_portal_snapshot_t **snapshot_out)
{
	(void)effective;
	if (model != production_model || service == NULL || snapshot_out == NULL ||
		*snapshot_out != NULL)
		return 0;
	*snapshot_out = (sg_rune_compact_portal_snapshot_t *)(uintptr_t)1U;
	return 1;
}

void SG_RuneCompactPortalSnapshotDestroy(
	sg_rune_compact_portal_snapshot_t *snapshot)
{
	(void)snapshot;
}

int SG_RuneCompactPortalSnapshotBindEffective(
	sg_rune_compact_portal_snapshot_t *snapshot,
	const sg_bsp_entity_semantics_t *effective)
{
	(void)effective;
	return snapshot != NULL;
}

int SG_RuneCompactPortalSnapshotPublish(
	sg_rune_compact_portal_snapshot_t *snapshot, uint64_t frame_sequence,
	sg_rune_compact_portal_snapshot_frame_t *frame_out)
{
	if (snapshot == NULL || frame_sequence == 0U || frame_out == NULL)
		return 0;
	memset(frame_out, 0, sizeof(*frame_out));
	return 1;
}

sg_identity_status_t SG_LevelIdentitySnapshot(const char *expected_mapname,
	sg_level_identity_t *out)
{
	if (out == NULL || expected_mapname == NULL)
		return SG_IDENTITY_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	if (identity_snapshot_status != SG_IDENTITY_OK)
		return identity_snapshot_status;
	if (identity_snapshot_override != NULL) {
		if (strcmp(expected_mapname, identity_snapshot_override->mapname) != 0)
			return SG_IDENTITY_MAPNAME_MISMATCH;
		*out = *identity_snapshot_override;
		return SG_IDENTITY_OK;
	}
	if (active_trace == NULL)
		return SG_IDENTITY_UNAVAILABLE;
	if (strcmp(expected_mapname, active_trace->identity.mapname) != 0)
		return SG_IDENTITY_MAPNAME_MISMATCH;
	*out = active_trace->identity;
	return SG_IDENTITY_OK;
}

int SG_HumanTraceVisitAcceptedV3Collection(
	const sg_level_identity_t *identity,
	const sg_human_trace_v3_collection_visitor_t *visitor, void *context)
{
	trace_fixture_t *trace = active_trace;
	uint8_t scope_seen[2] = { 0U, 0U };
	uint32_t index;
	uint32_t scope_index;

	if (trace == NULL || identity == NULL || visitor == NULL ||
		!trace->visit_result || !visitor->begin_root || !visitor->segment ||
		!visitor->scope || !visitor->event || !visitor->finish_root)
		return 0;
#if defined(SG_RUNE_COMPACT_LEARNING_HOST_LIFECYCLE_TEST)
	if (host_lifecycle_active && !host_trace_terminal)
		return 0;
	if (host_lifecycle_active)
		HostEvent(HOST_EVENT_POST_MATCH);
#endif
	if (!visitor->begin_root(context, &trace->root) ||
		!visitor->segment(context, &trace->segment))
		return 0;
	for (index = 0U; index < trace->event_count; index++) {
		scope_index = trace->event_scope[index];
		if (scope_index >= 2U)
			return 0;
		if (!scope_seen[scope_index]) {
			scope_seen[scope_index] = 1U;
			if (!visitor->scope(context, &trace->scopes[scope_index]))
				return 0;
		}
		if (!visitor->event(context, &trace->scopes[scope_index],
			&trace->segment, &trace->events[index]))
			return 0;
	}
	return visitor->finish_root(context);
}

sg_human_trace_visit_status_t SG_HumanTraceVisitAcceptedV3CollectionStatus(
	const sg_level_identity_t *identity,
	const sg_human_trace_v3_collection_visitor_t *visitor, void *context)
{
	if (active_trace != NULL && active_trace->visit_status !=
		SG_HUMAN_TRACE_VISIT_OK)
		return active_trace->visit_status;
	return SG_HumanTraceVisitAcceptedV3Collection(identity, visitor, context)
		? SG_HUMAN_TRACE_VISIT_OK : SG_HUMAN_TRACE_VISIT_INVALID_COLLECTION;
}

int SG_HumanTraceAcceptedV3ScopeView(
	const sg_human_trace_v3_scope_acceptance_t *scope,
	const sg_human_trace_v3_spool_ref_t **root_out,
	uint32_t *client_id_out, uint64_t *spawn_generation_out)
{
	trace_fixture_t *trace = active_trace;

	if (root_out != NULL)
		*root_out = NULL;
	if (client_id_out != NULL)
		*client_id_out = 0U;
	if (spawn_generation_out != NULL)
		*spawn_generation_out = 0U;
	if (trace == NULL ||
		(scope != &trace->scopes[0] && scope != &trace->scopes[1]) ||
		!trace->scope_view_result)
		return 0;
	if (root_out != NULL)
		*root_out = &trace->root;
	if (client_id_out != NULL)
		*client_id_out = scope == &trace->scopes[0] ? 7U : 8U;
	if (spawn_generation_out != NULL)
		*spawn_generation_out = scope == &trace->scopes[0] ?
			UINT64_C(11) : UINT64_C(12);
	return 1;
}

typedef struct validator_context_s
{
	uint32_t calls;
	uint32_t segment_calls;
	uint64_t last_order;
	int fail_at;
} validator_context_t;

static sg_rune_compact_learning_consumer_validation_t ValidateEvent(
	void *opaque, const sg_rune_compact_model_t *model,
	const sg_human_trace_v3_segment_ref_t *segment,
	const sg_human_trace_v3_event_t *event,
	sg_rune_compact_learning_consumer_claim_t *claim_out)
{
	validator_context_t *context = opaque;

	if (context == NULL || model == NULL || segment == NULL || event == NULL ||
		claim_out == NULL)
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_VALIDATION_FATAL;
	context->calls++;
	context->segment_calls++;
	context->last_order = event->order;
	if (context->fail_at >= 0 &&
		context->calls == (uint32_t)context->fail_at)
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_VALIDATION_FATAL;
	if (segment->gravity_bits != model->identity.physics.gravity_bits ||
		segment->airaccelerate_bits !=
			model->identity.physics.air_acceleration_bits ||
		segment->maxvelocity_bits != model->identity.physics.max_velocity_bits ||
		segment->pmove_substep_ms != model->identity.physics.substep_ms ||
		segment->server_frame_ms != model->identity.physics.frame_ms)
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_VALIDATION_FATAL;
	memset(claim_out, 0, sizeof(*claim_out));
	switch (event->kind) {
	case SG_HUMAN_TRACE_V3_EVENT_STEP:
		claim_out->key.kind =
			SG_RUNE_COMPACT_LEARNING_STABLE_CELL_CAPABILITY_COST;
		claim_out->key.value.cost.cell.value = 0U;
		claim_out->key.value.cost.capability.value = 0U;
		claim_out->key.value.cost.stance =
			SG_RUNE_STANCE_VALID_STANDING;
		claim_out->value = 2.0f;
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_VALIDATION_ACCEPT;
	case SG_HUMAN_TRACE_V3_EVENT_HOOK_ATTACH:
		claim_out->key.kind =
			SG_RUNE_COMPACT_LEARNING_LANDING_PREFERENCE;
		claim_out->key.value.landing.cell.value = 1U;
		claim_out->key.value.landing.stance = SG_RUNE_STANCE_VALID_STANDING;
		claim_out->value = 0.5f;
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_VALIDATION_ACCEPT;
	case SG_HUMAN_TRACE_V3_EVENT_HOOK_RELEASE:
		claim_out->key.kind = SG_RUNE_COMPACT_LEARNING_TACTICAL_PRIOR;
		claim_out->key.value.tactical.cell.value = 0U;
		claim_out->key.value.tactical.weapon_kernel = 0U;
		claim_out->value = 0.25f;
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_VALIDATION_ACCEPT;
	case SG_HUMAN_TRACE_V3_EVENT_HOOK_RESET:
		claim_out->key.kind = SG_RUNE_COMPACT_LEARNING_STRATEGY_OUTCOME;
		claim_out->key.value.strategy.landmark.value = 0U;
		claim_out->key.value.strategy.outcome =
			SG_RUNE_COMPACT_LEARNING_STRATEGY_SUCCEEDED;
		claim_out->value = 1.0f;
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_VALIDATION_ACCEPT;
	case SG_HUMAN_TRACE_V3_EVENT_HOOK_FIRE:
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_VALIDATION_SKIP;
	case SG_HUMAN_TRACE_V3_EVENT_KIND_COUNT:
		break;
	}
	return SG_RUNE_COMPACT_LEARNING_CONSUMER_VALIDATION_FATAL;
}

static sg_rune_compact_learning_consumer_validation_t SkipEvent(
	void *opaque, const sg_rune_compact_model_t *model,
	const sg_human_trace_v3_segment_ref_t *segment,
	const sg_human_trace_v3_event_t *event,
	sg_rune_compact_learning_consumer_claim_t *claim_out)
{
	validator_context_t *context = opaque;

	(void)model;
	(void)segment;
	(void)event;
	(void)claim_out;
	if (context != NULL)
		context->calls++;
	return SG_RUNE_COMPACT_LEARNING_CONSUMER_VALIDATION_SKIP;
}

static sg_rune_compact_learning_consumer_validation_t InvalidCapability(
	void *opaque, const sg_rune_compact_model_t *model,
	const sg_human_trace_v3_segment_ref_t *segment,
	const sg_human_trace_v3_event_t *event,
	sg_rune_compact_learning_consumer_claim_t *claim_out)
{
	(void)opaque;
	(void)model;
	(void)segment;
	(void)event;
	memset(claim_out, 0, sizeof(*claim_out));
	claim_out->key.kind =
		SG_RUNE_COMPACT_LEARNING_STABLE_CELL_CAPABILITY_COST;
	claim_out->key.value.cost.cell.value = 0U;
	claim_out->key.value.cost.capability.value = 2U;
	claim_out->key.value.cost.stance = SG_RUNE_STANCE_VALID_STANDING;
	claim_out->value = 1.0f;
	return SG_RUNE_COMPACT_LEARNING_CONSUMER_VALIDATION_ACCEPT;
}

static void EventInit(sg_human_trace_v3_event_t *event,
	sg_human_trace_v3_event_kind_t kind, uint64_t order)
{
	memset(event, 0, sizeof(*event));
	event->kind = kind;
	event->order = order;
	event->client_id = 7U;
	event->spawn_generation = UINT64_C(11);
	event->grounded = 1U;
	if (kind == SG_HUMAN_TRACE_V3_EVENT_STEP) {
		event->command = UINT64_C(100) + order;
		event->command_msec = 25U;
		event->step_evidence =
			SG_HUMAN_TRACE_V3_STEP_EVIDENCE_ORDINARY_DRY_WALK;
	} else {
		event->hook_event = order;
		event->hook_entity = 1;
	}
}

static void TraceInit(const sg_rune_compact_model_t *model,
	trace_fixture_t *trace, uint32_t event_count)
{
	memset(trace, 0, sizeof(*trace));
	CHECK(SG_TestCompactLearningConsumerParseModelIdentity(model,
		&trace->identity));
	CHECK(SG_TestCompactLearningConsumerParseModelIdentity(model,
		&trace->segment.identity));
	trace->segment.session = 9U;
	trace->segment.segment = 0U;
	trace->segment.physics_id = 0U;
	trace->segment.gravity_bits = model->identity.physics.gravity_bits;
	trace->segment.airaccelerate_bits =
		model->identity.physics.air_acceleration_bits;
	trace->segment.maxvelocity_bits =
		model->identity.physics.max_velocity_bits;
	trace->segment.pmove_substep_ms =
		(uint16_t)model->identity.physics.substep_ms;
	trace->segment.server_frame_ms =
		(uint16_t)model->identity.physics.frame_ms;
	trace->segment.module_revision = 1U;
	memcpy(trace->segment.module_version, "test-module",
		sizeof("test-module"));
	trace->segment.start_order = 10U;
	trace->segment.start_command = 110U;
	trace->segment.start_hook_event = 10U;
	trace->segment.header_sha256[0] = 0x42U;
	trace->root.root_segment = 0U;
	trace->root.completion.session = 9U;
	trace->root.completion.segment = 0U;
	trace->root.completion.continuation = 0U;
	memcpy(trace->root.completion.mapname, trace->identity.mapname,
		SG_LEVEL_IDENTITY_MAPNAME_BYTES);
	trace->root.completion.bsp_checksum = trace->identity.bsp_checksum;
	trace->root.completion.entity_crc32 = trace->identity.entity_crc32;
	trace->root.completion.bsp_bytes = trace->identity.bsp_bytes;
	memcpy(trace->root.completion.bsp_sha256, trace->identity.bsp_sha256,
		SG_LEVEL_BSP_SHA256_BYTES);
	trace->root.completion.host_physics_id = trace->identity.host_physics_id;
	trace->root.completion.gravity_bits = trace->segment.gravity_bits;
	trace->root.completion.airaccelerate_bits =
		trace->segment.airaccelerate_bits;
	trace->root.completion.maxvelocity_bits = trace->segment.maxvelocity_bits;
	trace->root.completion.pmove_substep_ms =
		trace->segment.pmove_substep_ms;
	trace->root.completion.server_frame_ms =
		trace->segment.server_frame_ms;
	trace->root.completion.module_revision = trace->segment.module_revision;
	memcpy(trace->root.completion.module_version,
		trace->segment.module_version, SG_HUMAN_TRACE_VERSION_BYTES);
	trace->root.completion.terminal_sha256[0] = 0x24U;
	trace->event_count = event_count;
	trace->visit_result = 1;
	trace->scope_view_result = 1;
	trace->scopes[0].token = 1U;
	trace->scopes[1].token = 2U;
}

static void TraceEvents(trace_fixture_t *trace)
{
	EventInit(&trace->events[0], SG_HUMAN_TRACE_V3_EVENT_STEP, 10U);
	EventInit(&trace->events[1], SG_HUMAN_TRACE_V3_EVENT_HOOK_ATTACH, 11U);
	EventInit(&trace->events[2], SG_HUMAN_TRACE_V3_EVENT_HOOK_RELEASE, 12U);
	EventInit(&trace->events[3], SG_HUMAN_TRACE_V3_EVENT_HOOK_RESET, 13U);
	trace->event_scope[0] = 0U;
	trace->event_scope[1] = 1U;
	trace->event_scope[2] = 0U;
	trace->event_scope[3] = 1U;
	trace->events[1].client_id = 8U;
	trace->events[1].spawn_generation = UINT64_C(12);
	trace->events[3].client_id = 8U;
	trace->events[3].spawn_generation = UINT64_C(12);
	trace->root.completion.end_order = trace->event_count == 0U ? 1U :
		trace->events[trace->event_count - 1U].order;
}

static void ConfigureModelForProductionAuthority(sg_rune_compact_model_t *model)
{
	sg_rune_source_weapon_law_t law;
	sg_weapon_profile_t profiles[
		SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT];

	CHECK(model != NULL);
	if (model == NULL)
		return;
	source_ctfflags.value = 0.0f;
	source_deathmatch.value = 1.0f;
	source_fastswitch.value = 0.0f;
	matchstate = MATCH_NONE;
	source_host_current = 1;
	source_host_epoch++;
	if (source_host_epoch == 0U)
		source_host_epoch = 1U;
	memset(&law, 0, sizeof(law));
	law.weapon_balance_compiled = (uint8_t)SG_WEAPON_BALANCE_COMPILED;
	law.deathmatch_active = 1U;
	CHECK(SG_RuneCompactWeaponProfilesResolve(&law,
		model->identity.physics_abi_id, profiles,
		SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT));
	CHECK(SG_RuneCompactWeaponLawIdentity(&law, profiles,
		SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT,
		&model->identity.weapon_law_id));
	model->identity.producer_identity =
		SG_RUNE_COMPACT_WEAPON_PRODUCER_ID;
#if !defined(SG_RUNE_COMPACT_LEARNING_HOST_LIFECYCLE_TEST)
	memset(&source_fixture_snapshot, 0, sizeof(source_fixture_snapshot));
	source_fixture_snapshot.version = SG_RUNE_SOURCE_AUTHORITY_VERSION;
	source_fixture_snapshot.publication_generation = source_host_epoch;
	source_fixture_snapshot.publication_generation_complement =
		~source_host_epoch;
	source_fixture_snapshot.host_authority.version =
		SG_HOST_LAW_RUNTIME_AUTHORITY_VERSION;
	source_fixture_snapshot.host_authority.epoch = source_host_epoch;
	source_fixture_snapshot.host_authority.epoch_complement = ~source_host_epoch;
	source_fixture_snapshot.host_authority.view.static_identity.physics_abi_id =
		model->identity.physics_abi_id;
	source_fixture_snapshot.weapon_law = law;
#endif
}

#if defined(SG_RUNE_COMPACT_LEARNING_HOST_LIFECYCLE_TEST)
static const char source_fixture_entities[] =
	"{\n\"classname\" \"worldspawn\"\n}\n";

static void ConfigureModelForSourceAuthority(sg_rune_compact_model_t *model)
{
	uint32_t entity_crc32 = 0U;

	ConfigureModelForProductionAuthority(model);
	CHECK(SG_CRC32Buffer(source_fixture_entities,
		strlen(source_fixture_entities), &entity_crc32));
	if (model != NULL)
		model->identity.entity_crc32 = entity_crc32;
}

static void PublishSourceAuthorityForCurrentLevel(void)
{
	CHECK(active_trace != NULL);
	if (active_trace == NULL)
		return;
	SG_RuneSourceAuthorityReset();
	CHECK(SG_RuneSourceAuthorityBegin(active_trace->identity.mapname,
		source_fixture_entities) == SG_RUNE_SOURCE_OK);
	CHECK(SG_RuneSourceAuthorityRecord(0U, 0) == SG_RUNE_SOURCE_OK);
	CHECK(SG_RuneSourceAuthorityPublish(active_trace->identity.mapname) ==
		SG_RUNE_SOURCE_OK);
}
#endif

static sg_rune_compact_learning_consumer_status_t Ingest(
	sg_rune_compact_learning_consumer_t *consumer, trace_fixture_t *trace,
	sg_rune_compact_learning_consumer_validate_fn validate, void *context,
	sg_rune_compact_learning_consumer_report_t *report)
{
	sg_rune_compact_learning_consumer_status_t status;

	active_trace = trace;
	status = SG_RuneCompactLearningConsumerIngestAcceptedV3Collection(consumer,
		&trace->identity, validate, context, report);
	active_trace = NULL;
	return status;
}

static sg_rune_compact_learning_consumer_status_t IngestCurrent(
	sg_rune_compact_learning_consumer_t *consumer, trace_fixture_t *trace,
	sg_rune_compact_learning_consumer_validate_fn validate, void *context,
	sg_rune_compact_learning_consumer_report_t *report)
{
	sg_rune_compact_learning_consumer_status_t status;

	active_trace = trace;
	status = SG_RuneCompactLearningConsumerIngestCurrentV3Collection(consumer,
		trace->identity.mapname, validate, context, report);
	active_trace = NULL;
	return status;
}

static sg_rune_compact_learning_consumer_t *CreateConsumer(
	const sg_rune_compact_model_t *model)
{
	sg_rune_compact_learning_consumer_t *consumer = NULL;
	sg_rune_compact_error_t error;

	CHECK(SG_RuneCompactLearningConsumerCreate(model, &model->identity,
		&consumer, &error) ==
		SG_RUNE_COMPACT_LEARNING_CONSUMER_OK);
	CHECK(consumer != NULL);
	return consumer;
}

static sg_rune_compact_learning_prior_t ReadPrior(
	const sg_rune_compact_learning_consumer_t *consumer, uint32_t index)
{
	sg_rune_compact_learning_prior_t prior;

	memset(&prior, 0, sizeof(prior));
	CHECK(SG_RuneCompactLearningConsumerPriorRead(consumer, index, &prior));
	return prior;
}

static void TestAcceptedCollectionTransaction(void)
{
	sg_rune_compact_model_t *model;
	trace_fixture_t trace;
	sg_rune_compact_learning_consumer_t *consumer;
	sg_rune_compact_learning_consumer_report_t report;
	validator_context_t context;

	model = SG_TestCompactLearningConsumerModel();
	TraceInit(model, &trace, 4U);
	TraceEvents(&trace);
	consumer = CreateConsumer(model);
	memset(&context, 0, sizeof(context));
	context.fail_at = -1;
	memset(&report, 0, sizeof(report));
	CHECK(IngestCurrent(consumer, &trace, ValidateEvent, &context, &report) ==
		SG_RUNE_COMPACT_LEARNING_CONSUMER_OK);
	CHECK(context.calls == 4U && context.segment_calls == 4U &&
		context.last_order == UINT64_C(13));
	CHECK(report.event_count == 4U && report.validated_count == 4U &&
		report.skipped_count == 0U && report.applied_count == 4U &&
		report.prior_count_before == 0U && report.prior_count_after == 4U);
	CHECK(SG_RuneCompactLearningConsumerPriorCount(consumer) == 4U);
	CHECK(ReadPrior(consumer, 0U).key.kind ==
		SG_RUNE_COMPACT_LEARNING_STABLE_CELL_CAPABILITY_COST);
	CHECK(ReadPrior(consumer, 1U).key.kind ==
		SG_RUNE_COMPACT_LEARNING_LANDING_PREFERENCE);
	CHECK(ReadPrior(consumer, 2U).key.kind ==
		SG_RUNE_COMPACT_LEARNING_TACTICAL_PRIOR);
	CHECK(ReadPrior(consumer, 3U).key.kind ==
		SG_RUNE_COMPACT_LEARNING_STRATEGY_OUTCOME);
	CHECK(ReadPrior(consumer, 0U).value_total_q16 == UINT64_C(131072));
	CHECK(ReadPrior(consumer, 1U).value_total_q16 == UINT64_C(32768));
	CHECK(ReadPrior(consumer, 2U).value_total_q16 == UINT64_C(16384));
	CHECK(ReadPrior(consumer, 3U).value_total_q16 == UINT64_C(65536));
	memset(&context, 0, sizeof(context));
	context.fail_at = -1;
	memset(&report, 0, sizeof(report));
	CHECK(Ingest(consumer, &trace, ValidateEvent, &context, &report) ==
		SG_RUNE_COMPACT_LEARNING_CONSUMER_OK);
	CHECK(context.calls == 0U && report.event_count == 0U &&
		report.validated_count == 0U && report.skipped_count == 0U &&
		report.applied_count == 0U &&
		report.prior_count_before == 4U && report.prior_count_after == 4U &&
		ReadPrior(consumer, 0U).human_samples == 1U &&
		ReadPrior(consumer, 0U).value_total_q16 == UINT64_C(131072));
	SG_RuneCompactLearningConsumerDestroy(consumer);
}

static void TestSkippedEventsAndNoOp(void)
{
	sg_rune_compact_model_t *model;
	trace_fixture_t trace;
	sg_rune_compact_learning_consumer_t *consumer;
	sg_rune_compact_learning_consumer_report_t report;
	validator_context_t context;

	model = SG_TestCompactLearningConsumerModel();
	TraceInit(model, &trace, 1U);
	TraceEvents(&trace);
	EventInit(&trace.events[0], SG_HUMAN_TRACE_V3_EVENT_HOOK_RESET, 10U);
	trace.root.completion.end_order = 10U;
	consumer = CreateConsumer(model);
	memset(&context, 0, sizeof(context));
	memset(&report, 0, sizeof(report));
	CHECK(Ingest(consumer, &trace, SkipEvent, &context, &report) ==
		SG_RUNE_COMPACT_LEARNING_CONSUMER_OK);
	CHECK(context.calls == 1U && report.event_count == 1U &&
		report.skipped_count == 1U && report.applied_count == 0U &&
		report.prior_count_after == 0U);
	TraceInit(model, &trace, 0U);
	TraceEvents(&trace);
	memset(&report, 0, sizeof(report));
	CHECK(Ingest(consumer, &trace, SkipEvent, &context, &report) ==
		SG_RUNE_COMPACT_LEARNING_CONSUMER_OK);
	CHECK(report.event_count == 0U && report.prior_count_before == 0U &&
		report.prior_count_after == 0U);
	SG_RuneCompactLearningConsumerDestroy(consumer);
}

static void TestIdentityLifeOrderPhysicsAndAtomicFailure(void)
{
	sg_rune_compact_model_t *model;
	trace_fixture_t trace;
	sg_rune_compact_learning_consumer_t *consumer;
	sg_rune_compact_learning_consumer_report_t report;
	sg_rune_compact_learning_consumer_report_t unchanged_report;
	validator_context_t context;

	model = SG_TestCompactLearningConsumerModel();
	TraceInit(model, &trace, 2U);
	TraceEvents(&trace);
	consumer = CreateConsumer(model);
	memset(&report, 0x4c, sizeof(report));
	unchanged_report = report;
	trace.identity.entity_crc32 ^= 1U;
	CHECK(Ingest(consumer, &trace, ValidateEvent, NULL, &report) ==
		SG_RUNE_COMPACT_LEARNING_CONSUMER_IDENTITY_MISMATCH);
	CHECK(SG_RuneCompactLearningConsumerPriorCount(consumer) == 0U &&
		memcmp(&report, &unchanged_report, sizeof(report)) == 0);
	TraceInit(model, &trace, 2U);
	TraceEvents(&trace);
	identity_snapshot_status = SG_IDENTITY_UNAVAILABLE;
	CHECK(IngestCurrent(consumer, &trace, ValidateEvent, NULL, &report) ==
		SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_TRACE);
	CHECK(SG_RuneCompactLearningConsumerPriorCount(consumer) == 0U &&
		memcmp(&report, &unchanged_report, sizeof(report)) == 0);
	identity_snapshot_status = SG_IDENTITY_MAPNAME_MISMATCH;
	CHECK(IngestCurrent(consumer, &trace, ValidateEvent, NULL, &report) ==
		SG_RUNE_COMPACT_LEARNING_CONSUMER_IDENTITY_MISMATCH);
	CHECK(SG_RuneCompactLearningConsumerPriorCount(consumer) == 0U &&
		memcmp(&report, &unchanged_report, sizeof(report)) == 0);
	identity_snapshot_status = SG_IDENTITY_OK;
	TraceInit(model, &trace, 2U);
	TraceEvents(&trace);
	trace.events[1].spawn_generation++;
	memset(&context, 0, sizeof(context));
	context.fail_at = -1;
	CHECK(Ingest(consumer, &trace, ValidateEvent, &context, &report) ==
		SG_RUNE_COMPACT_LEARNING_CONSUMER_LIFE_MISMATCH);
	CHECK(SG_RuneCompactLearningConsumerPriorCount(consumer) == 0U &&
		memcmp(&report, &unchanged_report, sizeof(report)) == 0);
	TraceInit(model, &trace, 2U);
	TraceEvents(&trace);
	trace.events[1].order = trace.events[0].order;
	memset(&context, 0, sizeof(context));
	context.fail_at = -1;
	CHECK(Ingest(consumer, &trace, ValidateEvent, &context, &report) ==
		SG_RUNE_COMPACT_LEARNING_CONSUMER_ORDER_MISMATCH);
	CHECK(SG_RuneCompactLearningConsumerPriorCount(consumer) == 0U &&
		memcmp(&report, &unchanged_report, sizeof(report)) == 0);
	TraceInit(model, &trace, 2U);
	TraceEvents(&trace);
	trace.segment.identity.entity_crc32 ^= 1U;
	CHECK(Ingest(consumer, &trace, ValidateEvent, NULL, &report) ==
		SG_RUNE_COMPACT_LEARNING_CONSUMER_IDENTITY_MISMATCH);
	CHECK(SG_RuneCompactLearningConsumerPriorCount(consumer) == 0U &&
		memcmp(&report, &unchanged_report, sizeof(report)) == 0);
	TraceInit(model, &trace, 2U);
	TraceEvents(&trace);
	trace.segment.gravity_bits ^= 1U;
	CHECK(Ingest(consumer, &trace, ValidateEvent, NULL, &report) ==
		SG_RUNE_COMPACT_LEARNING_CONSUMER_PHYSICS_MISMATCH);
	CHECK(SG_RuneCompactLearningConsumerPriorCount(consumer) == 0U &&
		memcmp(&report, &unchanged_report, sizeof(report)) == 0);
	TraceInit(model, &trace, 2U);
	TraceEvents(&trace);
	trace.scope_view_result = 0;
	CHECK(Ingest(consumer, &trace, ValidateEvent, NULL, &report) ==
		SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_TRACE);
	CHECK(SG_RuneCompactLearningConsumerPriorCount(consumer) == 0U &&
		memcmp(&report, &unchanged_report, sizeof(report)) == 0);
	TraceInit(model, &trace, 2U);
	TraceEvents(&trace);
	trace.visit_status = SG_HUMAN_TRACE_VISIT_ALLOCATION_FAILED;
	memset(&report, 0x4c, sizeof(report));
	unchanged_report = report;
	CHECK(Ingest(consumer, &trace, ValidateEvent, NULL, &report) ==
		SG_RUNE_COMPACT_LEARNING_CONSUMER_ALLOCATION_FAILED);
	CHECK(SG_RuneCompactLearningConsumerPriorCount(consumer) == 0U &&
		memcmp(&report, &unchanged_report, sizeof(report)) == 0);
	TraceInit(model, &trace, 2U);
	TraceEvents(&trace);
	memset(&context, 0, sizeof(context));
	context.fail_at = 2;
	CHECK(Ingest(consumer, &trace, ValidateEvent, &context, &report) ==
		SG_RUNE_COMPACT_LEARNING_CONSUMER_ENGINE_REJECTED);
	CHECK(SG_RuneCompactLearningConsumerPriorCount(consumer) == 0U &&
		memcmp(&report, &unchanged_report, sizeof(report)) == 0);
	SG_RuneCompactLearningConsumerDestroy(consumer);
}

static void TestInvalidCapabilityRejected(void)
{
	sg_rune_compact_model_t *model;
	trace_fixture_t trace;
	sg_rune_compact_learning_consumer_t *consumer;
	sg_rune_compact_learning_consumer_report_t report;
	sg_rune_compact_error_t error;

	model = SG_TestCompactLearningConsumerModel();
	CHECK(SG_RuneCompactModelValidate(model, &error));
	TraceInit(model, &trace, 1U);
	TraceEvents(&trace);
	consumer = CreateConsumer(model);
	memset(&report, 0x71, sizeof(report));
	CHECK(Ingest(consumer, &trace, InvalidCapability, NULL, &report) ==
		SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_REFERENCE);
	CHECK(SG_RuneCompactLearningConsumerPriorCount(consumer) == 0U &&
		report.event_count == UINT32_C(0x71717171));
	SG_RuneCompactLearningConsumerDestroy(consumer);
}

#if !defined(SG_RUNE_COMPACT_LEARNING_HOST_LIFECYCLE_TEST)
static void TestProductionLearningLifecycle(void)
{
	sg_rune_compact_model_t *model;
	trace_fixture_t trace;
	sg_rune_compact_production_t owner =
		SG_RUNE_COMPACT_PRODUCTION_INITIALIZER;
	sg_rune_compact_learning_consumer_report_t report;
	sg_rune_compact_learning_prior_t prior;
	validator_context_t context;

	model = SG_TestCompactLearningConsumerModel();
	ConfigureModelForProductionAuthority(model);
	TraceInit(model, &trace, 4U);
	TraceEvents(&trace);
	production_model = model;
	memset(&production_observation, 0, sizeof(production_observation));
	CHECK(SG_RuneCompactProductionInit(&owner).status ==
		SG_RUNE_COMPACT_PRODUCTION_OK);
	CHECK(SG_RuneCompactProductionLoad(&owner, "model.rune").status ==
		SG_RUNE_COMPACT_PRODUCTION_OK);
	CHECK(SG_RuneCompactProductionCurrent(&owner) && owner.learning != NULL);
	memset(&context, 0, sizeof(context));
	context.fail_at = -1;
	memset(&report, 0, sizeof(report));
	active_trace = &trace;
	CHECK(SG_RuneCompactLearningProductionIngest(&owner,
		trace.identity.mapname, ValidateEvent, &context, &report) ==
		SG_RUNE_COMPACT_LEARNING_CONSUMER_OK);
	active_trace = NULL;
	CHECK(report.applied_count == 4U &&
		SG_RuneCompactLearningProductionPriorCount(&owner) == 4U);
	memset(&prior, 0, sizeof(prior));
	CHECK(SG_RuneCompactLearningProductionPriorRead(&owner, 0U, &prior));
	CHECK(prior.key.kind ==
		SG_RUNE_COMPACT_LEARNING_STABLE_CELL_CAPABILITY_COST);
	SG_RuneCompactProductionClear(&owner);
	CHECK(!SG_RuneCompactProductionCurrent(&owner) && owner.learning == NULL);
	CHECK(SG_RuneCompactLearningProductionPriorCount(&owner) == 0U);
	CHECK(!SG_RuneCompactLearningProductionPriorRead(&owner, 0U, &prior));
}
#endif

#if defined(SG_RUNE_COMPACT_LEARNING_HOST_LIFECYCLE_TEST)
static void RunHostLearningCycle(edict_t *change_target,
	uint32_t expected_prior_count)
{
	level.intermissiontime = 0.0f;
	level.time += 1.0f;
	host_expected_priors_at_save = expected_prior_count;
	PublishSourceAuthorityForCurrentLevel();
	HostResetEvents();
	CHECK(SG_LevelSetup());
	CHECK(SG_CompactProductionLearningPriorCount() == 0U);
	BeginIntermission(change_target);
	CHECK(host_event_count >= 2U &&
		host_events[0] == HOST_EVENT_TRACE_END &&
		host_events[1] == HOST_EVENT_POST_MATCH);
	CHECK(SG_CompactProductionLearningPriorCount() == expected_prior_count);
	HostResetEvents();
	SG_LevelTransitionSaveOutgoingState();
	CHECK(host_event_count == 3U &&
		host_events[0] == HOST_EVENT_SAVE_CLIENTS &&
		host_events[1] == HOST_EVENT_SOURCE_RESET &&
		host_events[2] == HOST_EVENT_IDENTITY_RESET);
	CHECK(SG_CompactProductionLearningPriorCount() == 0U);
}

static void TestHostLifecycle(void)
{
	sg_rune_compact_model_t *model;
	sg_rune_compact_model_t replacement_model;
	trace_fixture_t outgoing_trace;
	sg_level_identity_t replacement_identity;
	edict_t change_target;

	model = SG_TestCompactLearningConsumerModel();
	ConfigureModelForSourceAuthority(model);
	TraceInit(model, &outgoing_trace, 1U);
	TraceEvents(&outgoing_trace);
	outgoing_trace.events[0].after_origin[0] = 96;
	outgoing_trace.events[0].after_origin[1] = 32;
	outgoing_trace.events[0].after_origin[2] = 32;
	production_model = model;
	memset(&production_observation, 0, sizeof(production_observation));
	memset(&game, 0, sizeof(game));
	memset(&level, 0, sizeof(level));
	memset(&change_target, 0, sizeof(change_target));
	memset(&sg_host, 0, sizeof(sg_host));
	sg_host.cvar = HostCvar;
	sg_host.dprint = HostDPrint;
	sg_host.flush = HostFlush;
	(void)snprintf(level.mapname, sizeof(level.mapname), "%s", "lmctf-test");
	level.time = 0.0f;
	change_target.map = "next";
	active_trace = &outgoing_trace;
	host_lifecycle_active = 1;
	outgoing_trace.events[0].grounded = 0U;
	RunHostLearningCycle(&change_target, 0U);
	outgoing_trace.events[0].grounded = 1U;
	outgoing_trace.events[0].step_evidence =
		SG_HUMAN_TRACE_V3_STEP_EVIDENCE_ORDINARY_DRY_WALK &
		~SG_HUMAN_TRACE_V3_STEP_EVIDENCE_NO_HOOK;
	RunHostLearningCycle(&change_target, 0U);
	outgoing_trace.events[0].step_evidence =
		SG_HUMAN_TRACE_V3_STEP_EVIDENCE_ORDINARY_DRY_WALK &
		~SG_HUMAN_TRACE_V3_STEP_EVIDENCE_DRY;
	RunHostLearningCycle(&change_target, 0U);
	outgoing_trace.events[0].step_evidence =
		SG_HUMAN_TRACE_V3_STEP_EVIDENCE_ORDINARY_DRY_WALK &
		~SG_HUMAN_TRACE_V3_STEP_EVIDENCE_MOVED;
	RunHostLearningCycle(&change_target, 0U);
	outgoing_trace.events[0].step_evidence =
		SG_HUMAN_TRACE_V3_STEP_EVIDENCE_ORDINARY_DRY_WALK;
	RunHostLearningCycle(&change_target, 1U);

	replacement_model = *model;
	replacement_model.identity.bsp_sha256[0] ^= UINT8_C(1);
	replacement_model.identity.entity_semantics_id++;
	production_model = &replacement_model;
	identity_snapshot_override = NULL;
	CHECK(SG_TestCompactLearningConsumerParseModelIdentity(&replacement_model,
		&replacement_identity));
	identity_snapshot_override = &replacement_identity;
	PublishSourceAuthorityForCurrentLevel();
	level.intermissiontime = 0.0f;
	level.time += 1.0f;
	HostResetEvents();
	CHECK(SG_LevelSetup());
	CHECK(SG_CompactProductionLearningPriorCount() == 0U);
	/* The preserved outgoing collection belongs to the old compact identity.
	 * The reloaded consumer must reject it rather than revive its priors. */
	BeginIntermission(&change_target);
	CHECK(host_event_count == 2U &&
		host_events[0] == HOST_EVENT_TRACE_END &&
		host_events[1] == HOST_EVENT_POST_MATCH);
	CHECK(SG_CompactProductionLearningPriorCount() == 0U);
	identity_snapshot_override = NULL;
	active_trace = NULL;
	SG_LevelChange();
	host_lifecycle_active = 0;
}
#endif

#if defined(SG_RUNE_COMPACT_LEARNING_CONSUMER_TEST_WRAP_ALLOC)
static int fail_calloc_after = -1;
static int fail_realloc_after = -1;

void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *pointer, size_t size);
void *__wrap_calloc(size_t count, size_t size);
void *__wrap_realloc(void *pointer, size_t size);
void *__wrap_calloc(size_t count, size_t size)
{
	if (fail_calloc_after == 0) {
		fail_calloc_after = -1;
		return NULL;
	}
	if (fail_calloc_after > 0)
		fail_calloc_after--;
	return __real_calloc(count, size);
}
void *__wrap_realloc(void *pointer, size_t size)
{
	if (fail_realloc_after == 0) {
		fail_realloc_after = -1;
		return NULL;
	}
	if (fail_realloc_after > 0)
		fail_realloc_after--;
	return __real_realloc(pointer, size);
}

static void TestAllocationRollback(void)
{
	sg_rune_compact_model_t *model;
	trace_fixture_t trace;
	sg_rune_compact_learning_consumer_t *consumer;
	sg_rune_compact_learning_consumer_t *unchanged_consumer;
	sg_rune_compact_learning_consumer_report_t report;
	validator_context_t context;
	sg_rune_compact_error_t error;

	model = SG_TestCompactLearningConsumerModel();
	TraceInit(model, &trace, 1U);
	TraceEvents(&trace);
	unchanged_consumer =
		(sg_rune_compact_learning_consumer_t *)(void *)&trace;
	/* Model validation builds the analytic-use and response-projection
	 * worklists before the consumer allocation.  OOM there must stay a
	 * consumer allocation failure and must not publish an output pointer. */
	fail_calloc_after = 5;
	CHECK(SG_RuneCompactLearningConsumerCreate(model, &model->identity,
		&unchanged_consumer, &error) ==
		SG_RUNE_COMPACT_LEARNING_CONSUMER_ALLOCATION_FAILED);
	CHECK(unchanged_consumer ==
		(sg_rune_compact_learning_consumer_t *)(void *)&trace);
	fail_calloc_after = -1;
	consumer = CreateConsumer(model);
	memset(&context, 0, sizeof(context));
	context.fail_at = -1;
	memset(&report, 0x3d, sizeof(report));
	fail_calloc_after = 5;
	CHECK(Ingest(consumer, &trace, ValidateEvent, &context, &report) ==
		SG_RUNE_COMPACT_LEARNING_CONSUMER_ALLOCATION_FAILED);
	CHECK(context.calls == 0U &&
		SG_RuneCompactLearningConsumerPriorCount(consumer) == 0U &&
		report.event_count == UINT32_C(0x3d3d3d3d));
	fail_calloc_after = -1;
	memset(&report, 0x3d, sizeof(report));
	fail_realloc_after = 0;
	CHECK(Ingest(consumer, &trace, ValidateEvent, &context, &report) ==
		SG_RUNE_COMPACT_LEARNING_CONSUMER_ALLOCATION_FAILED);
	CHECK(SG_RuneCompactLearningConsumerPriorCount(consumer) == 0U &&
		report.event_count == UINT32_C(0x3d3d3d3d));
	fail_realloc_after = -1;
	SG_RuneCompactLearningConsumerDestroy(consumer);
}
#endif

int main(void)
{
	TestAcceptedCollectionTransaction();
	TestSkippedEventsAndNoOp();
	TestIdentityLifeOrderPhysicsAndAtomicFailure();
	TestInvalidCapabilityRejected();
#if !defined(SG_RUNE_COMPACT_LEARNING_HOST_LIFECYCLE_TEST)
	TestProductionLearningLifecycle();
#endif
#if defined(SG_RUNE_COMPACT_LEARNING_HOST_LIFECYCLE_TEST)
	TestHostLifecycle();
#endif
#if defined(SG_RUNE_COMPACT_LEARNING_CONSUMER_TEST_WRAP_ALLOC)
	TestAllocationRollback();
#endif
	if (failures != 0) {
		fprintf(stderr, "%d compact RUNE learning-consumer checks failed\n",
			failures);
		return 1;
	}
	puts("compact RUNE learning-consumer checks passed");
	return 0;
}
