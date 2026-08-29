#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slipgate/sg_rune_dynamics_model.h"
#include "slipgate/sg_rune_dynamics_model_internal.h"

typedef sg_field_status_t (*field_create_fn)(
	const sg_field_model_publication_t *, sg_field_service_t **);
typedef sg_field_status_t (*field_publication_issue_fn)(
	const sg_field_model_source_t *, const sg_rune_runtime_snapshot_t *,
	const sg_rune_dynamics_model_t *, sg_field_model_publication_t **);
typedef void (*field_publication_destroy_fn)(sg_field_model_publication_t *);
typedef sg_field_status_t (*field_resolve_fn)(sg_field_service_t *,
	const sg_destination_terminal_t *, const sg_field_environment_t *, uint64_t,
	sg_field_handle_t *);
typedef sg_field_status_t (*field_refresh_fn)(sg_field_service_t *,
	const sg_field_handle_t *, const sg_destination_terminal_t *,
	const sg_field_environment_t *, uint64_t, sg_field_handle_t *);
typedef sg_field_status_t (*field_query_fn)(const sg_field_service_t *,
	const sg_field_handle_t *, const sg_localized_field_state_t *,
	const sg_field_environment_t *, sg_field_option_t *, size_t,
	sg_field_guidance_t *);
typedef sg_field_status_t (*field_release_fn)(sg_field_service_t *,
	const sg_field_handle_t *);

_Static_assert(_Generic(&SG_FieldServiceCreate, field_create_fn: 1,
	default: 0), "field create must consume an authenticated publication");
_Static_assert(_Generic(&SG_FieldModelPublicationIssue,
	field_publication_issue_fn: 1, default: 0),
	"only an opaque model source may publish the aggregate dynamics model");
_Static_assert(_Generic(&SG_FieldModelPublicationDestroy,
	field_publication_destroy_fn: 1, default: 0),
	"model publication lifetime must remain owner controlled");
_Static_assert(_Generic(&SG_FieldServiceResolve, field_resolve_fn: 1,
	default: 0), "field resolve signature changed");
_Static_assert(_Generic(&SG_FieldServiceRefresh, field_refresh_fn: 1,
	default: 0), "field refresh signature changed");
_Static_assert(_Generic(&SG_FieldServiceQuery, field_query_fn: 1,
	default: 0), "field query signature changed");
_Static_assert(_Generic(&SG_FieldServiceRelease, field_release_fn: 1,
	default: 0), "field release signature changed");
_Static_assert(SG_FIELD_STATUS_MODEL_INCOMPLETE != SG_FIELD_STATUS_PROOF_FAILED,
	"missing product coverage must not masquerade as a failed proof");
_Static_assert(SG_FIELD_STATUS_NUMERICAL_ERROR != SG_FIELD_STATUS_PROOF_FAILED,
	"numerical failure must not alter exact classification");

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static sg_rune_stable_id_t Stable(uint32_t domain, uint32_t ordinal)
{
	return (sg_rune_stable_id_t){
		.source_set_identity = 1U,
		.high = (uint64_t)domain << 32,
		.low = (uint64_t)ordinal << 32
	};
}

static sg_rune_interval_t Interval(float minimum, float maximum)
{
	return (sg_rune_interval_t){ minimum, maximum };
}

static sg_rune_interval3_t Interval3(float minimum, float maximum)
{
	return (sg_rune_interval3_t){
		Interval(minimum, maximum),
		Interval(minimum, maximum),
		Interval(minimum, maximum)
	};
}

static sg_rune_state_mode_t SupportedMode(void)
{
	sg_rune_state_mode_t mode = { 0 };

	mode.kind = SG_RUNE_STATE_MODE_SUPPORTED;
	mode.value.supported.support_surface.value =
		Stable(SG_RUNE_ORDER_SURFACE, 1U);
	return mode;
}

typedef struct dynamics_fixture_s
{
	sg_rune_cell_t cell;
	sg_rune_surface_t surface;
	sg_rune_phase_basis_t phase;
	sg_rune_model_t rune_model;
	sg_phase_coordinate_t phase_coordinate;
	sg_rune_runtime_snapshot_t snapshot;
	sg_rune_state_vertex_t vertices[16];
	sg_rune_state_chart_t charts[1];
	sg_rune_state_simplex_t simplices[2];
	sg_rune_state_domain_t domains[1];
	sg_rune_control_fiber_t fibers[1];
	sg_rune_control_domain_t control_domains[1];
	sg_rune_response_patch_t patches[1];
	sg_rune_boundary_transfer_t transfers[1];
	sg_field_reach_atom_t reach_atoms[2];
	sg_rune_state_simplex_owner_t simplex_owners[2];
	sg_rune_domain_support_certificate_t domain_support[1];
	sg_rune_domain_boundary_facet_t domain_boundary_facets[14];
	sg_field_refinement_vertex_ref_t domain_boundary_vertices[98];
	uint32_t exact_words[1];
	sg_field_outcome_image_t outcome_images[3];
	sg_field_outcome_cover_piece_t outcome_cover_pieces[4];
	sg_field_guard_requirement_t guard_requirements[2];
	sg_field_guard_effect_t guard_effects[1];
	sg_field_outcome_t outcomes[3];
	sg_field_choice_t choices[2];
	sg_field_local_progress_kernel_t local_progress_kernels[1];
	sg_field_refinement_node_t refinement_nodes[2];
	sg_field_refinement_vertex_t refinement_vertices[16];
	sg_field_refinement_vertex_ref_t node_vertices[16];
	sg_field_refinement_face_t refinement_faces[16];
	sg_field_refinement_vertex_ref_t face_vertices[112];
	sg_field_refinement_face_incidence_t face_incidences[16];
	sg_field_refinement_face_ref_t node_faces[16];
	uint32_t refinement_roots[2];
	sg_field_refinement_node_ref_t local_progress_sources[1];
	sg_field_choice_ref_t local_progress_choices[1];
	sg_field_progress_target_t local_progress_targets[3];
	sg_rune_field_region_t regions[1];
	uint32_t chart_leaf_regions[1];
	uint32_t domain_leaf_regions[1];
	uint32_t patch_leaf_regions[1];
	sg_rune_dynamics_model_t dynamics;
} dynamics_fixture_t;

