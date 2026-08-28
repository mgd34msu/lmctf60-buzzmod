/* RUNE v2 artifact lint and independent semantic completeness. */
#ifndef SG_RUNE_V2_ARTIFACT_SEMANTIC_H
#define SG_RUNE_V2_ARTIFACT_SEMANTIC_H

#include <stddef.h>
#include <stdint.h>

#include "sg_rune_model.h"
#include "sg_rune_v2_codec.h"
#include "sg_rune_v2_wire.h"

#define SG_RUNE_V2_SEMANTIC_CATALOG_VERSION UINT32_C(1)

typedef struct sg_rune_v2_expected_counts_s
{
	uint32_t planes;
	uint32_t portal_vertices;
	uint32_t phases;
	uint32_t phase_transitions;
	uint32_t cells;
	uint32_t portals;
	uint32_t surfaces;
	uint32_t affordances;
	uint32_t kernels;
	uint32_t landmarks;
	uint32_t mechanisms;
} sg_rune_v2_expected_counts_t;

/* A future independent complete-model proof provider must issue this evidence
 * only after consuming independently established BSP/configuration
 * completeness, entity semantics, movement/physics capability proofs, a
 * deterministic fixed-point proof, and the exact artifact binding. */
typedef struct sg_rune_v2_complete_model_proof_s
{
	uint32_t version;
	uint32_t reserved;
	uint64_t verifier_identity;
	uint64_t bsp_content_id;
	uint64_t source_set_identity;
	uint64_t fixed_point_identity;
	uint32_t fixed_point_rounds;
	uint32_t expected_cells;
	uint32_t represented_cells;
	uint32_t expected_portals;
	uint32_t represented_portals;
	uint32_t omitted_cells;
	uint32_t omitted_portals;
	uint32_t invented_portals;
	uint32_t invalid_portals;
	uint32_t pending_work;
} sg_rune_v2_complete_model_proof_t;

/* These fact types deliberately differ from generator model records. The
 * future complete-model proof provider must construct them from independent
 * proof inputs, never from the candidate artifact, its decoded model, or
 * generator output. */
typedef struct sg_rune_v2_expected_plane_s
{
	sg_rune_plane_id_t id;
	sg_rune_vec3_t normal;
	sg_rune_order_key_t order;
	float distance;
} sg_rune_v2_expected_plane_t;

typedef struct sg_rune_v2_expected_vertex_s
{
	float z;
	float x;
	float y;
} sg_rune_v2_expected_vertex_t;

typedef struct sg_rune_v2_expected_phase_s
{
	sg_rune_phase_id_t id;
	sg_rune_mechanism_ref_t mover;
	sg_rune_order_key_t order;
	sg_rune_stance_t stance;
	sg_rune_motion_t motion;
	sg_rune_support_t support;
	sg_rune_medium_t medium;
	sg_rune_void_relation_t void_relation;
	sg_rune_reference_frame_t reference_frame;
	sg_rune_interval3_t velocity;
	sg_rune_interval_t elapsed_ms;
	uint32_t time_quantum_ms;
	uint32_t time_horizon_ms;
} sg_rune_v2_expected_phase_t;

typedef struct sg_rune_v2_expected_cell_s
{
	sg_rune_cell_id_t id;
	sg_rune_source_geometry_ref_t geometry;
	sg_rune_order_key_t order;
	sg_rune_bounds_t bounds;
	sg_rune_plane_span_t boundary_planes;
	sg_rune_phase_span_t phases;
	sg_rune_surface_span_t surfaces;
	sg_rune_affordance_span_t affordances;
	sg_rune_kernel_span_t kernels;
	sg_rune_landmark_span_t landmarks;
	sg_rune_mechanism_span_t mechanisms;
	sg_rune_bsp_leaf_ref_t bsp_leaf;
	sg_rune_bsp_area_ref_t bsp_area;
	sg_rune_bsp_cluster_ref_t bsp_cluster;
	sg_rune_contents_mask_t contents;
	sg_rune_cell_semantics_t semantics;
} sg_rune_v2_expected_cell_t;

typedef struct sg_rune_v2_expected_portal_s
{
	sg_rune_portal_id_t id;
	sg_rune_source_geometry_ref_t geometry;
	sg_rune_order_key_t order;
	sg_rune_cell_ref_t from_cell;
	sg_rune_cell_ref_t to_cell;
	sg_rune_plane_ref_t boundary_plane;
	sg_rune_vertex_span_t boundary_vertices;
	sg_rune_phase_span_t phases;
	sg_rune_portal_direction_t direction;
	float clearance;
	sg_rune_contents_mask_t contents_from;
	sg_rune_contents_mask_t contents_to;
	sg_rune_portal_flags_t flags;
} sg_rune_v2_expected_portal_t;

