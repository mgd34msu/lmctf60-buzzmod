#ifndef SG_CONFIGURATION_SEMANTICS_H
#define SG_CONFIGURATION_SEMANTICS_H

#include <stdint.h>

#include "sg_configuration_space.h"

#define SG_CONFIGURATION_SEMANTICS_INDEX_NONE UINT32_MAX

typedef enum sg_configuration_semantics_error_code_e
{
	SG_CONFIGURATION_SEMANTICS_ERROR_NONE = 0,
	SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_ARGUMENT,
	SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE,
	SG_CONFIGURATION_SEMANTICS_ERROR_NONFINITE_GEOMETRY,
	SG_CONFIGURATION_SEMANTICS_ERROR_OVERFLOW,
	SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY,
	SG_CONFIGURATION_SEMANTICS_ERROR_SOLVER,
	SG_CONFIGURATION_SEMANTICS_ERROR_HOST_DISAGREEMENT
} sg_configuration_semantics_error_code_t;

typedef struct sg_configuration_semantics_error_s
{
	sg_configuration_semantics_error_code_t code;
	uint32_t source_index;
} sg_configuration_semantics_error_t;

typedef struct sg_configuration_semantics_limits_s
{
	uint32_t max_regions;
	uint32_t max_faces;
	uint32_t max_vertices;
	uint32_t max_boundaries;
	uint32_t max_hook_surfaces;
	uint32_t max_hook_vertices;
} sg_configuration_semantics_limits_t;

typedef enum sg_configuration_semantic_plane_source_e
{
	SG_CONFIGURATION_SEMANTIC_PLANE_CELL = 0,
	SG_CONFIGURATION_SEMANTIC_PLANE_CONTENTS_SAMPLE,
	SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_START,
	SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_REACH,
	SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_ENTER_ZERO,
	SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_LEAVE_ZERO,
	SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_LEAVE_END,
	SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_ORDER,
	SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_CLIP,
	/* Host trace leaf-selection boundary, shifted by the active hull. */
	SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_BSP_TRAVERSAL
} sg_configuration_semantic_plane_source_t;

typedef enum sg_configuration_semantic_face_kind_e
{
	SG_CONFIGURATION_SEMANTIC_FACE_FACET = 0,
	SG_CONFIGURATION_SEMANTIC_FACE_CONSTRAINT_ONLY
} sg_configuration_semantic_face_kind_t;

typedef struct sg_configuration_semantic_face_s
{
	float normal[3];
	float distance;
	uint32_t first_vertex;
	uint32_t vertex_count;
	uint32_t source_kind;
	uint32_t source_index;
	uint32_t source_variant;
	uint8_t sample_index;
	uint8_t reversed;
	uint8_t open;
	sg_configuration_semantic_face_kind_t kind;
} sg_configuration_semantic_face_t;

static inline int SG_ConfigurationSemanticFaceContainsPoint(
	const sg_configuration_semantic_face_t *face, const float point[3])
{
	double distance = (double)point[0] * face->normal[0] +
		(double)point[1] * face->normal[1] +
		(double)point[2] * face->normal[2];

	return face->open ? distance < (double)face->distance :
		distance <= (double)face->distance;
}

typedef uint32_t sg_configuration_semantic_region_flags_t;
enum
{
	SG_CONFIGURATION_SEMANTIC_REGION_WATER = UINT32_C(1) << 0,
	SG_CONFIGURATION_SEMANTIC_REGION_LAVA = UINT32_C(1) << 1,
	SG_CONFIGURATION_SEMANTIC_REGION_SLIME = UINT32_C(1) << 2,
	SG_CONFIGURATION_SEMANTIC_REGION_HAZARD = UINT32_C(1) << 3,
	SG_CONFIGURATION_SEMANTIC_REGION_VOID_ADJACENT = UINT32_C(1) << 4,
	SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED = UINT32_C(1) << 5,
	SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE = UINT32_C(1) << 6
};

typedef struct sg_configuration_semantic_region_s
{
	uint64_t id;
	uint32_t cell;
	uint32_t first_face;
	uint32_t face_count;
	sg_rune_bounds_t bounds;
	sg_rune_vec3_t interior_witness;
	sg_host_collision_contents_t origin_contents;
	sg_rune_contents_mask_t origin_rune_contents;
	sg_host_collision_contents_t sample_contents[3];
	uint32_t sample_leaves[3];
	uint32_t sample_areas[3];
	int32_t sample_clusters[3];
	sg_host_collision_contents_t water_type;
	sg_configuration_semantic_region_flags_t flags;
	uint8_t water_level;
	uint8_t reserved[3];
} sg_configuration_semantic_region_t;

typedef uint32_t sg_configuration_boundary_flags_t;
enum
{
	SG_CONFIGURATION_BOUNDARY_PHYSICAL = UINT32_C(1) << 0,
	SG_CONFIGURATION_BOUNDARY_SUPPORT_CANDIDATE = UINT32_C(1) << 1,
	SG_CONFIGURATION_BOUNDARY_VOID = UINT32_C(1) << 2
};

