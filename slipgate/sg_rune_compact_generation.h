/* Fail-closed construction transaction for one compact RUNE artifact. */
#ifndef SG_RUNE_COMPACT_GENERATION_H
#define SG_RUNE_COMPACT_GENERATION_H

#include <stddef.h>
#include <stdint.h>

#include "sg_rune_compact_artifact.h"
#include "sg_rune_compact_builder.h"
#include "sg_rune_compact_composer.h"
#include "sg_rune_compact_geometry.h"
#include "sg_rune_compact_mechanisms.h"
#include "sg_rune_compact_movement_fields.h"
#include "sg_rune_compact_response_partition.h"
#include "sg_rune_compact_static_materializer.h"
#include "sg_rune_compact_weapon_field.h"

typedef enum sg_rune_compact_generation_stage_e
{
	SG_RUNE_COMPACT_GENERATION_STAGE_NONE = 0,
	SG_RUNE_COMPACT_GENERATION_STAGE_BUILDER,
	SG_RUNE_COMPACT_GENERATION_STAGE_GEOMETRY,
	SG_RUNE_COMPACT_GENERATION_STAGE_RESPONSE,
	SG_RUNE_COMPACT_GENERATION_STAGE_MECHANISMS,
	SG_RUNE_COMPACT_GENERATION_STAGE_STATIC,
	SG_RUNE_COMPACT_GENERATION_STAGE_MOVEMENT,
	SG_RUNE_COMPACT_GENERATION_STAGE_RELATION,
	SG_RUNE_COMPACT_GENERATION_STAGE_WEAPON,
	SG_RUNE_COMPACT_GENERATION_STAGE_COMPOSER,
	SG_RUNE_COMPACT_GENERATION_STAGE_WIRE_ENCODE,
	SG_RUNE_COMPACT_GENERATION_STAGE_WIRE_DECODE,
	SG_RUNE_COMPACT_GENERATION_STAGE_PUBLICATION,
	SG_RUNE_COMPACT_GENERATION_STAGE_COUNT
} sg_rune_compact_generation_stage_t;

typedef struct sg_rune_compact_generation_counts_s
{
	uint32_t geometry_cells;
	uint32_t geometry_facets;
	uint32_t geometry_incidences;
	uint32_t geometry_cell_incidences;
	uint32_t geometry_vertices;
	uint32_t geometry_portals;
	uint32_t response_fragments;
	uint32_t response_halfspaces;
	uint32_t response_patches;
	uint32_t response_vertices;
	uint32_t response_splits;
	uint32_t response_pairs;
	uint32_t response_candidate_groups;
	uint32_t response_source_endpoint_groups;
	uint32_t response_target_endpoint_groups;
	uint32_t mechanism_authorities;
	uint32_t mechanism_controllers;
	uint32_t mechanism_topology_edges;
	uint32_t mechanism_transitions;
	uint32_t static_mechanisms;
	uint32_t static_mechanism_controllers;
	uint32_t static_mechanism_edges;
	uint32_t static_transitions;
	uint32_t static_landmarks;
	uint32_t static_landmark_cells;
	uint32_t static_facet_annotations;
	uint32_t static_portal_mechanisms;
	uint32_t movement_capabilities;
	uint32_t movement_states;
	uint32_t movement_fibers;
	uint32_t movement_hook_targets;
	uint32_t movement_fiber_function_refs;
	uint32_t movement_analytic_functions;
	uint32_t relations;
	uint32_t relation_candidate_groups;
	uint32_t relation_occluders;
	uint32_t weapon_kernels;
	uint32_t weapon_attachments;
	uint32_t weapon_relation_spans;
	uint32_t weapon_relation_refs;
	uint32_t weapon_function_refs;
	uint32_t weapon_analytic_functions;
	uint32_t composer_cells;
	uint32_t composer_facets;
	uint32_t composer_incidences;
	uint32_t composer_portals;
	uint32_t composer_movement_capabilities;
	uint32_t composer_movement_states;
	uint32_t composer_movement_fibers;
	uint32_t composer_movement_hook_targets;
	uint32_t composer_movement_fiber_function_refs;
	uint32_t composer_weapon_kernels;
	uint32_t composer_weapon_attachments;
	uint32_t composer_weapon_relation_spans;
	uint32_t composer_weapon_relation_refs;
	uint32_t composer_analytic_functions;
	size_t encoded_bytes;
} sg_rune_compact_generation_counts_t;

