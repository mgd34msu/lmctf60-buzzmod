#ifndef SG_RUNE_COMPACT_RESPONSE_PARTITION_H
#define SG_RUNE_COMPACT_RESPONSE_PARTITION_H

#include <stddef.h>
#include <stdint.h>

#include "sg_rune_compact_builder.h"
#include "sg_rune_compact_geometry.h"

typedef struct sg_rune_compact_response_partition_s
	sg_rune_compact_response_partition_t;

typedef struct sg_rune_compact_response_pvs_offset_s
{
	uint32_t value[SG_BSP_VISIBILITY_SET_COUNT];
} sg_rune_compact_response_pvs_offset_t;

typedef struct sg_rune_compact_response_binary32_point_s
{
	uint32_t value_bits[3];
} sg_rune_compact_response_binary32_point_t;

typedef struct sg_rune_compact_response_occluder_s
{
	uint32_t model;
	uint32_t brush;
	uint32_t contents;
	uint32_t conditional;
	uint32_t first_side;
	uint32_t side_count;
	uint32_t first_edge;
	uint32_t edge_count;
} sg_rune_compact_response_occluder_t;

typedef struct sg_rune_compact_response_occluder_side_s
{
	uint32_t occluder;
	uint32_t model;
	uint32_t brush;
	uint32_t contents;
	uint32_t conditional;
	uint32_t brush_side;
	uint32_t bsp_plane;
	sg_rune_binary32_plane_t halfspace_plane;
	sg_rune_binary32_plane_t plane;
} sg_rune_compact_response_occluder_side_t;

typedef struct sg_rune_compact_response_occluder_edge_s
{
	uint32_t occluder;
	uint32_t side;
	uint32_t ordinal;
	sg_rune_compact_response_binary32_point_t from;
	sg_rune_compact_response_binary32_point_t to;
} sg_rune_compact_response_occluder_edge_t;

typedef void *(*sg_rune_compact_response_allocate_fn)(void *context,
	size_t bytes);
typedef void (*sg_rune_compact_response_release_fn)(void *context,
	void *allocation);

typedef struct sg_rune_compact_response_allocator_s
{
	void *context;
	sg_rune_compact_response_allocate_fn allocate;
	sg_rune_compact_response_release_fn release;
} sg_rune_compact_response_allocator_t;

typedef enum sg_rune_compact_response_certificate_e
{
	SG_RUNE_COMPACT_RESPONSE_CERTIFIED_DIRECT = 0,
	SG_RUNE_COMPACT_RESPONSE_CERTIFIED_STATIC_IMPACT,
	SG_RUNE_COMPACT_RESPONSE_UNRESOLVED_EXACT_RAY,
	SG_RUNE_COMPACT_RESPONSE_CERTIFICATE_COUNT
} sg_rune_compact_response_certificate_t;

typedef struct sg_rune_compact_response_pair_s
{
	uint32_t source_fragment;
	uint32_t target_patch;
	sg_static_visibility_class_t classification;
	sg_static_visibility_reason_t reason;
	uint32_t first_hit_occluder;
	uint32_t requires_exact_ray;
	uint32_t requires_area_state;
	sg_rune_compact_response_certificate_t certificate;
	sg_rune_compact_static_relation_flags_t relation_flags;
	sg_rune_stance_validity_t source_valid_stances;
	sg_rune_stance_validity_t target_valid_stances;
	uint8_t reserved[2];
	uint32_t certificate_split;
	sg_rune_q8_vec3_t target_witness;
	sg_host_collision_trace_t trace;
} sg_rune_compact_response_pair_t;

