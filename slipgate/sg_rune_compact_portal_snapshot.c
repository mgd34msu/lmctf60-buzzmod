#include "sg_rune_compact_portal_snapshot.h"

#include "sg_rune_compact_mechanisms.h"
#include "sg_rune_mechanism_catalog.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct sg_rune_compact_portal_snapshot_root_s
{
	uint8_t source_blocked;
	uint8_t destination_blocked;
	uint8_t reserved[2];
} sg_rune_compact_portal_snapshot_root_t;

typedef struct sg_rune_compact_portal_snapshot_mechanism_s
{
	sg_mech_motion_state_t motion_state;
	uint8_t resolved;
	uint8_t phase_known;
	uint8_t reserved[2];
} sg_rune_compact_portal_snapshot_mechanism_t;

struct sg_rune_compact_portal_snapshot_s
{
	const sg_rune_compact_model_t *model;
	const sg_rune_compact_field_service_t *service;
	const sg_bsp_entity_semantics_t *effective_semantics;
	uint64_t service_identity;
	uint64_t service_generation;
	sg_rune_compact_field_portal_root_t *roots;
	sg_rune_compact_portal_snapshot_root_t *root_states;
	uint32_t root_count;
	sg_rune_compact_field_mechanism_phase_t *phases;
	sg_rune_compact_portal_snapshot_mechanism_t *mechanisms;
	uint32_t static_mechanism_count;
	uint32_t authority_mechanism_count;
	sg_rune_compact_field_mechanism_snapshot_t mechanism_snapshot;
	sg_rune_compact_field_portal_root_snapshot_t root_snapshot;
};

static void ClearFrame(sg_rune_compact_portal_snapshot_frame_t *frame_out)
{
	if (frame_out != NULL)
		memset(frame_out, 0, sizeof(*frame_out));
}

static int ArrayCountRepresentable(uint32_t count, size_t element_size)
{
	return (size_t)count <= SIZE_MAX / element_size;
}

static int EffectiveSemanticsShapeValid(
	const sg_bsp_entity_semantics_t *semantics)
{
	uint32_t index;

	if (semantics == NULL)
		return 1;
	if (semantics->source_set_identity == 0U ||
		semantics->source_set_identity == UINT64_MAX ||
		semantics->world.source_set_identity != semantics->source_set_identity ||
		(semantics->entity_count != 0U && semantics->entities == NULL))
		return 0;
	/* The compact mechanism source is a canonical semantic ordinal.  Require
	 * the authoritative semantic table to make that index explicit before any
	 * source provenance can reach the catalog resolver. */
	for (index = 0U; index < semantics->entity_count; index++)
	{
		const sg_bsp_entity_semantic_t *entity = &semantics->entities[index];

		if (entity->source_set_identity != semantics->source_set_identity ||
			entity->canonical_ordinal != index)
			return 0;
	}
	return 1;
}

static int ServiceCurrent(const sg_rune_compact_portal_snapshot_t *snapshot)
{
	return snapshot != NULL && snapshot->model != NULL &&
		snapshot->service != NULL &&
		SG_RuneCompactFieldServiceModel(snapshot->service) == snapshot->model &&
		SG_RuneCompactFieldServiceIdentity(snapshot->service) ==
			snapshot->service_identity && snapshot->service_identity != 0U &&
		SG_RuneCompactFieldServiceGeneration(snapshot->service) ==
			snapshot->service_generation && snapshot->service_generation != 0U;
}