typedef struct sg_configuration_boundary_s
{
	uint64_t id;
	uint32_t cell;
	uint32_t configuration_face;
	uint32_t brush;
	uint32_t brush_side;
	uint32_t texinfo;
	int32_t surface_flags;
	float origin_normal[3];
	float origin_distance;
	float surface_normal[3];
	float surface_distance;
	sg_configuration_boundary_flags_t flags;
} sg_configuration_boundary_t;

typedef uint32_t sg_configuration_hook_surface_flags_t;
enum
{
	SG_CONFIGURATION_HOOK_SURFACE_SKY = UINT32_C(1) << 0,
	SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE = UINT32_C(1) << 1,
	SG_CONFIGURATION_HOOK_SURFACE_MOVING_MODEL = UINT32_C(1) << 2
};

typedef struct sg_configuration_hook_surface_s
{
	uint64_t id;
	uint32_t model;
	uint32_t brush;
	uint32_t brush_side;
	uint32_t texinfo;
	int32_t surface_flags;
	float normal[3];
	float distance;
	uint32_t first_vertex;
	uint32_t vertex_count;
	sg_rune_bounds_t bounds;
	sg_configuration_hook_surface_flags_t flags;
} sg_configuration_hook_surface_t;

typedef struct sg_configuration_semantics_s
{
	sg_rune_model_identity_t identity;
	sg_configuration_semantic_region_t *regions;
	uint32_t region_count;
	sg_configuration_semantic_face_t *faces;
	uint32_t face_count;
	sg_rune_vec3_t *vertices;
	uint32_t vertex_count;
	sg_configuration_boundary_t *boundaries;
	uint32_t boundary_count;
	sg_configuration_hook_surface_t *hook_surfaces;
	uint32_t hook_surface_count;
	sg_rune_vec3_t *hook_vertices;
	uint32_t hook_vertex_count;
	uint64_t lattice_solve_calls;
	uint64_t lattice_constraints;
	uint32_t lattice_maximum_binary_shift;
} sg_configuration_semantics_t;

typedef enum sg_configuration_semantics_audit_code_e
{
	SG_CONFIGURATION_SEMANTICS_AUDIT_OK = 0,
	SG_CONFIGURATION_SEMANTICS_AUDIT_INVALID_ARGUMENT,
	SG_CONFIGURATION_SEMANTICS_AUDIT_SOURCE_MISMATCH,
	SG_CONFIGURATION_SEMANTICS_AUDIT_OMITTED_REGION,
	SG_CONFIGURATION_SEMANTICS_AUDIT_INVENTED_REGION,
	SG_CONFIGURATION_SEMANTICS_AUDIT_REGION_DISAGREEMENT,
	SG_CONFIGURATION_SEMANTICS_AUDIT_OMITTED_BOUNDARY,
	SG_CONFIGURATION_SEMANTICS_AUDIT_INVENTED_BOUNDARY,
	SG_CONFIGURATION_SEMANTICS_AUDIT_BOUNDARY_DISAGREEMENT,
	SG_CONFIGURATION_SEMANTICS_AUDIT_OMITTED_HOOK_SURFACE,
	SG_CONFIGURATION_SEMANTICS_AUDIT_INVENTED_HOOK_SURFACE,
	SG_CONFIGURATION_SEMANTICS_AUDIT_HOOK_SURFACE_DISAGREEMENT,
	SG_CONFIGURATION_SEMANTICS_AUDIT_SOLVER,
	SG_CONFIGURATION_SEMANTICS_AUDIT_OUT_OF_MEMORY
} sg_configuration_semantics_audit_code_t;

typedef struct sg_configuration_semantics_audit_result_s
{
	sg_configuration_semantics_audit_code_t code;
	uint32_t record;
	uint64_t lattice_solve_calls;
	uint64_t lattice_constraints;
	uint32_t lattice_maximum_binary_shift;
} sg_configuration_semantics_audit_result_t;

void SG_ConfigurationSemanticsDefaultLimits(
	sg_configuration_semantics_limits_t *limits_out);
int SG_ConfigurationSemanticsBuild(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_limits_t *limits,
	sg_configuration_semantics_t **semantics_out,
	sg_configuration_semantics_error_t *error_out);
int SG_ConfigurationSemanticsAudit(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	sg_configuration_semantics_audit_result_t *result_out);
void SG_ConfigurationSemanticsDestroy(
	sg_configuration_semantics_t *semantics);
const char *SG_ConfigurationSemanticsErrorString(
	sg_configuration_semantics_error_code_t code);
const char *SG_ConfigurationSemanticsAuditCodeString(
	sg_configuration_semantics_audit_code_t code);

#if defined(SG_CONFIGURATION_SEMANTICS_TESTING)
int SG_ConfigurationSemanticsTestMixedConstraintMesh(void);
void SG_ConfigurationSemanticsTestAuditAllocationFailAt(uint64_t allocation);
uint64_t SG_ConfigurationSemanticsTestAuditAllocationCount(void);
#endif

#endif