typedef struct sg_rune_compact_response_partition_view_s
{
	sg_rune_compact_identity_t identity;
	const sg_rune_compact_response_fragment_t *source_fragments;
	uint32_t source_fragment_count;
	const sg_rune_compact_response_halfspace_t *source_halfspaces;
	uint32_t source_halfspace_count;
	const sg_rune_compact_response_patch_t *target_patches;
	uint32_t target_patch_count;
	const sg_rune_q8_vec3_t *target_vertices;
	uint32_t target_vertex_count;
	const sg_rune_compact_response_split_t *splits;
	uint32_t split_count;
	const sg_rune_compact_response_pair_t *response_pairs;
	uint32_t response_pair_count;
	const sg_rune_compact_response_candidate_group_t *candidate_groups;
	uint32_t candidate_group_count;
	const sg_rune_compact_response_endpoint_group_t *source_endpoint_groups;
	uint32_t source_endpoint_group_count;
	const uint32_t *source_endpoint_members;
	uint32_t source_endpoint_member_count;
	const sg_rune_compact_response_endpoint_group_t *target_endpoint_groups;
	uint32_t target_endpoint_group_count;
	const uint32_t *target_endpoint_members;
	uint32_t target_endpoint_member_count;
	uint32_t static_occluder_count;
	const sg_rune_compact_response_occluder_t *static_occluders;
	const sg_rune_compact_response_occluder_side_t *static_occluder_sides;
	uint32_t static_occluder_side_count;
	const sg_rune_compact_response_occluder_edge_t *static_occluder_edges;
	uint32_t static_occluder_edge_count;
	uint32_t compact_facet_count;
	const sg_rune_compact_facet_t *compact_facets;
	uint32_t compact_cell_count;
	const sg_rune_compact_source_surface_t *compact_source_surfaces;
	uint32_t compact_source_surface_count;
	const sg_rune_q8_vec3_t *compact_source_surface_vertices;
	uint32_t compact_source_surface_vertex_count;
	const sg_rune_compact_response_pvs_offset_t *bsp_visibility_bit_offsets;
	uint32_t bsp_visibility_cluster_count;
	const uint8_t *bsp_visibility_bytes;
	uint32_t bsp_visibility_byte_count;
	const uint32_t *area_components;
	uint32_t area_component_count;
	sg_rune_compact_response_seal_t seal;
} sg_rune_compact_response_partition_view_t;

typedef enum sg_rune_compact_response_error_code_e
{
	SG_RUNE_COMPACT_RESPONSE_ERROR_NONE = 0,
	SG_RUNE_COMPACT_RESPONSE_ERROR_INVALID_ARGUMENT,
	SG_RUNE_COMPACT_RESPONSE_ERROR_BUILDER_READ,
	SG_RUNE_COMPACT_RESPONSE_ERROR_GEOMETRY_READ,
	SG_RUNE_COMPACT_RESPONSE_ERROR_IDENTITY_MISMATCH,
	SG_RUNE_COMPACT_RESPONSE_ERROR_INVALID_SOURCE,
	SG_RUNE_COMPACT_RESPONSE_ERROR_INVALID_GEOMETRY,
	SG_RUNE_COMPACT_RESPONSE_ERROR_PARTITION,
	SG_RUNE_COMPACT_RESPONSE_ERROR_NONFINITE,
	SG_RUNE_COMPACT_RESPONSE_ERROR_Q8_CONVERSION,
	SG_RUNE_COMPACT_RESPONSE_ERROR_OVERFLOW,
	SG_RUNE_COMPACT_RESPONSE_ERROR_OUT_OF_MEMORY,
	SG_RUNE_COMPACT_RESPONSE_ERROR_CODE_COUNT
} sg_rune_compact_response_error_code_t;

typedef enum sg_rune_compact_response_record_domain_e
{
	SG_RUNE_COMPACT_RESPONSE_RECORD_RESULT = 0,
	SG_RUNE_COMPACT_RESPONSE_RECORD_CELL,
	SG_RUNE_COMPACT_RESPONSE_RECORD_SURFACE,
	SG_RUNE_COMPACT_RESPONSE_RECORD_OCCLUDER,
	SG_RUNE_COMPACT_RESPONSE_RECORD_SPLIT
} sg_rune_compact_response_record_domain_t;

typedef struct sg_rune_compact_response_error_s
{
	sg_rune_compact_response_error_code_t code;
	sg_rune_compact_response_record_domain_t domain;
	uint32_t record;
} sg_rune_compact_response_error_t;

int SG_RuneCompactResponsePartitionBuild(
	const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	const sg_rune_compact_response_allocator_t *allocator,
	sg_rune_compact_response_partition_t **partition_out,
	sg_rune_compact_response_error_t *error_out);

int SG_RuneCompactResponsePartitionRead(
	const sg_rune_compact_response_partition_t *partition,
	sg_rune_compact_response_partition_view_t *view_out);

int SG_RuneCompactResponsePartitionRetain(
	sg_rune_compact_response_partition_t *partition);

int SG_RuneCompactResponsePartitionSealValid(
	const sg_rune_compact_response_partition_view_t *view);

int SG_RuneCompactResponsePartitionQuery(
	const sg_rune_compact_response_partition_view_t *view,
	uint32_t source_fragment, uint32_t target_patch,
	sg_rune_compact_response_pair_t *result_out);

void SG_RuneCompactResponsePartitionDestroy(
	sg_rune_compact_response_partition_t *partition);

const char *SG_RuneCompactResponsePartitionErrorString(
	sg_rune_compact_response_error_code_t code);

#endif