static void BuildFixture(dynamics_fixture_t *fixture)
{
	size_t index;
	sg_rune_state_chart_t *chart;
	sg_rune_response_patch_t *patch;
	sg_rune_boundary_transfer_t *transfer;

	memset(fixture, 0, sizeof(*fixture));
	fixture->rune_model.version = SG_RUNE_MODEL_VERSION;
	fixture->rune_model.schema_tag = SG_RUNE_MODEL_SCHEMA_TAG;
	fixture->rune_model.flags = SG_RUNE_MODEL_IMMUTABLE |
		SG_RUNE_MODEL_EXACT_BOUND | SG_RUNE_MODEL_NO_RUNTIME_ACTORS;
	fixture->rune_model.identity.source_set_identity = 1U;
	fixture->rune_model.completeness.state = SG_RUNE_COMPLETENESS_COMPLETE;
	fixture->rune_model.cells = &fixture->cell;
	fixture->rune_model.cell_count = 1U;
	fixture->cell.id.value = Stable(SG_RUNE_ORDER_CELL, 1U);
	fixture->surface.id.value = Stable(SG_RUNE_ORDER_SURFACE, 1U);
	fixture->rune_model.surfaces = &fixture->surface;
	fixture->rune_model.surface_count = 1U;
	fixture->rune_model.phases = &fixture->phase;
	fixture->rune_model.phase_count = 1U;
	fixture->snapshot.identity = 2U;
	fixture->snapshot.topology_revision = 3U;
	fixture->snapshot.cell_count = 1U;
	fixture->snapshot.phase_count = 1U;
	fixture->snapshot.region_count = 1U;
	fixture->snapshot.model = &fixture->rune_model;
	fixture->snapshot.phases = &fixture->phase_coordinate;

	for (index = 0U; index < 16U; index++)
	{
		fixture->vertices[index].id.value =
			Stable(SG_RUNE_ORDER_STATE_VERTEX, (uint32_t)index + 1U);
		fixture->vertices[index].chart.value =
			Stable(SG_RUNE_ORDER_STATE_CHART, 1U);
	}
	fixture->vertices[1].position.value[0] = 1.0f;
	fixture->vertices[2].position.value[1] = 1.0f;
	fixture->vertices[3].position.value[2] = 1.0f;
	fixture->vertices[4].velocity.value[0] = 1.0f;
	fixture->vertices[5].velocity.value[1] = 1.0f;
	fixture->vertices[6].velocity.value[2] = 1.0f;
	fixture->vertices[7].elapsed_ms = 1.0f;
	for (index = 0U; index < 7U; index++)
	{
		fixture->vertices[index + 8U].position =
			fixture->vertices[index + 1U].position;
		fixture->vertices[index + 8U].velocity =
			fixture->vertices[index + 1U].velocity;
		fixture->vertices[index + 8U].elapsed_ms =
			fixture->vertices[index + 1U].elapsed_ms;
	}
	fixture->vertices[15].position.value[0] = 1.0f;
	fixture->vertices[15].position.value[1] = 1.0f;
	fixture->vertices[15].position.value[2] = 1.0f;
	fixture->vertices[15].velocity.value[0] = 1.0f;
	fixture->vertices[15].velocity.value[1] = 1.0f;
	fixture->vertices[15].velocity.value[2] = 1.0f;
	fixture->vertices[15].elapsed_ms = 1.0f;
	chart = &fixture->charts[0];
	chart->id.value = Stable(SG_RUNE_ORDER_STATE_CHART, 1U);
	chart->configuration_cell.value = Stable(SG_RUNE_ORDER_CELL, 1U);
	chart->mode = SupportedMode();
	chart->embedding.position = Interval3(-64.0f, 64.0f);
	chart->embedding.velocity = Interval3(-320.0f, 320.0f);
	chart->embedding.elapsed_ms = Interval(0.0f, 100.0f);
	chart->embedding.dimension_count = SG_RUNE_STATE_DIMENSION_COUNT;
	chart->state_vertices = (sg_rune_state_vertex_span_t){ 0U, 16U };
	chart->simplices = (sg_rune_state_simplex_span_t){ 0U, 2U };
	chart->state_domains = (sg_rune_state_domain_span_t){ 0U, 1U };
	chart->control_fibers = (sg_rune_control_fiber_span_t){ 0U, 1U };
	chart->response_patches = (sg_rune_response_patch_span_t){ 0U, 1U };
	chart->boundary_transfers =
		(sg_rune_boundary_transfer_span_t){ 0U, 1U };
	chart->coverage_proof.value = Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 1U);

	fixture->simplices[0].id.value =
		Stable(SG_RUNE_ORDER_STATE_SIMPLEX, 1U);
	fixture->simplices[0].chart = chart->id;
	fixture->simplices[0].vertices =
		(sg_rune_state_vertex_span_t){ 0U, 8U };
	fixture->simplices[1].id.value =
		Stable(SG_RUNE_ORDER_STATE_SIMPLEX, 2U);
	fixture->simplices[1].chart = chart->id;
	fixture->simplices[1].vertices =
		(sg_rune_state_vertex_span_t){ 8U, 8U };
	fixture->domains[0].id.value = Stable(SG_RUNE_ORDER_STATE_DOMAIN, 1U);
	fixture->domains[0].chart = chart->id;
	fixture->domains[0].simplices =
		(sg_rune_state_simplex_span_t){ 0U, 2U };
	fixture->fibers[0].id.value = Stable(SG_RUNE_ORDER_CONTROL_FIBER, 1U);
	fixture->fibers[0].source_chart = chart->id;
	fixture->fibers[0].domain.value =
		Stable(SG_RUNE_ORDER_CONTROL_DOMAIN, 1U);
	fixture->fibers[0].condition.value =
		Stable(SG_RUNE_ORDER_GUARD_CONDITION, 1U);
	fixture->fibers[0].coverage_proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 2U);
	fixture->control_domains[0].id.value =
		Stable(SG_RUNE_ORDER_CONTROL_DOMAIN, 1U);
	fixture->control_domains[0].source_chart = chart->id;
	fixture->control_domains[0].forward_move = Interval(-400.0f, 400.0f);
	fixture->control_domains[0].side_move = Interval(-400.0f, 400.0f);
	fixture->control_domains[0].up_move = Interval(-400.0f, 400.0f);
	fixture->control_domains[0].allowed_buttons = UINT32_MAX;
	fixture->control_domains[0].admissibility_proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 7U);

	patch = &fixture->patches[0];
	patch->id.value = Stable(SG_RUNE_ORDER_RESPONSE_PATCH, 1U);
	patch->source_chart = chart->id;
	patch->source_simplex = fixture->simplices[0].id;
	patch->controls = (sg_rune_control_fiber_span_t){ 0U, 1U };
	patch->flow.position = Interval3(0.0f, 1.0f);
	patch->flow.velocity = Interval3(0.0f, 1.0f);
	patch->flow.elapsed_ms = Interval(0.0f, 1.0f);
	patch->running_cost = (sg_rune_cost_bounds_t){ 1000U, 2000U };
	patch->destination_domains =
		(sg_rune_state_domain_span_t){ 0U, 1U };
	patch->flow_proof.value = Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 3U);

	transfer = &fixture->transfers[0];
	transfer->id.value = Stable(SG_RUNE_ORDER_BOUNDARY_TRANSFER, 1U);
	transfer->source_chart = chart->id;
	transfer->source_domain = fixture->domains[0].id;
	transfer->condition.value = Stable(SG_RUNE_ORDER_GUARD_CONDITION, 2U);
	transfer->destination_chart = chart->id;
	transfer->destination_domain = fixture->domains[0].id;
	transfer->reset_enclosure = patch->flow;
	transfer->transfer_proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 4U);

	for (index = 0U; index < 2U; index++)
	{
		fixture->reach_atoms[index].id.value =
			Stable(SG_RUNE_ORDER_FIELD_REACH_ATOM, (uint32_t)index + 1U);
		fixture->reach_atoms[index].domain = fixture->domains[0].id;
		fixture->reach_atoms[index].simplices =
			(sg_rune_state_simplex_span_t){ (uint32_t)index, 1U };
		fixture->reach_atoms[index].state_bounds.position =
			Interval3(0.0f, 1.0f);
		fixture->reach_atoms[index].state_bounds.velocity =
			Interval3(0.0f, 1.0f);
		fixture->reach_atoms[index].state_bounds.elapsed_ms =
			Interval(0.0f, 1.0f);
		fixture->reach_atoms[index].partition_proof.value =
			Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, (uint32_t)index + 20U);
		fixture->simplex_owners[index].simplex = fixture->simplices[index].id;
		fixture->simplex_owners[index].domain = fixture->domains[0].id;
		fixture->simplex_owners[index].atom = fixture->reach_atoms[index].id;
		fixture->simplex_owners[index].proof.value = Stable(
			SG_RUNE_ORDER_SIMPLEX_OWNERSHIP_PROOF, (uint32_t)index + 1U);
		fixture->refinement_nodes[index].id.value = Stable(
			SG_RUNE_ORDER_FIELD_REFINEMENT_NODE, (uint32_t)index + 1U);
		fixture->refinement_nodes[index].parent = UINT32_MAX;
		fixture->refinement_nodes[index].atom = fixture->reach_atoms[index].id;
		fixture->refinement_nodes[index].vertices =
			(sg_field_refinement_vertex_ref_span_t){ (uint32_t)(index * 8U), 8U };
		fixture->refinement_nodes[index].faces =
			(sg_field_refinement_face_ref_span_t){ (uint32_t)(index * 8U), 8U };
		fixture->refinement_nodes[index].orientation = -1;
		fixture->refinement_nodes[index].state_bounds =
			fixture->reach_atoms[index].state_bounds;
		fixture->refinement_nodes[index].interpolation_error.position =
			Interval3(0.0f, 0.0f);
		fixture->refinement_nodes[index].interpolation_error.velocity =
			Interval3(0.0f, 0.0f);
		fixture->refinement_nodes[index].interpolation_error.elapsed_ms =
			Interval(0.0f, 0.0f);
		fixture->refinement_nodes[index].geometry_proof.value =
			Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, (uint32_t)index + 30U);
		fixture->refinement_nodes[index].interpolation_proof.value =
			Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, (uint32_t)index + 32U);
		fixture->refinement_roots[index] = (uint32_t)index;
	}
	for (index = 0U; index < 9U; index++)
	{
		const sg_rune_state_vertex_t *source = index < 8U ?
			&fixture->vertices[index] : &fixture->vertices[15];
		fixture->refinement_vertices[index].id.value = Stable(
			SG_RUNE_ORDER_FIELD_REFINEMENT_VERTEX, (uint32_t)index + 1U);
		fixture->refinement_vertices[index].position = source->position;
		fixture->refinement_vertices[index].velocity = source->velocity;
		fixture->refinement_vertices[index].elapsed_ms = source->elapsed_ms;
		fixture->refinement_vertices[index].proof.value = Stable(
			SG_RUNE_ORDER_DYNAMICS_PROOF, (uint32_t)index + 100U);
	}
	for (index = 0U; index < 8U; index++)
	{
		fixture->node_vertices[index] = fixture->refinement_vertices[index].id;
		fixture->node_vertices[index + 8U] =
			fixture->refinement_vertices[index + 1U].id;
	}
	for (index = 0U; index < 15U; index++)
	{
		fixture->refinement_faces[index].id.value = Stable(
			SG_RUNE_ORDER_FIELD_REFINEMENT_FACE, (uint32_t)index + 1U);
		fixture->refinement_faces[index].vertices =
			(sg_field_refinement_vertex_ref_span_t){ (uint32_t)(index * 7U), 7U };
		fixture->refinement_faces[index].parent_face.value =
			SG_RUNE_STABLE_ID_NONE;
		fixture->refinement_faces[index].proof.value = Stable(
			SG_RUNE_ORDER_DYNAMICS_PROOF, (uint32_t)index + 130U);
	}
	/* Face zero is the exact shared face: node 0 omits vertex 0 and node 1
	 * omits vertex 7. All remaining faces are exterior. */
	fixture->refinement_faces[0].incidences =
		(sg_field_refinement_incidence_span_t){ 0U, 2U };
	fixture->face_incidences[0].node = fixture->refinement_nodes[0].id;
	fixture->face_incidences[0].local_face = 0U;
	fixture->face_incidences[0].orientation = -1;
	fixture->face_incidences[1].node = fixture->refinement_nodes[1].id;
	fixture->face_incidences[1].local_face = 7U;
	fixture->face_incidences[1].orientation = 1;
	for (index = 0U; index < 7U; index++)
		fixture->face_vertices[index] = fixture->node_vertices[index + 1U];
	fixture->node_faces[0] = fixture->refinement_faces[0].id;
	fixture->node_faces[15] = fixture->refinement_faces[0].id;
	{
		size_t face_index = 1U;
		size_t incidence_index = 2U;
		size_t node_index;
		for (node_index = 0U; node_index < 2U; node_index++)
		{
			size_t local_face;
			for (local_face = 0U; local_face < 8U; local_face++)
			{
				size_t node_vertex;
				size_t face_vertex = 0U;
				if ((node_index == 0U && local_face == 0U) ||
				    (node_index == 1U && local_face == 7U))
					continue;
				fixture->refinement_faces[face_index].incidences =
					(sg_field_refinement_incidence_span_t){
						(uint32_t)incidence_index, 1U };
				fixture->face_incidences[incidence_index].node =
					fixture->refinement_nodes[node_index].id;
				fixture->face_incidences[incidence_index].local_face =
					(uint8_t)local_face;
				fixture->face_incidences[incidence_index].orientation =
					(local_face & 1U) != 0U ? 1 : -1;
				fixture->node_faces[node_index * 8U + local_face] =
					fixture->refinement_faces[face_index].id;
				for (node_vertex = 0U; node_vertex < 8U; node_vertex++)
					if (node_vertex != local_face)
						fixture->face_vertices[face_index * 7U +
							face_vertex++] = fixture->node_vertices[
								node_index * 8U + node_vertex];
				face_index++;
				incidence_index++;
			}
		}
	}
	fixture->domain_support[0].domain = fixture->domains[0].id;
	fixture->domain_support[0].boundary_facets =
		(sg_rune_domain_boundary_facet_span_t){ 0U, 14U };
	fixture->domain_support[0].normalized_volume.magnitude =
		(sg_rune_exact_word_span_t){ 0U, 1U };
	fixture->domain_support[0].normalized_volume.exponent = 0;
	fixture->domain_support[0].proof.value =
		Stable(SG_RUNE_ORDER_DOMAIN_SUPPORT_PROOF, 1U);
	fixture->exact_words[0] = 7U;
	for (index = 0U; index < 14U; index++)
	{
		size_t source_face = index + 1U;
		size_t vertex;
		fixture->domain_boundary_facets[index].domain = fixture->domains[0].id;
		fixture->domain_boundary_facets[index].vertices =
			(sg_field_refinement_vertex_ref_span_t){ (uint32_t)(index * 7U), 7U };
		fixture->domain_boundary_facets[index].proof.value = Stable(
			SG_RUNE_ORDER_DOMAIN_BOUNDARY_PROOF, (uint32_t)index + 1U);
		fixture->domain_boundary_facets[index].orientation =
			fixture->face_incidences[
				fixture->refinement_faces[source_face].incidences.first].orientation;
		for (vertex = 0U; vertex < 7U; vertex++)
			fixture->domain_boundary_vertices[index * 7U + vertex] =
				fixture->face_vertices[source_face * 7U + vertex];
	}
	fixture->guard_effects[0].condition = transfer->condition;
	fixture->guard_effects[0].required_before = SG_FIELD_GUARD_FALSE;
	fixture->guard_effects[0].resulting_after = SG_FIELD_GUARD_TRUE;
	fixture->guard_effects[0].controllable = 1U;
	fixture->guard_requirements[0].condition = fixture->fibers[0].condition;
	fixture->guard_requirements[0].required = SG_FIELD_GUARD_TRUE;
	fixture->guard_requirements[1].condition = fixture->transfers[0].condition;
	fixture->guard_requirements[1].required = SG_FIELD_GUARD_FALSE;
	for (index = 0U; index < 3U; index++)
	{
		uint32_t row;
		fixture->outcomes[index].id.value =
			Stable(SG_RUNE_ORDER_FIELD_OUTCOME, (uint32_t)index + 1U);
		for (row = 0U; row < SG_RUNE_STATE_DIMENSION_COUNT; row++)
			fixture->outcomes[index].endpoint.coefficient[row]
				[SG_RUNE_STATE_DIMENSION_COUNT] =
					row == 0U ? 0.25f : 0.125f;
		fixture->outcomes[index].endpoint.exact_rank = 0U;
		fixture->outcomes[index].endpoint.operator_proof.value = Stable(
			SG_RUNE_ORDER_DYNAMICS_PROOF, (uint32_t)index + 60U);
		fixture->outcomes[index].endpoint.image_proof.value = Stable(
			SG_RUNE_ORDER_DYNAMICS_PROOF, (uint32_t)index + 70U);
		fixture->outcomes[index].endpoint.cover_proof.value = Stable(
			SG_RUNE_ORDER_DYNAMICS_PROOF, (uint32_t)index + 80U);
		fixture->outcomes[index].remainder.position = Interval3(0.0f, 0.0f);
		fixture->outcomes[index].remainder.velocity = Interval3(0.0f, 0.0f);
		fixture->outcomes[index].remainder.elapsed_ms = Interval(0.0f, 0.0f);
		fixture->outcome_cover_pieces[index].source_refinement_node =
			fixture->refinement_nodes[0].id;
		fixture->outcome_images[index].id.value = Stable(
			SG_RUNE_ORDER_FIELD_OUTCOME_IMAGE, (uint32_t)index + 1U);
		fixture->outcome_images[index].outcome = fixture->outcomes[index].id;
		fixture->outcome_images[index].source_leaf =
			fixture->refinement_nodes[0].id;
		fixture->outcome_images[index].canonical_image.position.x =
			Interval(0.25f, 0.25f);
		fixture->outcome_images[index].canonical_image.position.y =
			Interval(0.125f, 0.125f);
		fixture->outcome_images[index].canonical_image.position.z =
			Interval(0.125f, 0.125f);
		fixture->outcome_images[index].canonical_image.velocity =
			Interval3(0.125f, 0.125f);
		fixture->outcome_images[index].canonical_image.elapsed_ms =
			Interval(0.125f, 0.125f);
		fixture->outcome_images[index].destination_cover =
			(sg_field_outcome_cover_piece_span_t){ (uint32_t)index, 1U };
		fixture->outcome_images[index].proof.value = Stable(
			SG_RUNE_ORDER_FIELD_OUTCOME_IMAGE_PROOF, (uint32_t)index + 1U);
		fixture->outcome_cover_pieces[index].source_image =
			fixture->outcome_images[index].id;
		fixture->outcome_cover_pieces[index].atom =
			fixture->reach_atoms[1].id;
		fixture->outcome_cover_pieces[index].refinement_node =
			fixture->refinement_nodes[1].id;
		fixture->outcome_cover_pieces[index].image_piece.position.x =
			Interval(0.25f, 0.25f);
		fixture->outcome_cover_pieces[index].image_piece.position.y =
			Interval(0.125f, 0.125f);
		fixture->outcome_cover_pieces[index].image_piece.position.z =
			Interval(0.125f, 0.125f);
		fixture->outcome_cover_pieces[index].image_piece.velocity =
			Interval3(0.125f, 0.125f);
		fixture->outcome_cover_pieces[index].image_piece.elapsed_ms =
			Interval(0.125f, 0.125f);
		fixture->outcome_cover_pieces[index].proof.value = Stable(
			SG_RUNE_ORDER_FIELD_OUTCOME_COVER_PROOF, (uint32_t)index + 1U);
		fixture->outcomes[index].source_images =
			(sg_field_outcome_image_span_t){ (uint32_t)index, 1U };
		fixture->outcomes[index].destination_cover =
			(sg_field_outcome_cover_piece_span_t){ (uint32_t)index, 1U };
		fixture->outcomes[index].cover_split_dimension = 0U;
		fixture->outcomes[index].absolute_time_advance =
			(sg_rune_time_advance_t){ index == 2U ? 0U : 1U,
				index == 2U ? 0U : 1U };
		fixture->outcomes[index].proof.value = Stable(
			SG_RUNE_ORDER_DYNAMICS_PROOF, (uint32_t)index + 40U);
	}
	fixture->outcomes[2].guard_effects =
		(sg_field_guard_effect_span_t){ 0U, 1U };
	fixture->choices[0].id.value = Stable(SG_RUNE_ORDER_FIELD_CHOICE, 1U);
	fixture->choices[0].kind = SG_FIELD_CHOICE_CONTROL;
	fixture->choices[0].authority.control = fixture->fibers[0].id;
	fixture->choices[0].guard_requirements =
		(sg_field_guard_requirement_span_t){ 0U, 1U };
	fixture->choices[0].source_atom = fixture->reach_atoms[0].id;
	fixture->choices[0].outcomes = (sg_field_outcome_span_t){ 0U, 2U };
	fixture->choices[0].cost = (sg_rune_cost_bounds_t){ 0U, 10U };
	fixture->choices[0].proof.value = Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 50U);
	fixture->choices[1].id.value = Stable(SG_RUNE_ORDER_FIELD_CHOICE, 2U);
	fixture->choices[1].kind = SG_FIELD_CHOICE_TRANSFER;
	fixture->choices[1].authority.transfer = fixture->transfers[0].id;
	fixture->choices[1].guard_requirements =
		(sg_field_guard_requirement_span_t){ 1U, 1U };
	fixture->choices[1].source_atom = fixture->reach_atoms[0].id;
	fixture->choices[1].outcomes = (sg_field_outcome_span_t){ 2U, 1U };
	fixture->choices[1].cost = (sg_rune_cost_bounds_t){ 0U, 0U };
	fixture->choices[1].proof.value = Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 51U);
	fixture->local_progress_kernels[0].id.value =
		Stable(SG_RUNE_ORDER_FIELD_LOCAL_PROGRESS, 1U);
	fixture->local_progress_kernels[0].source_atom = fixture->reach_atoms[0].id;
	fixture->local_progress_kernels[0].covered_sources =
		(sg_field_refinement_node_ref_span_t){ 0U, 1U };
	fixture->local_progress_kernels[0].target_atom = fixture->reach_atoms[1].id;
	fixture->local_progress_kernels[0].terminal_parameters.anchor_bounds.position.x =
		Interval(0.25f, 0.25f);
	fixture->local_progress_kernels[0].terminal_parameters.anchor_bounds.position.y =
		Interval(0.125f, 0.125f);
	fixture->local_progress_kernels[0].terminal_parameters.anchor_bounds.position.z =
		Interval(0.125f, 0.125f);
	fixture->local_progress_kernels[0].terminal_parameters.anchor_bounds.velocity =
		Interval3(0.125f, 0.125f);
	fixture->local_progress_kernels[0].terminal_parameters.anchor_bounds.elapsed_ms =
		Interval(0.125f, 0.125f);
	fixture->local_progress_kernels[0].terminal_parameters.position_offset_bounds =
		Interval3(0.0f, 0.0f);
	fixture->local_progress_kernels[0].terminal_parameters.velocity_bounds =
		Interval3(0.125f, 0.125f);
	fixture->local_progress_kernels[0].terminal_parameters.local_elapsed_bounds =
		Interval(0.125f, 0.125f);
	fixture->local_progress_kernels[0].terminal_parameters.proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 54U);
	fixture->local_progress_kernels[0].admissible_choices =
		(sg_field_choice_ref_span_t){ 0U, 1U };
	fixture->local_progress_kernels[0].whole_outcome_targets =
		(sg_field_progress_target_span_t){ 0U, 2U };
	fixture->local_progress_kernels[0].finite_rank = 1U;
	fixture->local_progress_kernels[0].state_lyapunov[0] = 1.0f;
	fixture->local_progress_kernels[0].anchor_lyapunov[0] = -1.0f;
	fixture->local_progress_kernels[0].minimum_lyapunov_decrease = 0.125f;
	fixture->local_progress_kernels[0].proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 52U);
	fixture->local_progress_sources[0] = fixture->refinement_nodes[0].id;
	fixture->local_progress_choices[0] = fixture->choices[0].id;
	fixture->local_progress_targets[0].outcome = fixture->outcomes[0].id;
	fixture->local_progress_targets[0].atom = fixture->reach_atoms[1].id;
	fixture->local_progress_targets[1].outcome = fixture->outcomes[1].id;
	fixture->local_progress_targets[1].atom = fixture->reach_atoms[1].id;

	fixture->regions[0].id.value = Stable(SG_RUNE_ORDER_FIELD_REGION, 1U);
	fixture->regions[0].parent_region = SG_RUNE_FIELD_NO_REGION;
	fixture->regions[0].charts = (sg_rune_state_chart_span_t){ 0U, 1U };
	fixture->regions[0].state_domains =
		(sg_rune_state_domain_span_t){ 0U, 1U };
	fixture->regions[0].response_patches =
		(sg_rune_response_patch_span_t){ 0U, 1U };
	fixture->regions[0].coverage_proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 5U);

	fixture->dynamics.version = SG_RUNE_DYNAMICS_MODEL_VERSION;
	fixture->dynamics.id.value = Stable(SG_RUNE_ORDER_DYNAMICS_MODEL, 1U);
	fixture->dynamics.rune_identity = fixture->snapshot.identity;
	fixture->dynamics.topology_revision = fixture->snapshot.topology_revision;
	fixture->dynamics.state_vertices = fixture->vertices;
	fixture->dynamics.state_vertex_count = 16U;
	fixture->dynamics.state_charts = fixture->charts;
	fixture->dynamics.state_chart_count = 1U;
	fixture->dynamics.state_simplices = fixture->simplices;
	fixture->dynamics.state_simplex_count = 2U;
	fixture->dynamics.state_domains = fixture->domains;
	fixture->dynamics.state_domain_count = 1U;
	fixture->dynamics.control_fibers = fixture->fibers;
	fixture->dynamics.control_fiber_count = 1U;
	fixture->dynamics.control_domains = fixture->control_domains;
	fixture->dynamics.control_domain_count = 1U;
	fixture->dynamics.response_patches = fixture->patches;
	fixture->dynamics.response_patch_count = 1U;
	fixture->dynamics.boundary_transfers = fixture->transfers;
	fixture->dynamics.boundary_transfer_count = 1U;
	fixture->dynamics.reach_atoms = fixture->reach_atoms;
	fixture->dynamics.reach_atom_count = 2U;
	fixture->dynamics.simplex_owners = fixture->simplex_owners;
	fixture->dynamics.simplex_owner_count = 2U;
	fixture->dynamics.domain_support = fixture->domain_support;
	fixture->dynamics.domain_support_count = 1U;
	fixture->dynamics.domain_boundary_facets = fixture->domain_boundary_facets;
	fixture->dynamics.domain_boundary_facet_count = 14U;
	fixture->dynamics.domain_boundary_vertices =
		fixture->domain_boundary_vertices;
	fixture->dynamics.domain_boundary_vertex_count = 98U;
	fixture->dynamics.exact_words = fixture->exact_words;
	fixture->dynamics.exact_word_count = 1U;
	fixture->dynamics.outcome_images = fixture->outcome_images;
	fixture->dynamics.outcome_image_count = 3U;
	fixture->dynamics.outcome_cover_pieces = fixture->outcome_cover_pieces;
	fixture->dynamics.outcome_cover_piece_count = 3U;
	fixture->dynamics.guard_requirements = fixture->guard_requirements;
	fixture->dynamics.guard_requirement_count = 2U;
	fixture->dynamics.guard_effects = fixture->guard_effects;
	fixture->dynamics.guard_effect_count = 1U;
	fixture->dynamics.outcomes = fixture->outcomes;
	fixture->dynamics.outcome_count = 3U;
	fixture->dynamics.choices = fixture->choices;
	fixture->dynamics.choice_count = 2U;
	fixture->dynamics.local_progress_kernels = fixture->local_progress_kernels;
	fixture->dynamics.local_progress_kernel_count = 1U;
	fixture->dynamics.local_progress_sources = fixture->local_progress_sources;
	fixture->dynamics.local_progress_source_count = 1U;
	fixture->dynamics.local_progress_choices = fixture->local_progress_choices;
	fixture->dynamics.local_progress_choice_count = 1U;
	fixture->dynamics.local_progress_targets = fixture->local_progress_targets;
	fixture->dynamics.local_progress_target_count = 2U;
	fixture->dynamics.refinement_tree.id.value =
		Stable(SG_RUNE_ORDER_FIELD_REFINEMENT_TREE, 1U);
	fixture->dynamics.refinement_tree.nodes = fixture->refinement_nodes;
	fixture->dynamics.refinement_tree.node_count = 2U;
	fixture->dynamics.refinement_tree.vertices = fixture->refinement_vertices;
	fixture->dynamics.refinement_tree.vertex_count = 9U;
	fixture->dynamics.refinement_tree.node_vertices = fixture->node_vertices;
	fixture->dynamics.refinement_tree.node_vertex_count = 16U;
	fixture->dynamics.refinement_tree.faces = fixture->refinement_faces;
	fixture->dynamics.refinement_tree.face_count = 15U;
	fixture->dynamics.refinement_tree.face_vertices = fixture->face_vertices;
	fixture->dynamics.refinement_tree.face_vertex_count = 105U;
	fixture->dynamics.refinement_tree.face_incidences =
		fixture->face_incidences;
	fixture->dynamics.refinement_tree.face_incidence_count = 16U;
	fixture->dynamics.refinement_tree.node_faces = fixture->node_faces;
	fixture->dynamics.refinement_tree.node_face_count = 16U;
	fixture->dynamics.refinement_tree.atom_roots = fixture->refinement_roots;
	fixture->dynamics.refinement_tree.atom_count = 2U;
	fixture->dynamics.refinement_tree.proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 53U);
	fixture->dynamics.hierarchy.id.value =
		Stable(SG_RUNE_ORDER_FIELD_HIERARCHY, 1U);
	fixture->dynamics.hierarchy.regions = fixture->regions;
	fixture->dynamics.hierarchy.region_count = 1U;
	fixture->dynamics.hierarchy.chart_leaf_regions =
		fixture->chart_leaf_regions;
	fixture->dynamics.hierarchy.state_domain_leaf_regions =
		fixture->domain_leaf_regions;
	fixture->dynamics.hierarchy.response_patch_leaf_regions =
		fixture->patch_leaf_regions;
	fixture->dynamics.hierarchy.chart_count = 1U;
	fixture->dynamics.hierarchy.state_domain_count = 1U;
	fixture->dynamics.hierarchy.response_patch_count = 1U;
	fixture->dynamics.hierarchy.hierarchy_proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 6U);
	fixture->dynamics.error_contract.id.value =
		Stable(SG_RUNE_ORDER_FIELD_ERROR_CONTRACT, 1U);
	fixture->dynamics.error_contract.cost_quantum_us = 100U;
	fixture->dynamics.error_contract.maximum_value_width_us = 200U;
	fixture->dynamics.error_contract.maximum_bellman_residual_us = 50U;
	fixture->dynamics.error_contract.position_error = Interval3(-0.5f, 0.5f);
	fixture->dynamics.error_contract.velocity_error = Interval3(-1.0f, 1.0f);
	fixture->dynamics.error_contract.time_error = Interval(-0.5f, 0.5f);
}

