#if defined(SG_RUNE_COMPACT_PRODUCTION_HOST_AUTHORITY_TEST)
#include "../g_local.h"
#undef world
#include "../g_tourney.h"
#endif

#include "../slipgate/sg_rune_compact_production.h"
#include "../slipgate/sg_rune_compact_weapon_catalog.h"
#include "../slipgate/sg_rune_mechanism_catalog.h"

#if defined(SG_RUNE_COMPACT_PRODUCTION_HOST_AUTHORITY_TEST)
#include "../slipgate/sg_crc32.h"
#include "../slipgate/sg_rune_source_authority_owner.h"
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum
{
	EVENT_LOADER_INIT = 1,
	EVENT_LOAD,
	EVENT_SNAPSHOT,
	EVENT_SPATIAL_BUILD,
	EVENT_SPATIAL_DESTROY,
	EVENT_HOST_ACQUIRE,
	EVENT_RUNTIME_INSTALL,
	EVENT_PORTAL_SOURCE_PREPARE,
	EVENT_PORTAL_SOURCE_DESTROY,
	EVENT_RUNTIME_CLEAR,
	EVENT_LOADER_RESET,
	EVENT_LOADER_DESTROY,
	EVENT_LEARNING_INSTALL,
	EVENT_LEARNING_RETIRE,
	EVENT_SOURCE_ACQUIRE,
	EVENT_SOURCE_SNAPSHOT,
	EVENT_SOURCE_CURRENT,
	EVENT_SOURCE_DESTROY
};

#if !defined(SG_RUNE_COMPACT_PRODUCTION_HOST_AUTHORITY_TEST)
struct sg_rune_source_authority_s { uint32_t unused; };
#endif

static int failures;
static int events[32];
static size_t event_count;
static int load_ok = 1;
static int spatial_ok = 1;
static int host_ok = 1;
static int runtime_ok = 1;
static int learning_ok = 1;
static sg_rune_compact_model_t model;
static sg_rune_compact_static_t static_data;
static sg_rune_compact_cell_t cell;
static sg_rune_compact_facet_t facet;
static sg_rune_compact_incidence_t incidence;
static sg_rune_compact_incidence_index_t cell_incidence;
static sg_compact_localization_observation_owner_t observation_owner;
#if !defined(SG_RUNE_COMPACT_PRODUCTION_HOST_AUTHORITY_TEST)
static struct sg_rune_source_authority_s source_authority;
static sg_rune_source_snapshot_t source_snapshot;
static int source_acquire_ok = 1;
static int source_snapshot_ok = 1;
static int source_current_ok = 1;
#else
static cvar_t source_ctfflags;
static cvar_t source_deathmatch;
static cvar_t source_fastswitch;
static sg_level_identity_t source_identity;
static int source_identity_current = 1;

cvar_t *ctfflags = &source_ctfflags;
cvar_t *deathmatch = &source_deathmatch;
cvar_t *fastswitch = &source_fastswitch;
int matchstate = MATCH_NONE;
#endif

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static void Event(int event)
{
	if (event_count < sizeof(events) / sizeof(events[0]))
		events[event_count++] = event;
}

static void ResetEvents(void)
{
	event_count = 0U;
	memset(events, 0, sizeof(events));
}

#if !defined(SG_RUNE_COMPACT_PRODUCTION_HOST_AUTHORITY_TEST)
static void CheckEvents(const int *expected, size_t count)
{
	size_t index;

	CHECK(event_count == count);
	for (index = 0U; index < count && index < event_count; index++)
		CHECK(events[index] == expected[index]);
}
#endif

int SG_RuneCompactLearningProductionInstall(
	sg_rune_compact_production_t *owner,
	const sg_rune_compact_model_t *accepted_model,
	const sg_rune_compact_identity_t *identity);
void SG_RuneCompactLearningProductionRetire(
	sg_rune_compact_production_t *owner);

