#ifndef SG_RUNE_DYNAMICS_MODEL_H
#define SG_RUNE_DYNAMICS_MODEL_H

#include <stddef.h>
#include <stdint.h>

#include "sg_destination.h"

#define SG_RUNE_DYNAMICS_MODEL_VERSION UINT16_C(4)
#define SG_RUNE_STATE_DIMENSION_COUNT UINT8_C(7)
#define SG_RUNE_FIELD_COST_INFINITE UINT64_MAX
#define SG_RUNE_FIELD_NO_REGION UINT32_MAX

typedef struct sg_rune_dynamics_model_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_dynamics_model_id_t;

typedef struct sg_rune_state_vertex_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_state_vertex_id_t;
typedef sg_rune_state_vertex_id_t sg_rune_state_vertex_ref_t;

typedef struct sg_rune_state_simplex_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_state_simplex_id_t;
typedef sg_rune_state_simplex_id_t sg_rune_state_simplex_ref_t;

typedef struct sg_rune_control_fiber_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_control_fiber_id_t;
typedef sg_rune_control_fiber_id_t sg_rune_control_fiber_ref_t;

typedef struct sg_rune_response_patch_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_response_patch_id_t;
typedef sg_rune_response_patch_id_t sg_rune_response_patch_ref_t;

typedef struct sg_rune_boundary_transfer_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_boundary_transfer_id_t;
typedef sg_rune_boundary_transfer_id_t sg_rune_boundary_transfer_ref_t;

typedef struct sg_rune_control_domain_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_control_domain_id_t;
typedef sg_rune_control_domain_id_t sg_rune_control_domain_ref_t;

typedef struct sg_rune_guard_condition_ref_s
{
	sg_rune_stable_id_t value;
} sg_rune_guard_condition_ref_t;

typedef struct sg_rune_dynamics_proof_ref_s
{
	sg_rune_stable_id_t value;
} sg_rune_dynamics_proof_ref_t;

typedef struct sg_rune_simplex_ownership_proof_ref_s
{
	sg_rune_stable_id_t value;
} sg_rune_simplex_ownership_proof_ref_t;

typedef struct sg_rune_domain_support_proof_ref_s
{
	sg_rune_stable_id_t value;
} sg_rune_domain_support_proof_ref_t;

typedef struct sg_rune_domain_boundary_proof_ref_s
{
	sg_rune_stable_id_t value;
} sg_rune_domain_boundary_proof_ref_t;

typedef struct sg_field_outcome_image_proof_ref_s
{
	sg_rune_stable_id_t value;
} sg_field_outcome_image_proof_ref_t;

typedef struct sg_field_outcome_cover_proof_ref_s
{
	sg_rune_stable_id_t value;
} sg_field_outcome_cover_proof_ref_t;

typedef struct sg_rune_field_region_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_field_region_id_t;

typedef struct sg_rune_field_hierarchy_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_field_hierarchy_id_t;

typedef struct sg_rune_field_error_contract_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_field_error_contract_id_t;

typedef struct sg_field_choice_id_s
{
	sg_rune_stable_id_t value;
} sg_field_choice_id_t;
typedef sg_field_choice_id_t sg_field_choice_ref_t;

typedef struct sg_field_outcome_id_s
{
	sg_rune_stable_id_t value;
} sg_field_outcome_id_t;
typedef sg_field_outcome_id_t sg_field_outcome_ref_t;

typedef struct sg_field_outcome_image_id_s
{
	sg_rune_stable_id_t value;
} sg_field_outcome_image_id_t;
typedef sg_field_outcome_image_id_t sg_field_outcome_image_ref_t;

typedef struct sg_field_reach_atom_id_s
{
	sg_rune_stable_id_t value;
} sg_field_reach_atom_id_t;
typedef sg_field_reach_atom_id_t sg_field_reach_atom_ref_t;

typedef struct sg_field_local_progress_id_s
{
	sg_rune_stable_id_t value;
} sg_field_local_progress_id_t;

typedef struct sg_field_refinement_node_id_s
{
	sg_rune_stable_id_t value;
} sg_field_refinement_node_id_t;
typedef sg_field_refinement_node_id_t sg_field_refinement_node_ref_t;

typedef struct sg_field_refinement_vertex_id_s
{
	sg_rune_stable_id_t value;
} sg_field_refinement_vertex_id_t;
typedef sg_field_refinement_vertex_id_t sg_field_refinement_vertex_ref_t;

typedef struct sg_field_refinement_face_id_s
{
	sg_rune_stable_id_t value;
} sg_field_refinement_face_id_t;
typedef sg_field_refinement_face_id_t sg_field_refinement_face_ref_t;

typedef struct sg_field_refinement_tree_id_s
{
	sg_rune_stable_id_t value;
} sg_field_refinement_tree_id_t;

typedef struct sg_rune_state_vertex_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_state_vertex_span_t;

typedef struct sg_rune_state_chart_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_state_chart_span_t;

typedef struct sg_rune_state_simplex_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_state_simplex_span_t;

typedef struct sg_rune_state_domain_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_state_domain_span_t;

