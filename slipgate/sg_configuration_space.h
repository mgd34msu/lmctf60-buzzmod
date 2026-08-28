/* Exact finite player-origin configuration space derived from a Quake II BSP. */
#ifndef SG_CONFIGURATION_SPACE_H
#define SG_CONFIGURATION_SPACE_H

#include <stdint.h>

#include "sg_host_collision.h"

#define SG_CONFIGURATION_INDEX_NONE UINT32_MAX
/* Quake II pmove_state_t stores origin in signed 12.3 fixed-point shorts. */
#define SG_CONFIGURATION_PMOVE_ORIGIN_MIN (-4096.0f)
#define SG_CONFIGURATION_PMOVE_ORIGIN_MAX (4095.875f)
#define SG_CONFIGURATION_DEFAULT_MAX_CELLS SG_RUNE_MODEL_MAX_CELLS
#define SG_CONFIGURATION_DEFAULT_MAX_FACES SG_RUNE_MODEL_MAX_PLANES
#define SG_CONFIGURATION_DEFAULT_MAX_VERTICES \
	SG_RUNE_MODEL_MAX_PORTAL_VERTICES
#define SG_CONFIGURATION_DEFAULT_MAX_PORTALS SG_RUNE_MODEL_MAX_PORTALS
#define SG_CONFIGURATION_DEFAULT_MAX_STANCE_OVERLAPS \
	SG_RUNE_MODEL_MAX_PHASE_TRANSITIONS
#define SG_CONFIGURATION_DEFAULT_MAX_CERTIFICATE_NODES \
	UINT32_C(16777216)

typedef enum sg_configuration_error_code_e
{
	SG_CONFIGURATION_ERROR_NONE = 0,
	SG_CONFIGURATION_ERROR_INVALID_ARGUMENT,
	SG_CONFIGURATION_ERROR_INVALID_WORLD,
	SG_CONFIGURATION_ERROR_INVALID_HULL,
	SG_CONFIGURATION_ERROR_NONFINITE_GEOMETRY,
	SG_CONFIGURATION_ERROR_DEGENERATE_GEOMETRY,
	SG_CONFIGURATION_ERROR_OVERFLOW,
	SG_CONFIGURATION_ERROR_OUT_OF_MEMORY,
	SG_CONFIGURATION_ERROR_HOST_DISAGREEMENT
} sg_configuration_error_code_t;

typedef struct sg_configuration_error_s
{
	sg_configuration_error_code_t code;
	uint32_t source_index;
} sg_configuration_error_t;

/* These are representation limits, not search budgets. Construction either
 * reaches the exact fixed point or reports which representation overflowed. */
typedef struct sg_configuration_limits_s
{
	uint32_t max_cells;
	uint32_t max_faces;
	uint32_t max_vertices;
	uint32_t max_portals;
	uint32_t max_stance_overlaps;
	uint32_t max_certificate_nodes;
} sg_configuration_limits_t;

typedef enum sg_configuration_plane_source_e
{
	SG_CONFIGURATION_PLANE_DOMAIN = 0,
	SG_CONFIGURATION_PLANE_BSP,
	SG_CONFIGURATION_PLANE_EXPANDED_BRUSH
} sg_configuration_plane_source_t;

typedef struct sg_configuration_plane_s
{
	float normal[3];
	float distance;
	/* Both sides of one construction split carry the same key. */
	uint32_t source_kind;
	uint32_t source_index;
	uint32_t source_variant;
	uint32_t reversed;
} sg_configuration_plane_t;

typedef struct sg_configuration_face_s
{
	sg_configuration_plane_t plane;
	uint32_t first_vertex;
	uint32_t vertex_count;
} sg_configuration_face_t;

typedef uint32_t sg_configuration_pose_flags_t;
enum
{
	SG_CONFIGURATION_POSE_SUPPORTED = UINT32_C(1) << 0,
	SG_CONFIGURATION_POSE_AIRBORNE = UINT32_C(1) << 1,
	SG_CONFIGURATION_POSE_WATER = UINT32_C(1) << 2,
	SG_CONFIGURATION_POSE_VOID_ADJACENT = UINT32_C(1) << 3
};