int SG_RuneCompactLearningProductionInstall(
	sg_rune_compact_production_t *owner,
	const sg_rune_compact_model_t *accepted_model,
	const sg_rune_compact_identity_t *identity)
{
	Event(EVENT_LEARNING_INSTALL);
	CHECK(owner != NULL && owner->learning == NULL && accepted_model == &model &&
		identity == &owner->identity);
	if (!learning_ok || owner == NULL || accepted_model != &model ||
		identity != &owner->identity)
		return 0;
	owner->learning =
		(sg_rune_compact_learning_consumer_t *)(void *)&model;
	return 1;
}

#if !defined(SG_RUNE_COMPACT_PRODUCTION_HOST_AUTHORITY_TEST)
sg_rune_source_status_t SG_RuneSourceAuthorityAcquire(
	sg_rune_source_authority_t **authority_out)
{
	Event(EVENT_SOURCE_ACQUIRE);
	if (authority_out == NULL || !source_acquire_ok)
		return SG_RUNE_SOURCE_INVALID_STATE;
	*authority_out = (sg_rune_source_authority_t *)(void *)&source_authority;
	return SG_RUNE_SOURCE_OK;
}

sg_rune_source_status_t SG_RuneSourceAuthoritySnapshot(
	const sg_rune_source_authority_t *authority,
	sg_rune_source_snapshot_t *snapshot_out)
{
	Event(EVENT_SOURCE_SNAPSHOT);
	if (authority != (const sg_rune_source_authority_t *)(const void *)
		&source_authority || snapshot_out == NULL || !source_snapshot_ok)
		return SG_RUNE_SOURCE_INVALID_STATE;
	*snapshot_out = source_snapshot;
	return SG_RUNE_SOURCE_OK;
}

sg_rune_source_status_t SG_RuneSourceAuthorityCurrent(
	const sg_rune_source_authority_t *authority)
{
	Event(EVENT_SOURCE_CURRENT);
	return authority == (const sg_rune_source_authority_t *)(const void *)
		&source_authority && source_current_ok ? SG_RUNE_SOURCE_OK :
		SG_RUNE_SOURCE_WEAPON_DRIFT;
}

void SG_RuneSourceAuthorityDestroy(sg_rune_source_authority_t *authority)
{
	if (authority == (sg_rune_source_authority_t *)(void *)&source_authority)
		Event(EVENT_SOURCE_DESTROY);
}
#endif

void SG_RuneCompactLearningProductionRetire(
	sg_rune_compact_production_t *owner)
{
	if (owner == NULL || owner->learning == NULL)
		return;
	Event(EVENT_LEARNING_RETIRE);
	owner->learning = NULL;
}

int SG_RuneCompactArtifactLoaderInit(
	sg_rune_compact_artifact_loader_t *loader)
{
	Event(EVENT_LOADER_INIT);
	if (loader == NULL)
		return 0;
	loader->state = 1U;
	return 1;
}

void SG_RuneCompactArtifactLoaderReset(
	sg_rune_compact_artifact_loader_t *loader)
{
	Event(EVENT_LOADER_RESET);
	if (loader != NULL)
		loader->published = NULL;
}

void SG_RuneCompactArtifactLoaderDestroy(
	sg_rune_compact_artifact_loader_t *loader)
{
	Event(EVENT_LOADER_DESTROY);
	if (loader != NULL)
		memset(loader, 0, sizeof(*loader));
}

const sg_rune_compact_model_t *SG_RuneCompactArtifactLoaderSnapshot(
	const sg_rune_compact_artifact_loader_t *loader)
{
	Event(EVENT_SNAPSHOT);
	return loader != NULL && loader->published != NULL ? &model : NULL;
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

	Event(EVENT_LOAD);
	memset(&result, 0, sizeof(result));
	if (info_out != NULL)
		memset(info_out, 0, sizeof(*info_out));
	result.diagnostic = load_ok ? SG_RUNE_COMPACT_ARTIFACT_LOAD_OK :
		SG_RUNE_COMPACT_ARTIFACT_LOAD_WIRE_REJECTED;
	if (load_ok && loader != NULL && path != NULL && identity_out != NULL &&
		info_out != NULL)
	{
		loader->published = (sg_rune_compact_wire_decoded_t *)(uintptr_t)1U;
		*identity_out = model.identity;
		info_out->wire_version = SG_RUNE_COMPACT_WIRE_VERSION;
		info_out->model_version = SG_RUNE_COMPACT_MODEL_VERSION;
		info_out->analytic_version = SG_RUNE_COMPACT_ANALYTIC_VERSION;
		info_out->schema_tag = SG_RUNE_COMPACT_MODEL_SCHEMA_TAG;
		info_out->image_bytes = 1U;
		info_out->checksum = 1U;
		info_out->identity = model.identity;
		loader->published_info = *info_out;
	}
	return result;
}