static void TestTypedIdsAndModes(void)
{
	sg_rune_state_mode_t mode = SupportedMode();

#define CHECK_TYPED_ID(validator, type, domain) do { \
	type value = { Stable(domain, 1U) }; \
	CHECK(validator(&value)); \
	value.value = Stable(SG_RUNE_ORDER_CELL, 1U); \
	CHECK(!validator(&value)); \
} while (0)
	CHECK_TYPED_ID(SG_RuneDynamicsModelIdValid,
		sg_rune_dynamics_model_id_t, SG_RUNE_ORDER_DYNAMICS_MODEL);
	CHECK_TYPED_ID(SG_RuneStateVertexIdValid,
		sg_rune_state_vertex_id_t, SG_RUNE_ORDER_STATE_VERTEX);
	CHECK_TYPED_ID(SG_RuneStateChartIdValid,
		sg_rune_state_chart_id_t, SG_RUNE_ORDER_STATE_CHART);
	CHECK_TYPED_ID(SG_RuneStateSimplexIdValid,
		sg_rune_state_simplex_id_t, SG_RUNE_ORDER_STATE_SIMPLEX);
	CHECK_TYPED_ID(SG_RuneStateDomainIdValid,
		sg_rune_state_domain_id_t, SG_RUNE_ORDER_STATE_DOMAIN);
	CHECK_TYPED_ID(SG_RuneControlFiberIdValid,
		sg_rune_control_fiber_id_t, SG_RUNE_ORDER_CONTROL_FIBER);
	CHECK_TYPED_ID(SG_RuneControlDomainIdValid,
		sg_rune_control_domain_id_t, SG_RUNE_ORDER_CONTROL_DOMAIN);
	CHECK_TYPED_ID(SG_RuneResponsePatchIdValid,
		sg_rune_response_patch_id_t, SG_RUNE_ORDER_RESPONSE_PATCH);
	CHECK_TYPED_ID(SG_RuneBoundaryTransferIdValid,
		sg_rune_boundary_transfer_id_t, SG_RUNE_ORDER_BOUNDARY_TRANSFER);
	CHECK_TYPED_ID(SG_RuneControlDomainRefValid,
		sg_rune_control_domain_ref_t, SG_RUNE_ORDER_CONTROL_DOMAIN);
	CHECK_TYPED_ID(SG_RuneGuardConditionRefValid,
		sg_rune_guard_condition_ref_t, SG_RUNE_ORDER_GUARD_CONDITION);
	CHECK_TYPED_ID(SG_RuneDynamicsProofRefValid,
		sg_rune_dynamics_proof_ref_t, SG_RUNE_ORDER_DYNAMICS_PROOF);
	CHECK_TYPED_ID(SG_RuneSimplexOwnershipProofRefValid,
		sg_rune_simplex_ownership_proof_ref_t,
		SG_RUNE_ORDER_SIMPLEX_OWNERSHIP_PROOF);
	CHECK_TYPED_ID(SG_RuneDomainSupportProofRefValid,
		sg_rune_domain_support_proof_ref_t,
		SG_RUNE_ORDER_DOMAIN_SUPPORT_PROOF);
	CHECK_TYPED_ID(SG_RuneDomainBoundaryProofRefValid,
		sg_rune_domain_boundary_proof_ref_t,
		SG_RUNE_ORDER_DOMAIN_BOUNDARY_PROOF);
	CHECK_TYPED_ID(SG_FieldOutcomeImageIdValid,
		sg_field_outcome_image_id_t, SG_RUNE_ORDER_FIELD_OUTCOME_IMAGE);
	CHECK_TYPED_ID(SG_FieldOutcomeImageProofRefValid,
		sg_field_outcome_image_proof_ref_t,
		SG_RUNE_ORDER_FIELD_OUTCOME_IMAGE_PROOF);
	CHECK_TYPED_ID(SG_FieldOutcomeCoverProofRefValid,
		sg_field_outcome_cover_proof_ref_t,
		SG_RUNE_ORDER_FIELD_OUTCOME_COVER_PROOF);
	CHECK_TYPED_ID(SG_RuneFieldRegionIdValid,
		sg_rune_field_region_id_t, SG_RUNE_ORDER_FIELD_REGION);
	CHECK_TYPED_ID(SG_RuneFieldHierarchyIdValid,
		sg_rune_field_hierarchy_id_t, SG_RUNE_ORDER_FIELD_HIERARCHY);
	CHECK_TYPED_ID(SG_RuneFieldErrorContractIdValid,
		sg_rune_field_error_contract_id_t,
		SG_RUNE_ORDER_FIELD_ERROR_CONTRACT);
	CHECK_TYPED_ID(SG_FieldChoiceIdValid,
		sg_field_choice_id_t, SG_RUNE_ORDER_FIELD_CHOICE);
	CHECK_TYPED_ID(SG_FieldOutcomeIdValid,
		sg_field_outcome_id_t, SG_RUNE_ORDER_FIELD_OUTCOME);
	CHECK_TYPED_ID(SG_FieldReachAtomIdValid,
		sg_field_reach_atom_id_t, SG_RUNE_ORDER_FIELD_REACH_ATOM);
	CHECK_TYPED_ID(SG_FieldLocalProgressIdValid,
		sg_field_local_progress_id_t, SG_RUNE_ORDER_FIELD_LOCAL_PROGRESS);
	CHECK_TYPED_ID(SG_FieldRefinementVertexIdValid,
		sg_field_refinement_vertex_id_t,
		SG_RUNE_ORDER_FIELD_REFINEMENT_VERTEX);
	CHECK_TYPED_ID(SG_FieldRefinementFaceIdValid,
		sg_field_refinement_face_id_t,
		SG_RUNE_ORDER_FIELD_REFINEMENT_FACE);
	CHECK_TYPED_ID(SG_FieldRefinementNodeIdValid,
		sg_field_refinement_node_id_t,
		SG_RUNE_ORDER_FIELD_REFINEMENT_NODE);
	CHECK_TYPED_ID(SG_FieldRefinementTreeIdValid,
		sg_field_refinement_tree_id_t,
		SG_RUNE_ORDER_FIELD_REFINEMENT_TREE);
