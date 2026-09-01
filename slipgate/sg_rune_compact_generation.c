#include "sg_rune_compact_generation.h"

#include <stdlib.h>
#include <string.h>

#include "sg_rune_compact_builder_owner.h"
#include "sg_rune_compact_mechanisms.h"
#include "sg_rune_compact_weapon_relations.h"

static void SetResult(sg_rune_compact_generation_result_t *result,
	sg_rune_compact_generation_error_code_t error,
	sg_rune_compact_generation_stage_t stage)
{
	result->error = error;
	result->stage = stage;
}

static void ReportProgress(const sg_rune_compact_generation_input_t *input,
	sg_rune_compact_generation_result_t *result,
	sg_rune_compact_generation_stage_t stage)
{
	if (input->progress != NULL)
		input->progress(input->progress_context, stage, &result->accepted);
}

static int IdentityMatches(const sg_rune_compact_identity_t *left,
	const sg_rune_compact_identity_t *right)
{
	return left != NULL && right != NULL &&
		SG_RuneCompactIdentityMatches(left, right);
}

typedef struct generation_work_s
{
	sg_rune_compact_builder_t *builder;
	sg_rune_compact_geometry_t *geometry;
	sg_rune_compact_response_partition_t *response_partition;
	sg_rune_compact_mechanisms_t *mechanisms;
	sg_rune_compact_static_materializer_t *static_materializer;
	sg_rune_compact_movement_fields_t *movement_fields;
	sg_rune_compact_weapon_relations_t *relations;
	sg_rune_compact_weapon_field_t *weapon_field;
	sg_rune_compact_composer_t *composer;
	sg_rune_compact_wire_decoded_t *decoded;
	unsigned char *image;
	size_t image_size;
	sg_rune_compact_builder_view_t builder_view;
	sg_rune_compact_builder_owner_view_t owner_view;
	sg_rune_compact_geometry_view_t geometry_view;
	sg_rune_compact_response_partition_view_t response_view;
	sg_rune_compact_mechanisms_view_t mechanisms_view;
	sg_rune_compact_static_t static_view;
	sg_rune_compact_movement_fields_view_t movement_view;
	sg_rune_compact_weapon_relations_view_t relation_view;
	sg_rune_compact_weapon_field_view_t weapon_view;
	sg_rune_compact_identity_t static_identity;
	sg_rune_compact_identity_t movement_identity;
} generation_work_t;

static void DestroyWork(generation_work_t *work)
{
	if (work == NULL)
		return;
	SG_RuneCompactWireDestroy(work->decoded);
	free(work->image);
	SG_RuneCompactComposerDestroy(work->composer);
	SG_RuneCompactWeaponFieldDestroy(work->weapon_field);
	SG_RuneCompactWeaponRelationsDestroy(work->relations);
	SG_RuneCompactMovementFieldsDestroy(work->movement_fields);
	SG_RuneCompactStaticMaterializerDestroy(work->static_materializer);
	SG_RuneCompactMechanismsDestroy(work->mechanisms);
	SG_RuneCompactResponsePartitionDestroy(work->response_partition);
	SG_RuneCompactGeometryDestroy(work->geometry);
	SG_RuneCompactBuilderDestroy(work->builder);
}

static void SetRelationError(sg_rune_compact_generation_result_t *result,
	const sg_rune_compact_weapon_relations_error_t *error)
{
	if (error == NULL)
		return;
	result->relation_error.code = (uint32_t)error->code;
	result->relation_error.record = error->record;
	result->relation_error.expected = error->expected;
	result->relation_error.observed = error->observed;
}

static void CopyGeometryView(sg_rune_compact_static_geometry_view_t *output,
	const sg_rune_compact_geometry_view_t *input)
{
	output->identity = input->identity;
	output->cells = input->cells;
	output->cell_count = input->cell_count;
	output->facets = input->facets;
	output->facet_count = input->facet_count;
	output->incidences = input->incidences;
	output->incidence_count = input->incidence_count;
	output->cell_incidences = input->cell_incidences;
	output->cell_incidence_count = input->cell_incidence_count;
	output->vertices = input->vertices;
	output->vertex_count = input->vertex_count;
	output->portals = input->portals;
	output->portal_count = input->portal_count;
	output->source_surfaces = input->source_surfaces;
	output->source_surface_count = input->source_surface_count;
	output->source_surface_vertices = input->source_surface_vertices;
	output->source_surface_vertex_count = input->source_surface_vertex_count;
	output->compact_cells_for_configuration_cell =
		input->compact_cells_for_configuration_cell;
	output->compact_cells_for_configuration_cell_count =
		input->compact_cells_for_configuration_cell_count;
	output->configuration_cell_compact_cells =
		input->configuration_cell_compact_cells;
	output->configuration_cell_compact_cell_count =
		input->configuration_cell_compact_cell_count;
}