typedef struct sg_rune_control_fiber_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_control_fiber_span_t;

typedef struct sg_rune_response_patch_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_response_patch_span_t;

typedef struct sg_rune_boundary_transfer_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_boundary_transfer_span_t;

typedef struct sg_rune_field_region_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_field_region_span_t;

typedef struct sg_field_choice_span_s
{
	uint32_t first;
	uint32_t count;
} sg_field_choice_span_t;

typedef struct sg_field_outcome_span_s
{
	uint32_t first;
	uint32_t count;
} sg_field_outcome_span_t;

typedef struct sg_field_reach_atom_span_s
{
	uint32_t first;
	uint32_t count;
} sg_field_reach_atom_span_t;

typedef struct sg_field_guard_effect_span_s
{
	uint32_t first;
	uint32_t count;
} sg_field_guard_effect_span_t;

typedef struct sg_field_guard_requirement_span_s
{
	uint32_t first;
	uint32_t count;
} sg_field_guard_requirement_span_t;

typedef struct sg_field_outcome_cover_piece_span_s
{
	uint32_t first;
	uint32_t count;
} sg_field_outcome_cover_piece_span_t;

typedef struct sg_field_outcome_image_span_s
{
	uint32_t first;
	uint32_t count;
} sg_field_outcome_image_span_t;

typedef struct sg_rune_domain_boundary_facet_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_domain_boundary_facet_span_t;

typedef struct sg_rune_exact_word_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_exact_word_span_t;

typedef struct sg_field_refinement_node_ref_span_s
{
	uint32_t first;
	uint32_t count;
} sg_field_refinement_node_ref_span_t;

typedef struct sg_field_choice_ref_span_s
{
	uint32_t first;
	uint32_t count;
} sg_field_choice_ref_span_t;

typedef struct sg_field_progress_target_span_s
{
	uint32_t first;
	uint32_t count;
} sg_field_progress_target_span_t;

typedef struct sg_field_refinement_node_span_s
{
	uint32_t first;
	uint32_t count;
} sg_field_refinement_node_span_t;

typedef struct sg_field_refinement_vertex_ref_span_s
{
	uint32_t first;
	uint32_t count;
} sg_field_refinement_vertex_ref_span_t;

typedef struct sg_field_refinement_face_ref_span_s
{
	uint32_t first;
	uint32_t count;
} sg_field_refinement_face_ref_span_t;

typedef struct sg_field_refinement_incidence_span_s
{
	uint32_t first;
	uint32_t count;
} sg_field_refinement_incidence_span_t;

typedef enum sg_rune_state_mode_kind_e
{
	SG_RUNE_STATE_MODE_SUPPORTED = 0,
	SG_RUNE_STATE_MODE_WATER,
	SG_RUNE_STATE_MODE_AIRBORNE,
	SG_RUNE_STATE_MODE_HOOK_BOLT,
	SG_RUNE_STATE_MODE_HOOK_PULL,
	SG_RUNE_STATE_MODE_HOOK_COAST,
	SG_RUNE_STATE_MODE_MOVER_RELATIVE,
	SG_RUNE_STATE_MODE_KIND_COUNT
} sg_rune_state_mode_kind_t;

typedef struct sg_rune_supported_mode_s
{
	sg_rune_surface_ref_t support_surface;
} sg_rune_supported_mode_t;

typedef struct sg_rune_water_mode_s
{
	sg_rune_medium_t medium;
	sg_rune_contents_mask_t contents;
} sg_rune_water_mode_t;

typedef struct sg_rune_airborne_mode_s
{
	sg_rune_void_relation_t void_relation;
} sg_rune_airborne_mode_t;

typedef struct sg_rune_hook_bolt_mode_s
{
	sg_rune_affordance_ref_t visibility_relation;
} sg_rune_hook_bolt_mode_t;

typedef struct sg_rune_hook_pull_mode_s
{
	sg_rune_surface_ref_t anchor_surface;
} sg_rune_hook_pull_mode_t;

typedef struct sg_rune_hook_coast_mode_s
{
	sg_rune_void_relation_t void_relation;
} sg_rune_hook_coast_mode_t;

typedef struct sg_rune_mover_relative_mode_s
{
	sg_rune_mechanism_ref_t mover;
} sg_rune_mover_relative_mode_t;

typedef struct sg_rune_state_mode_s
{
	sg_rune_state_mode_kind_t kind;
	union
	{
		sg_rune_supported_mode_t supported;
		sg_rune_water_mode_t water;
		sg_rune_airborne_mode_t airborne;
		sg_rune_hook_bolt_mode_t hook_bolt;
		sg_rune_hook_pull_mode_t hook_pull;
		sg_rune_hook_coast_mode_t hook_coast;
		sg_rune_mover_relative_mode_t mover_relative;
	} value;
} sg_rune_state_mode_t;

typedef struct sg_rune_state_embedding_s
{
	sg_rune_interval3_t position;
	sg_rune_interval3_t velocity;
	sg_rune_interval_t elapsed_ms;
	uint8_t dimension_count;
	uint8_t reserved[3];
} sg_rune_state_embedding_t;