#undef CHECK_TYPED_ID
	CHECK(SG_RuneStateModeValid(&mode));
	mode.kind = SG_RUNE_STATE_MODE_WATER;
	mode.value.water.medium = SG_RUNE_MEDIUM_WATER;
	mode.value.water.contents =
		SG_RUNE_CONTENTS_WATER | SG_RUNE_CONTENTS_CURRENT_90;
	CHECK(SG_RuneStateModeValid(&mode));
	mode.value.water.contents = SG_RUNE_CONTENTS_WATER |
		SG_RUNE_CONTENTS_LAVA;
	CHECK(!SG_RuneStateModeValid(&mode));
	mode.value.water.contents = SG_RUNE_CONTENTS_WATER |
		SG_RUNE_CONTENTS_SOLID;
	CHECK(!SG_RuneStateModeValid(&mode));
	mode.value.water.medium = SG_RUNE_MEDIUM_LAVA;
	mode.value.water.contents = SG_RUNE_CONTENTS_LAVA;
	CHECK(SG_RuneStateModeValid(&mode));
	mode.value.water.contents = SG_RUNE_CONTENTS_WATER;
	CHECK(!SG_RuneStateModeValid(&mode));
}

static void TestAggregateOwnership(void)
{
	dynamics_fixture_t fixture;

	BuildFixture(&fixture);
	CHECK(SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.dynamics.state_vertices = NULL;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.dynamics.state_vertices = fixture.vertices;
	fixture.vertices[0].chart.value = Stable(SG_RUNE_ORDER_STATE_CHART, 2U);
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.vertices[0].chart = fixture.charts[0].id;
	fixture.patches[0].source_simplex.value =
		Stable(SG_RUNE_ORDER_STATE_DOMAIN, 1U);
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.patches[0].source_simplex = fixture.simplices[0].id;
	fixture.charts[0].configuration_cell.value =
		Stable(SG_RUNE_ORDER_CELL, 999U);
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.charts[0].configuration_cell.value =
		Stable(SG_RUNE_ORDER_CELL, 1U);
	fixture.charts[0].mode.value.supported.support_surface.value =
		Stable(SG_RUNE_ORDER_SURFACE, 999U);
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.charts[0].mode = SupportedMode();
	fixture.fibers[0].domain.value =
		Stable(SG_RUNE_ORDER_CONTROL_DOMAIN, 999U);
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.fibers[0].domain = fixture.control_domains[0].id;
	fixture.dynamics.topology_revision++;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
}

static void TestChoiceOutcomeAndProgressContracts(void)
{
	dynamics_fixture_t fixture;

	BuildFixture(&fixture);
	CHECK(SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	/* AND is the complete outcome span of one choice. */
	fixture.choices[0].outcomes.count = 1U;
	fixture.choices[1].outcomes = (sg_field_outcome_span_t){ 1U, 2U };
	fixture.local_progress_kernels[0].whole_outcome_targets.count = 1U;
	CHECK(SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.choices[0].outcomes.count = 2U;
	fixture.choices[1].outcomes = (sg_field_outcome_span_t){ 2U, 1U };
	fixture.local_progress_kernels[0].whole_outcome_targets.count = 2U;
	fixture.outcomes[1].destination_cover.first = 3U;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.outcomes[1].destination_cover.first = 1U;
	/* Guard changes are explicit controllable effects, never future truth. */
	fixture.guard_effects[0].controllable = 0U;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.guard_effects[0].controllable = 1U;
	fixture.guard_effects[0].resulting_after = SG_FIELD_GUARD_UNKNOWN;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.guard_effects[0].resulting_after = SG_FIELD_GUARD_TRUE;
	/* Local progress must execute through a nonempty same-atom choice set. */
	fixture.local_progress_kernels[0].admissible_choices.count = 0U;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.local_progress_kernels[0].admissible_choices.count = 1U;
	fixture.local_progress_kernels[0].minimum_lyapunov_decrease = 0.0f;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.local_progress_kernels[0].minimum_lyapunov_decrease = 0.125f;
	/* Zero absolute-time advance is legal; local elapsed remains governed by
	 * the independent affine endpoint row. */
	fixture.outcomes[2].absolute_time_advance =
		(sg_rune_time_advance_t){ 0U, 0U };
	CHECK(SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.outcomes[2].absolute_time_advance =
		(sg_rune_time_advance_t){ 2U, 1U };
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.outcomes[2].absolute_time_advance =
		(sg_rune_time_advance_t){ 0U, 0U };
	fixture.refinement_roots[1] = 0U;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
}

static void TestExecutableCoverageAndOperatorRanks(void)
{
	dynamics_fixture_t fixture;
	sg_rune_affine_state_operator_t saved;
	sg_destination_terminal_capture_t first = { 0 };
	sg_destination_terminal_capture_t second = { 0 };
	uint32_t rank;

	BuildFixture(&fixture);
	CHECK(SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	first.anchor.owner_identity = 901U;
	first.anchor.destination.kind = SG_DESTINATION_WAYPOINT;
	first.anchor.destination.value.point.point_id = 902U;
	first.anchor.destination_generation = 903U;
	first.anchor.position[0] = 0.25f;
	first.anchor.position[1] = 0.125f;
	first.anchor.position[2] = 0.125f;
	first.anchor.velocity[0] = 0.125f;
	first.anchor.velocity[1] = 0.125f;
	first.anchor.velocity[2] = 0.125f;
	first.anchor.local_elapsed_ms = 0.125f;
	first.velocity = (sg_destination_interval3_t){
		{ 0.125f, 0.125f }, { 0.125f, 0.125f },
		{ 0.125f, 0.125f }
	};
	first.local_elapsed_ms = (sg_destination_interval_t){ 0.125f, 0.125f };
	second = first;
	second.anchor.owner_identity = 904U;
	second.anchor.destination.value.point.point_id = 905U;
	second.anchor.destination_generation = 906U;
	CHECK(SG_FieldLocalProgressKernelAcceptsCapture(
		&fixture.local_progress_kernels[0], &first));
	CHECK(SG_FieldLocalProgressKernelAcceptsCapture(
		&fixture.local_progress_kernels[0], &second));
	second.anchor.position[0] = 0.5f;
	CHECK(!SG_FieldLocalProgressKernelAcceptsCapture(
		&fixture.local_progress_kernels[0], &second));
	saved = fixture.outcomes[0].endpoint;
	for (rank = 0U; rank <= SG_RUNE_STATE_DIMENSION_COUNT; rank++)
	{
		uint32_t diagonal;
		memset(fixture.outcomes[0].endpoint.coefficient, 0,
			sizeof(fixture.outcomes[0].endpoint.coefficient));
		for (diagonal = 0U; diagonal < rank; diagonal++)
			fixture.outcomes[0].endpoint.coefficient[diagonal][diagonal] = 1.0f;
		fixture.outcomes[0].endpoint.exact_rank = (uint8_t)rank;
		CHECK(SG_FieldOutcomeShapeValid(&fixture.outcomes[0]));
	}
	fixture.outcomes[0].endpoint.exact_rank = 2U;
	memset(fixture.outcomes[0].endpoint.coefficient, 0,
		sizeof(fixture.outcomes[0].endpoint.coefficient));
	fixture.outcomes[0].endpoint.coefficient[0][0] = 1.0f;
	CHECK(!SG_FieldOutcomeShapeValid(&fixture.outcomes[0]));
	fixture.outcomes[0].endpoint = saved;
	CHECK(SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.outcome_cover_pieces[0].atom.value =
		Stable(SG_RUNE_ORDER_STATE_DOMAIN, 1U);
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.outcome_cover_pieces[0].atom = fixture.reach_atoms[1].id;
	fixture.local_progress_sources[0].value =
		Stable(SG_RUNE_ORDER_FIELD_REFINEMENT_NODE, 999U);
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.local_progress_sources[0] = fixture.refinement_nodes[0].id;
	fixture.local_progress_kernels[0].target_atom.value =
		Stable(SG_RUNE_ORDER_FIELD_REACH_ATOM, 999U);
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	BuildFixture(&fixture);
	fixture.choices[0].guard_requirements.count = 2U;
	CHECK(SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.guard_requirements[1] = fixture.guard_requirements[0];
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
}

static void SplitFirstOutcomeCover(dynamics_fixture_t *fixture)
{
	fixture->outcome_cover_pieces[3] = fixture->outcome_cover_pieces[2];
	fixture->outcome_cover_pieces[2] = fixture->outcome_cover_pieces[1];
	fixture->outcome_cover_pieces[1] = fixture->outcome_cover_pieces[0];
	fixture->outcomes[0].destination_cover.count = 2U;
	fixture->outcomes[1].destination_cover.first = 2U;
	fixture->outcomes[2].destination_cover.first = 3U;
	fixture->outcome_images[0].destination_cover.count = 2U;
	fixture->outcome_images[1].destination_cover.first = 2U;
	fixture->outcome_images[2].destination_cover.first = 3U;
	fixture->dynamics.outcome_cover_piece_count = 4U;
	fixture->outcome_cover_pieces[0].atom = fixture->reach_atoms[0].id;
	fixture->outcome_cover_pieces[0].refinement_node =
		fixture->refinement_nodes[0].id;
	fixture->local_progress_targets[2] = fixture->local_progress_targets[1];
	fixture->local_progress_targets[1] = fixture->local_progress_targets[0];
	fixture->local_progress_targets[0].atom = fixture->reach_atoms[0].id;
	fixture->local_progress_kernels[0].whole_outcome_targets.count = 3U;
	fixture->dynamics.local_progress_target_count = 3U;
}

static void TestExactFieldPartitionAndCover(void)
{
	dynamics_fixture_t fixture;
	uint32_t dimension;

	BuildFixture(&fixture);
	fixture.reach_atoms[1].simplices.first = 0U;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	BuildFixture(&fixture);
	fixture.outcome_cover_pieces[0].source_refinement_node =
		fixture.refinement_nodes[1].id;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	BuildFixture(&fixture);
	SplitFirstOutcomeCover(&fixture);
	/* One complete outcome may land on the shared face of two target atoms.
	 * Local progress publishes both targets; the attractor derives ranks. */
	CHECK(SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.outcome_cover_pieces[1].image_piece.position.x.min_value = 0.375f;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.outcome_cover_pieces[1].image_piece.position.x.min_value = 0.125f;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.outcome_cover_pieces[1].image_piece.position.x.min_value = 0.25f;
	fixture.local_progress_kernels[0].whole_outcome_targets.count = 2U;
	fixture.dynamics.local_progress_target_count = 2U;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	BuildFixture(&fixture);
	for (dimension = 0U; dimension < SG_RUNE_STATE_DIMENSION_COUNT;
	     dimension++)
		fixture.outcomes[0].endpoint.coefficient[dimension]
			[SG_RUNE_STATE_DIMENSION_COUNT] = 0.0f;
	fixture.outcome_cover_pieces[0].image_piece.position = Interval3(0.0f, 0.0f);
	fixture.outcome_cover_pieces[0].image_piece.velocity = Interval3(0.0f, 0.0f);
	fixture.outcome_cover_pieces[0].image_piece.elapsed_ms = Interval(0.0f, 0.0f);
	/* The origin is inside node 1's AABB but outside its 7-simplex. */
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	BuildFixture(&fixture);
	fixture.local_progress_kernels[0].terminal_parameters
		.position_offset_bounds.x.max_value = 1.0f;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
}

static void TestExactRootPartitionGeometry(void)
{
	dynamics_fixture_t fixture;
	uint32_t dimension;

	BuildFixture(&fixture);
	/* Put the second opposite vertex on the first simplex side of their
	 * shared face. Both cells remain full rank and keep the same AABB. */
	for (dimension = 0U; dimension < SG_RUNE_STATE_DIMENSION_COUNT;
	     dimension++)
	{
		if (dimension < 3U)
		{
			fixture.vertices[15].position.value[dimension] = 0.125f;
			fixture.refinement_vertices[8].position.value[dimension] = 0.125f;
		}
		else if (dimension < 6U)
		{
			fixture.vertices[15].velocity.value[dimension - 3U] = 0.125f;
			fixture.refinement_vertices[8].velocity.value[dimension - 3U] =
				0.125f;
		}
		else
		{
			fixture.vertices[15].elapsed_ms = 0.125f;
			fixture.refinement_vertices[8].elapsed_ms = 0.125f;
		}
	}
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	BuildFixture(&fixture);
	/* Split the declared shared face into two coincident boundary faces. The
	 * cells remain individually valid and separated, but their domain now has
	 * a topological cut, the zero-width limit of a sliver gap. */
	{
		sg_field_refinement_face_incidence_t second =
			fixture.face_incidences[1];
		size_t face;
		size_t vertex;
		for (face = 1U; face < 15U; face++)
		{
			fixture.face_incidences[face] = fixture.face_incidences[face + 1U];
			fixture.refinement_faces[face].incidences.first--;
		}
		fixture.refinement_faces[0].incidences.count = 1U;
		fixture.refinement_faces[15].id.value =
			Stable(SG_RUNE_ORDER_FIELD_REFINEMENT_FACE, 16U);
		fixture.refinement_faces[15].vertices =
			(sg_field_refinement_vertex_ref_span_t){ 105U, 7U };
		fixture.refinement_faces[15].incidences =
			(sg_field_refinement_incidence_span_t){ 15U, 1U };
		fixture.refinement_faces[15].parent_face.value = SG_RUNE_STABLE_ID_NONE;
		fixture.refinement_faces[15].proof.value =
			Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 200U);
		for (vertex = 0U; vertex < 7U; vertex++)
			fixture.face_vertices[105U + vertex] = fixture.face_vertices[vertex];
		fixture.face_incidences[15] = second;
		fixture.node_faces[15] = fixture.refinement_faces[15].id;
		fixture.dynamics.refinement_tree.face_count = 16U;
		fixture.dynamics.refinement_tree.face_vertex_count = 112U;
	}
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
}

static void TestConformingRefinementAndCanonicalFloats(void)
{
	dynamics_fixture_t fixture;
	size_t vertex;

	CHECK(SG_DestinationFloatValid(0.0f));
	CHECK(!SG_DestinationFloatValid(-0.0f));
	BuildFixture(&fixture);
	fixture.outcomes[0].endpoint.coefficient[0]
		[SG_RUNE_STATE_DIMENSION_COUNT] = -0.0f;
	CHECK(!SG_FieldOutcomeShapeValid(&fixture.outcomes[0]));
	BuildFixture(&fixture);
	fixture.node_faces[15] = fixture.refinement_faces[14].id;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	BuildFixture(&fixture);
	fixture.face_incidences[1].orientation = -1;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	BuildFixture(&fixture);
	fixture.refinement_vertices[8].position.value[0] = -0.0f;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	BuildFixture(&fixture);
	/* Geometric identity is coordinate-derived: the second root may use
	 * independently named copies of every shared-face vertex. */
	for (vertex = 0U; vertex < 7U; vertex++)
	{
		fixture.refinement_vertices[9U + vertex] =
			fixture.refinement_vertices[1U + vertex];
		fixture.refinement_vertices[9U + vertex].id.value = Stable(
			SG_RUNE_ORDER_FIELD_REFINEMENT_VERTEX, (uint32_t)vertex + 10U);
		fixture.refinement_vertices[9U + vertex].proof.value = Stable(
			SG_RUNE_ORDER_DYNAMICS_PROOF, (uint32_t)vertex + 210U);
	}
	fixture.node_vertices[8] = fixture.refinement_vertices[8].id;
	for (vertex = 0U; vertex < 7U; vertex++)
		fixture.node_vertices[9U + vertex] =
			fixture.refinement_vertices[9U + vertex].id;
	fixture.node_faces[8] = fixture.refinement_faces[0].id;
	fixture.face_incidences[1].local_face = 0U;
	for (vertex = 1U; vertex < 8U; vertex++)
	{
		size_t face = 7U + vertex;
		size_t node_vertex;
		size_t face_vertex = 0U;
		fixture.node_faces[8U + vertex] = fixture.refinement_faces[face].id;
		fixture.face_incidences[face + 1U].local_face = (uint8_t)vertex;
		fixture.face_incidences[face + 1U].orientation =
			(vertex & 1U) != 0U ? -1 : 1;
		for (node_vertex = 0U; node_vertex < 8U; node_vertex++)
			if (node_vertex != vertex)
				fixture.face_vertices[face * 7U + face_vertex++] =
					fixture.node_vertices[8U + node_vertex];
	}
	fixture.dynamics.refinement_tree.vertex_count = 16U;
	CHECK(SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
}

static void TestSimplexGeometry(void)
{
	dynamics_fixture_t fixture;
	sg_rune_state_vertex_t saved;

	BuildFixture(&fixture);
	CHECK(SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	saved = fixture.vertices[1];
	fixture.vertices[1].position = fixture.vertices[0].position;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.vertices[1] = saved;
	fixture.vertices[1].position.value[0] = 65.0f;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	fixture.vertices[1] = saved;
	fixture.vertices[7].elapsed_ms = 0.0f;
	fixture.vertices[7].position.value[0] = 0.5f;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
}

static void TestNearDegenerateSimplexGeometry(void)
{
	dynamics_fixture_t fixture;

	BuildFixture(&fixture);
	fixture.charts[0].embedding.position.x = Interval(0.0f, 3.0f);
	fixture.charts[0].embedding.position.y = Interval(0.0f, 1.0f);
	fixture.charts[0].embedding.position.z = Interval(0.0f, 1.0f);
	fixture.charts[0].embedding.velocity = Interval3(0.0f, 1.0f);
	fixture.charts[0].embedding.elapsed_ms = Interval(0.0f, 1.0f);
	fixture.vertices[7].elapsed_ms = 1e-16f;
	fixture.refinement_vertices[7].elapsed_ms = 1e-16f;
	{
		const sg_field_refinement_vertex_t *cell[8];
		size_t vertex;
		for (vertex = 0U; vertex < 8U; vertex++)
			cell[vertex] = &fixture.refinement_vertices[vertex];
		CHECK(SG_FieldRefinementCellFullRank(cell));
	}
}

static void SetRefinementCoordinate(sg_field_refinement_vertex_t *vertex,
	uint32_t dimension, float value)
{
	if (dimension < 3U)
		vertex->position.value[dimension] = value;
	else if (dimension < 6U)
		vertex->velocity.value[dimension - 3U] = value;
	else
		vertex->elapsed_ms = value;
}

static void StandardRefinementSimplex(sg_field_refinement_vertex_t vertices[8])
{
	uint32_t vertex;
	memset(vertices, 0, 8U * sizeof(*vertices));
	for (vertex = 1U; vertex < 8U; vertex++)
		SetRefinementCoordinate(&vertices[vertex], vertex - 1U, 1.0f);
}

static float TestRefinementCoordinate(
	const sg_field_refinement_vertex_t *vertex, uint32_t dimension)
{
	if (dimension < 3U)
		return vertex->position.value[dimension];
	if (dimension < 6U)
		return vertex->velocity.value[dimension - 3U];
	return vertex->elapsed_ms;
}

static int TestRefinementCoordinateOrder(
	const sg_field_refinement_vertex_t *left,
	const sg_field_refinement_vertex_t *right)
{
	uint32_t dimension;
	for (dimension = 0U; dimension < SG_RUNE_STATE_DIMENSION_COUNT;
	     dimension++)
	{
		float left_value = TestRefinementCoordinate(left, dimension);
		float right_value = TestRefinementCoordinate(right, dimension);
		if (left_value < right_value)
			return -1;
		if (left_value > right_value)
			return 1;
	}
	return 0;
}

static void TestRefinementBounds(
	const sg_field_refinement_vertex_t *const vertices[8],
	sg_rune_flow_enclosure_t *bounds)
{
	uint32_t dimension;
	memset(bounds, 0, sizeof(*bounds));
	for (dimension = 0U; dimension < SG_RUNE_STATE_DIMENSION_COUNT;
	     dimension++)
	{
		float minimum = TestRefinementCoordinate(vertices[0], dimension);
		float maximum = minimum;
		uint32_t vertex;
		for (vertex = 1U; vertex < 8U; vertex++)
		{
			float value = TestRefinementCoordinate(vertices[vertex], dimension);
			if (value < minimum)
				minimum = value;
			if (value > maximum)
				maximum = value;
		}
		if (dimension < 3U)
		{
			sg_rune_interval_t *interval = dimension == 0U ?
				&bounds->position.x : dimension == 1U ?
				&bounds->position.y : &bounds->position.z;
			interval->min_value = minimum;
			interval->max_value = maximum;
		}
		else if (dimension < 6U)
		{
			sg_rune_interval_t *interval = dimension == 3U ?
				&bounds->velocity.x : dimension == 4U ?
				&bounds->velocity.y : &bounds->velocity.z;
			interval->min_value = minimum;
			interval->max_value = maximum;
		}
		else
		{
			bounds->elapsed_ms.min_value = minimum;
			bounds->elapsed_ms.max_value = maximum;
		}
	}
}

typedef struct refinement_face_builder_s
{
	uint32_t vertices[7];
	uint32_t nodes[2];
	uint8_t local_faces[2];
	uint8_t depth;
	uint8_t incidence_count;
} refinement_face_builder_t;

static int TestFaceKeySame(const uint32_t left[7], const uint32_t right[7])
{
	return memcmp(left, right, 7U * sizeof(*left)) == 0;
}

static int TestVertexInsideFace(
	const sg_field_refinement_vertex_t *vertex, const uint32_t face[7],
	const sg_field_refinement_vertex_t *vertices)
{
	uint32_t left;
	for (left = 0U; left < 7U; left++)
	{
		uint32_t right;
		if (TestRefinementCoordinateOrder(vertex, &vertices[face[left]]) == 0)
			return 1;
		for (right = left + 1U; right < 7U; right++)
			if (SG_FieldRefinementVertexExactMidpoint(vertex,
				&vertices[face[left]], &vertices[face[right]]))
				return 1;
	}
	return 0;
}

static int TestFaceInsideFace(const uint32_t child[7],
	const uint32_t parent[7], const sg_field_refinement_vertex_t *vertices)
{
	uint32_t vertex;
	for (vertex = 0U; vertex < 7U; vertex++)
		if (!TestVertexInsideFace(&vertices[child[vertex]], parent, vertices))
			return 0;
	return 1;
}

static int TestLocalFaceOrientation(const uint32_t node_vertices[8],
	uint8_t local_face, const sg_field_refinement_vertex_t *vertices)
{
	const sg_field_refinement_vertex_t *cell[8];
	const sg_field_refinement_vertex_t *omitted;
	uint32_t vertex;
	uint32_t sorted_position = 0U;
	for (vertex = 0U; vertex < 8U; vertex++)
		cell[vertex] = &vertices[node_vertices[vertex]];
	omitted = cell[local_face];
	for (vertex = 0U; vertex < 8U; vertex++)
		if (TestRefinementCoordinateOrder(cell[vertex], omitted) < 0)
			sorted_position++;
	return SG_FieldRefinementCellOrientation(cell) *
		((sorted_position & 1U) != 0U ? -1 : 1);
}

static void TestRepeatedRefinementFaceLineage(void)
{
	static const uint32_t node_vertex_indices[5][8] = {
		{ 0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U },
		{ 0U, 1U, 3U, 4U, 5U, 6U, 7U, 8U },
		{ 0U, 2U, 3U, 4U, 5U, 6U, 7U, 8U },
		{ 0U, 1U, 3U, 4U, 5U, 6U, 7U, 9U },
		{ 0U, 3U, 4U, 5U, 6U, 7U, 8U, 9U }
	};
	static const uint8_t depths[5] = { 0U, 1U, 1U, 2U, 2U };
	sg_field_reach_atom_t atom = { 0 };
	sg_field_refinement_tree_t tree = { 0 };
	sg_field_refinement_node_t nodes[5] = { 0 };
	sg_field_refinement_vertex_t vertices[10] = { 0 };
	sg_field_refinement_vertex_ref_t node_vertices[40] = { 0 };
	sg_field_refinement_face_t faces[40] = { 0 };
	sg_field_refinement_vertex_ref_t face_vertices[280] = { 0 };
	sg_field_refinement_face_incidence_t incidences[40] = { 0 };
	sg_field_refinement_face_ref_t node_faces[40] = { 0 };
	refinement_face_builder_t builders[40] = { 0 };
	uint32_t face_for_local[5][8] = { { 0 } };
	uint32_t children[4] = { 1U, 2U, 3U, 4U };
	uint32_t roots[1] = { 0U };
	size_t face_count = 0U;
	size_t incidence_count = 0U;
	uint32_t node;

	StandardRefinementSimplex(vertices);
	vertices[8] = vertices[0];
	SetRefinementCoordinate(&vertices[8], 0U, 0.5f);
	SetRefinementCoordinate(&vertices[8], 1U, 0.5f);
	vertices[9] = vertices[0];
	SetRefinementCoordinate(&vertices[9], 0U, 0.75f);
	SetRefinementCoordinate(&vertices[9], 1U, 0.25f);
	for (node = 0U; node < 10U; node++)
	{
		vertices[node].id.value = Stable(
			SG_RUNE_ORDER_FIELD_REFINEMENT_VERTEX, node + 1U);
		vertices[node].proof.value = Stable(
			SG_RUNE_ORDER_DYNAMICS_PROOF, node + 1U);
	}
	atom.id.value = Stable(SG_RUNE_ORDER_FIELD_REACH_ATOM, 1U);
	atom.domain.value = Stable(SG_RUNE_ORDER_STATE_DOMAIN, 1U);
	atom.simplices = (sg_rune_state_simplex_span_t){ 0U, 1U };
	atom.partition_proof.value = Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 20U);
	for (node = 0U; node < 5U; node++)
	{
		const sg_field_refinement_vertex_t *cell[8];
		uint32_t local;
		nodes[node].id.value = Stable(
			SG_RUNE_ORDER_FIELD_REFINEMENT_NODE, node + 1U);
		nodes[node].parent = node == 0U ? UINT32_MAX :
			(node <= 2U ? 0U : 1U);
		nodes[node].children = node == 0U ?
			(sg_field_refinement_node_span_t){ 0U, 2U } : node == 1U ?
			(sg_field_refinement_node_span_t){ 2U, 2U } :
			(sg_field_refinement_node_span_t){ 4U, 0U };
		nodes[node].atom = atom.id;
		nodes[node].vertices = (sg_field_refinement_vertex_ref_span_t){
			node * 8U, 8U };
		nodes[node].faces = (sg_field_refinement_face_ref_span_t){
			node * 8U, 8U };
		nodes[node].geometry_proof.value = Stable(
			SG_RUNE_ORDER_DYNAMICS_PROOF, 40U + node);
		nodes[node].interpolation_proof.value = Stable(
			SG_RUNE_ORDER_DYNAMICS_PROOF, 50U + node);
		for (local = 0U; local < 8U; local++)
		{
			node_vertices[node * 8U + local] =
				vertices[node_vertex_indices[node][local]].id;
			cell[local] = &vertices[node_vertex_indices[node][local]];
		}
		nodes[node].orientation =
			(int8_t)SG_FieldRefinementCellOrientation(cell);
		TestRefinementBounds(cell, &nodes[node].state_bounds);
	}
	atom.state_bounds = nodes[0].state_bounds;

	for (node = 0U; node < 5U; node++)
	{
		uint32_t local_face;
		for (local_face = 0U; local_face < 8U; local_face++)
		{
			uint32_t key[7];
			uint32_t local;
			uint32_t key_index = 0U;
			size_t face;
			for (local = 0U; local < 8U; local++)
				if (local != local_face)
					key[key_index++] = node_vertex_indices[node][local];
			for (face = 0U; face < face_count; face++)
				if (builders[face].depth == depths[node] &&
				    TestFaceKeySame(builders[face].vertices, key))
					break;
			if (face == face_count)
			{
				memcpy(builders[face].vertices, key, sizeof(key));
				builders[face].depth = depths[node];
				face_count++;
			}
			CHECK(builders[face].incidence_count < 2U);
			builders[face].nodes[builders[face].incidence_count] = node;
			builders[face].local_faces[builders[face].incidence_count] =
				(uint8_t)local_face;
			builders[face].incidence_count++;
			face_for_local[node][local_face] = (uint32_t)face;
		}
	}
	CHECK(face_count == 38U);
	for (size_t face = 0U; face < face_count; face++)
	{
		uint32_t item;
		faces[face].id.value = Stable(
			SG_RUNE_ORDER_FIELD_REFINEMENT_FACE, (uint32_t)face + 1U);
		faces[face].vertices = (sg_field_refinement_vertex_ref_span_t){
			(uint32_t)(face * 7U), 7U };
		faces[face].incidences = (sg_field_refinement_incidence_span_t){
			(uint32_t)incidence_count, builders[face].incidence_count };
		faces[face].parent_face.value = SG_RUNE_STABLE_ID_NONE;
		faces[face].proof.value = Stable(
			SG_RUNE_ORDER_DYNAMICS_PROOF, (uint32_t)face + 100U);
		for (item = 0U; item < 7U; item++)
			face_vertices[face * 7U + item] =
				vertices[builders[face].vertices[item]].id;
		for (item = 0U; item < builders[face].incidence_count; item++)
		{
			uint32_t incidence_node = builders[face].nodes[item];
			uint8_t local_face = builders[face].local_faces[item];
			incidences[incidence_count].node = nodes[incidence_node].id;
			incidences[incidence_count].local_face = local_face;
			incidences[incidence_count].orientation =
				(int8_t)TestLocalFaceOrientation(
					node_vertex_indices[incidence_node], local_face, vertices);
			node_faces[incidence_node * 8U + local_face] = faces[face].id;
			incidence_count++;
		}
	}
	CHECK(incidence_count == 40U);
	for (size_t face = 0U; face < face_count; face++)
		if (builders[face].incidence_count == 1U &&
		    builders[face].nodes[0] != 0U)
		{
			uint32_t child_node = builders[face].nodes[0];
			uint32_t parent_node = nodes[child_node].parent;
			uint32_t parent_local;
			for (parent_local = 0U; parent_local < 8U; parent_local++)
			{
				uint32_t parent_face = face_for_local[parent_node][parent_local];
				if (TestFaceInsideFace(builders[face].vertices,
					builders[parent_face].vertices, vertices))
				{
					faces[face].parent_face = faces[parent_face].id;
					break;
				}
			}
			CHECK(parent_local < 8U);
		}

	tree.id.value = Stable(SG_RUNE_ORDER_FIELD_REFINEMENT_TREE, 1U);
	tree.nodes = nodes;
	tree.node_count = 5U;
	tree.vertices = vertices;
	tree.vertex_count = 10U;
	tree.node_vertices = node_vertices;
	tree.node_vertex_count = 40U;
	tree.faces = faces;
	tree.face_count = face_count;
	tree.face_vertices = face_vertices;
	tree.face_vertex_count = face_count * 7U;
	tree.face_incidences = incidences;
	tree.face_incidence_count = incidence_count;
	tree.node_faces = node_faces;
	tree.node_face_count = 40U;
	tree.children = children;
	tree.child_count = 4U;
	tree.atom_roots = roots;
	tree.atom_count = 1U;
	tree.proof.value = Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 200U);
	CHECK(SG_FieldRefinementTreeValid(&tree, &atom, 1U));
}

static void TestExactIntersectionHostiles(void)
{
	sg_field_refinement_vertex_t left_storage[8];
	sg_field_refinement_vertex_t right_storage[8];
	sg_field_refinement_vertex_t child_storage[8];
	const sg_field_refinement_vertex_t *left[8];
	const sg_field_refinement_vertex_t *right[8];
	const sg_field_refinement_vertex_t *child[8];
	uint32_t vertex;

	StandardRefinementSimplex(left_storage);
	for (vertex = 0U; vertex < 8U; vertex++)
		left[vertex] = &left_storage[vertex];
	/* Equal coordinates under different IDs still form one exact shared face. */
	memset(right_storage, 0, sizeof(right_storage));
	for (vertex = 0U; vertex < 7U; vertex++)
		right_storage[vertex] = left_storage[vertex + 1U];
	for (vertex = 0U; vertex < SG_RUNE_STATE_DIMENSION_COUNT; vertex++)
		SetRefinementCoordinate(&right_storage[7], vertex, 1.0f);
	for (vertex = 0U; vertex < 8U; vertex++)
		right[vertex] = &right_storage[vertex];
	CHECK(SG_FieldRefinementCellsProperlyMeet(left, right));
	/* Refining only one side of that shared face leaves a hanging midpoint.
	 * Each child meets the unsplit neighbor in more than their shared keys. */
	child_storage[0] = left_storage[0];
	child_storage[1] = left_storage[1];
	memset(&child_storage[2], 0, sizeof(child_storage[2]));
	SetRefinementCoordinate(&child_storage[2], 0U, 0.5f);
	SetRefinementCoordinate(&child_storage[2], 1U, 0.5f);
	for (vertex = 3U; vertex < 8U; vertex++)
		child_storage[vertex] = left_storage[vertex];
	for (vertex = 0U; vertex < 8U; vertex++)
		child[vertex] = &child_storage[vertex];
	CHECK(SG_FieldRefinementCellFullRank(child));
	CHECK(!SG_FieldRefinementCellsProperlyMeet(child, right));

	/* A 6-simplex strictly inside the shared face is a T-junction, not a
	 * conforming face, despite lying exactly on the separating hyperplane. */
	memset(right_storage, 0, sizeof(right_storage));
	for (vertex = 0U; vertex < 7U; vertex++)
	{
		SetRefinementCoordinate(&right_storage[vertex], vertex, 0.5f);
		SetRefinementCoordinate(&right_storage[vertex], (vertex + 1U) % 7U,
			0.5f);
	}
	for (vertex = 0U; vertex < 7U; vertex++)
		SetRefinementCoordinate(&right_storage[7], vertex, 1.0f);
	CHECK(SG_FieldRefinementCellFullRank(right));
	CHECK(!SG_FieldRefinementCellsProperlyMeet(left, right));

	/* A vertex on a face is a real intersection even when no input vertex key
	 * is shared. The other seven vertices lie strictly outside that face. */
	memset(right_storage, 0, sizeof(right_storage));
	SetRefinementCoordinate(&right_storage[0], 0U, 0.5f);
	SetRefinementCoordinate(&right_storage[0], 1U, 0.5f);
	for (vertex = 0U; vertex < 7U; vertex++)
	{
		right_storage[vertex + 1U] = right_storage[0];
		SetRefinementCoordinate(&right_storage[vertex + 1U], vertex,
			(vertex < 2U ? 0.5f : 0.0f) + 0.25f);
	}
	CHECK(SG_FieldRefinementCellFullRank(right));
	CHECK(!SG_FieldRefinementCellsProperlyMeet(left, right));

	/* Two full-dimensional simplexes cross around the barycenter while every
	 * vertex of either simplex lies outside the other. */
	for (vertex = 0U; vertex < 8U; vertex++)
	{
		uint32_t dimension;
		memset(&right_storage[vertex], 0, sizeof(right_storage[vertex]));
		for (dimension = 0U; dimension < 7U; dimension++)
			SetRefinementCoordinate(&right_storage[vertex], dimension,
				vertex != 0U && dimension == vertex - 1U ? 1.875f : -0.125f);
	}
	CHECK(SG_FieldRefinementCellFullRank(right));
	CHECK(!SG_FieldRefinementCellsProperlyMeet(left, right));

	/* Duplicate cells and exact degeneracy reject; the smallest positive
	 * binary32 edge remains a nonzero exact determinant. */
	for (vertex = 0U; vertex < 8U; vertex++)
		right_storage[vertex] = left_storage[vertex];
	CHECK(!SG_FieldRefinementCellsProperlyMeet(left, right));
	right_storage[7] = right_storage[0];
	CHECK(!SG_FieldRefinementCellFullRank(right));
	StandardRefinementSimplex(right_storage);
	SetRefinementCoordinate(&right_storage[7], 6U, 0x1p-149f);
	CHECK(SG_FieldRefinementCellFullRank(right));
}

static void TestAuthenticatedGeometryCertificates(void)
{
	dynamics_fixture_t fixture;
	sg_rune_cell_t cells[2];
	sg_rune_state_domain_t domains[2];
	sg_rune_domain_support_certificate_t support[2];
	sg_rune_domain_boundary_facet_t boundary[28];
	sg_field_refinement_vertex_ref_t boundary_vertices[196];
	uint32_t words[2] = { 7U, 7U };

	BuildFixture(&fixture);
	fixture.simplex_owners[0].proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 1U);
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	BuildFixture(&fixture);
	fixture.simplex_owners[1].atom = fixture.reach_atoms[0].id;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	BuildFixture(&fixture);
	fixture.exact_words[0] = 6U;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	BuildFixture(&fixture);
	fixture.domain_boundary_facets[0].orientation *= -1;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	BuildFixture(&fixture);
	fixture.domain_support[0].boundary_facets.count = 13U;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	BuildFixture(&fixture);
	fixture.outcome_images[0].canonical_image.position.x.min_value =
		nextafterf(0.25f, -INFINITY);
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	BuildFixture(&fixture);
	fixture.outcome_images[0].proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 1U);
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	BuildFixture(&fixture);
	fixture.outcome_cover_pieces[0].proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 1U);
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	BuildFixture(&fixture);
	fixture.vertices[8].position.value[0] = nextafterf(1.0f, INFINITY);
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	BuildFixture(&fixture);
	for (size_t vertex = 0U; vertex < 8U; vertex++)
	{
		fixture.vertices[8U + vertex].position =
			fixture.vertices[vertex].position;
		fixture.vertices[8U + vertex].velocity =
			fixture.vertices[vertex].velocity;
		fixture.vertices[8U + vertex].elapsed_ms =
			fixture.vertices[vertex].elapsed_ms;
	}
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	BuildFixture(&fixture);
	cells[0] = fixture.cell;
	cells[1] = fixture.cell;
	cells[1].id.value = Stable(SG_RUNE_ORDER_CELL, 2U);
	fixture.rune_model.cells = cells;
	fixture.rune_model.cell_count = 2U;
	CHECK(!SG_RuneDynamicsModelValid(&fixture.dynamics, &fixture.snapshot));
	BuildFixture(&fixture);
	domains[0] = fixture.domains[0];
	domains[1] = fixture.domains[0];
	domains[1].id.value = Stable(SG_RUNE_ORDER_STATE_DOMAIN, 2U);
	support[0] = fixture.domain_support[0];
	support[1] = fixture.domain_support[0];
	support[1].domain = domains[1].id;
	support[1].boundary_facets.first = 14U;
	support[1].normalized_volume.magnitude.first = 1U;
	support[1].proof.value = Stable(SG_RUNE_ORDER_DOMAIN_SUPPORT_PROOF, 2U);
	for (size_t facet = 0U; facet < 14U; facet++)
	{
		boundary[facet] = fixture.domain_boundary_facets[facet];
		boundary[14U + facet] = fixture.domain_boundary_facets[facet];
		boundary[14U + facet].domain = domains[1].id;
		boundary[14U + facet].vertices.first += 98U;
		boundary[14U + facet].proof.value = Stable(
			SG_RUNE_ORDER_DOMAIN_BOUNDARY_PROOF, (uint32_t)facet + 20U);
	}
	for (size_t vertex = 0U; vertex < 98U; vertex++)
	{
		boundary_vertices[vertex] = fixture.domain_boundary_vertices[vertex];
		boundary_vertices[98U + vertex] =
			fixture.domain_boundary_vertices[vertex];
	}
	fixture.charts[0].state_domains.count = 2U;
	fixture.dynamics.state_domains = domains;
	fixture.dynamics.state_domain_count = 2U;
	fixture.dynamics.domain_support = support;
	fixture.dynamics.domain_support_count = 2U;
	fixture.dynamics.domain_boundary_facets = boundary;
	fixture.dynamics.domain_boundary_facet_count = 28U;
	fixture.dynamics.domain_boundary_vertices = boundary_vertices;
	fixture.dynamics.domain_boundary_vertex_count = 196U;
	fixture.dynamics.exact_words = words;
	fixture.dynamics.exact_word_count = 2U;
	CHECK(!SG_RuneDynamicsGeometryValid(&fixture.dynamics));
}

static void TestRefinedBoundaryCoverage(void)
{
	dynamics_fixture_t fixture;
	sg_field_refinement_node_t nodes[6];
	sg_field_refinement_vertex_t vertices[10];
	sg_field_refinement_vertex_ref_t node_vertices[48];
	uint32_t children[4] = { 2U, 3U, 4U, 5U };
	const uint32_t child_indices[4][8] = {
		{ 0U, 1U, 3U, 4U, 5U, 6U, 7U, 9U },
		{ 0U, 2U, 3U, 4U, 5U, 6U, 7U, 9U },
		{ 1U, 3U, 4U, 5U, 6U, 7U, 8U, 9U },
		{ 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U }
	};
	size_t node;
	size_t vertex;

	BuildFixture(&fixture);
	memcpy(nodes, fixture.refinement_nodes,
		2U * sizeof(*fixture.refinement_nodes));
	memcpy(vertices, fixture.refinement_vertices,
		9U * sizeof(*fixture.refinement_vertices));
	memcpy(node_vertices, fixture.node_vertices,
		16U * sizeof(*fixture.node_vertices));
	vertices[9] = fixture.refinement_vertices[0];
	vertices[9].id.value =
		Stable(SG_RUNE_ORDER_FIELD_REFINEMENT_VERTEX, 10U);
	vertices[9].proof.value = Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 250U);
	vertices[9].position.value[0] = 0.5f;
	vertices[9].position.value[1] = 0.5f;
	nodes[0].children = (sg_field_refinement_node_span_t){ 0U, 2U };
	nodes[1].children = (sg_field_refinement_node_span_t){ 2U, 2U };
	for (node = 0U; node < 4U; node++)
	{
		nodes[2U + node] = fixture.refinement_nodes[node >= 2U ? 1U : 0U];
		nodes[2U + node].id.value = Stable(
			SG_RUNE_ORDER_FIELD_REFINEMENT_NODE, (uint32_t)node + 3U);
		nodes[2U + node].parent = node >= 2U ? 1U : 0U;
		nodes[2U + node].children =
			(sg_field_refinement_node_span_t){ 0U, 0U };
		nodes[2U + node].vertices =
			(sg_field_refinement_vertex_ref_span_t){
				(uint32_t)(16U + node * 8U), 8U };
		for (vertex = 0U; vertex < 8U; vertex++)
			node_vertices[16U + node * 8U + vertex] =
				vertices[child_indices[node][vertex]].id;
	}
	fixture.dynamics.refinement_tree.nodes = nodes;
	fixture.dynamics.refinement_tree.node_count = 6U;
	fixture.dynamics.refinement_tree.children = children;
	fixture.dynamics.refinement_tree.child_count = 4U;
	fixture.dynamics.refinement_tree.vertices = vertices;
	fixture.dynamics.refinement_tree.vertex_count = 10U;
	fixture.dynamics.refinement_tree.node_vertices = node_vertices;
	fixture.dynamics.refinement_tree.node_vertex_count = 48U;
	CHECK(SG_RuneDynamicsGeometryValid(&fixture.dynamics));
	/* Leaving the opposite root unsplit exposes a hanging midpoint on the
	 * internal face and must fail the active-leaf complex. */
	nodes[1].children = (sg_field_refinement_node_span_t){ 0U, 0U };
	fixture.dynamics.refinement_tree.node_count = 4U;
	fixture.dynamics.refinement_tree.child_count = 2U;
	CHECK(!SG_RuneDynamicsGeometryValid(&fixture.dynamics));
}

static void TestCoincidentChartsRemainIndependent(void)
{
	dynamics_fixture_t fixture;
	sg_rune_state_vertex_t state_vertices[32];
	sg_rune_state_chart_t charts[2];
	sg_rune_state_simplex_t simplices[4];
	sg_rune_state_domain_t domains[2];
	sg_rune_state_simplex_owner_t owners[4];
	sg_field_reach_atom_t atoms[4];
	sg_rune_domain_support_certificate_t support[2];
	sg_rune_domain_boundary_facet_t boundary[28];
	sg_field_refinement_vertex_ref_t boundary_vertices[196];
	uint32_t words[2] = { 7U, 7U };
	sg_field_refinement_node_t nodes[4];
	sg_field_refinement_vertex_ref_t node_vertices[32];
	uint32_t roots[4] = { 0U, 1U, 2U, 3U };
	size_t index;

	BuildFixture(&fixture);
	memcpy(state_vertices, fixture.vertices, 16U * sizeof(*state_vertices));
	memcpy(charts, fixture.charts, sizeof(fixture.charts));
	memcpy(simplices, fixture.simplices, sizeof(fixture.simplices));
	memcpy(domains, fixture.domains, sizeof(fixture.domains));
	memcpy(owners, fixture.simplex_owners, sizeof(fixture.simplex_owners));
	memcpy(atoms, fixture.reach_atoms, sizeof(fixture.reach_atoms));
	memcpy(nodes, fixture.refinement_nodes,
		2U * sizeof(*fixture.refinement_nodes));
	memcpy(node_vertices, fixture.node_vertices,
		16U * sizeof(*fixture.node_vertices));
	charts[1] = charts[0];
	charts[1].id.value = Stable(SG_RUNE_ORDER_STATE_CHART, 2U);
	charts[1].state_vertices = (sg_rune_state_vertex_span_t){ 16U, 16U };
	charts[1].simplices = (sg_rune_state_simplex_span_t){ 2U, 2U };
	charts[1].state_domains = (sg_rune_state_domain_span_t){ 1U, 1U };
	for (index = 0U; index < 16U; index++)
	{
		state_vertices[16U + index] = state_vertices[index];
		state_vertices[16U + index].id.value = Stable(
			SG_RUNE_ORDER_STATE_VERTEX, (uint32_t)index + 17U);
		state_vertices[16U + index].chart = charts[1].id;
	}
	for (index = 0U; index < 2U; index++)
	{
		simplices[2U + index] = simplices[index];
		simplices[2U + index].id.value = Stable(
			SG_RUNE_ORDER_STATE_SIMPLEX, (uint32_t)index + 3U);
		simplices[2U + index].chart = charts[1].id;
		simplices[2U + index].vertices.first = (uint32_t)(16U + index * 8U);
	}
	domains[1] = domains[0];
	domains[1].id.value = Stable(SG_RUNE_ORDER_STATE_DOMAIN, 2U);
	domains[1].chart = charts[1].id;
	domains[1].simplices = (sg_rune_state_simplex_span_t){ 2U, 2U };
	for (index = 0U; index < 2U; index++)
	{
		atoms[2U + index] = atoms[index];
		atoms[2U + index].id.value = Stable(
			SG_RUNE_ORDER_FIELD_REACH_ATOM, (uint32_t)index + 3U);
		atoms[2U + index].domain = domains[1].id;
		atoms[2U + index].simplices.first = (uint32_t)index + 2U;
		owners[2U + index].simplex = simplices[2U + index].id;
		owners[2U + index].domain = domains[1].id;
		owners[2U + index].atom = atoms[2U + index].id;
		owners[2U + index].proof.value = Stable(
			SG_RUNE_ORDER_SIMPLEX_OWNERSHIP_PROOF, (uint32_t)index + 3U);
		nodes[2U + index] = nodes[index];
		nodes[2U + index].id.value = Stable(
			SG_RUNE_ORDER_FIELD_REFINEMENT_NODE, (uint32_t)index + 3U);
		nodes[2U + index].atom = atoms[2U + index].id;
		nodes[2U + index].vertices.first = (uint32_t)(16U + index * 8U);
		memcpy(&node_vertices[16U + index * 8U],
			&fixture.node_vertices[index * 8U], 8U * sizeof(*node_vertices));
	}
	support[0] = fixture.domain_support[0];
	support[1] = fixture.domain_support[0];
	support[1].domain = domains[1].id;
	support[1].boundary_facets.first = 14U;
	support[1].normalized_volume.magnitude.first = 1U;
	support[1].proof.value = Stable(SG_RUNE_ORDER_DOMAIN_SUPPORT_PROOF, 2U);
	for (index = 0U; index < 14U; index++)
	{
		boundary[index] = fixture.domain_boundary_facets[index];
		boundary[14U + index] = fixture.domain_boundary_facets[index];
		boundary[14U + index].domain = domains[1].id;
		boundary[14U + index].vertices.first += 98U;
		boundary[14U + index].proof.value = Stable(
			SG_RUNE_ORDER_DOMAIN_BOUNDARY_PROOF, (uint32_t)index + 20U);
	}
	for (index = 0U; index < 98U; index++)
	{
		boundary_vertices[index] = fixture.domain_boundary_vertices[index];
		boundary_vertices[98U + index] =
			fixture.domain_boundary_vertices[index];
	}
	fixture.dynamics.state_vertices = state_vertices;
	fixture.dynamics.state_vertex_count = 32U;
	fixture.dynamics.state_charts = charts;
	fixture.dynamics.state_chart_count = 2U;
	fixture.dynamics.state_simplices = simplices;
	fixture.dynamics.state_simplex_count = 4U;
	fixture.dynamics.state_domains = domains;
	fixture.dynamics.state_domain_count = 2U;
	fixture.dynamics.simplex_owners = owners;
	fixture.dynamics.simplex_owner_count = 4U;
	fixture.dynamics.reach_atoms = atoms;
	fixture.dynamics.reach_atom_count = 4U;
	fixture.dynamics.domain_support = support;
	fixture.dynamics.domain_support_count = 2U;
	fixture.dynamics.domain_boundary_facets = boundary;
	fixture.dynamics.domain_boundary_facet_count = 28U;
	fixture.dynamics.domain_boundary_vertices = boundary_vertices;
	fixture.dynamics.domain_boundary_vertex_count = 196U;
	fixture.dynamics.exact_words = words;
	fixture.dynamics.exact_word_count = 2U;
	fixture.dynamics.refinement_tree.nodes = nodes;
	fixture.dynamics.refinement_tree.node_count = 4U;
	fixture.dynamics.refinement_tree.node_vertices = node_vertices;
	fixture.dynamics.refinement_tree.node_vertex_count = 32U;
	fixture.dynamics.refinement_tree.atom_roots = roots;
	fixture.dynamics.refinement_tree.atom_count = 4U;
	CHECK(SG_RuneDynamicsGeometryValid(&fixture.dynamics));
	/* The same bytes become an overlap once the second component claims the
	 * first chart identity. */
	domains[1].chart = charts[0].id;
	simplices[2].chart = charts[0].id;
	simplices[3].chart = charts[0].id;
	CHECK(!SG_RuneDynamicsGeometryValid(&fixture.dynamics));
}

static sg_rune_field_region_t Region(uint32_t ordinal, uint32_t parent,
	uint32_t level)
{
	sg_rune_field_region_t region = { 0 };

	region.id.value = Stable(SG_RUNE_ORDER_FIELD_REGION, ordinal);
	region.parent_region = parent;
	region.level = level;
	region.coverage_proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, ordinal);
	return region;
}

static void TestExactLeafOwnership(void)
{
	sg_rune_field_region_t regions[3] = {
		Region(1U, SG_RUNE_FIELD_NO_REGION, 0U),
		Region(2U, 0U, 1U),
		Region(3U, 0U, 1U)
	};
	uint32_t children[2] = { 1U, 2U };
	uint32_t chart_owners[2] = { 1U, 2U };
	uint32_t domain_owners[2] = { 1U, 2U };
	uint32_t patch_owners[2] = { 1U, 2U };
	sg_rune_field_region_hierarchy_t hierarchy = { 0 };

	regions[0].children = (sg_rune_field_region_span_t){ 0U, 2U };
	regions[0].charts = (sg_rune_state_chart_span_t){ 0U, 2U };
	regions[0].state_domains = (sg_rune_state_domain_span_t){ 0U, 2U };
	regions[0].response_patches =
		(sg_rune_response_patch_span_t){ 0U, 2U };
	regions[1].children.first = 2U;
	regions[1].charts = (sg_rune_state_chart_span_t){ 0U, 1U };
	regions[1].state_domains = (sg_rune_state_domain_span_t){ 0U, 1U };
	regions[1].response_patches =
		(sg_rune_response_patch_span_t){ 0U, 1U };
	regions[2].children.first = 2U;
	regions[2].charts = (sg_rune_state_chart_span_t){ 1U, 1U };
	regions[2].state_domains = (sg_rune_state_domain_span_t){ 1U, 1U };
	regions[2].response_patches =
		(sg_rune_response_patch_span_t){ 1U, 1U };
	hierarchy.id.value = Stable(SG_RUNE_ORDER_FIELD_HIERARCHY, 1U);
	hierarchy.regions = regions;
	hierarchy.region_count = 3U;
	hierarchy.children = children;
	hierarchy.child_count = 2U;
	hierarchy.chart_leaf_regions = chart_owners;
	hierarchy.state_domain_leaf_regions = domain_owners;
	hierarchy.response_patch_leaf_regions = patch_owners;
	hierarchy.chart_count = 2U;
	hierarchy.state_domain_count = 2U;
	hierarchy.response_patch_count = 2U;
	hierarchy.hierarchy_proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 10U);
	CHECK(SG_RuneFieldRegionHierarchyValid(&hierarchy));
	regions[0].charts.count = 1U;
	CHECK(!SG_RuneFieldRegionHierarchyValid(&hierarchy));
	regions[0].charts.count = 3U;
	CHECK(!SG_RuneFieldRegionHierarchyValid(&hierarchy));
	regions[0].charts.count = 2U;
	regions[0].state_domains.count = 1U;
	CHECK(!SG_RuneFieldRegionHierarchyValid(&hierarchy));
	regions[0].state_domains.count = 2U;
	regions[0].response_patches.count = 1U;
	CHECK(!SG_RuneFieldRegionHierarchyValid(&hierarchy));
	regions[0].response_patches.count = 2U;
	regions[2].charts.first = 0U;
	CHECK(!SG_RuneFieldRegionHierarchyValid(&hierarchy));
	regions[2].charts.first = 1U;
	domain_owners[1] = 1U;
	CHECK(!SG_RuneFieldRegionHierarchyValid(&hierarchy));
	domain_owners[1] = 2U;
	patch_owners[1] = 1U;
	CHECK(!SG_RuneFieldRegionHierarchyValid(&hierarchy));
	patch_owners[1] = 2U;
	children[1] = 1U;
	CHECK(!SG_RuneFieldRegionHierarchyValid(&hierarchy));
}

static void TestInternalHierarchySummaries(void)
{
	sg_rune_field_region_t regions[4] = {
		Region(1U, SG_RUNE_FIELD_NO_REGION, 0U),
		Region(2U, 0U, 1U), Region(3U, 1U, 2U), Region(4U, 0U, 1U)
	};
	uint32_t children[3] = { 1U, 3U, 2U };
	uint32_t owners[2] = { 2U, 3U };
	sg_rune_field_region_hierarchy_t hierarchy = { 0 };
	size_t index;

	regions[0].children = (sg_rune_field_region_span_t){ 0U, 2U };
	regions[1].children = (sg_rune_field_region_span_t){ 2U, 1U };
	regions[2].children.first = 3U;
	regions[3].children.first = 3U;
	regions[0].charts = (sg_rune_state_chart_span_t){ 0U, 2U };
	regions[1].charts = (sg_rune_state_chart_span_t){ 0U, 1U };
	regions[2].charts = (sg_rune_state_chart_span_t){ 0U, 1U };
	regions[3].charts = (sg_rune_state_chart_span_t){ 1U, 1U };
	for (index = 0U; index < 4U; index++)
	{
		regions[index].state_domains = (sg_rune_state_domain_span_t){
			regions[index].charts.first, regions[index].charts.count };
		regions[index].response_patches =
			(sg_rune_response_patch_span_t){ regions[index].charts.first,
				regions[index].charts.count };
	}
	hierarchy.id.value = Stable(SG_RUNE_ORDER_FIELD_HIERARCHY, 1U);
	hierarchy.regions = regions;
	hierarchy.region_count = 4U;
	hierarchy.children = children;
	hierarchy.child_count = 3U;
	hierarchy.chart_leaf_regions = owners;
	hierarchy.state_domain_leaf_regions = owners;
	hierarchy.response_patch_leaf_regions = owners;
	hierarchy.chart_count = 2U;
	hierarchy.state_domain_count = 2U;
	hierarchy.response_patch_count = 2U;
	hierarchy.hierarchy_proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 10U);
	CHECK(SG_RuneFieldRegionHierarchyValid(&hierarchy));
	regions[1].charts.count = 2U;
	CHECK(!SG_RuneFieldRegionHierarchyValid(&hierarchy));
}