static void RecordGeometryCounts(sg_rune_compact_generation_counts_t *counts,
	const sg_rune_compact_geometry_view_t *view)
{
	counts->geometry_cells = view->cell_count;
	counts->geometry_facets = view->facet_count;
	counts->geometry_incidences = view->incidence_count;
	counts->geometry_cell_incidences = view->cell_incidence_count;
	counts->geometry_vertices = view->vertex_count;
	counts->geometry_portals = view->portal_count;
}

static void RecordStaticCounts(sg_rune_compact_generation_counts_t *counts,
	const sg_rune_compact_static_t *view)
{
	counts->static_mechanisms = view->mechanism_count;
	counts->static_mechanism_controllers = view->mechanism_controller_count;
	counts->static_mechanism_edges = view->mechanism_edge_count;
	counts->static_transitions = view->transition_count;
	counts->static_landmarks = view->landmark_count;
	counts->static_landmark_cells = view->landmark_cell_count;
	counts->static_facet_annotations = view->facet_annotation_count;
	counts->static_portal_mechanisms = view->portal_mechanism_count;
}

static void RecordResponseCounts(sg_rune_compact_generation_counts_t *counts,
	const sg_rune_compact_response_partition_view_t *view)
{
	counts->response_fragments = view->source_fragment_count;
	counts->response_halfspaces = view->source_halfspace_count;
	counts->response_patches = view->target_patch_count;
	counts->response_vertices = view->target_vertex_count;
	counts->response_splits = view->split_count;
	counts->response_pairs = view->response_pair_count;
	counts->response_candidate_groups = view->candidate_group_count;
	counts->response_source_endpoint_groups =
		view->source_endpoint_group_count;
	counts->response_target_endpoint_groups =
		view->target_endpoint_group_count;
}

static void RecordMechanismCounts(sg_rune_compact_generation_counts_t *counts,
	const sg_rune_compact_mechanisms_view_t *view)
{
	counts->mechanism_authorities = view->mechanism_count;
	counts->mechanism_controllers = view->controller_count;
	counts->mechanism_topology_edges = view->topology_edge_count;
	counts->mechanism_transitions = view->transition_count;
}

static void RecordMovementCounts(sg_rune_compact_generation_counts_t *counts,
	const sg_rune_compact_movement_fields_view_t *view)
{
	counts->movement_capabilities = view->capability_count;
	counts->movement_states = view->state_count;
	counts->movement_fibers = view->fiber_count;
	counts->movement_hook_targets = view->hook_target_count;
	counts->movement_fiber_function_refs = view->fiber_function_ref_count;
	counts->movement_analytic_functions = view->analytic.function_count;
}

static void RecordWeaponCounts(sg_rune_compact_generation_counts_t *counts,
	const sg_rune_compact_weapon_field_view_t *view)
{
	counts->weapon_kernels = view->kernel_count;
	counts->weapon_attachments = view->attachment_count;
	counts->weapon_relation_spans = view->relation_span_count;
	counts->weapon_relation_refs = view->relation_ref_count;
	counts->weapon_function_refs = view->weapon_function_ref_count;
	counts->weapon_analytic_functions = view->analytic.function_count;
}

static void RecordComposerCounts(sg_rune_compact_generation_counts_t *counts,
	const sg_rune_compact_model_t *model)
{
	counts->composer_cells = model->cell_count;
	counts->composer_facets = model->facet_count;
	counts->composer_incidences = model->incidence_count;
	counts->composer_portals = model->portal_count;
	counts->composer_movement_capabilities = model->movement_capability_count;
	counts->composer_movement_states = model->movement_state_count;
	counts->composer_movement_fibers = model->movement_fiber_count;
	counts->composer_movement_hook_targets = model->movement_hook_target_count;
	counts->composer_movement_fiber_function_refs =
		model->movement_fiber_function_ref_count;
	counts->composer_weapon_kernels = model->weapon_kernel_count;
	counts->composer_weapon_attachments = model->weapon_attachment_count;
	counts->composer_weapon_relation_spans = model->weapon_relation_span_count;
	counts->composer_weapon_relation_refs = model->weapon_relation_ref_count;
	counts->composer_analytic_functions = model->analytic->function_count;
}