typedef struct sg_rune_state_vertex_s
{
	sg_rune_state_vertex_id_t id;
	sg_rune_state_chart_ref_t chart;
	sg_rune_vec3_t position;
	sg_rune_vec3_t velocity;
	float elapsed_ms;
} sg_rune_state_vertex_t;

typedef struct sg_rune_state_simplex_s
{
	sg_rune_state_simplex_id_t id;
	sg_rune_state_chart_ref_t chart;
	sg_rune_state_vertex_span_t vertices;
} sg_rune_state_simplex_t;

typedef struct sg_rune_state_domain_s
{
	sg_rune_state_domain_id_t id;
	sg_rune_state_chart_ref_t chart;
	sg_rune_state_simplex_span_t simplices;
} sg_rune_state_domain_t;

typedef struct sg_rune_state_chart_s
{
	sg_rune_state_chart_id_t id;
	sg_rune_cell_ref_t configuration_cell;
	sg_rune_state_mode_t mode;
	sg_rune_state_embedding_t embedding;
	sg_rune_state_vertex_span_t state_vertices;
	sg_rune_state_simplex_span_t simplices;
	sg_rune_state_domain_span_t state_domains;
	sg_rune_control_fiber_span_t control_fibers;
	sg_rune_response_patch_span_t response_patches;
	sg_rune_boundary_transfer_span_t boundary_transfers;
	sg_rune_dynamics_proof_ref_t coverage_proof;
} sg_rune_state_chart_t;

typedef struct sg_rune_control_fiber_s
{
	sg_rune_control_fiber_id_t id;
	sg_rune_state_chart_ref_t source_chart;
	sg_rune_control_domain_ref_t domain;
	sg_rune_guard_condition_ref_t condition;
	sg_rune_dynamics_proof_ref_t coverage_proof;
} sg_rune_control_fiber_t;

typedef struct sg_rune_control_domain_s
{
	sg_rune_control_domain_id_t id;
	sg_rune_state_chart_ref_t source_chart;
	sg_rune_interval_t forward_move;
	sg_rune_interval_t side_move;
	sg_rune_interval_t up_move;
	uint32_t required_buttons;
	uint32_t allowed_buttons;
	sg_rune_dynamics_proof_ref_t admissibility_proof;
} sg_rune_control_domain_t;

typedef struct sg_rune_flow_enclosure_s
{
	sg_rune_interval3_t position;
	sg_rune_interval3_t velocity;
	sg_rune_interval_t elapsed_ms;
} sg_rune_flow_enclosure_t;

typedef struct sg_rune_cost_bounds_s
{
	uint64_t lower_us;
	uint64_t upper_us;
} sg_rune_cost_bounds_t;

typedef struct sg_rune_time_advance_s
{
	uint64_t minimum_ms;
	uint64_t maximum_ms;
} sg_rune_time_advance_t;

typedef struct sg_rune_affine_state_operator_s
{
	/* Canonical IEC-60559 binary32 coefficient bits. Remainder is added
	 * outward after the affine image; local elapsed remains a state
	 * coordinate. */
	float coefficient[SG_RUNE_STATE_DIMENSION_COUNT]
		[SG_RUNE_STATE_DIMENSION_COUNT + 1U];
	uint8_t exact_rank;
	uint8_t reserved[3];
	sg_rune_dynamics_proof_ref_t operator_proof;
	sg_rune_dynamics_proof_ref_t image_proof;
	sg_rune_dynamics_proof_ref_t cover_proof;
} sg_rune_affine_state_operator_t;

typedef enum sg_field_guard_truth_e
{
	SG_FIELD_GUARD_FALSE = 0,
	SG_FIELD_GUARD_TRUE,
	SG_FIELD_GUARD_UNKNOWN,
	SG_FIELD_GUARD_TRUTH_COUNT
} sg_field_guard_truth_t;

typedef struct sg_field_guard_effect_s
{
	sg_rune_guard_condition_ref_t condition;
	sg_field_guard_truth_t required_before;
	sg_field_guard_truth_t resulting_after;
	uint8_t controllable;
	uint8_t reserved[3];
} sg_field_guard_effect_t;

typedef struct sg_field_guard_requirement_s
{
	sg_rune_guard_condition_ref_t condition;
	sg_field_guard_truth_t required;
	uint32_t reserved;
} sg_field_guard_requirement_t;

typedef struct sg_field_reach_atom_s
{
	sg_field_reach_atom_id_t id;
	sg_rune_state_domain_ref_t domain;
	sg_rune_state_simplex_span_t simplices;
	sg_rune_flow_enclosure_t state_bounds;
	sg_rune_dynamics_proof_ref_t partition_proof;
} sg_field_reach_atom_t;

typedef struct sg_rune_state_simplex_owner_s
{
	sg_rune_state_simplex_ref_t simplex;
	sg_rune_state_domain_ref_t domain;
	sg_field_reach_atom_ref_t atom;
	sg_rune_simplex_ownership_proof_ref_t proof;
} sg_rune_state_simplex_owner_t;