static void TestLinearHierarchy(void)
{
	const size_t count = 2048U;
	sg_rune_field_region_t *regions = calloc(count, sizeof(*regions));
	uint32_t *children = calloc(count - 1U, sizeof(*children));
	uint32_t *chart_owners = calloc(count - 1U, sizeof(*chart_owners));
	uint32_t *domain_owners = calloc(count - 1U, sizeof(*domain_owners));
	uint32_t *patch_owners = calloc(count - 1U, sizeof(*patch_owners));
	sg_rune_field_region_hierarchy_t hierarchy = { 0 };
	size_t index;

	CHECK(regions && children && chart_owners && domain_owners && patch_owners);
	if (!regions || !children || !chart_owners || !domain_owners ||
	    !patch_owners)
		goto cleanup;
	regions[0] = Region(1U, SG_RUNE_FIELD_NO_REGION, 0U);
	regions[0].children =
		(sg_rune_field_region_span_t){ 0U, (uint32_t)count - 1U };
	regions[0].charts =
		(sg_rune_state_chart_span_t){ 0U, (uint32_t)count - 1U };
	regions[0].state_domains =
		(sg_rune_state_domain_span_t){ 0U, (uint32_t)count - 1U };
	regions[0].response_patches =
		(sg_rune_response_patch_span_t){ 0U, (uint32_t)count - 1U };
	for (index = 1U; index < count; index++)
	{
		regions[index] = Region((uint32_t)index + 1U, 0U, 1U);
		regions[index].children.first = (uint32_t)count - 1U;
		regions[index].charts =
			(sg_rune_state_chart_span_t){ (uint32_t)index - 1U, 1U };
		regions[index].state_domains =
			(sg_rune_state_domain_span_t){ (uint32_t)index - 1U, 1U };
		regions[index].response_patches =
			(sg_rune_response_patch_span_t){ (uint32_t)index - 1U, 1U };
		children[index - 1U] = (uint32_t)index;
		chart_owners[index - 1U] = (uint32_t)index;
		domain_owners[index - 1U] = (uint32_t)index;
		patch_owners[index - 1U] = (uint32_t)index;
	}
	hierarchy.id.value = Stable(SG_RUNE_ORDER_FIELD_HIERARCHY, 1U);
	hierarchy.regions = regions;
	hierarchy.region_count = count;
	hierarchy.children = children;
	hierarchy.child_count = count - 1U;
	hierarchy.chart_leaf_regions = chart_owners;
	hierarchy.state_domain_leaf_regions = domain_owners;
	hierarchy.response_patch_leaf_regions = patch_owners;
	hierarchy.chart_count = count - 1U;
	hierarchy.state_domain_count = count - 1U;
	hierarchy.response_patch_count = count - 1U;
	hierarchy.hierarchy_proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 1U);
	CHECK(SG_RuneFieldRegionHierarchyValid(&hierarchy));