int SG_RuneCompactGenerationRun(
	const sg_rune_compact_generation_input_t *input,
	sg_rune_compact_generation_result_t *result)
{
	generation_work_t work;
	sg_rune_compact_static_materializer_input_t static_input;
	sg_rune_compact_movement_fields_input_t movement_input;
	sg_rune_compact_weapon_field_input_t weapon_input;
	sg_rune_compact_weapon_relations_error_t relation_error;
	sg_rune_compact_weapon_field_status_t weapon_status;
	sg_rune_compact_builder_input_t builder_input;
	const sg_rune_compact_model_t *model;
	int success = 0;

	if (result == NULL)
		return 0;
	memset(result, 0, sizeof(*result));
	if (input == NULL || input->destination == NULL) {
		SetResult(result, SG_RUNE_COMPACT_GENERATION_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_GENERATION_STAGE_NONE);
		return 0;
	}
	builder_input = input->builder_input;
	memset(&work, 0, sizeof(work));
	memset(&static_input, 0, sizeof(static_input));
	memset(&movement_input, 0, sizeof(movement_input));
	memset(&weapon_input, 0, sizeof(weapon_input));
	memset(&relation_error, 0, sizeof(relation_error));

	if (!SG_RuneCompactBuilderBuild(&builder_input, &work.builder,
		&result->builder_error)) {
		SetResult(result, SG_RUNE_COMPACT_GENERATION_ERROR_BUILDER_REJECTED,
			SG_RUNE_COMPACT_GENERATION_STAGE_BUILDER);
		goto cleanup;
	}
	if (!SG_RuneCompactBuilderRead(work.builder, &work.builder_view) ||
		!SG_RuneCompactBuilderOwnerRead(work.builder, &work.owner_view) ||
		!IdentityMatches(&work.builder_view.identity, &work.owner_view.identity) ||
		work.owner_view.host_law == NULL || work.owner_view.weapon_law == NULL ||
		work.owner_view.semantics == NULL || work.owner_view.entity_semantics == NULL ||
		work.owner_view.visibility == NULL) {
		SetResult(result, SG_RUNE_COMPACT_GENERATION_ERROR_IDENTITY_MISMATCH,
			SG_RUNE_COMPACT_GENERATION_STAGE_BUILDER);
		goto cleanup;
	}
	ReportProgress(input, result, SG_RUNE_COMPACT_GENERATION_STAGE_BUILDER);

	if (!SG_RuneCompactGeometryMaterialize(work.builder,
		input->geometry_allocator, &work.geometry, &result->geometry_error) ||
		!SG_RuneCompactGeometryRead(work.geometry, &work.geometry_view)) {
		SetResult(result, SG_RUNE_COMPACT_GENERATION_ERROR_GEOMETRY_REJECTED,
			SG_RUNE_COMPACT_GENERATION_STAGE_GEOMETRY);
		goto cleanup;
	}
	if (!IdentityMatches(&work.builder_view.identity, &work.geometry_view.identity)) {
		SetResult(result, SG_RUNE_COMPACT_GENERATION_ERROR_IDENTITY_MISMATCH,
			SG_RUNE_COMPACT_GENERATION_STAGE_GEOMETRY);
		goto cleanup;
	}
	RecordGeometryCounts(&result->accepted, &work.geometry_view);
	ReportProgress(input, result, SG_RUNE_COMPACT_GENERATION_STAGE_GEOMETRY);

	if (!SG_RuneCompactResponsePartitionBuild(work.builder, work.geometry,
		NULL, &work.response_partition,
		&result->response_error) ||
		!SG_RuneCompactResponsePartitionRead(work.response_partition,
			&work.response_view) ||
		!SG_RuneCompactResponsePartitionSealValid(&work.response_view)) {
		SetResult(result, SG_RUNE_COMPACT_GENERATION_ERROR_RESPONSE_REJECTED,
			SG_RUNE_COMPACT_GENERATION_STAGE_RESPONSE);
		goto cleanup;
	}
	if (!IdentityMatches(&work.builder_view.identity,
		&work.response_view.identity)) {
		SetResult(result, SG_RUNE_COMPACT_GENERATION_ERROR_IDENTITY_MISMATCH,
			SG_RUNE_COMPACT_GENERATION_STAGE_RESPONSE);
		goto cleanup;
	}
	RecordResponseCounts(&result->accepted, &work.response_view);
	ReportProgress(input, result, SG_RUNE_COMPACT_GENERATION_STAGE_RESPONSE);

	if (!SG_RuneCompactMechanismsMaterialize(work.builder, work.geometry,
		&work.mechanisms, &result->mechanisms_error) ||
		!SG_RuneCompactMechanismsRead(work.mechanisms, &work.mechanisms_view)) {
		SetResult(result, SG_RUNE_COMPACT_GENERATION_ERROR_MECHANISMS_REJECTED,
			SG_RUNE_COMPACT_GENERATION_STAGE_MECHANISMS);
		goto cleanup;
	}
	if (!IdentityMatches(&work.builder_view.identity,
		&work.mechanisms_view.identity)) {
		SetResult(result, SG_RUNE_COMPACT_GENERATION_ERROR_IDENTITY_MISMATCH,
			SG_RUNE_COMPACT_GENERATION_STAGE_MECHANISMS);
		goto cleanup;
	}
	RecordMechanismCounts(&result->accepted, &work.mechanisms_view);
	ReportProgress(input, result,
		SG_RUNE_COMPACT_GENERATION_STAGE_MECHANISMS);

	CopyGeometryView(&static_input.geometry, &work.geometry_view);
	static_input.entities = work.owner_view.entity_semantics;
	static_input.configuration = work.owner_view.semantics;
	static_input.visibility = work.owner_view.visibility;
	static_input.mechanisms = work.mechanisms;
	if (!SG_RuneCompactStaticMaterializerBuild(&static_input,
		&work.static_materializer, &result->static_error) ||
		!SG_RuneCompactStaticMaterializerReadBound(work.static_materializer,
			&work.static_identity, &work.static_view)) {
		SetResult(result, SG_RUNE_COMPACT_GENERATION_ERROR_STATIC_REJECTED,
			SG_RUNE_COMPACT_GENERATION_STAGE_STATIC);
		goto cleanup;
	}
	if (!IdentityMatches(&work.builder_view.identity, &work.static_identity)) {
		SetResult(result, SG_RUNE_COMPACT_GENERATION_ERROR_IDENTITY_MISMATCH,
			SG_RUNE_COMPACT_GENERATION_STAGE_STATIC);
		goto cleanup;
	}
	RecordStaticCounts(&result->accepted, &work.static_view);
	ReportProgress(input, result, SG_RUNE_COMPACT_GENERATION_STAGE_STATIC);

	movement_input.builder = work.builder;
	movement_input.host_owner = builder_input.construction;
	movement_input.geometry_owner = work.geometry;
	movement_input.response_owner = work.response_partition;
	movement_input.mechanisms_owner = work.mechanisms;
	movement_input.collision_scene = input->collision_scene;
	movement_input.static_owner = work.static_materializer;
	if (!SG_RuneCompactMovementFieldsBuild(&movement_input, &work.movement_fields,
		&result->movement_error) ||
		!SG_RuneCompactMovementFieldsReadBound(work.movement_fields,
			&work.movement_identity, &work.movement_view)) {
		SetResult(result, SG_RUNE_COMPACT_GENERATION_ERROR_MOVEMENT_REJECTED,
			SG_RUNE_COMPACT_GENERATION_STAGE_MOVEMENT);
		goto cleanup;
	}
	if (!IdentityMatches(&work.builder_view.identity, &work.movement_identity)) {
		SetResult(result, SG_RUNE_COMPACT_GENERATION_ERROR_IDENTITY_MISMATCH,
			SG_RUNE_COMPACT_GENERATION_STAGE_MOVEMENT);
		goto cleanup;
	}
	RecordMovementCounts(&result->accepted, &work.movement_view);
	ReportProgress(input, result, SG_RUNE_COMPACT_GENERATION_STAGE_MOVEMENT);

	if (!SG_RuneCompactWeaponRelationsBuild(work.builder, work.geometry,
		work.response_partition, &work.relations, &relation_error) ||
		!SG_RuneCompactWeaponRelationsRead(work.relations, &work.relation_view) ||
		work.relation_view.response.exact_live_prefire_trace_required != 1U) {
		SetRelationError(result, &relation_error);
		SetResult(result, SG_RUNE_COMPACT_GENERATION_ERROR_RELATION_REJECTED,
			SG_RUNE_COMPACT_GENERATION_STAGE_RELATION);
		goto cleanup;
	}
	if (!IdentityMatches(&work.builder_view.identity, &work.relation_view.identity)) {
		SetResult(result, SG_RUNE_COMPACT_GENERATION_ERROR_IDENTITY_MISMATCH,
			SG_RUNE_COMPACT_GENERATION_STAGE_RELATION);
		goto cleanup;
	}
	result->accepted.relations = work.relation_view.response.fact_count;
	result->accepted.relation_candidate_groups =
		work.relation_view.response.candidate_group_count;
	result->accepted.relation_occluders =
		work.relation_view.response.occluder_count;
	ReportProgress(input, result, SG_RUNE_COMPACT_GENERATION_STAGE_RELATION);

	weapon_input.identity = &work.builder_view.identity;
	weapon_input.compact_profiles = work.builder_view.weapon_profiles;
	weapon_input.resolved_profiles = work.builder_view.resolved_weapon_profiles;
	weapon_input.profile_count = work.builder_view.weapon_profile_count;
	weapon_input.weapon_law = work.owner_view.weapon_law;
	weapon_input.physics_abi_id = work.builder_view.identity.physics_abi_id;
	weapon_input.weapon_law_id = work.builder_view.identity.weapon_law_id;
	weapon_input.relations_owner = work.relations;
	weapon_status = SG_RuneCompactWeaponFieldBuild(&weapon_input,
		&work.weapon_field, &result->weapon_error);
	if (weapon_status != SG_RUNE_COMPACT_WEAPON_FIELD_OK ||
		!SG_RuneCompactWeaponFieldReadBound(work.weapon_field,
			&work.weapon_view)) {
		SetResult(result, SG_RUNE_COMPACT_GENERATION_ERROR_WEAPON_REJECTED,
			SG_RUNE_COMPACT_GENERATION_STAGE_WEAPON);
		goto cleanup;
	}
	if (!IdentityMatches(&work.builder_view.identity,
		&work.weapon_view.identity)) {
		SetResult(result, SG_RUNE_COMPACT_GENERATION_ERROR_IDENTITY_MISMATCH,
			SG_RUNE_COMPACT_GENERATION_STAGE_WEAPON);
		goto cleanup;
	}
	RecordWeaponCounts(&result->accepted, &work.weapon_view);
	ReportProgress(input, result, SG_RUNE_COMPACT_GENERATION_STAGE_WEAPON);

	if (!SG_RuneCompactComposerBuild(work.builder, work.geometry,
		work.mechanisms, work.static_materializer, work.movement_fields,
		work.relations,
		work.weapon_field,
		&work.composer, &result->composer_error) ||
		(work.composer == NULL) ||
		(model = SG_RuneCompactComposerModel(work.composer)) == NULL ||
		model->analytic == NULL) {
		SetResult(result, SG_RUNE_COMPACT_GENERATION_ERROR_COMPOSER_REJECTED,
			SG_RUNE_COMPACT_GENERATION_STAGE_COMPOSER);
		goto cleanup;
	}
	if (!IdentityMatches(&work.builder_view.identity, &model->identity)) {
		SetResult(result, SG_RUNE_COMPACT_GENERATION_ERROR_IDENTITY_MISMATCH,
			SG_RUNE_COMPACT_GENERATION_STAGE_COMPOSER);
		goto cleanup;
	}
	RecordComposerCounts(&result->accepted, model);
	ReportProgress(input, result, SG_RUNE_COMPACT_GENERATION_STAGE_COMPOSER);

	if (!SG_RuneCompactArtifactEncode(model, &work.image, &work.image_size,
		&result->wire_encode_error)) {
		SetResult(result, SG_RUNE_COMPACT_GENERATION_ERROR_WIRE_ENCODE_REJECTED,
			SG_RUNE_COMPACT_GENERATION_STAGE_WIRE_ENCODE);
		goto cleanup;
	}
	result->accepted.encoded_bytes = work.image_size;
	ReportProgress(input, result,
		SG_RUNE_COMPACT_GENERATION_STAGE_WIRE_ENCODE);

	if (!SG_RuneCompactWireDecode(work.image, work.image_size,
		&work.builder_view.identity, &work.decoded,
		&result->wire_decode_error)) {
		SetResult(result, SG_RUNE_COMPACT_GENERATION_ERROR_WIRE_DECODE_REJECTED,
			SG_RUNE_COMPACT_GENERATION_STAGE_WIRE_DECODE);
		goto cleanup;
	}
	ReportProgress(input, result,
		SG_RUNE_COMPACT_GENERATION_STAGE_WIRE_DECODE);

	result->publication = SG_RuneCompactArtifactPublish(input->destination,
		work.image, work.image_size, &work.builder_view.identity,
		input->artifact_fs_ops);
	result->published = result->publication.published;
	result->durable = result->publication.durable;
	if (!result->published) {
		SetResult(result, SG_RUNE_COMPACT_GENERATION_ERROR_PUBLICATION_REJECTED,
			SG_RUNE_COMPACT_GENERATION_STAGE_PUBLICATION);
		goto cleanup;
	}
	ReportProgress(input, result, SG_RUNE_COMPACT_GENERATION_STAGE_PUBLICATION);
	SetResult(result, SG_RUNE_COMPACT_GENERATION_ERROR_NONE,
		SG_RUNE_COMPACT_GENERATION_STAGE_PUBLICATION);
	success = 1;

cleanup:
	DestroyWork(&work);
	return success;
}