static int TransitionRootState(const sg_rune_compact_model_t *model,
	uint32_t mechanism_index, uint32_t portal_index, uint8_t *source_blocked_out,
	uint8_t *destination_blocked_out)
{
	const sg_rune_compact_static_t *static_data;
	const sg_rune_compact_mechanism_t *mechanism;
	uint32_t transition_index;
	int found = 0;
	uint8_t source_blocked = 0U;
	uint8_t destination_blocked = 0U;

	if (source_blocked_out != NULL)
		*source_blocked_out = 0U;
	if (destination_blocked_out != NULL)
		*destination_blocked_out = 0U;
	if (model == NULL || model->static_data == NULL ||
		mechanism_index >= model->static_data->mechanism_count)
		return 0;
	static_data = model->static_data;
	mechanism = &static_data->mechanisms[mechanism_index];
	if (mechanism->transitions.first > static_data->transition_count ||
		mechanism->transitions.count > static_data->transition_count -
			mechanism->transitions.first)
		return 0;
	for (transition_index = mechanism->transitions.first;
		transition_index < mechanism->transitions.first +
			mechanism->transitions.count; transition_index++) {
		const sg_rune_compact_static_transition_t *transition =
			&static_data->transitions[transition_index];

		if (transition->kind !=
			SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE ||
			transition->mechanism.value != mechanism_index ||
			transition->value.portal_state.portal.value != portal_index)
			continue;
		if (found || transition->value.portal_state.source_blocked > 1U ||
			transition->value.portal_state.destination_blocked > 1U ||
			transition->value.portal_state.source_blocked ==
				transition->value.portal_state.destination_blocked)
			return 0;
		found = 1;
		source_blocked = transition->value.portal_state.source_blocked;
		destination_blocked = transition->value.portal_state.destination_blocked;
	}
	if (!found)
		return 0;
	if (source_blocked_out != NULL)
		*source_blocked_out = source_blocked;
	if (destination_blocked_out != NULL)
		*destination_blocked_out = destination_blocked;
	return 1;
}

static int RootLayoutCurrent(const sg_rune_compact_portal_snapshot_t *snapshot)
{
	uint32_t index;

	if (!ServiceCurrent(snapshot) ||
		SG_RuneCompactFieldServicePortalRootCount(snapshot->service) !=
			snapshot->root_count)
		return 0;
	for (index = 0U; index < snapshot->root_count; index++) {
		sg_rune_compact_portal_index_t portal;
		sg_rune_compact_mechanism_index_t mechanism;

		if (!SG_RuneCompactFieldServicePortalRootAt(snapshot->service, index,
			&portal, &mechanism) ||
			portal.value != snapshot->roots[index].portal.value ||
			mechanism.value != snapshot->roots[index].mechanism.value)
			return 0;
	}
	return 1;
}

static int SemanticSourceOrdinal(const sg_bsp_entity_semantics_t *semantics,
	uint32_t canonical_ordinal, uint32_t *source_ordinal_out)
{
	const sg_bsp_entity_semantic_t *entity;

	if (source_ordinal_out != NULL)
		*source_ordinal_out = 0U;
	if (!EffectiveSemanticsShapeValid(semantics) ||
		canonical_ordinal >= semantics->entity_count)
		return 0;
	entity = &semantics->entities[canonical_ordinal];
	if (source_ordinal_out != NULL)
		*source_ordinal_out = entity->source_entity_ordinal;
	return 1;
}