cleanup:
	free(patch_owners);
	free(domain_owners);
	free(chart_owners);
	free(children);
	free(regions);
}

static void TestDeepLinearHierarchy(void)
{
	const size_t count = 2048U;
	sg_rune_field_region_t *regions = calloc(count, sizeof(*regions));
	uint32_t *children = calloc(count - 1U, sizeof(*children));
	uint32_t owner = (uint32_t)count - 1U;
	sg_rune_field_region_hierarchy_t hierarchy = { 0 };
	size_t index;

	CHECK(regions && children);
	if (!regions || !children)
		goto cleanup;
	for (index = 0U; index < count; index++)
	{
		regions[index] = Region((uint32_t)index + 1U,
			index == 0U ? SG_RUNE_FIELD_NO_REGION : (uint32_t)index - 1U,
			(uint32_t)index);
		regions[index].children.first = (uint32_t)index;
		regions[index].children.count = index + 1U < count ? 1U : 0U;
		regions[index].charts = (sg_rune_state_chart_span_t){ 0U, 1U };
		regions[index].state_domains =
			(sg_rune_state_domain_span_t){ 0U, 1U };
		regions[index].response_patches =
			(sg_rune_response_patch_span_t){ 0U, 1U };
		if (index + 1U < count)
			children[index] = (uint32_t)index + 1U;
	}
	hierarchy.id.value = Stable(SG_RUNE_ORDER_FIELD_HIERARCHY, 1U);
	hierarchy.regions = regions;
	hierarchy.region_count = count;
	hierarchy.children = children;
	hierarchy.child_count = count - 1U;
	hierarchy.chart_leaf_regions = &owner;
	hierarchy.state_domain_leaf_regions = &owner;
	hierarchy.response_patch_leaf_regions = &owner;
	hierarchy.chart_count = 1U;
	hierarchy.state_domain_count = 1U;
	hierarchy.response_patch_count = 1U;
	hierarchy.hierarchy_proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 1U);
	CHECK(SG_RuneFieldRegionHierarchyValid(&hierarchy));

