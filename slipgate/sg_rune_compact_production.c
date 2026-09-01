#include "sg_rune_compact_production.h"

#include "sg_rune_compact_weapon_catalog.h"
#include "sg_rune_source_authority.h"

#include <stdlib.h>
#include <string.h>

/* The recorder-facing lifecycle stays in its own game adapter so this compact
 * production owner does not include the game header surface. */
int SG_RuneCompactLearningProductionInstall(sg_rune_compact_production_t *owner,
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *identity);
void SG_RuneCompactLearningProductionRetire(
	sg_rune_compact_production_t *owner);

static float FloatFromBits(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static int SpatialIndexFromAcceptedModel(
	const sg_rune_compact_model_t *model,
	sg_rune_compact_spatial_index_t **index_out)
{
	sg_rune_compact_spatial_cell_input_t *cells = NULL;
	sg_rune_compact_spatial_face_input_t *faces = NULL;
	sg_rune_compact_spatial_portal_input_t *portals = NULL;
	sg_rune_compact_spatial_topology_input_t topology;
	sg_rune_compact_spatial_error_t error;
	uint32_t face_cursor = 0U;
	uint32_t cell_index;
	uint32_t portal_index;
	int built = 0;

	if (!model || !index_out || *index_out || !model->cells ||
		!model->facets || !model->incidences || !model->cell_incidences ||
		model->cell_count == 0U || model->cell_incidence_count == 0U)
		return 0;
	cells = calloc((size_t)model->cell_count, sizeof(*cells));
	faces = calloc((size_t)model->cell_incidence_count, sizeof(*faces));
	if (model->portal_count != 0U)
		portals = calloc((size_t)model->portal_count, sizeof(*portals));
	if (!cells || !faces || (model->portal_count != 0U && !portals))
		goto done;
	for (cell_index = 0U; cell_index < model->cell_count; cell_index++)
	{
		const sg_rune_compact_cell_t *cell = &model->cells[cell_index];
		uint32_t axis;
		uint32_t local;

		if (cell->incidences.first > model->cell_incidence_count ||
			cell->incidences.count > model->cell_incidence_count -
				cell->incidences.first ||
			face_cursor > model->cell_incidence_count - cell->incidences.count)
			goto done;
		cells[cell_index].first_face = face_cursor;
		cells[cell_index].face_count = cell->incidences.count;
		for (axis = 0U; axis < 3U; axis++)
		{
			cells[cell_index].bounds.mins.value[axis] =
				(float)cell->bounds.mins.value[axis] * 0.125f;
			cells[cell_index].bounds.maxs.value[axis] =
				(float)cell->bounds.maxs.value[axis] * 0.125f;
		}
		for (local = 0U; local < cell->incidences.count; local++)
		{
			const uint32_t incidence_index = model->cell_incidences[
				cell->incidences.first + local].value;
			const sg_rune_compact_incidence_t *incidence;
			const sg_rune_binary32_plane_t *plane;
			sg_rune_compact_spatial_face_input_t *face = &faces[face_cursor++];
			float sign;

			if (incidence_index >= model->incidence_count)
				goto done;
			incidence = &model->incidences[incidence_index];
			if (incidence->cell.value != cell_index ||
				incidence->cell_ordinal != local ||
				incidence->facet.value >= model->facet_count ||
				incidence->side >= SG_RUNE_FACET_SIDE_COUNT ||
				incidence->boundary >= SG_RUNE_BOUNDARY_OWNERSHIP_COUNT)
				goto done;
			plane = &model->facets[incidence->facet.value].plane;
			sign = incidence->side == SG_RUNE_FACET_NEGATIVE_SIDE ?
				1.0f : -1.0f;
			face->bounds = cells[cell_index].bounds;
			for (axis = 0U; axis < 3U; axis++)
				face->normal[axis] = sign *
					FloatFromBits(plane->normal_bits[axis]);
			face->distance = sign * FloatFromBits(plane->distance_bits);
			face->source_boundary = incidence->facet.value;
			face->ownership = incidence->boundary;
		}
	}
	if (face_cursor != model->cell_incidence_count)
		goto done;
	for (portal_index = 0U; portal_index < model->portal_count; portal_index++)
	{
		const sg_rune_compact_portal_t *portal = &model->portals[portal_index];
		const sg_rune_compact_incidence_t *negative;
		const sg_rune_compact_incidence_t *positive;

		if (portal->facet.value >= model->facet_count ||
			portal->negative_incidence.value >= model->incidence_count ||
			portal->positive_incidence.value >= model->incidence_count)
			goto done;
		negative = &model->incidences[portal->negative_incidence.value];
		positive = &model->incidences[portal->positive_incidence.value];
		if (negative->facet.value != portal->facet.value ||
			positive->facet.value != portal->facet.value ||
			negative->side != SG_RUNE_FACET_NEGATIVE_SIDE ||
			positive->side != SG_RUNE_FACET_POSITIVE_SIDE)
			goto done;
		portals[portal_index].source_boundary = portal->facet.value;
		portals[portal_index].negative_cell = negative->cell.value;
		portals[portal_index].positive_cell = positive->cell.value;
	}
	memset(&topology, 0, sizeof(topology));
	topology.cells = cells;
	topology.cell_count = model->cell_count;
	topology.faces = faces;
	topology.face_count = model->cell_incidence_count;
	topology.portals = portals;
	topology.portal_count = model->portal_count;
	memset(&error, 0, sizeof(error));
	built = SG_RuneCompactSpatialIndexBuildTopology(&topology, NULL,
		index_out, &error);

done:
	free(cells);
	free(faces);
	free(portals);
	return built;
}

static sg_rune_compact_production_result_t Result(
	sg_rune_compact_production_status_t status)
{
	sg_rune_compact_production_result_t result;

	memset(&result, 0, sizeof(result));
	result.status = status;
	result.artifact.diagnostic = SG_RUNE_COMPACT_ARTIFACT_LOAD_INVALID_ARGUMENT;
	result.artifact.stage = SG_RUNE_COMPACT_ARTIFACT_LOAD_STAGE_NONE;
	result.artifact.wire_error.section = SG_RUNE_COMPACT_WIRE_SECTION_COUNT;
	result.artifact.wire_error.record = UINT32_MAX;
	result.host.status = SG_HOST_LAW_INVALID_ARGUMENT;
	result.host.field = SG_HOST_LAW_FIELD_NONE;
	result.runtime = SG_COMPACT_RUNTIME_LEVEL_INVALID_ARGUMENT;
	return result;
}

static uint64_t RuntimeIdentity(
	const sg_rune_compact_identity_t *identity)
{
	uint64_t value = UINT64_C(14695981039346656037);
	uint32_t index;

	for (index = 0U; index < 32U; index++)
		value = (value ^ (uint64_t)identity->bsp_sha256[index]) *
			UINT64_C(1099511628211);
	value = (value ^ (uint64_t)identity->entity_crc32) *
		UINT64_C(1099511628211);
	value = (value ^ identity->entity_semantics_id) *
		UINT64_C(1099511628211);
	return value == 0U ? UINT64_C(1) : value;
}

static int ArtifactWeaponLawCurrent(const sg_rune_compact_model_t *model,
	sg_rune_source_authority_t **authority_out)
{
	sg_rune_source_authority_t *authority = NULL;
	sg_rune_source_snapshot_t snapshot;
	sg_weapon_profile_t profiles[
		SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT];
	uint64_t weapon_law_id;
	sg_rune_source_status_t source_status;

	if (authority_out == NULL || *authority_out != NULL || model == NULL ||
		model->identity.physics_abi_id == 0U ||
		model->identity.weapon_law_id == 0U ||
		model->identity.producer_identity !=
			SG_RUNE_COMPACT_WEAPON_PRODUCER_ID)
		return 0;
	source_status = SG_RuneSourceAuthorityAcquire(&authority);
	if (source_status != SG_RUNE_SOURCE_OK || authority == NULL)
		return 0;
	memset(&snapshot, 0, sizeof(snapshot));
	source_status = SG_RuneSourceAuthoritySnapshot(authority, &snapshot);
	if (source_status != SG_RUNE_SOURCE_OK ||
		snapshot.host_authority.view.static_identity.physics_abi_id !=
			model->identity.physics_abi_id ||
		!SG_RuneCompactWeaponProfilesResolve(&snapshot.weapon_law,
			model->identity.physics_abi_id, profiles,
			SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT) ||
		!SG_RuneCompactWeaponLawIdentity(&snapshot.weapon_law, profiles,
			SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT,
			&weapon_law_id) || weapon_law_id != model->identity.weapon_law_id ||
		SG_RuneSourceAuthorityCurrent(authority) != SG_RUNE_SOURCE_OK) {
		SG_RuneSourceAuthorityDestroy(authority);
		return 0;
	}
	*authority_out = authority;
	return 1;
}

sg_rune_compact_production_result_t SG_RuneCompactProductionInit(
	sg_rune_compact_production_t *owner)
{
	sg_rune_compact_production_result_t result = Result(
		SG_RUNE_COMPACT_PRODUCTION_INVALID_ARGUMENT);

	if (owner == NULL)
		return result;
	if (owner->initialized != 0U || owner->active != 0U)
	{
		result.status = SG_RUNE_COMPACT_PRODUCTION_ALREADY_ACTIVE;
		return result;
	}
	memset(owner, 0, sizeof(*owner));
	if (!SG_RuneCompactArtifactLoaderInit(&owner->loader))
	{
		result.status = SG_RUNE_COMPACT_PRODUCTION_LOADER_REJECTED;
		return result;
	}
	owner->initialized = 1U;
	result.status = SG_RUNE_COMPACT_PRODUCTION_OK;
	return result;
}

sg_rune_compact_production_result_t SG_RuneCompactProductionLoad(
	sg_rune_compact_production_t *owner, const char *path)
{
	sg_rune_compact_production_result_t result = Result(
		SG_RUNE_COMPACT_PRODUCTION_INVALID_ARGUMENT);
	const sg_rune_compact_model_t *model;
	const sg_rune_compact_field_service_t *service;
	sg_rune_compact_portal_snapshot_source_t *portal_source = NULL;
	sg_rune_compact_portal_snapshot_t *portal_snapshot = NULL;
	sg_host_law_runtime_authority_t authority;
	sg_rune_compact_wire_info_t artifact_info;
	sg_rune_source_authority_t *source_authority = NULL;
	uint64_t rune_identity;
	const sg_compact_localization_observation_owner_t *observation_owner;

	if (owner == NULL || path == NULL || path[0] == '\0')
		return result;
	if (owner->initialized != 1U)
	{
		result.status = SG_RUNE_COMPACT_PRODUCTION_NOT_INITIALIZED;
		return result;
	}
	if (owner->active != 0U)
	{
		result.status = SG_RUNE_COMPACT_PRODUCTION_ALREADY_ACTIVE;
		return result;
	}
	memset(&owner->identity, 0, sizeof(owner->identity));
	memset(&artifact_info, 0, sizeof(artifact_info));
	result.artifact = SG_RuneCompactArtifactLoaderLoadAcceptedFileWithInfo(
		&owner->loader, path, &owner->identity, &artifact_info);
	if (result.artifact.diagnostic != SG_RUNE_COMPACT_ARTIFACT_LOAD_OK)
	{
		result.status = SG_RUNE_COMPACT_PRODUCTION_ARTIFACT_REJECTED;
		SG_RuneCompactArtifactLoaderReset(&owner->loader);
		return result;
	}
	model = SG_RuneCompactArtifactLoaderSnapshot(&owner->loader);
	if (model == NULL || !SG_RuneCompactIdentityMatches(&model->identity,
		&owner->identity))
	{
		result.status = SG_RUNE_COMPACT_PRODUCTION_MODEL_REJECTED;
		SG_RuneCompactArtifactLoaderReset(&owner->loader);
		memset(&owner->identity, 0, sizeof(owner->identity));
		return result;
	}
	if (!ArtifactWeaponLawCurrent(model, &source_authority))
	{
		result.status = SG_RUNE_COMPACT_PRODUCTION_WEAPON_AUTHORITY_REJECTED;
		SG_RuneCompactArtifactLoaderReset(&owner->loader);
		memset(&owner->identity, 0, sizeof(owner->identity));
		return result;
	}
	if (!SpatialIndexFromAcceptedModel(model, &owner->spatial_index))
	{
		result.status = SG_RUNE_COMPACT_PRODUCTION_SPATIAL_INDEX_REJECTED;
		SG_RuneCompactArtifactLoaderReset(&owner->loader);
		SG_RuneSourceAuthorityDestroy(source_authority);
		memset(&owner->identity, 0, sizeof(owner->identity));
		return result;
	}
	memset(&authority, 0, sizeof(authority));
	result.host = SG_HostLawProductionAcquire(&authority);
	if (result.host.status != SG_HOST_LAW_OK)
	{
		result.status = SG_RUNE_COMPACT_PRODUCTION_HOST_AUTHORITY_REJECTED;
		SG_RuneCompactSpatialIndexDestroy(owner->spatial_index);
		owner->spatial_index = NULL;
		SG_RuneCompactArtifactLoaderReset(&owner->loader);
		SG_RuneSourceAuthorityDestroy(source_authority);
		memset(&owner->identity, 0, sizeof(owner->identity));
		return result;
	}
	rune_identity = RuntimeIdentity(&owner->identity);
	observation_owner = SG_BotLocalizationObservationOwner();
	result.runtime = SG_CompactRuntimeLevelInstall(&owner->runtime, model,
		&owner->identity, owner->spatial_index, observation_owner,
		&authority, rune_identity,
		owner->identity.entity_semantics_id);
	if (result.runtime != SG_COMPACT_RUNTIME_LEVEL_OK)
	{
		result.status = SG_RUNE_COMPACT_PRODUCTION_RUNTIME_REJECTED;
		SG_CompactRuntimeLevelClear(&owner->runtime);
		SG_RuneCompactSpatialIndexDestroy(owner->spatial_index);
		owner->spatial_index = NULL;
		SG_RuneCompactArtifactLoaderReset(&owner->loader);
		SG_RuneSourceAuthorityDestroy(source_authority);
		memset(&owner->identity, 0, sizeof(owner->identity));
		return result;
	}
	service = SG_CompactRuntimeLevelFieldService(&owner->runtime);
	/* Effective spawn semantics are a live optional authority: a missing or
	 * stale source does not tear down an otherwise accepted compact runtime.
	 * The snapshot owner instead publishes complete UNKNOWN roots, which the
	 * field rejects as a traversable crossing. */
	(void)SG_RuneCompactPortalSnapshotSourcePrepare(model, &portal_source);
	if (service == NULL || !SG_RuneCompactPortalSnapshotCreate(model, service,
		portal_source != NULL ?
			SG_RuneCompactPortalSnapshotSourceEffectiveSemantics(portal_source) :
			NULL, &portal_snapshot))
	{
		result.status = SG_RUNE_COMPACT_PRODUCTION_PORTAL_SNAPSHOT_REJECTED;
		SG_RuneCompactPortalSnapshotDestroy(portal_snapshot);
		SG_RuneCompactPortalSnapshotSourceDestroy(portal_source);
		SG_CompactRuntimeLevelClear(&owner->runtime);
		SG_RuneCompactSpatialIndexDestroy(owner->spatial_index);
		owner->spatial_index = NULL;
		SG_RuneCompactArtifactLoaderReset(&owner->loader);
		SG_RuneSourceAuthorityDestroy(source_authority);
		memset(&owner->identity, 0, sizeof(owner->identity));
		return result;
	}
	owner->portal_snapshot_source = portal_source;
	owner->portal_snapshot = portal_snapshot;
	if (!SG_RuneCompactLearningProductionInstall(owner, model,
		&owner->identity))
	{
		result.status = SG_RUNE_COMPACT_PRODUCTION_LEARNING_REJECTED;
		SG_RuneCompactPortalSnapshotDestroy(portal_snapshot);
		SG_RuneCompactPortalSnapshotSourceDestroy(portal_source);
		owner->portal_snapshot = NULL;
		owner->portal_snapshot_source = NULL;
		SG_CompactRuntimeLevelClear(&owner->runtime);
		SG_RuneCompactSpatialIndexDestroy(owner->spatial_index);
		owner->spatial_index = NULL;
		SG_RuneCompactArtifactLoaderReset(&owner->loader);
		SG_RuneSourceAuthorityDestroy(source_authority);
		memset(&owner->identity, 0, sizeof(owner->identity));
		return result;
	}
	owner->source_authority = source_authority;
	owner->active = 1U;
	result.status = SG_RUNE_COMPACT_PRODUCTION_OK;
	return result;
}

void SG_RuneCompactProductionClear(sg_rune_compact_production_t *owner)
{
	if (owner == NULL)
		return;
	SG_RuneCompactLearningProductionRetire(owner);
	SG_RuneCompactPortalSnapshotDestroy(owner->portal_snapshot);
	SG_RuneCompactPortalSnapshotSourceDestroy(owner->portal_snapshot_source);
	SG_RuneSourceAuthorityDestroy(owner->source_authority);
	SG_CompactRuntimeLevelClear(&owner->runtime);
	SG_RuneCompactSpatialIndexDestroy(owner->spatial_index);
	SG_RuneCompactArtifactLoaderDestroy(&owner->loader);
	memset(owner, 0, sizeof(*owner));
}

int SG_RuneCompactProductionCurrent(
	const sg_rune_compact_production_t *owner)
{
	sg_rune_compact_wire_info_t artifact_info;

	if (owner == NULL || owner->initialized != 1U || owner->active != 1U ||
		!SG_CompactRuntimeLevelCurrent(&owner->runtime) ||
		SG_RuneCompactArtifactLoaderSnapshot(&owner->loader) == NULL ||
		owner->spatial_index == NULL || owner->portal_snapshot == NULL ||
		owner->learning == NULL || owner->source_authority == NULL ||
		SG_RuneSourceAuthorityCurrent(owner->source_authority) !=
			SG_RUNE_SOURCE_OK)
		return 0;
	memset(&artifact_info, 0, sizeof(artifact_info));
	return SG_RuneCompactArtifactLoaderSnapshotInfo(&owner->loader,
		&artifact_info) &&
		artifact_info.wire_version == SG_RUNE_COMPACT_WIRE_VERSION &&
		SG_RuneCompactIdentityMatches(&artifact_info.identity,
			&owner->identity);
}

const sg_rune_compact_model_t *SG_RuneCompactProductionModel(
	const sg_rune_compact_production_t *owner)
{
	return SG_RuneCompactProductionCurrent(owner) ?
		SG_RuneCompactArtifactLoaderSnapshot(&owner->loader) : NULL;
}

int SG_RuneCompactProductionArtifactInfo(
	const sg_rune_compact_production_t *owner,
	sg_rune_compact_wire_info_t *info_out)
{
	if (info_out != NULL)
	memset(info_out, 0, sizeof(*info_out));
	if (!SG_RuneCompactProductionCurrent(owner) || info_out == NULL)
		return 0;
	return SG_RuneCompactArtifactLoaderSnapshotInfo(&owner->loader, info_out);
}

int SG_RuneCompactProductionFrameSnapshot(
	sg_rune_compact_production_t *owner, uint64_t frame_sequence,
	sg_rune_compact_portal_snapshot_frame_t *frame_out)
{
	const sg_rune_compact_model_t *model;
	const sg_bsp_entity_semantics_t *effective = NULL;

	if (frame_out != NULL)
		memset(frame_out, 0, sizeof(*frame_out));
	if (!SG_RuneCompactProductionCurrent(owner) || frame_out == NULL ||
		frame_sequence == 0U)
		return 0;
	model = SG_RuneCompactArtifactLoaderSnapshot(&owner->loader);
	if (model == NULL)
		return 0;
	if (owner->portal_snapshot_source != NULL &&
		SG_RuneCompactPortalSnapshotSourceCurrent(
			owner->portal_snapshot_source, model))
		effective = SG_RuneCompactPortalSnapshotSourceEffectiveSemantics(
			owner->portal_snapshot_source);
	if (!SG_RuneCompactPortalSnapshotBindEffective(owner->portal_snapshot,
		effective))
		return 0;
	return SG_RuneCompactPortalSnapshotPublish(owner->portal_snapshot,
		frame_sequence, frame_out);
}

const char *SG_RuneCompactProductionStatusString(
	sg_rune_compact_production_status_t status)
{
	static const char *const names[SG_RUNE_COMPACT_PRODUCTION_STATUS_COUNT] = {
		"ok",
		"invalid argument",
		"already active",
		"not initialized",
		"loader initialization rejected",
		"artifact rejected",
		"decoded model rejected",
		"spatial index rejected",
		"host authority rejected",
		"weapon authority rejected",
		"runtime installation rejected",
		"portal snapshot installation rejected",
		"learning consumer installation rejected"
	};

	return (uint32_t)status < (uint32_t)SG_RUNE_COMPACT_PRODUCTION_STATUS_COUNT ?
		names[(uint32_t)status] : "unknown compact production status";
}