typedef struct sg_rune_exact_positive_dyadic_s
{
	/* Canonical little-endian magnitude. The low word is odd, the high word
	 * is nonzero, and value = magnitude * 2^exponent. */
	sg_rune_exact_word_span_t magnitude;
	int32_t exponent;
} sg_rune_exact_positive_dyadic_t;

typedef struct sg_rune_domain_boundary_facet_s
{
	sg_rune_state_domain_ref_t domain;
	sg_field_refinement_vertex_ref_span_t vertices;
	/* Coefficient relative to lexicographic coordinate-key order. */
	int8_t orientation;
	uint8_t reserved[3];
	sg_rune_domain_boundary_proof_ref_t proof;
} sg_rune_domain_boundary_facet_t;

typedef struct sg_rune_domain_support_certificate_s
{
	sg_rune_state_domain_ref_t domain;
	sg_rune_domain_boundary_facet_span_t boundary_facets;
	sg_rune_exact_positive_dyadic_t normalized_volume;
	sg_rune_domain_support_proof_ref_t proof;
} sg_rune_domain_support_certificate_t;

typedef struct sg_field_outcome_image_s
{
	sg_field_outcome_image_id_t id;
	sg_field_outcome_ref_t outcome;
	sg_field_refinement_node_ref_t source_leaf;
	sg_rune_flow_enclosure_t canonical_image;
	sg_field_outcome_cover_piece_span_t destination_cover;
	sg_field_outcome_image_proof_ref_t proof;
} sg_field_outcome_image_t;

typedef struct sg_field_outcome_s
{
	sg_field_outcome_id_t id;
	sg_rune_affine_state_operator_t endpoint;
	sg_rune_flow_enclosure_t remainder;
	sg_field_outcome_image_span_t source_images;
	sg_field_outcome_cover_piece_span_t destination_cover;
	/* The cover is a canonical exact slab partition of the outward-rounded
	 * image along this state coordinate. */
	uint8_t cover_split_dimension;
	uint8_t reserved[3];
	sg_rune_time_advance_t absolute_time_advance;
	sg_field_guard_effect_span_t guard_effects;
	sg_rune_dynamics_proof_ref_t proof;
} sg_field_outcome_t;

typedef struct sg_field_outcome_cover_piece_s
{
	sg_field_outcome_image_ref_t source_image;
	sg_field_refinement_node_ref_t source_refinement_node;
	sg_field_reach_atom_ref_t atom;
	sg_field_refinement_node_ref_t refinement_node;
	sg_rune_flow_enclosure_t image_piece;
	sg_field_outcome_cover_proof_ref_t proof;
} sg_field_outcome_cover_piece_t;

typedef enum sg_field_choice_kind_e
{
	SG_FIELD_CHOICE_CONTROL = 0,
	SG_FIELD_CHOICE_TRANSFER,
	SG_FIELD_CHOICE_KIND_COUNT
} sg_field_choice_kind_t;

typedef struct sg_field_choice_s
{
	sg_field_choice_id_t id;
	sg_field_choice_kind_t kind;
	union
	{
		sg_rune_control_fiber_ref_t control;
		sg_rune_boundary_transfer_ref_t transfer;
	} authority;
	sg_field_guard_requirement_span_t guard_requirements;
	sg_field_reach_atom_ref_t source_atom;
	sg_field_outcome_span_t outcomes;
	sg_rune_cost_bounds_t cost;
	sg_rune_dynamics_proof_ref_t proof;
} sg_field_choice_t;

typedef struct sg_field_terminal_parameter_domain_s
{
	sg_rune_flow_enclosure_t anchor_bounds;
	sg_rune_interval3_t position_offset_bounds;
	sg_rune_interval3_t velocity_bounds;
	sg_rune_interval_t local_elapsed_bounds;
	sg_rune_dynamics_proof_ref_t proof;
} sg_field_terminal_parameter_domain_t;

typedef struct sg_field_local_progress_kernel_s
{
	sg_field_local_progress_id_t id;
	sg_field_reach_atom_ref_t source_atom;
	sg_field_refinement_node_ref_span_t covered_sources;
	sg_field_reach_atom_ref_t target_atom;
	sg_field_terminal_parameter_domain_t terminal_parameters;
	sg_field_choice_ref_span_t admissible_choices;
	sg_field_progress_target_span_t whole_outcome_targets;
	uint32_t finite_rank;
	uint32_t reserved;
	float state_lyapunov[SG_RUNE_STATE_DIMENSION_COUNT];
	float anchor_lyapunov[SG_RUNE_STATE_DIMENSION_COUNT];
	float lyapunov_constant;
	float minimum_lyapunov_decrease;
	sg_rune_dynamics_proof_ref_t proof;
} sg_field_local_progress_kernel_t;

typedef struct sg_field_progress_target_s
{
	sg_field_outcome_ref_t outcome;
	sg_field_reach_atom_ref_t atom;
} sg_field_progress_target_t;