cleanup:
	free(children);
	free(regions);
}

static void TestGuidanceIntervals(void)
{
	sg_field_option_t option = {
		.kind = SG_FIELD_OPTION_CONTROL,
		.value.control = {
			.control = { { 0 } },
			.minimum_descent_us = 100U,
			.endpoint_cost = { 1000U, 2000U }
		}
	};
	sg_field_guidance_t guidance = {
		.field = { 1U, 2U, 3U, 4U, 5U, 6U },
		.pose_revision = 6U,
		.sampled_at_ms = 7U,
		.kind = SG_FIELD_GUIDANCE_DESCENT
	};

	option.value.control.control.value =
		Stable(SG_RUNE_ORDER_CONTROL_FIBER, 1U);
	guidance.value.descent.arrival_cost =
		(sg_rune_cost_bounds_t){ 2500U, 3000U };
	guidance.value.descent.residual_bound_us = 10U;
	guidance.value.descent.spatial_subgradient = Interval3(-1.0f, 1.0f);
	guidance.value.descent.velocity_subgradient = Interval3(-1.0f, 1.0f);
	guidance.value.descent.time_subgradient = Interval(-1.0f, 0.0f);
	guidance.value.descent.position_error = Interval3(-0.5f, 0.5f);
	guidance.value.descent.velocity_error = Interval3(-1.0f, 1.0f);
	guidance.value.descent.time_error = Interval(-0.5f, 0.5f);
	guidance.value.descent.options = &option;
	guidance.value.descent.option_count = 1U;
	guidance.value.descent.required_option_capacity = 1U;
	CHECK(SG_FieldGuidanceValid(&guidance));
	option.value.control.endpoint_cost =
		(sg_rune_cost_bounds_t){ 1000U, 2450U };
	option.value.control.minimum_descent_us = 100U;
	CHECK(!SG_FieldGuidanceValid(&guidance));
	option.value.control.endpoint_cost =
		(sg_rune_cost_bounds_t){ 1000U, 2400U };
	option.value.control.minimum_descent_us = 100U;
	CHECK(SG_FieldGuidanceValid(&guidance));
	option.value.control.control.value =
		Stable(SG_RUNE_ORDER_STATE_CHART, 1U);
	CHECK(!SG_FieldGuidanceValid(&guidance));
	CHECK(SG_FieldHandleValid(&guidance.field));
}