int SG_RuneCompactIdentityMatches(
	const sg_rune_compact_identity_t *actual,
	const sg_rune_compact_identity_t *expected)
{
	return actual != NULL && expected != NULL &&
		actual->entity_semantics_id == expected->entity_semantics_id &&
		memcmp(actual->bsp_sha256, expected->bsp_sha256,
			sizeof(actual->bsp_sha256)) == 0;
}

sg_host_law_result_t SG_HostLawProductionAcquire(
	sg_host_law_runtime_authority_t *authority_out)
{
	sg_host_law_result_t result;

	Event(EVENT_HOST_ACQUIRE);
	memset(&result, 0, sizeof(result));
	result.status = host_ok ? SG_HOST_LAW_OK : SG_HOST_LAW_HOST_UNAVAILABLE;
	if (host_ok && authority_out != NULL)
	{
		memset(authority_out, 0, sizeof(*authority_out));
		authority_out->version = SG_HOST_LAW_RUNTIME_AUTHORITY_VERSION;
		authority_out->epoch = 1U;
		authority_out->epoch_complement = ~UINT64_C(1);
#if defined(SG_RUNE_COMPACT_PRODUCTION_HOST_AUTHORITY_TEST)
		authority_out->view.static_identity.physics_abi_id =
			model.identity.physics_abi_id;
#endif
	}
	return result;
}

#if defined(SG_RUNE_COMPACT_PRODUCTION_HOST_AUTHORITY_TEST)
sg_host_law_result_t SG_HostLawProductionAuthorityCurrent(
	const sg_host_law_runtime_authority_t *authority)
{
	sg_host_law_result_t result;

	memset(&result, 0, sizeof(result));
	result.status = host_ok && authority != NULL &&
		authority->version == SG_HOST_LAW_RUNTIME_AUTHORITY_VERSION &&
		authority->epoch == UINT64_C(1) &&
		authority->epoch_complement == ~authority->epoch ? SG_HOST_LAW_OK :
		SG_HOST_LAW_PRODUCTION_DRIFT;
	return result;
}

sg_identity_status_t SG_LevelIdentitySnapshot(const char *expected_mapname,
	sg_level_identity_t *identity_out)
{
	if (expected_mapname == NULL || identity_out == NULL)
		return SG_IDENTITY_INVALID_ARGUMENT;
	if (!source_identity_current)
		return SG_IDENTITY_UNAVAILABLE;
	if (strcmp(expected_mapname, source_identity.mapname) != 0)
		return SG_IDENTITY_MAPNAME_MISMATCH;
	memset(identity_out, 0, sizeof(*identity_out));
	*identity_out = source_identity;
	return SG_IDENTITY_OK;
}
#endif

sg_compact_runtime_level_status_t SG_CompactRuntimeLevelInstall(
	sg_compact_runtime_level_t *runtime,
	const sg_rune_compact_model_t *accepted_model,
	const sg_rune_compact_identity_t *expected_identity,
	const sg_rune_compact_spatial_index_t *spatial_index,
	const sg_compact_localization_observation_owner_t *candidate_owner,
	const sg_host_law_runtime_authority_t *host_authority,
	uint64_t rune_identity, uint64_t topology_revision)
{
	Event(EVENT_RUNTIME_INSTALL);
	CHECK(runtime != NULL && accepted_model == &model &&
		expected_identity != NULL && spatial_index != NULL &&
		candidate_owner == &observation_owner && host_authority != NULL &&
		rune_identity != 0U &&
		topology_revision == model.identity.entity_semantics_id);
	if (!runtime_ok)
		return SG_COMPACT_RUNTIME_LEVEL_FIELD_SERVICE_REJECTED;
	runtime->active = 1U;
	runtime->field_service =
		(sg_rune_compact_field_service_t *)(uintptr_t)1U;
	return SG_COMPACT_RUNTIME_LEVEL_OK;
}