int SG_RuneCompactPortalSnapshotCreate(const sg_rune_compact_model_t *model,
	const sg_rune_compact_field_service_t *service,
	const sg_bsp_entity_semantics_t *accepted_effective_semantics,
	sg_rune_compact_portal_snapshot_t **snapshot_out)
{
	sg_rune_compact_portal_snapshot_t *snapshot = NULL;
	const sg_rune_compact_static_t *static_data;
	uint32_t index;

	if (snapshot_out == NULL || *snapshot_out != NULL || model == NULL ||
		service == NULL || SG_RuneCompactFieldServiceModel(service) != model ||
		SG_RuneCompactFieldServiceIdentity(service) == 0U ||
		SG_RuneCompactFieldServiceGeneration(service) == 0U ||
		!EffectiveSemanticsShapeValid(accepted_effective_semantics) ||
		model->static_data == NULL)
		return 0;
	*snapshot_out = NULL;
	static_data = model->static_data;
	if ((static_data->mechanism_count != 0U &&
		static_data->mechanisms == NULL) ||
		(static_data->transition_count != 0U &&
		static_data->transitions == NULL) ||
		(model->mechanism_authority_count != 0U &&
		 model->mechanism_authorities == NULL))
		return 0;
	snapshot = calloc(1U, sizeof(*snapshot));
	if (snapshot == NULL)
		return 0;
	snapshot->model = model;
	snapshot->service = service;
	snapshot->effective_semantics = accepted_effective_semantics;
	snapshot->service_identity = SG_RuneCompactFieldServiceIdentity(service);
	snapshot->service_generation = SG_RuneCompactFieldServiceGeneration(service);
	snapshot->root_count = SG_RuneCompactFieldServicePortalRootCount(service);
	snapshot->static_mechanism_count = static_data->mechanism_count;
	snapshot->authority_mechanism_count = model->mechanism_authority_count;
	if (!ArrayCountRepresentable(snapshot->root_count,
		sizeof(*snapshot->roots)) || !ArrayCountRepresentable(
		snapshot->root_count, sizeof(*snapshot->root_states)) ||
		!ArrayCountRepresentable(snapshot->authority_mechanism_count,
			sizeof(*snapshot->phases)) || !ArrayCountRepresentable(
			snapshot->static_mechanism_count, sizeof(*snapshot->mechanisms)))
		goto reject;
	if (snapshot->root_count != 0U) {
		snapshot->roots = calloc((size_t)snapshot->root_count,
			sizeof(*snapshot->roots));
		snapshot->root_states = calloc((size_t)snapshot->root_count,
			sizeof(*snapshot->root_states));
		if (snapshot->roots == NULL || snapshot->root_states == NULL)
			goto reject;
	}
	if (snapshot->authority_mechanism_count != 0U) {
		snapshot->phases = calloc((size_t)snapshot->authority_mechanism_count,
			sizeof(*snapshot->phases));
		if (snapshot->phases == NULL)
			goto reject;
	}
	if (snapshot->static_mechanism_count != 0U) {
		snapshot->mechanisms = calloc((size_t)snapshot->static_mechanism_count,
			sizeof(*snapshot->mechanisms));
		if (snapshot->mechanisms == NULL)
			goto reject;
	}
	for (index = 0U; index < snapshot->root_count; index++) {
		sg_rune_compact_portal_index_t portal;
		sg_rune_compact_mechanism_index_t mechanism;

		if (!SG_RuneCompactFieldServicePortalRootAt(service, index, &portal,
			&mechanism) || !TransitionRootState(model, mechanism.value,
			portal.value, &snapshot->root_states[index].source_blocked,
			&snapshot->root_states[index].destination_blocked))
			goto reject;
		snapshot->roots[index].portal = portal;
		snapshot->roots[index].mechanism = mechanism;
		snapshot->roots[index].state =
			SG_RUNE_COMPACT_FIELD_PORTAL_ROOT_UNKNOWN;
	}
	snapshot->mechanism_snapshot.model_identity = &model->identity;
	snapshot->mechanism_snapshot.phases = snapshot->phases;
	snapshot->mechanism_snapshot.phase_count = 0U;
	snapshot->root_snapshot.model_identity = &model->identity;
	snapshot->root_snapshot.roots = snapshot->roots;
	snapshot->root_snapshot.root_count = snapshot->root_count;
	*snapshot_out = snapshot;
	return 1;

reject:
	SG_RuneCompactPortalSnapshotDestroy(snapshot);
	return 0;
}

int SG_RuneCompactPortalSnapshotBindEffective(
	sg_rune_compact_portal_snapshot_t *snapshot,
	const sg_bsp_entity_semantics_t *accepted_effective_semantics)
{
	if (snapshot == NULL || !EffectiveSemanticsShapeValid(
		accepted_effective_semantics))
		return 0;
	snapshot->effective_semantics = accepted_effective_semantics;
	return 1;
}