static void TestEnvironmentGuardAndAbsoluteTimeContracts(void)
{
	sg_field_guard_state_t guards[2] = { 0 };
	sg_field_guard_state_t future[2] = { 0 };
	sg_field_event_slab_t slabs[2] = { 0 };
	sg_field_environment_t environment = { 0 };
	sg_localized_field_state_t state = { 0 };

	guards[0].condition.value = Stable(SG_RUNE_ORDER_GUARD_CONDITION, 1U);
	guards[0].truth = SG_FIELD_GUARD_TRUE;
	guards[1].condition.value = Stable(SG_RUNE_ORDER_GUARD_CONDITION, 2U);
	guards[1].truth = SG_FIELD_GUARD_FALSE;
	future[0] = guards[0];
	future[1] = guards[1];
	slabs[0].valid_from_ms = 40U;
	slabs[0].valid_until_ms = 100U;
	slabs[0].exogenous_guards = &future[0];
	slabs[0].exogenous_guard_count = 1U;
	slabs[0].schedule_proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 1U);
	slabs[1].valid_from_ms = 100U;
	slabs[1].valid_until_ms = 200U;
	slabs[1].exogenous_guards = &future[1];
	slabs[1].exogenous_guard_count = 1U;
	slabs[1].schedule_proof.value =
		Stable(SG_RUNE_ORDER_DYNAMICS_PROOF, 2U);
	environment.rune_identity = 1U;
	environment.topology_revision = 2U;
	environment.environment_revision = 3U;
	environment.sampled_at_ms = 50U;
	environment.authority_identity = 4U;
	environment.guards = guards;
	environment.guard_count = 2U;
	environment.event_slabs = slabs;
	environment.event_slab_count = 2U;
	environment.authenticated = 1U;
	CHECK(SG_FieldEnvironmentValid(&environment));
	future[1].truth = SG_FIELD_GUARD_UNKNOWN;
	CHECK(!SG_FieldEnvironmentValid(&environment));
	future[1].truth = SG_FIELD_GUARD_FALSE;
	slabs[1].valid_from_ms = 101U;
	CHECK(!SG_FieldEnvironmentValid(&environment));
	slabs[1].valid_from_ms = 100U;
	guards[1].condition = guards[0].condition;
	CHECK(!SG_FieldEnvironmentValid(&environment));
	guards[1].condition.value = Stable(SG_RUNE_ORDER_GUARD_CONDITION, 2U);
	state.rune_identity = 1U;
	state.topology_revision = 2U;
	state.pose_revision = 3U;
	state.sampled_at_ms = 50U;
	state.chart.value = Stable(SG_RUNE_ORDER_STATE_CHART, 1U);
	state.mode = SupportedMode();
	state.elapsed_ms = 1000.0f;
	CHECK(SG_LocalizedFieldStateValid(&state));
	CHECK(SG_FieldEnvironmentValid(&environment));
}

int main(void)
{
	TestTypedIdsAndModes();
	TestAggregateOwnership();
	TestChoiceOutcomeAndProgressContracts();
	TestExecutableCoverageAndOperatorRanks();
	TestExactFieldPartitionAndCover();
	TestExactRootPartitionGeometry();
	TestConformingRefinementAndCanonicalFloats();
	TestSimplexGeometry();
	TestNearDegenerateSimplexGeometry();
	TestExactIntersectionHostiles();
	TestRepeatedRefinementFaceLineage();
	TestAuthenticatedGeometryCertificates();
	TestRefinedBoundaryCoverage();
	TestCoincidentChartsRemainIndependent();
	TestExactLeafOwnership();
	TestInternalHierarchySummaries();
	TestLinearHierarchy();
	TestDeepLinearHierarchy();
	TestGuidanceIntervals();
	TestEnvironmentGuardAndAbsoluteTimeContracts();
	if (failures != 0)
	{
		fprintf(stderr, "sg_rune_dynamics_model_test: %d failure(s)\n",
			failures);
		return 1;
	}
	puts("sg_rune_dynamics_model_test: ok");
	return 0;
}