typedef struct sg_configuration_cell_s
{
	sg_rune_cell_id_t id;
	sg_rune_order_key_t order;
	sg_rune_stance_t stance;
	uint32_t first_face;
	uint32_t face_count;
	sg_rune_bounds_t bounds;
	sg_rune_vec3_t interior_witness;
	sg_rune_bsp_leaf_ref_t bsp_leaf;
	sg_rune_bsp_area_ref_t bsp_area;
	sg_rune_bsp_cluster_ref_t bsp_cluster;
	sg_rune_contents_mask_t contents;
	/* Diagnostic classification at interior_witness only. Support and water
	 * level are not uniform over a general 3D cell and must not be serialized
	 * as cell-wide semantics. */
	sg_configuration_pose_flags_t witness_pose_flags;
	uint8_t witness_water_level;
	uint8_t reserved[3];
} sg_configuration_cell_t;

typedef struct sg_configuration_portal_s
{
	sg_rune_portal_id_t id;
	sg_rune_order_key_t order;
	uint32_t from_cell;
	uint32_t to_cell;
	sg_rune_stance_t stance;
	sg_configuration_plane_t plane;
	uint32_t first_vertex;
	uint32_t vertex_count;
	float clearance;
} sg_configuration_portal_t;

/* The exact nonzero-volume intersection of one standing and one crouching
 * cell. A later phase builder can derive stance transitions from these
 * records without another geometric approximation. */
typedef struct sg_configuration_stance_overlap_s
{
	uint32_t standing_cell;
	uint32_t crouching_cell;
	uint32_t first_face;
	uint32_t face_count;
	sg_rune_bounds_t bounds;
	sg_rune_vec3_t interior_witness;
} sg_configuration_stance_overlap_t;

typedef enum sg_configuration_certificate_kind_e
{
	SG_CONFIGURATION_CERTIFICATE_SPLIT = 0,
	SG_CONFIGURATION_CERTIFICATE_VALID,
	SG_CONFIGURATION_CERTIFICATE_BLOCKED,
	SG_CONFIGURATION_CERTIFICATE_EMPTY
} sg_configuration_certificate_kind_t;

/* A split node partitions its inherited convex region into front and back.
 * Terminal nodes let an independent checker reconstruct the whole domain. */
typedef struct sg_configuration_certificate_node_s
{
	sg_configuration_certificate_kind_t kind;
	sg_configuration_plane_t plane;
	uint32_t front;
	uint32_t back;
	uint32_t cell;
	uint32_t blocking_brush;
	uint32_t bsp_leaf;
	sg_rune_stance_t stance;
} sg_configuration_certificate_node_t;

typedef struct sg_configuration_space_s
{
	sg_rune_model_identity_t identity;
	sg_rune_bounds_t domain;
	sg_configuration_cell_t *cells;
	uint32_t cell_count;
	sg_configuration_face_t *faces;
	uint32_t face_count;
	sg_rune_vec3_t *vertices;
	uint32_t vertex_count;
	sg_configuration_portal_t *portals;
	uint32_t portal_count;
	sg_configuration_stance_overlap_t *stance_overlaps;
	uint32_t stance_overlap_count;
	sg_configuration_certificate_node_t *certificate_nodes;
	uint32_t certificate_node_count;
	uint32_t certificate_roots[SG_RUNE_STANCE_COUNT];
	uint64_t lattice_solve_calls;
	uint64_t lattice_constraints;
	uint32_t lattice_maximum_binary_shift;
} sg_configuration_space_t;

void SG_ConfigurationDefaultLimits(sg_configuration_limits_t *limits_out);
int SG_ConfigurationBuild(const sg_host_collision_authority_t *authority,
	const sg_configuration_limits_t *limits,
	sg_configuration_space_t **space_out, sg_configuration_error_t *error_out);
void SG_ConfigurationDestroy(sg_configuration_space_t *space);
const char *SG_ConfigurationErrorString(sg_configuration_error_code_t code);

#endif