typedef struct sg_field_refinement_vertex_s
{
	sg_field_refinement_vertex_id_t id;
	sg_rune_vec3_t position;
	sg_rune_vec3_t velocity;
	float elapsed_ms;
	sg_rune_dynamics_proof_ref_t proof;
} sg_field_refinement_vertex_t;

typedef struct sg_field_refinement_face_incidence_s
{
	sg_field_refinement_node_ref_t node;
	uint8_t local_face;
	int8_t orientation;
	uint8_t reserved[2];
} sg_field_refinement_face_incidence_t;

typedef struct sg_field_refinement_face_s
{
	sg_field_refinement_face_id_t id;
	sg_field_refinement_vertex_ref_span_t vertices;
	sg_field_refinement_incidence_span_t incidences;
	/* NONE for an interior or root-boundary face. A child-boundary face
	 * names the exact parent face that contains it. */
	sg_field_refinement_face_ref_t parent_face;
	sg_rune_dynamics_proof_ref_t proof;
} sg_field_refinement_face_t;

typedef struct sg_field_refinement_node_s
{
	sg_field_refinement_node_id_t id;
	uint32_t parent;
	sg_field_refinement_node_span_t children;
	sg_field_reach_atom_ref_t atom;
	sg_field_refinement_vertex_ref_span_t vertices;
	sg_field_refinement_face_ref_span_t faces;
	int8_t orientation;
	uint8_t reserved[3];
	sg_rune_flow_enclosure_t state_bounds;
	sg_rune_flow_enclosure_t interpolation_error;
	sg_rune_dynamics_proof_ref_t geometry_proof;
	sg_rune_dynamics_proof_ref_t interpolation_proof;
} sg_field_refinement_node_t;

typedef struct sg_field_refinement_tree_s
{
	sg_field_refinement_tree_id_t id;
	const sg_field_refinement_node_t *nodes;
	size_t node_count;
	const sg_field_refinement_vertex_t *vertices;
	size_t vertex_count;
	const sg_field_refinement_vertex_ref_t *node_vertices;
	size_t node_vertex_count;
	const sg_field_refinement_face_t *faces;
	size_t face_count;
	const sg_field_refinement_vertex_ref_t *face_vertices;
	size_t face_vertex_count;
	const sg_field_refinement_face_incidence_t *face_incidences;
	size_t face_incidence_count;
	const sg_field_refinement_face_ref_t *node_faces;
	size_t node_face_count;
	const uint32_t *children;
	size_t child_count;
	const uint32_t *atom_roots;
	size_t atom_count;
	sg_rune_dynamics_proof_ref_t proof;
} sg_field_refinement_tree_t;

typedef struct sg_rune_response_patch_s
{
	sg_rune_response_patch_id_t id;
	sg_rune_state_chart_ref_t source_chart;
	sg_rune_state_simplex_ref_t source_simplex;
	sg_rune_control_fiber_span_t controls;
	sg_rune_flow_enclosure_t flow;
	sg_rune_cost_bounds_t running_cost;
	sg_rune_state_domain_span_t destination_domains;
	sg_rune_dynamics_proof_ref_t flow_proof;
} sg_rune_response_patch_t;

typedef struct sg_rune_boundary_transfer_s
{
	sg_rune_boundary_transfer_id_t id;
	sg_rune_state_chart_ref_t source_chart;
	sg_rune_state_domain_ref_t source_domain;
	sg_rune_guard_condition_ref_t condition;
	sg_rune_state_chart_ref_t destination_chart;
	sg_rune_state_domain_ref_t destination_domain;
	sg_rune_flow_enclosure_t reset_enclosure;
	sg_rune_dynamics_proof_ref_t transfer_proof;
} sg_rune_boundary_transfer_t;

typedef struct sg_rune_field_region_s
{
	sg_rune_field_region_id_t id;
	uint32_t parent_region;
	uint32_t level;
	sg_rune_field_region_span_t children;
	sg_rune_state_chart_span_t charts;
	sg_rune_state_domain_span_t state_domains;
	sg_rune_response_patch_span_t response_patches;
	sg_rune_dynamics_proof_ref_t coverage_proof;
} sg_rune_field_region_t;

typedef struct sg_rune_field_region_hierarchy_s
{
	sg_rune_field_hierarchy_id_t id;
	const sg_rune_field_region_t *regions;
	size_t region_count;
	const uint32_t *children;
	size_t child_count;
	const uint32_t *chart_leaf_regions;
	const uint32_t *state_domain_leaf_regions;
	const uint32_t *response_patch_leaf_regions;
	size_t chart_count;
	size_t state_domain_count;
	size_t response_patch_count;
	sg_rune_dynamics_proof_ref_t hierarchy_proof;
} sg_rune_field_region_hierarchy_t;

typedef struct sg_rune_field_error_contract_s
{
	sg_rune_field_error_contract_id_t id;
	uint64_t cost_quantum_us;
	uint64_t maximum_value_width_us;
	uint64_t maximum_bellman_residual_us;
	sg_rune_interval3_t position_error;
	sg_rune_interval3_t velocity_error;
	sg_rune_interval_t time_error;
} sg_rune_field_error_contract_t;