typedef struct sg_rune_v2_expected_surface_s
{
	sg_rune_surface_id_t id;
	sg_rune_source_geometry_ref_t geometry;
	sg_rune_order_key_t order;
	sg_rune_cell_ref_t owner_cell;
	sg_rune_plane_ref_t plane;
	sg_rune_vec3_t normal;
	sg_rune_contents_mask_t contents;
	sg_rune_surface_semantics_t semantics;
} sg_rune_v2_expected_surface_t;

typedef struct sg_rune_v2_expected_affordance_s
{
	sg_rune_affordance_id_t id;
	sg_rune_cell_ref_t owner_cell;
	sg_rune_order_key_t order;
	sg_rune_surface_span_t surfaces;
	sg_rune_phase_span_t phases;
	sg_rune_affordance_kind_t kind;
	sg_rune_interval_t range;
	uint32_t flags;
} sg_rune_v2_expected_affordance_t;

typedef struct sg_rune_v2_expected_transition_s
{
	sg_rune_phase_transition_id_t id;
	sg_rune_cell_ref_t cell;
	sg_rune_order_key_t order;
	sg_rune_phase_ref_t source_phase;
	sg_rune_phase_ref_t destination_phase;
	sg_rune_phase_transition_kind_t kind;
	sg_rune_interval_t duration_ms;
	uint32_t flags;
} sg_rune_v2_expected_transition_t;

typedef struct sg_rune_v2_expected_kernel_s
{
	sg_rune_kernel_id_t id;
	sg_rune_cell_ref_t source_cell;
	sg_rune_order_key_t order;
	sg_rune_cell_ref_t destination_cell;
	sg_rune_portal_ref_t boundary;
	sg_rune_affordance_ref_t affordance;
	sg_rune_mechanism_ref_t mechanism;
	sg_rune_phase_ref_t source_phase;
	sg_rune_phase_ref_t destination_phase;
	sg_rune_phase_transition_ref_t transition;
	sg_rune_capability_family_t family;
	sg_rune_cost_law_t cost_law;
	sg_rune_kernel_parameters_t parameters;
	sg_rune_kernel_flags_t flags;
} sg_rune_v2_expected_kernel_t;

typedef struct sg_rune_v2_expected_landmark_s
{
	sg_rune_landmark_id_t id;
	sg_rune_source_geometry_ref_t geometry;
	sg_rune_order_key_t order;
	sg_rune_cell_ref_t cell;
	sg_rune_entity_ref_t entity;
	sg_rune_landmark_kind_t kind;
	sg_rune_vec3_t origin;
	sg_rune_bounds_t bounds;
	sg_rune_mechanism_ref_t mechanism;
	sg_rune_surface_ref_t surface;
	uint32_t semantics;
} sg_rune_v2_expected_landmark_t;

typedef struct sg_rune_v2_expected_mechanism_s
{
	sg_rune_mechanism_id_t id;
	sg_rune_cell_ref_t entry_cell;
	sg_rune_order_key_t order;
	sg_rune_mechanism_kind_t kind;
	sg_rune_cell_ref_t exit_cell;
	sg_rune_landmark_ref_t activation_landmark;
	sg_rune_entity_ref_t entity;
	sg_rune_interval_t dwell_ms;
	sg_rune_interval_t travel_ms;
	sg_rune_mechanism_span_t topology;
	uint32_t flags;
} sg_rune_v2_expected_mechanism_t;

/* A future audited complete-model proof provider must issue and own these
 * private objects. Neither object has caller-constructible representation in
 * the consumer contract. */
typedef struct sg_rune_v2_complete_model_proof_output_s
	sg_rune_v2_complete_model_proof_output_t;
typedef struct sg_rune_v2_semantic_catalog_s sg_rune_v2_semantic_catalog_t;

/* Provider view reserved for the future audited complete-model proof boundary.
 * Callers cannot pass a view to lint or acceptance. */
typedef struct sg_rune_v2_semantic_catalog_view_s
{
	uint32_t version;
	uint32_t reserved;
	sg_rune_v2_wire_binding_t binding;
	sg_rune_model_identity_t identity;
	sg_rune_v2_expected_counts_t counts;
	sg_rune_v2_complete_model_proof_t complete_model_proof;
	const sg_rune_v2_expected_plane_t *planes;
	const sg_rune_v2_expected_vertex_t *portal_vertices;
	const sg_rune_v2_expected_phase_t *phases;
	const sg_rune_v2_expected_cell_t *cells;
	const sg_rune_v2_expected_portal_t *portals;
	const sg_rune_v2_expected_transition_t *phase_transitions;
	const sg_rune_v2_expected_surface_t *surfaces;
	const sg_rune_v2_expected_affordance_t *affordances;
	const sg_rune_v2_expected_kernel_t *kernels;
	const sg_rune_v2_expected_landmark_t *landmarks;
	const sg_rune_v2_expected_mechanism_t *mechanisms;
} sg_rune_v2_semantic_catalog_view_t;