typedef void (*sg_rune_compact_generation_progress_fn)(void *context,
	sg_rune_compact_generation_stage_t stage,
	const sg_rune_compact_generation_counts_t *counts);

typedef struct sg_rune_compact_generation_input_s
{
	sg_rune_compact_builder_input_t builder_input;
	const char *destination;
	const sg_rune_compact_geometry_allocator_t *geometry_allocator;
	const sg_rune_compact_artifact_fs_ops_t *artifact_fs_ops;
	const sg_host_collision_scene_t *collision_scene;
	sg_rune_compact_generation_progress_fn progress;
	void *progress_context;
} sg_rune_compact_generation_input_t;

typedef enum sg_rune_compact_generation_error_code_e
{
	SG_RUNE_COMPACT_GENERATION_ERROR_NONE = 0,
	SG_RUNE_COMPACT_GENERATION_ERROR_INVALID_ARGUMENT,
	SG_RUNE_COMPACT_GENERATION_ERROR_BUILDER_REJECTED,
	SG_RUNE_COMPACT_GENERATION_ERROR_GEOMETRY_REJECTED,
	SG_RUNE_COMPACT_GENERATION_ERROR_RESPONSE_REJECTED,
	SG_RUNE_COMPACT_GENERATION_ERROR_MECHANISMS_REJECTED,
	SG_RUNE_COMPACT_GENERATION_ERROR_STATIC_REJECTED,
	SG_RUNE_COMPACT_GENERATION_ERROR_MOVEMENT_REJECTED,
	SG_RUNE_COMPACT_GENERATION_ERROR_RELATION_REJECTED,
	SG_RUNE_COMPACT_GENERATION_ERROR_WEAPON_REJECTED,
	SG_RUNE_COMPACT_GENERATION_ERROR_COMPOSER_REJECTED,
	SG_RUNE_COMPACT_GENERATION_ERROR_WIRE_ENCODE_REJECTED,
	SG_RUNE_COMPACT_GENERATION_ERROR_WIRE_DECODE_REJECTED,
	SG_RUNE_COMPACT_GENERATION_ERROR_PUBLICATION_REJECTED,
	SG_RUNE_COMPACT_GENERATION_ERROR_IDENTITY_MISMATCH,
	SG_RUNE_COMPACT_GENERATION_ERROR_CODE_COUNT
} sg_rune_compact_generation_error_code_t;

typedef struct sg_rune_compact_generation_relation_error_s
{
	uint32_t code;
	uint32_t record;
	uint64_t expected;
	uint64_t observed;
} sg_rune_compact_generation_relation_error_t;

typedef struct sg_rune_compact_generation_result_s
{
	sg_rune_compact_generation_error_code_t error;
	sg_rune_compact_generation_stage_t stage;
	sg_rune_compact_generation_counts_t accepted;
	sg_rune_compact_builder_error_t builder_error;
	sg_rune_compact_geometry_error_t geometry_error;
	sg_rune_compact_response_error_t response_error;
	sg_rune_compact_mechanisms_error_t mechanisms_error;
	sg_rune_compact_static_materializer_error_t static_error;
	sg_rune_compact_movement_fields_error_t movement_error;
	sg_rune_compact_generation_relation_error_t relation_error;
	sg_rune_compact_weapon_field_error_t weapon_error;
	sg_rune_compact_composer_error_t composer_error;
	sg_rune_compact_wire_error_t wire_encode_error;
	sg_rune_compact_wire_error_t wire_decode_error;
	sg_rune_compact_artifact_publication_result_t publication;
	int published;
	int durable;
} sg_rune_compact_generation_result_t;

int SG_RuneCompactGenerationRun(
	const sg_rune_compact_generation_input_t *input,
	sg_rune_compact_generation_result_t *result_out);

const char *SG_RuneCompactGenerationErrorString(
	sg_rune_compact_generation_error_code_t error);
const char *SG_RuneCompactGenerationStageString(
	sg_rune_compact_generation_stage_t stage);

#endif