typedef struct sg_rune_dynamics_model_s
{
	uint16_t version;
	uint16_t reserved;
	sg_rune_dynamics_model_id_t id;
	uint64_t rune_identity;
	uint64_t topology_revision;
	const sg_rune_state_vertex_t *state_vertices;
	size_t state_vertex_count;
	const sg_rune_state_chart_t *state_charts;
	size_t state_chart_count;
	const sg_rune_state_simplex_t *state_simplices;
	size_t state_simplex_count;
	const sg_rune_state_domain_t *state_domains;
	size_t state_domain_count;
	const sg_rune_control_fiber_t *control_fibers;
	size_t control_fiber_count;
	const sg_rune_control_domain_t *control_domains;
	size_t control_domain_count;
	const sg_rune_response_patch_t *response_patches;
	size_t response_patch_count;
	const sg_rune_boundary_transfer_t *boundary_transfers;
	size_t boundary_transfer_count;
	const sg_field_reach_atom_t *reach_atoms;
	size_t reach_atom_count;
	const sg_rune_state_simplex_owner_t *simplex_owners;
	size_t simplex_owner_count;
	const sg_rune_domain_support_certificate_t *domain_support;
	size_t domain_support_count;
	const sg_rune_domain_boundary_facet_t *domain_boundary_facets;
	size_t domain_boundary_facet_count;
	const sg_field_refinement_vertex_ref_t *domain_boundary_vertices;
	size_t domain_boundary_vertex_count;
	const uint32_t *exact_words;
	size_t exact_word_count;
	const sg_field_outcome_image_t *outcome_images;
	size_t outcome_image_count;
	const sg_field_outcome_cover_piece_t *outcome_cover_pieces;
	size_t outcome_cover_piece_count;
	const sg_field_guard_requirement_t *guard_requirements;
	size_t guard_requirement_count;
	const sg_field_guard_effect_t *guard_effects;
	size_t guard_effect_count;
	const sg_field_outcome_t *outcomes;
	size_t outcome_count;
	const sg_field_choice_t *choices;
	size_t choice_count;
	const sg_field_local_progress_kernel_t *local_progress_kernels;
	size_t local_progress_kernel_count;
	const sg_field_refinement_node_ref_t *local_progress_sources;
	size_t local_progress_source_count;
	const sg_field_choice_ref_t *local_progress_choices;
	size_t local_progress_choice_count;
	const sg_field_progress_target_t *local_progress_targets;
	size_t local_progress_target_count;
	sg_field_refinement_tree_t refinement_tree;
	sg_rune_field_region_hierarchy_t hierarchy;
	sg_rune_field_error_contract_t error_contract;
} sg_rune_dynamics_model_t;

typedef struct sg_field_service_s sg_field_service_t;
/* Both handles are opaque. Only the RUNE dynamics owner can mint a source;
 * only that source can publish a complete, authenticated product graph. */
typedef struct sg_field_model_source_s sg_field_model_source_t;
typedef struct sg_field_model_publication_s sg_field_model_publication_t;

typedef struct sg_field_handle_s
{
	uint64_t service_identity;
	uint64_t service_generation;
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t terminal_generation;
	uint64_t field_generation;
} sg_field_handle_t;

typedef struct sg_localized_field_state_s
{
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t pose_revision;
	uint64_t sampled_at_ms;
	sg_rune_state_chart_ref_t chart;
	sg_rune_state_mode_t mode;
	sg_rune_vec3_t position;
	sg_rune_vec3_t velocity;
	float elapsed_ms;
} sg_localized_field_state_t;

typedef struct sg_field_environment_s
{
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t environment_revision;
	uint64_t sampled_at_ms;
	uint64_t authority_identity;
	const struct sg_field_guard_state_s *guards;
	size_t guard_count;
	const struct sg_field_event_slab_s *event_slabs;
	size_t event_slab_count;
	uint8_t authenticated;
	uint8_t reserved[7];
} sg_field_environment_t;

typedef struct sg_field_guard_state_s
{
	sg_rune_guard_condition_ref_t condition;
	sg_field_guard_truth_t truth;
	uint32_t reserved;
} sg_field_guard_state_t;

typedef struct sg_field_event_slab_s
{
	uint64_t valid_from_ms;
	uint64_t valid_until_ms;
	const sg_field_guard_state_t *exogenous_guards;
	size_t exogenous_guard_count;
	sg_rune_dynamics_proof_ref_t schedule_proof;
} sg_field_event_slab_t;

typedef struct sg_rune_field_descent_s
{
	sg_rune_control_fiber_ref_t control;
	uint64_t minimum_descent_us;
	sg_rune_cost_bounds_t endpoint_cost;
} sg_rune_field_descent_t;

typedef struct sg_rune_field_transfer_descent_s
{
	sg_rune_boundary_transfer_ref_t transfer;
	sg_rune_cost_bounds_t endpoint_cost;
	uint64_t minimum_descent_us;
} sg_rune_field_transfer_descent_t;