/* A future independent complete-model proof provider must implement these
 * three APIs. Issuance must reject generator-owned, candidate-derived, or
 * caller-assembled expected facts and return immutable storage. A BSP-only
 * producer cannot issue this catalog. */
const sg_rune_v2_semantic_catalog_t *SG_RuneV2CompleteModelProofSemanticCatalog(
	const sg_rune_v2_complete_model_proof_output_t *complete_model_proof_output);
int SG_RuneV2CompleteModelProofSemanticCatalogRead(
	const sg_rune_v2_semantic_catalog_t *catalog,
	const sg_rune_v2_semantic_catalog_view_t **view_out);
/* Reports whether range overlaps the opaque catalog object or any immutable
 * storage reached through its view. The query compares addresses only; it
 * must not dereference range or an unrecognized catalog. Consumers must
 * authenticate with SG_RuneV2CompleteModelProofSemanticCatalogRead before
 * calling this query. */
int SG_RuneV2CompleteModelProofSemanticCatalogStorageOverlaps(
	const sg_rune_v2_semantic_catalog_t *catalog,
	const void *range, size_t range_size);

typedef enum sg_rune_v2_semantic_diagnostic_e
{
	SG_RUNE_V2_SEMANTIC_OK = 0,
	SG_RUNE_V2_SEMANTIC_INVALID_ARGUMENT,
	SG_RUNE_V2_SEMANTIC_WIRE_REJECTED,
	SG_RUNE_V2_SEMANTIC_BINDING_MISMATCH,
	SG_RUNE_V2_SEMANTIC_SECTION_COUNT_MISMATCH,
	SG_RUNE_V2_SEMANTIC_CATALOG_REJECTED,
	SG_RUNE_V2_SEMANTIC_CIRCULAR_AUTHORITY,
	SG_RUNE_V2_SEMANTIC_MODEL_REJECTED,
	SG_RUNE_V2_SEMANTIC_EVIDENCE_MISMATCH,
	SG_RUNE_V2_SEMANTIC_RECORD_MISMATCH
} sg_rune_v2_semantic_diagnostic_t;

typedef struct sg_rune_v2_semantic_report_s
{
	sg_rune_v2_semantic_diagnostic_t diagnostic;
	sg_rune_v2_wire_diagnostic_t wire_diagnostic;
	sg_rune_failure_reason_t model_failure;
	uint16_t section;
	uint32_t record;
} sg_rune_v2_semantic_report_t;

/* Lint is read-only and structural. It checks the canonical wire image,
 * binding, and section counts. It does not prove record completeness. */
sg_rune_v2_semantic_diagnostic_t SG_RuneV2ArtifactLint(
	const unsigned char *encoded, size_t encoded_size,
	const sg_rune_v2_semantic_catalog_t *catalog,
	sg_rune_v2_semantic_report_t *report_out);

/* Integrated semantic acceptance. Once the future complete-model proof
 * provider and its publication caller are integrated, production publication
 * must use this entry point. It lints and decodes the same encoded bytes, then
 * compares the candidate with one provider-issued catalog view. Published
 * storage and scalar outputs change only after semantic success. */
sg_rune_v2_semantic_diagnostic_t SG_RuneV2ArtifactAccept(
	const unsigned char *encoded, size_t encoded_size,
	const sg_rune_v2_semantic_catalog_t *catalog,
	const sg_rune_v2_codec_storage_t *scratch,
	const sg_rune_v2_codec_storage_t *published,
	sg_rune_v2_wire_binding_t *binding_out,
	sg_rune_model_t *model_out,
	sg_rune_validation_evidence_t *evidence_out,
	sg_rune_v2_semantic_report_t *report_out);

#ifdef SG_RUNE_V2_SEMANTIC_TESTING
/* Focused test seam; absent from production declarations and call sites. */
sg_rune_v2_semantic_diagnostic_t SG_RuneV2ArtifactSemanticCompareForTesting(
	const sg_rune_model_t *candidate,
	const sg_rune_validation_evidence_t *candidate_evidence,
	const sg_rune_v2_semantic_catalog_t *catalog,
	sg_rune_v2_semantic_report_t *report_out);
#endif

const char *SG_RuneV2SemanticDiagnosticString(
	sg_rune_v2_semantic_diagnostic_t diagnostic);

#endif /* SG_RUNE_V2_ARTIFACT_SEMANTIC_H */