const sg_compact_localization_observation_owner_t *
SG_BotLocalizationObservationOwner(void)
{
	return &observation_owner;
}

int SG_RuneCompactSpatialIndexBuildTopology(
	const sg_rune_compact_spatial_topology_input_t *topology,
	const sg_rune_compact_spatial_allocator_t *allocator,
	sg_rune_compact_spatial_index_t **index_out,
	sg_rune_compact_spatial_error_t *error_out)
{
	(void)allocator;
	(void)error_out;
	Event(EVENT_SPATIAL_BUILD);
	CHECK(topology != NULL && topology->cell_count == 1U &&
		topology->face_count == 1U && topology->portal_count == 0U &&
		index_out != NULL && *index_out == NULL);
	if (!topology || !index_out || *index_out || !spatial_ok)
		return 0;
	*index_out = (sg_rune_compact_spatial_index_t *)(uintptr_t)1U;
	return 1;
}

void SG_RuneCompactSpatialIndexDestroy(
	sg_rune_compact_spatial_index_t *index)
{
	if (index != NULL)
		Event(EVENT_SPATIAL_DESTROY);
}

void SG_CompactRuntimeLevelClear(sg_compact_runtime_level_t *runtime)
{
	Event(EVENT_RUNTIME_CLEAR);
	if (runtime != NULL)
		memset(runtime, 0, sizeof(*runtime));
}

int SG_CompactRuntimeLevelCurrent(
	const sg_compact_runtime_level_t *runtime)
{
	return runtime != NULL && runtime->active == 1U;
}

const sg_rune_compact_field_service_t *SG_CompactRuntimeLevelFieldService(
	const sg_compact_runtime_level_t *runtime)
{
	return runtime != NULL && runtime->active == 1U ?
		runtime->field_service : NULL;
}

const sg_rune_compact_model_t *SG_RuneCompactFieldServiceModel(
	const sg_rune_compact_field_service_t *service)
{
	return service != NULL ? &model : NULL;
}

uint64_t SG_RuneCompactFieldServiceIdentity(
	const sg_rune_compact_field_service_t *service)
{
	return service != NULL ? UINT64_C(1) : 0U;
}

uint64_t SG_RuneCompactFieldServiceGeneration(
	const sg_rune_compact_field_service_t *service)
{
	return service != NULL ? UINT64_C(1) : 0U;
}

uint32_t SG_RuneCompactFieldServicePortalRootCount(
	const sg_rune_compact_field_service_t *service)
{
	(void)service;
	return 0U;
}

int SG_RuneCompactFieldServicePortalRootAt(
	const sg_rune_compact_field_service_t *service, uint32_t root_index,
	sg_rune_compact_portal_index_t *portal_out,
	sg_rune_compact_mechanism_index_t *mechanism_out)
{
	(void)service;
	(void)root_index;
	(void)portal_out;
	(void)mechanism_out;
	return 0;
}

int SG_MechCatalogResolveSourceOrdinal(uint32_t source_ordinal,
	sg_mech_catalog_source_resolution_t *resolution_out)
{
	(void)source_ordinal;
	if (resolution_out != NULL)
		memset(resolution_out, 0, sizeof(*resolution_out));
	return 0;
}

int SG_RuneCompactPortalSnapshotSourcePrepare(
	const sg_rune_compact_model_t *accepted_model,
	sg_rune_compact_portal_snapshot_source_t **source_out)
{
	Event(EVENT_PORTAL_SOURCE_PREPARE);
	if (accepted_model != &model || source_out == NULL || *source_out != NULL)
		return 0;
	*source_out = (sg_rune_compact_portal_snapshot_source_t *)(uintptr_t)1U;
	return 1;
}

int SG_RuneCompactPortalSnapshotSourceCurrent(
	const sg_rune_compact_portal_snapshot_source_t *source,
	const sg_rune_compact_model_t *accepted_model)
{
	return source != NULL && accepted_model == &model;
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
	if (source != NULL)
		Event(EVENT_PORTAL_SOURCE_DESTROY);
}