typedef enum sg_field_option_kind_e
{
	SG_FIELD_OPTION_CONTROL = 0,
	SG_FIELD_OPTION_TRANSFER,
	SG_FIELD_OPTION_KIND_COUNT
} sg_field_option_kind_t;

typedef struct sg_field_option_s
{
	sg_field_option_kind_t kind;
	union
	{
		sg_rune_field_descent_t control;
		sg_rune_field_transfer_descent_t transfer;
	} value;
} sg_field_option_t;

typedef enum sg_field_guidance_kind_e
{
	SG_FIELD_GUIDANCE_TERMINAL = 0,
	SG_FIELD_GUIDANCE_DESCENT,
	SG_FIELD_GUIDANCE_UNREACHABLE,
	SG_FIELD_GUIDANCE_KIND_COUNT
} sg_field_guidance_kind_t;

typedef struct sg_field_terminal_guidance_s
{
	sg_rune_cost_bounds_t arrival_cost;
	uint64_t residual_bound_us;
} sg_field_terminal_guidance_t;

typedef struct sg_field_descent_guidance_s
{
	sg_rune_cost_bounds_t arrival_cost;
	uint64_t residual_bound_us;
	sg_rune_interval3_t spatial_subgradient;
	sg_rune_interval3_t velocity_subgradient;
	sg_rune_interval_t time_subgradient;
	sg_rune_interval3_t position_error;
	sg_rune_interval3_t velocity_error;
	sg_rune_interval_t time_error;
	const sg_field_option_t *options;
	size_t option_count;
	size_t required_option_capacity;
} sg_field_descent_guidance_t;

typedef struct sg_field_unreachable_guidance_s
{
	sg_rune_cost_bounds_t arrival_cost;
} sg_field_unreachable_guidance_t;

typedef struct sg_field_guidance_s
{
	sg_field_handle_t field;
	uint64_t pose_revision;
	uint64_t sampled_at_ms;
	sg_field_guidance_kind_t kind;
	union
	{
		sg_field_terminal_guidance_t terminal;
		sg_field_descent_guidance_t descent;
		sg_field_unreachable_guidance_t unreachable;
	} value;
} sg_field_guidance_t;

typedef enum sg_field_status_e
{
	SG_FIELD_STATUS_OK = 0,
	SG_FIELD_STATUS_INVALID_ARGUMENT,
	SG_FIELD_STATUS_INVALID_MODEL,
	/* The accepted RUNE publication lacks product states, transitions, or
	 * local-progress coverage needed to classify this valid terminal. */
	SG_FIELD_STATUS_MODEL_INCOMPLETE,
	SG_FIELD_STATUS_IDENTITY_MISMATCH,
	SG_FIELD_STATUS_STALE,
	SG_FIELD_STATUS_UNREACHABLE,
	SG_FIELD_STATUS_PROOF_FAILED,
	/* Exact classification succeeded, but the bounded numerical field could
	 * not satisfy its published error contract. */
	SG_FIELD_STATUS_NUMERICAL_ERROR,
	SG_FIELD_STATUS_STORAGE,
	SG_FIELD_STATUS_CAPACITY,
	SG_FIELD_STATUS_COUNT
} sg_field_status_t;

int SG_RuneDynamicsModelIdValid(const sg_rune_dynamics_model_id_t *id);
int SG_RuneStateVertexIdValid(const sg_rune_state_vertex_id_t *id);
int SG_RuneStateChartIdValid(const sg_rune_state_chart_id_t *id);
int SG_RuneStateSimplexIdValid(const sg_rune_state_simplex_id_t *id);
int SG_RuneStateDomainIdValid(const sg_rune_state_domain_id_t *id);
int SG_RuneControlFiberIdValid(const sg_rune_control_fiber_id_t *id);
int SG_RuneControlDomainIdValid(const sg_rune_control_domain_id_t *id);
int SG_RuneResponsePatchIdValid(const sg_rune_response_patch_id_t *id);
int SG_RuneBoundaryTransferIdValid(
	const sg_rune_boundary_transfer_id_t *id);
int SG_RuneControlDomainRefValid(const sg_rune_control_domain_ref_t *ref);
int SG_RuneGuardConditionRefValid(const sg_rune_guard_condition_ref_t *ref);
int SG_RuneDynamicsProofRefValid(const sg_rune_dynamics_proof_ref_t *ref);
int SG_RuneSimplexOwnershipProofRefValid(
	const sg_rune_simplex_ownership_proof_ref_t *ref);
int SG_RuneDomainSupportProofRefValid(
	const sg_rune_domain_support_proof_ref_t *ref);
int SG_RuneDomainBoundaryProofRefValid(
	const sg_rune_domain_boundary_proof_ref_t *ref);
int SG_FieldOutcomeImageIdValid(const sg_field_outcome_image_id_t *id);
int SG_FieldOutcomeImageProofRefValid(
	const sg_field_outcome_image_proof_ref_t *ref);
int SG_FieldOutcomeCoverProofRefValid(
	const sg_field_outcome_cover_proof_ref_t *ref);