int SG_RuneCompactPortalSnapshotPublish(
	sg_rune_compact_portal_snapshot_t *snapshot, uint64_t frame_sequence,
	sg_rune_compact_portal_snapshot_frame_t *frame_out)
{
	const sg_rune_compact_static_t *static_data;
	uint32_t index;
	uint32_t resolved_phase_count = 0U;

	ClearFrame(frame_out);
	if (snapshot == NULL || frame_out == NULL || frame_sequence == 0U ||
		!RootLayoutCurrent(snapshot))
		return 0;
	static_data = snapshot->model->static_data;
	if (snapshot->static_mechanism_count != 0U)
		memset(snapshot->mechanisms, 0,
			(size_t)snapshot->static_mechanism_count *
			sizeof(*snapshot->mechanisms));
	/* Field phase consumers name authority mechanisms.  Resolve the authority
	 * sources directly so independently sorted static records cannot change
	 * the meaning of a phase index. */
	for (index = 0U; index < snapshot->authority_mechanism_count; index++) {
		uint32_t source_ordinal;
		sg_mech_catalog_source_resolution_t resolution;

		if (snapshot->effective_semantics == NULL ||
			!SemanticSourceOrdinal(snapshot->effective_semantics,
				snapshot->model->mechanism_authorities[index].source.entity_ordinal,
				&source_ordinal))
			continue;
		memset(&resolution, 0, sizeof(resolution));
		if (!SG_MechCatalogResolveSourceOrdinal(source_ordinal, &resolution) ||
			resolution.phase_known == 0U)
			continue;
		snapshot->phases[resolved_phase_count].mechanism.value = index;
		snapshot->phases[resolved_phase_count].phase = resolution.phase;
		resolved_phase_count++;
	}
	/* Portal root occupancy remains a static projection.  Its mechanism
	 * indices are resolved independently through static source provenance. */
	for (index = 0U; index < snapshot->static_mechanism_count; index++) {
		uint32_t source_ordinal;
		sg_mech_catalog_source_resolution_t resolution;

		if (snapshot->effective_semantics == NULL ||
			!SemanticSourceOrdinal(snapshot->effective_semantics,
				static_data->mechanisms[index].source.entity_ordinal,
				&source_ordinal))
			continue;
		memset(&resolution, 0, sizeof(resolution));
		if (!SG_MechCatalogResolveSourceOrdinal(source_ordinal, &resolution))
			continue;
		snapshot->mechanisms[index].motion_state = resolution.motion_state;
		snapshot->mechanisms[index].resolved = 1U;
		snapshot->mechanisms[index].phase_known = resolution.phase_known;
	}
	for (index = 0U; index < snapshot->root_count; index++) {
		sg_rune_compact_field_portal_root_t *root = &snapshot->roots[index];
		const sg_rune_compact_portal_snapshot_root_t *root_state =
			&snapshot->root_states[index];
		const uint32_t mechanism = root->mechanism.value;

		root->state = SG_RUNE_COMPACT_FIELD_PORTAL_ROOT_UNKNOWN;
		if (mechanism >= snapshot->static_mechanism_count ||
			!snapshot->mechanisms[mechanism].resolved ||
			!snapshot->mechanisms[mechanism].phase_known)
			continue;
		switch (snapshot->mechanisms[mechanism].motion_state) {
		case SG_MECH_MOTION_AT_ORIGIN:
			root->state = root_state->source_blocked != 0U ?
				SG_RUNE_COMPACT_FIELD_PORTAL_ROOT_BLOCKED :
				SG_RUNE_COMPACT_FIELD_PORTAL_ROOT_UNBLOCKED;
			break;
		case SG_MECH_MOTION_AT_DESTINATION:
			root->state = root_state->destination_blocked != 0U ?
				SG_RUNE_COMPACT_FIELD_PORTAL_ROOT_BLOCKED :
				SG_RUNE_COMPACT_FIELD_PORTAL_ROOT_UNBLOCKED;
			break;
		case SG_MECH_MOTION_TO_DESTINATION:
		case SG_MECH_MOTION_TO_ORIGIN:
		default:
			break;
		}
	}
	snapshot->mechanism_snapshot.frame_sequence = frame_sequence;
	snapshot->mechanism_snapshot.phase_count = resolved_phase_count;
	snapshot->root_snapshot.frame_sequence = frame_sequence;
	frame_out->mechanisms = &snapshot->mechanism_snapshot;
	frame_out->portal_roots = &snapshot->root_snapshot;
	return 1;
}

void SG_RuneCompactPortalSnapshotDestroy(
	sg_rune_compact_portal_snapshot_t *snapshot)
{
	if (snapshot == NULL)
		return;
	free(snapshot->mechanisms);
	free(snapshot->phases);
	free(snapshot->root_states);
	free(snapshot->roots);
	free(snapshot);
}