#if defined(SG_RUNE_COMPACT_PRODUCTION_HOST_AUTHORITY_TEST)
static void PublishHostSourceAuthority(void)
{
	static const char entity_text[] =
		"{\n\"classname\" \"worldspawn\"\n}\n";
	uint32_t entity_crc32 = 0U;

	CHECK(SG_CRC32Buffer(entity_text, strlen(entity_text), &entity_crc32));
	memset(&source_identity, 0, sizeof(source_identity));
	source_identity.bsp_checksum = UINT32_C(0x10203040);
	source_identity.entity_crc32 = entity_crc32;
	source_identity.host_physics_id = SG_HOST_PHYSICS_EPOCH;
	source_identity.bsp_bytes = UINT64_C(123456);
	memset(source_identity.bsp_sha256, 0x5a,
		sizeof(source_identity.bsp_sha256));
	memcpy(source_identity.mapname, "compact-law",
		sizeof("compact-law"));
	source_identity_current = 1;
	SG_RuneSourceAuthorityReset();
	CHECK(SG_RuneSourceAuthorityBegin(source_identity.mapname, entity_text) ==
		SG_RUNE_SOURCE_OK);
	CHECK(SG_RuneSourceAuthorityRecord(0U, 0) == SG_RUNE_SOURCE_OK);
	CHECK(SG_RuneSourceAuthorityPublish(source_identity.mapname) ==
		SG_RUNE_SOURCE_OK);
}
#endif

static void InitModel(void)
{
	sg_weapon_profile_t resolved[
		SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT];
	sg_rune_source_weapon_law_t weapon_law;

	memset(&model, 0, sizeof(model));
	memset(&static_data, 0, sizeof(static_data));
	memset(&cell, 0, sizeof(cell));
	memset(&facet, 0, sizeof(facet));
	memset(&incidence, 0, sizeof(incidence));
	memset(&cell_incidence, 0, sizeof(cell_incidence));
	memset(&observation_owner, 0, sizeof(observation_owner));
	memset(&weapon_law, 0, sizeof(weapon_law));
#if !defined(SG_RUNE_COMPACT_PRODUCTION_HOST_AUTHORITY_TEST)
	memset(&source_snapshot, 0, sizeof(source_snapshot));
	memset(&source_authority, 0, sizeof(source_authority));
	source_acquire_ok = 1;
	source_snapshot_ok = 1;
	source_current_ok = 1;
#else
	memset(&source_ctfflags, 0, sizeof(source_ctfflags));
	memset(&source_deathmatch, 0, sizeof(source_deathmatch));
	memset(&source_fastswitch, 0, sizeof(source_fastswitch));
	source_ctfflags.value = 0.0f;
	source_deathmatch.value = 1.0f;
	source_fastswitch.value = 0.0f;
	matchstate = MATCH_NONE;
#endif
	model.identity.bsp_sha256[0] = 0xa5U;
	model.identity.entity_crc32 = UINT32_C(11);
	model.identity.entity_semantics_id = UINT64_C(19);
	model.identity.physics_abi_id = UINT64_C(0x22);
	model.identity.producer_identity = SG_RUNE_COMPACT_WEAPON_PRODUCER_ID;
	weapon_law.weapon_balance_compiled = (uint8_t)SG_WEAPON_BALANCE_COMPILED;
#if !defined(SG_RUNE_COMPACT_PRODUCTION_HOST_AUTHORITY_TEST)
	source_snapshot.weapon_law.weapon_balance_compiled =
		(uint8_t)SG_WEAPON_BALANCE_COMPILED;
	source_snapshot.host_authority.view.static_identity.physics_abi_id =
		model.identity.physics_abi_id;
	weapon_law = source_snapshot.weapon_law;
#else
	weapon_law.deathmatch_active = 1U;
#endif
	CHECK(SG_RuneCompactWeaponProfilesResolve(&weapon_law,
		model.identity.physics_abi_id, resolved,
		SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT));
	CHECK(SG_RuneCompactWeaponLawIdentity(&weapon_law,
		resolved, SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT,
		&model.identity.weapon_law_id));
	model.static_data = &static_data;
	cell.bounds.mins.value[0] = -8;
	cell.bounds.mins.value[1] = -8;
	cell.bounds.mins.value[2] = -8;
	cell.bounds.maxs.value[0] = 8;
	cell.bounds.maxs.value[1] = 8;
	cell.bounds.maxs.value[2] = 8;
	cell.incidences.count = 1U;
	facet.plane.normal_bits[0] = UINT32_C(0x3f800000);
	incidence.cell.value = 0U;
	incidence.facet.value = 0U;
	incidence.side = SG_RUNE_FACET_NEGATIVE_SIDE;
	incidence.boundary = SG_RUNE_BOUNDARY_CLOSED;
	model.cells = &cell;
	model.cell_count = 1U;
	model.facets = &facet;
	model.facet_count = 1U;
	model.incidences = &incidence;
	model.incidence_count = 1U;
	model.cell_incidences = &cell_incidence;
	model.cell_incidence_count = 1U;
#if defined(SG_RUNE_COMPACT_PRODUCTION_HOST_AUTHORITY_TEST)
	PublishHostSourceAuthority();
#endif
}