int SG_RuneFieldRegionIdValid(const sg_rune_field_region_id_t *id);
int SG_RuneFieldHierarchyIdValid(const sg_rune_field_hierarchy_id_t *id);
int SG_RuneFieldErrorContractIdValid(
	const sg_rune_field_error_contract_id_t *id);
int SG_FieldChoiceIdValid(const sg_field_choice_id_t *id);
int SG_FieldOutcomeIdValid(const sg_field_outcome_id_t *id);
int SG_FieldReachAtomIdValid(const sg_field_reach_atom_id_t *id);
int SG_FieldLocalProgressIdValid(const sg_field_local_progress_id_t *id);
int SG_FieldRefinementVertexIdValid(
	const sg_field_refinement_vertex_id_t *id);
int SG_FieldRefinementFaceIdValid(const sg_field_refinement_face_id_t *id);
int SG_FieldRefinementNodeIdValid(const sg_field_refinement_node_id_t *id);
int SG_FieldRefinementTreeIdValid(const sg_field_refinement_tree_id_t *id);
int SG_RuneStateModeValid(const sg_rune_state_mode_t *mode);
int SG_RuneStateVertexShapeValid(const sg_rune_state_vertex_t *vertex);
int SG_RuneStateSimplexShapeValid(const sg_rune_state_simplex_t *simplex);
int SG_RuneStateDomainShapeValid(const sg_rune_state_domain_t *domain);
int SG_RuneStateChartShapeValid(const sg_rune_state_chart_t *chart);
int SG_RuneControlFiberShapeValid(const sg_rune_control_fiber_t *fiber);
int SG_RuneControlDomainShapeValid(const sg_rune_control_domain_t *domain);
int SG_RuneResponsePatchShapeValid(const sg_rune_response_patch_t *patch);
int SG_RuneBoundaryTransferShapeValid(
	const sg_rune_boundary_transfer_t *transfer);
int SG_RuneFieldRegionShapeValid(const sg_rune_field_region_t *region);
int SG_RuneFieldRegionHierarchyValid(
	const sg_rune_field_region_hierarchy_t *hierarchy);
int SG_RuneFieldErrorContractValid(
	const sg_rune_field_error_contract_t *contract);
int SG_FieldReachAtomShapeValid(const sg_field_reach_atom_t *atom);
int SG_FieldGuardEffectValid(const sg_field_guard_effect_t *effect);
int SG_FieldOutcomeShapeValid(const sg_field_outcome_t *outcome);
int SG_FieldChoiceShapeValid(const sg_field_choice_t *choice);
int SG_FieldLocalProgressKernelShapeValid(
	const sg_field_local_progress_kernel_t *kernel);
int SG_FieldLocalProgressKernelAcceptsCapture(
	const sg_field_local_progress_kernel_t *kernel,
	const sg_destination_terminal_capture_t *capture);
int SG_FieldRefinementTreeValid(const sg_field_refinement_tree_t *tree,
	const sg_field_reach_atom_t *atoms, size_t atom_count);
int SG_RuneDynamicsModelValid(const sg_rune_dynamics_model_t *model,
	const sg_rune_runtime_snapshot_t *snapshot);
int SG_LocalizedFieldStateValid(const sg_localized_field_state_t *state);
int SG_FieldEnvironmentValid(const sg_field_environment_t *environment);
int SG_FieldHandleValid(const sg_field_handle_t *handle);
int SG_FieldGuidanceValid(const sg_field_guidance_t *guidance);

/* Publication deep-copies the complete graph. Missing state, choice, outcome,
 * guard, time, or local-progress coverage returns MODEL_INCOMPLETE and never
 * produces a publication. */
sg_field_status_t SG_FieldModelPublicationIssue(
	const sg_field_model_source_t *source,
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_rune_dynamics_model_t *dynamics_model,
	sg_field_model_publication_t **publication_out);
void SG_FieldModelPublicationDestroy(sg_field_model_publication_t *publication);

sg_field_status_t SG_FieldServiceCreate(
	const sg_field_model_publication_t *publication,
	sg_field_service_t **service_out);
void SG_FieldServiceDestroy(sg_field_service_t *service);
sg_field_status_t SG_FieldServiceResolve(sg_field_service_t *service,
	const sg_destination_terminal_t *terminal,
	const sg_field_environment_t *environment, uint64_t now_ms,
	sg_field_handle_t *handle_out);
sg_field_status_t SG_FieldServiceRefresh(sg_field_service_t *service,
	const sg_field_handle_t *previous,
	const sg_destination_terminal_t *terminal,
	const sg_field_environment_t *environment, uint64_t now_ms,
	sg_field_handle_t *handle_out);
sg_field_status_t SG_FieldServiceQuery(const sg_field_service_t *service,
	const sg_field_handle_t *handle,
	const sg_localized_field_state_t *state,
	const sg_field_environment_t *environment,
	sg_field_option_t *option_storage, size_t option_capacity,
	sg_field_guidance_t *guidance_out);
sg_field_status_t SG_FieldServiceRelease(sg_field_service_t *service,
	const sg_field_handle_t *handle);

#endif