const char *SG_RuneCompactGenerationErrorString(
	sg_rune_compact_generation_error_code_t error)
{
	switch (error) {
	case SG_RUNE_COMPACT_GENERATION_ERROR_NONE:
		return "none";
	case SG_RUNE_COMPACT_GENERATION_ERROR_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_RUNE_COMPACT_GENERATION_ERROR_BUILDER_REJECTED:
		return "builder rejected";
	case SG_RUNE_COMPACT_GENERATION_ERROR_GEOMETRY_REJECTED:
		return "geometry rejected";
	case SG_RUNE_COMPACT_GENERATION_ERROR_RESPONSE_REJECTED:
		return "response partition rejected";
	case SG_RUNE_COMPACT_GENERATION_ERROR_MECHANISMS_REJECTED:
		return "mechanism authority rejected";
	case SG_RUNE_COMPACT_GENERATION_ERROR_STATIC_REJECTED:
		return "static materializer rejected";
	case SG_RUNE_COMPACT_GENERATION_ERROR_MOVEMENT_REJECTED:
		return "movement fields rejected";
	case SG_RUNE_COMPACT_GENERATION_ERROR_RELATION_REJECTED:
		return "weapon relations rejected";
	case SG_RUNE_COMPACT_GENERATION_ERROR_WEAPON_REJECTED:
		return "weapon field rejected";
	case SG_RUNE_COMPACT_GENERATION_ERROR_COMPOSER_REJECTED:
		return "compact composer rejected";
	case SG_RUNE_COMPACT_GENERATION_ERROR_WIRE_ENCODE_REJECTED:
		return "wire encode rejected";
	case SG_RUNE_COMPACT_GENERATION_ERROR_WIRE_DECODE_REJECTED:
		return "wire decode rejected";
	case SG_RUNE_COMPACT_GENERATION_ERROR_PUBLICATION_REJECTED:
		return "artifact publication rejected";
	case SG_RUNE_COMPACT_GENERATION_ERROR_IDENTITY_MISMATCH:
		return "identity mismatch";
	case SG_RUNE_COMPACT_GENERATION_ERROR_CODE_COUNT:
		break;
	}
	return "unknown";
}