#if !defined(SG_RUNE_COMPACT_PRODUCTION_HOST_AUTHORITY_TEST)
static void TestSuccessAndLifetime(void)
{
	sg_rune_compact_production_t owner =
		SG_RUNE_COMPACT_PRODUCTION_INITIALIZER;
	sg_rune_compact_production_result_t result;
	sg_rune_compact_wire_info_t artifact_info;
	const int install_events[] = { EVENT_LOADER_INIT, EVENT_LOAD,
		EVENT_SNAPSHOT, EVENT_SOURCE_ACQUIRE, EVENT_SOURCE_SNAPSHOT,
		EVENT_SOURCE_CURRENT, EVENT_SPATIAL_BUILD, EVENT_HOST_ACQUIRE, EVENT_RUNTIME_INSTALL,
		EVENT_PORTAL_SOURCE_PREPARE, EVENT_LEARNING_INSTALL };
	const int clear_events[] = { EVENT_LEARNING_RETIRE,
		EVENT_PORTAL_SOURCE_DESTROY, EVENT_SOURCE_DESTROY, EVENT_RUNTIME_CLEAR,
		EVENT_SPATIAL_DESTROY, EVENT_LOADER_DESTROY };

	load_ok = 1;
	spatial_ok = 1;
	host_ok = 1;
	runtime_ok = 1;
	learning_ok = 1;
	ResetEvents();
	result = SG_RuneCompactProductionInit(&owner);
	CHECK(result.status == SG_RUNE_COMPACT_PRODUCTION_OK);
	result = SG_RuneCompactProductionLoad(&owner, "map.rune");
	CHECK(result.status == SG_RUNE_COMPACT_PRODUCTION_OK);
	CheckEvents(install_events, sizeof(install_events) / sizeof(install_events[0]));
	CHECK(SG_RuneCompactProductionCurrent(&owner));
	CHECK(SG_RuneCompactProductionModel(&owner) == &model);
	memset(&artifact_info, 0, sizeof(artifact_info));
	CHECK(SG_RuneCompactProductionArtifactInfo(&owner, &artifact_info));
	CHECK(artifact_info.wire_version == SG_RUNE_COMPACT_WIRE_VERSION);
	CHECK(SG_RuneCompactIdentityMatches(&artifact_info.identity,
		&model.identity));
	ResetEvents();
	result = SG_RuneCompactProductionLoad(&owner, "map.rune");
	CHECK(result.status == SG_RUNE_COMPACT_PRODUCTION_ALREADY_ACTIVE);
	CHECK(event_count == 0U);
	SG_RuneCompactProductionClear(&owner);
	CheckEvents(clear_events, sizeof(clear_events) / sizeof(clear_events[0]));
	CHECK(owner.initialized == 0U && owner.active == 0U);
}