const char *SG_RuneCompactGenerationStageString(
	sg_rune_compact_generation_stage_t stage)
{
	switch (stage) {
	case SG_RUNE_COMPACT_GENERATION_STAGE_NONE:
		return "none";
	case SG_RUNE_COMPACT_GENERATION_STAGE_BUILDER:
		return "builder";
	case SG_RUNE_COMPACT_GENERATION_STAGE_GEOMETRY:
		return "geometry";
	case SG_RUNE_COMPACT_GENERATION_STAGE_RESPONSE:
		return "response";
	case SG_RUNE_COMPACT_GENERATION_STAGE_MECHANISMS:
		return "mechanisms";
	case SG_RUNE_COMPACT_GENERATION_STAGE_STATIC:
		return "static";
	case SG_RUNE_COMPACT_GENERATION_STAGE_MOVEMENT:
		return "movement";
	case SG_RUNE_COMPACT_GENERATION_STAGE_RELATION:
		return "relations";
	case SG_RUNE_COMPACT_GENERATION_STAGE_WEAPON:
		return "weapon";
	case SG_RUNE_COMPACT_GENERATION_STAGE_COMPOSER:
		return "composer";
	case SG_RUNE_COMPACT_GENERATION_STAGE_WIRE_ENCODE:
		return "wire encode";
	case SG_RUNE_COMPACT_GENERATION_STAGE_WIRE_DECODE:
		return "wire decode";
	case SG_RUNE_COMPACT_GENERATION_STAGE_PUBLICATION:
		return "publication";
	case SG_RUNE_COMPACT_GENERATION_STAGE_COUNT:
		break;
	}
	return "unknown";
}