static void TestFailureRollback(void)
{
	sg_rune_compact_production_t owner =
		SG_RUNE_COMPACT_PRODUCTION_INITIALIZER;
	sg_rune_compact_production_result_t result;
	const int host_events[] = { EVENT_LOAD, EVENT_SNAPSHOT,
		EVENT_SOURCE_ACQUIRE, EVENT_SOURCE_SNAPSHOT, EVENT_SOURCE_CURRENT,
		EVENT_SPATIAL_BUILD, EVENT_HOST_ACQUIRE, EVENT_SPATIAL_DESTROY,
		EVENT_LOADER_RESET, EVENT_SOURCE_DESTROY };
	const int runtime_events[] = { EVENT_LOAD, EVENT_SNAPSHOT,
		EVENT_SOURCE_ACQUIRE, EVENT_SOURCE_SNAPSHOT, EVENT_SOURCE_CURRENT,
		EVENT_SPATIAL_BUILD, EVENT_HOST_ACQUIRE, EVENT_RUNTIME_INSTALL,
		EVENT_RUNTIME_CLEAR, EVENT_SPATIAL_DESTROY, EVENT_LOADER_RESET,
		EVENT_SOURCE_DESTROY };
	const int spatial_events[] = { EVENT_LOAD, EVENT_SNAPSHOT,
		EVENT_SOURCE_ACQUIRE, EVENT_SOURCE_SNAPSHOT, EVENT_SOURCE_CURRENT,
		EVENT_SPATIAL_BUILD, EVENT_LOADER_RESET, EVENT_SOURCE_DESTROY };
	const int learning_events[] = { EVENT_LOAD, EVENT_SNAPSHOT,
		EVENT_SOURCE_ACQUIRE, EVENT_SOURCE_SNAPSHOT, EVENT_SOURCE_CURRENT,
		EVENT_SPATIAL_BUILD, EVENT_HOST_ACQUIRE, EVENT_RUNTIME_INSTALL,
		EVENT_PORTAL_SOURCE_PREPARE, EVENT_LEARNING_INSTALL,
		EVENT_PORTAL_SOURCE_DESTROY, EVENT_RUNTIME_CLEAR,
		EVENT_SPATIAL_DESTROY, EVENT_LOADER_RESET, EVENT_SOURCE_DESTROY };

	CHECK(SG_RuneCompactProductionInit(&owner).status ==
		SG_RUNE_COMPACT_PRODUCTION_OK);
	spatial_ok = 0;
	ResetEvents();
	result = SG_RuneCompactProductionLoad(&owner, "map.rune");
	CHECK(result.status ==
		SG_RUNE_COMPACT_PRODUCTION_SPATIAL_INDEX_REJECTED);
	CheckEvents(spatial_events,
		sizeof(spatial_events) / sizeof(spatial_events[0]));
	spatial_ok = 1;
	host_ok = 0;
	ResetEvents();
	result = SG_RuneCompactProductionLoad(&owner, "map.rune");
	CHECK(result.status ==
		SG_RUNE_COMPACT_PRODUCTION_HOST_AUTHORITY_REJECTED);
	CheckEvents(host_events, sizeof(host_events) / sizeof(host_events[0]));
	host_ok = 1;
	runtime_ok = 0;
	ResetEvents();
	result = SG_RuneCompactProductionLoad(&owner, "map.rune");
	CHECK(result.status == SG_RUNE_COMPACT_PRODUCTION_RUNTIME_REJECTED);
	CheckEvents(runtime_events,
		sizeof(runtime_events) / sizeof(runtime_events[0]));
	runtime_ok = 1;
	learning_ok = 0;
	ResetEvents();
	result = SG_RuneCompactProductionLoad(&owner, "map.rune");
	CHECK(result.status == SG_RUNE_COMPACT_PRODUCTION_LEARNING_REJECTED);
	CheckEvents(learning_events,
		sizeof(learning_events) / sizeof(learning_events[0]));
	learning_ok = 1;
	SG_RuneCompactProductionClear(&owner);
}

static void TestWeaponLawAdmissionAndDrift(void)
{
	sg_rune_compact_production_t owner =
		SG_RUNE_COMPACT_PRODUCTION_INITIALIZER;
	sg_rune_compact_production_result_t result;
	const uint64_t accepted_law_id = model.identity.weapon_law_id;
	const sg_rune_source_weapon_law_t accepted_law =
		source_snapshot.weapon_law;

	CHECK(SG_RuneCompactProductionInit(&owner).status ==
		SG_RUNE_COMPACT_PRODUCTION_OK);
	model.identity.weapon_law_id++;
	ResetEvents();
	result = SG_RuneCompactProductionLoad(&owner, "map.rune");
	CHECK(result.status == SG_RUNE_COMPACT_PRODUCTION_WEAPON_AUTHORITY_REJECTED);
	CHECK(owner.active == 0U && owner.source_authority == NULL);
	CHECK(event_count == 6U && events[0] == EVENT_LOAD &&
		events[1] == EVENT_SNAPSHOT && events[2] == EVENT_SOURCE_ACQUIRE &&
		events[3] == EVENT_SOURCE_SNAPSHOT &&
		events[4] == EVENT_SOURCE_DESTROY && events[5] == EVENT_LOADER_RESET);
	model.identity.weapon_law_id = accepted_law_id;
	source_snapshot.weapon_law.fast_switch_enabled = 1U;
	ResetEvents();
	result = SG_RuneCompactProductionLoad(&owner, "map.rune");
	CHECK(result.status == SG_RUNE_COMPACT_PRODUCTION_WEAPON_AUTHORITY_REJECTED);
	CHECK(owner.active == 0U && owner.source_authority == NULL);
	source_snapshot.weapon_law = accepted_law;
	ResetEvents();
	result = SG_RuneCompactProductionLoad(&owner, "map.rune");
	CHECK(result.status == SG_RUNE_COMPACT_PRODUCTION_OK);
	CHECK(SG_RuneCompactProductionCurrent(&owner));
	source_current_ok = 0;
	CHECK(!SG_RuneCompactProductionCurrent(&owner));
	CHECK(SG_RuneCompactProductionModel(&owner) == NULL);
	source_current_ok = 1;
	SG_RuneCompactProductionClear(&owner);
}
#else
static void TestRealSourceWeaponLawAdmissionAndDrift(void)
{
	sg_rune_compact_production_t owner =
		SG_RUNE_COMPACT_PRODUCTION_INITIALIZER;
	sg_rune_compact_production_result_t result;
	const uint64_t accepted_law_id = model.identity.weapon_law_id;

	CHECK(SG_RuneCompactProductionInit(&owner).status ==
		SG_RUNE_COMPACT_PRODUCTION_OK);
	model.identity.weapon_law_id++;
	result = SG_RuneCompactProductionLoad(&owner, "map.rune");
	CHECK(result.status == SG_RUNE_COMPACT_PRODUCTION_WEAPON_AUTHORITY_REJECTED);
	CHECK(owner.active == 0U && owner.source_authority == NULL);
	model.identity.weapon_law_id = accepted_law_id;
	result = SG_RuneCompactProductionLoad(&owner, "map.rune");
	CHECK(result.status == SG_RUNE_COMPACT_PRODUCTION_OK);
	CHECK(SG_RuneCompactProductionCurrent(&owner));
	source_fastswitch.value = 1.0f;
	CHECK(!SG_RuneCompactProductionCurrent(&owner));
	CHECK(SG_RuneCompactProductionModel(&owner) == NULL);
	source_fastswitch.value = 0.0f;
	SG_RuneCompactProductionClear(&owner);
	SG_RuneSourceAuthorityReset();
}
#endif

int main(void)
{
	InitModel();
#if !defined(SG_RUNE_COMPACT_PRODUCTION_HOST_AUTHORITY_TEST)
	TestSuccessAndLifetime();
	TestFailureRollback();
	InitModel();
	TestWeaponLawAdmissionAndDrift();
#else
	ResetEvents();
	TestRealSourceWeaponLawAdmissionAndDrift();
#endif
	if (failures != 0)
	{
		fprintf(stderr, "%d compact production tests failed\n", failures);
		return 1;
	}
	puts("sg_rune_compact_production_test: PASS");
	return 0;
}
