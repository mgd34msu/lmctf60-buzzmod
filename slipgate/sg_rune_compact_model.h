/* Compact shared static RUNE model. This is the successor to sg_rune_model_t. */
#ifndef SG_RUNE_COMPACT_MODEL_H
#define SG_RUNE_COMPACT_MODEL_H

#include <stdint.h>

#include "sg_rune_compact_analytic.h"

#define SG_RUNE_COMPACT_MODEL_VERSION UINT16_C(2)
#define SG_RUNE_COMPACT_MODEL_SCHEMA_TAG UINT32_C(0x4d434e52)
#define SG_RUNE_COMPACT_INDEX_NONE UINT32_MAX

#define SG_RUNE_COMPACT_MAX_CELLS UINT32_C(1048576)
#define SG_RUNE_COMPACT_MAX_FACETS UINT32_C(4194304)
#define SG_RUNE_COMPACT_MAX_INCIDENCES UINT32_C(8388608)
#define SG_RUNE_COMPACT_MAX_VERTICES UINT32_C(16777216)
#define SG_RUNE_COMPACT_MAX_PORTALS UINT32_C(2097152)
#define SG_RUNE_COMPACT_MAX_MOVEMENT_FIELDS UINT32_C(4194304)
#define SG_RUNE_COMPACT_MAX_WEAPON_REGIONS UINT32_C(4194304)
#define SG_RUNE_COMPACT_MAX_WEAPON_KERNELS UINT32_C(8388608)
#define SG_RUNE_COMPACT_MAX_WEAPON_PROFILES UINT32_C(256)
#define SG_RUNE_COMPACT_MAX_ANALYTIC_FUNCTION_REFS UINT32_C(33554432)

typedef struct sg_rune_q8_vec3_s
{
	int32_t value[3];
} sg_rune_q8_vec3_t;

typedef struct sg_rune_q8_bounds_s
{
	sg_rune_q8_vec3_t mins;
	sg_rune_q8_vec3_t maxs;
} sg_rune_q8_bounds_t;

/* Binary32 bit patterns retain the exact BSP coefficients. Validation rejects
 * non-finite values and negative zero, leaving one byte representation. */
typedef struct sg_rune_binary32_plane_s
{
	uint32_t normal_bits[3];
	uint32_t distance_bits;
} sg_rune_binary32_plane_t;

typedef struct sg_rune_compact_hull_s
{
	sg_rune_q8_vec3_t mins;
	sg_rune_q8_vec3_t maxs;
} sg_rune_compact_hull_t;

typedef struct sg_rune_compact_physics_s
{
	uint32_t gravity_bits;
	uint32_t ground_acceleration_bits;
	uint32_t air_acceleration_bits;
	uint32_t water_acceleration_bits;
	uint32_t hook_acceleration_bits;
	uint32_t external_acceleration_bits;
	uint32_t water_drag_bits;
	uint32_t max_velocity_bits;
	uint32_t frame_ms;
	uint32_t substep_ms;
} sg_rune_compact_physics_t;

typedef struct sg_rune_compact_source_counts_s
{
	uint32_t model_count;
	uint32_t leaf_count;
	uint32_t area_count;
	uint32_t plane_count;
	uint32_t brush_count;
	uint32_t brush_side_count;
	uint32_t entity_count;
} sg_rune_compact_source_counts_t;

typedef struct sg_rune_compact_identity_s
{
	uint8_t bsp_sha256[32];
	uint64_t bsp_bytes;
	uint32_t bsp_checksum;
	uint32_t entity_crc32;
	uint64_t entity_semantics_id;
	uint64_t physics_abi_id;
	uint64_t collision_law_id;
	uint64_t pmove_law_id;
	uint64_t gravity_law_id;
	uint64_t hook_law_id;
	uint64_t mechanism_law_id;
	uint64_t weapon_law_id;
	uint64_t construction_id;
	uint64_t schema_id;
	uint64_t producer_identity;
	sg_rune_compact_source_counts_t source_counts;
	sg_rune_compact_hull_t standing_hull;
	sg_rune_compact_hull_t crouching_hull;
	sg_rune_compact_physics_t physics;
} sg_rune_compact_identity_t;

#define SG_RUNE_COMPACT_INDEX_TYPE(name) \
	typedef struct name##_s { uint32_t value; } name##_t

SG_RUNE_COMPACT_INDEX_TYPE(sg_rune_compact_cell_index);
SG_RUNE_COMPACT_INDEX_TYPE(sg_rune_compact_facet_index);
SG_RUNE_COMPACT_INDEX_TYPE(sg_rune_compact_incidence_index);
SG_RUNE_COMPACT_INDEX_TYPE(sg_rune_compact_portal_index);
SG_RUNE_COMPACT_INDEX_TYPE(sg_rune_weapon_response_region_index);

#undef SG_RUNE_COMPACT_INDEX_TYPE

#define SG_RUNE_COMPACT_SPAN_TYPE(name) \
	typedef struct name##_s { uint32_t first; uint32_t count; } name##_t

SG_RUNE_COMPACT_SPAN_TYPE(sg_rune_compact_vertex_span);
SG_RUNE_COMPACT_SPAN_TYPE(sg_rune_compact_incidence_span);
SG_RUNE_COMPACT_SPAN_TYPE(sg_rune_compact_cell_incidence_span);
SG_RUNE_COMPACT_SPAN_TYPE(sg_rune_movement_field_span);
SG_RUNE_COMPACT_SPAN_TYPE(sg_rune_weapon_response_region_span);
SG_RUNE_COMPACT_SPAN_TYPE(sg_rune_weapon_response_kernel_span);

#undef SG_RUNE_COMPACT_SPAN_TYPE

typedef enum sg_rune_compact_source_kind_e
{
	SG_RUNE_COMPACT_SOURCE_DOMAIN = 0,
	SG_RUNE_COMPACT_SOURCE_BSP_PLANE,
	SG_RUNE_COMPACT_SOURCE_EXPANDED_BRUSH_SIDE,
	SG_RUNE_COMPACT_SOURCE_SPLIT,
	SG_RUNE_COMPACT_SOURCE_KIND_COUNT
} sg_rune_compact_source_kind_t;

typedef struct sg_rune_compact_domain_source_s
{
	uint32_t axis;
	uint32_t maximum_side;
} sg_rune_compact_domain_source_t;

typedef struct sg_rune_compact_bsp_plane_source_s
{
	uint32_t model;
	uint32_t leaf;
	uint32_t plane;
} sg_rune_compact_bsp_plane_source_t;

typedef struct sg_rune_compact_brush_side_source_s
{
	uint32_t model;
	uint32_t brush;
	uint32_t brush_side;
	uint32_t plane;
} sg_rune_compact_brush_side_source_t;

typedef struct sg_rune_compact_split_source_s
{
	sg_rune_compact_facet_index_t parent_facet;
	uint32_t ordinal;
} sg_rune_compact_split_source_t;

typedef struct sg_rune_compact_source_s
{
	sg_rune_compact_source_kind_t kind;
	union
	{
		sg_rune_compact_domain_source_t domain;
		sg_rune_compact_bsp_plane_source_t bsp_plane;
		sg_rune_compact_brush_side_source_t brush_side;
		sg_rune_compact_split_source_t split;
	} value;
} sg_rune_compact_source_t;

typedef struct sg_rune_compact_cell_source_s
{
	uint32_t model;
	uint32_t leaf;
	uint32_t area;
	int32_t cluster;
	uint32_t split_ordinal;
} sg_rune_compact_cell_source_t;

typedef uint8_t sg_rune_stance_validity_t;
enum
{
	SG_RUNE_STANCE_VALID_STANDING = UINT8_C(1) << 0,
	SG_RUNE_STANCE_VALID_CROUCHING = UINT8_C(1) << 1
};

#define SG_RUNE_STANCE_VALID_ALL \
	(SG_RUNE_STANCE_VALID_STANDING | SG_RUNE_STANCE_VALID_CROUCHING)

typedef uint32_t sg_rune_compact_contents_mask_t;
enum
{
	SG_RUNE_COMPACT_CONTENTS_SOLID = UINT32_C(1) << 0,
	SG_RUNE_COMPACT_CONTENTS_WINDOW = UINT32_C(1) << 1,
	SG_RUNE_COMPACT_CONTENTS_WATER = UINT32_C(1) << 2,
	SG_RUNE_COMPACT_CONTENTS_LAVA = UINT32_C(1) << 3,
	SG_RUNE_COMPACT_CONTENTS_SLIME = UINT32_C(1) << 4,
	SG_RUNE_COMPACT_CONTENTS_PLAYER_CLIP = UINT32_C(1) << 5,
	SG_RUNE_COMPACT_CONTENTS_SKY = UINT32_C(1) << 6,
	SG_RUNE_COMPACT_CONTENTS_CURRENT_0 = UINT32_C(1) << 7,
	SG_RUNE_COMPACT_CONTENTS_CURRENT_90 = UINT32_C(1) << 8,
	SG_RUNE_COMPACT_CONTENTS_CURRENT_180 = UINT32_C(1) << 9,
	SG_RUNE_COMPACT_CONTENTS_CURRENT_270 = UINT32_C(1) << 10,
	SG_RUNE_COMPACT_CONTENTS_CURRENT_UP = UINT32_C(1) << 11,
	SG_RUNE_COMPACT_CONTENTS_CURRENT_DOWN = UINT32_C(1) << 12
};

#define SG_RUNE_COMPACT_CONTENTS_KNOWN \
	(SG_RUNE_COMPACT_CONTENTS_SOLID | SG_RUNE_COMPACT_CONTENTS_WINDOW | \
	 SG_RUNE_COMPACT_CONTENTS_WATER | SG_RUNE_COMPACT_CONTENTS_LAVA | \
	 SG_RUNE_COMPACT_CONTENTS_SLIME | SG_RUNE_COMPACT_CONTENTS_PLAYER_CLIP | \
	 SG_RUNE_COMPACT_CONTENTS_SKY | SG_RUNE_COMPACT_CONTENTS_CURRENT_0 | \
	 SG_RUNE_COMPACT_CONTENTS_CURRENT_90 | \
	 SG_RUNE_COMPACT_CONTENTS_CURRENT_180 | \
	 SG_RUNE_COMPACT_CONTENTS_CURRENT_270 | \
	 SG_RUNE_COMPACT_CONTENTS_CURRENT_UP | \
	 SG_RUNE_COMPACT_CONTENTS_CURRENT_DOWN)

typedef uint32_t sg_rune_compact_cell_semantics_t;
enum
{
	SG_RUNE_COMPACT_CELL_HAZARD = UINT32_C(1) << 0,
	SG_RUNE_COMPACT_CELL_SKY_BOUNDARY = UINT32_C(1) << 1,
	SG_RUNE_COMPACT_CELL_VOID_BOUNDARY = UINT32_C(1) << 2,
	SG_RUNE_COMPACT_CELL_MOVER_VOLUME = UINT32_C(1) << 3
};

#define SG_RUNE_COMPACT_CELL_SEMANTICS_KNOWN \
	(SG_RUNE_COMPACT_CELL_HAZARD | SG_RUNE_COMPACT_CELL_SKY_BOUNDARY | \
	 SG_RUNE_COMPACT_CELL_VOID_BOUNDARY | SG_RUNE_COMPACT_CELL_MOVER_VOLUME)

typedef enum sg_rune_facet_side_e
{
	SG_RUNE_FACET_NEGATIVE_SIDE = 0,
	SG_RUNE_FACET_POSITIVE_SIDE,
	SG_RUNE_FACET_SIDE_COUNT
} sg_rune_facet_side_t;

typedef enum sg_rune_boundary_ownership_e
{
	SG_RUNE_BOUNDARY_OPEN = 0,
	SG_RUNE_BOUNDARY_CLOSED,
	SG_RUNE_BOUNDARY_OWNERSHIP_COUNT
} sg_rune_boundary_ownership_t;

typedef struct sg_rune_compact_cell_s
{
	sg_rune_compact_cell_source_t source;
	sg_rune_q8_bounds_t bounds;
	sg_rune_compact_cell_incidence_span_t incidences;
	sg_rune_movement_field_span_t movement_fields;
	sg_rune_weapon_response_region_span_t weapon_regions;
	sg_rune_compact_contents_mask_t contents;
	sg_rune_compact_cell_semantics_t semantics;
	sg_rune_stance_validity_t valid_stances;
	uint8_t reserved[3];
} sg_rune_compact_cell_t;

typedef enum sg_rune_compact_facet_kind_e
{
	SG_RUNE_COMPACT_FACET_POLYGON = 0,
	SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY,
	SG_RUNE_COMPACT_FACET_KIND_COUNT
} sg_rune_compact_facet_kind_t;

typedef struct sg_rune_compact_facet_s
{
	sg_rune_compact_source_t source;
	sg_rune_binary32_plane_t plane;
	sg_rune_compact_vertex_span_t vertices;
	sg_rune_compact_incidence_span_t incidences;
	sg_rune_compact_portal_index_t portal;
	sg_rune_compact_facet_kind_t kind;
} sg_rune_compact_facet_t;

typedef struct sg_rune_compact_incidence_s
{
	sg_rune_compact_cell_index_t cell;
	sg_rune_compact_facet_index_t facet;
	uint32_t cell_ordinal;
	sg_rune_facet_side_t side;
	sg_rune_boundary_ownership_t boundary;
} sg_rune_compact_incidence_t;

typedef enum sg_rune_portal_continuity_e
{
	SG_RUNE_PORTAL_CONTINUITY_BOTH = 0,
	SG_RUNE_PORTAL_CONTINUITY_NEGATIVE_TO_POSITIVE,
	SG_RUNE_PORTAL_CONTINUITY_POSITIVE_TO_NEGATIVE,
	SG_RUNE_PORTAL_CONTINUITY_COUNT
} sg_rune_portal_continuity_t;

typedef struct sg_rune_compact_portal_s
{
	sg_rune_compact_source_t source;
	sg_rune_compact_facet_index_t facet;
	sg_rune_compact_incidence_index_t negative_incidence;
	sg_rune_compact_incidence_index_t positive_incidence;
	uint32_t clearance_q8;
	sg_rune_portal_continuity_t direction;
	sg_rune_stance_validity_t valid_stances;
	uint8_t reserved[3];
} sg_rune_compact_portal_t;

typedef enum sg_rune_movement_field_family_e
{
	SG_RUNE_MOVEMENT_FIELD_GROUND = 0,
	SG_RUNE_MOVEMENT_FIELD_WATER,
	SG_RUNE_MOVEMENT_FIELD_AIR,
	SG_RUNE_MOVEMENT_FIELD_HOOK,
	SG_RUNE_MOVEMENT_FIELD_MOVER,
	SG_RUNE_MOVEMENT_FIELD_EXTERNAL_FORCE,
	SG_RUNE_MOVEMENT_FIELD_FAMILY_COUNT
} sg_rune_movement_field_family_t;

typedef struct sg_rune_movement_field_attachment_s
{
	sg_rune_compact_cell_index_t cell;
	/* NONE attaches to the cell interior. Otherwise this narrows the field to
	 * one portal incident on cell without assigning a movement command. */
	sg_rune_compact_portal_index_t boundary_portal;
	sg_rune_movement_field_family_t family;
	sg_rune_stance_validity_t valid_stances;
	uint8_t reserved[3];
	/* Ordered typed scalar outputs for this local capability. */
	sg_rune_analytic_function_span_t functions;
} sg_rune_movement_field_attachment_t;

typedef enum sg_rune_weapon_response_family_e
{
	SG_RUNE_WEAPON_RESPONSE_HITSCAN = 0,
	SG_RUNE_WEAPON_RESPONSE_RAIL,
	SG_RUNE_WEAPON_RESPONSE_AUTOMATIC_SPREAD,
	SG_RUNE_WEAPON_RESPONSE_SHOTGUN_CONE,
	SG_RUNE_WEAPON_RESPONSE_STRAIGHT_BOLT,
	SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT,
	SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH,
	SG_RUNE_WEAPON_RESPONSE_GRENADE_FLIGHT,
	SG_RUNE_WEAPON_RESPONSE_GRENADE_BOUNCE_FUSE,
	SG_RUNE_WEAPON_RESPONSE_HYPERBLASTER,
	SG_RUNE_WEAPON_RESPONSE_BFG,
	SG_RUNE_WEAPON_RESPONSE_SPECIAL,
	SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT
} sg_rune_weapon_response_family_t;

_Static_assert(SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT > 0 &&
	SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT <= 32,
	"weapon response family mask requires one to 32 families");

typedef uint32_t sg_rune_weapon_response_family_mask_t;

#define SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(family) \
	(UINT32_C(1) << (uint32_t)(family))
#define SG_RUNE_WEAPON_RESPONSE_FAMILIES_ALL \
	(UINT32_MAX >> (32U - (uint32_t)SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT))

typedef struct sg_rune_weapon_response_region_s
{
	sg_rune_compact_cell_index_t cell;
	/* A subspan of the owning cell's incidence-reference list. */
	sg_rune_compact_cell_incidence_span_t boundary_incidences;
	sg_rune_weapon_response_kernel_span_t kernels;
} sg_rune_weapon_response_region_t;

typedef struct sg_rune_weapon_profile_s
{
	uint32_t source_profile;
	sg_rune_weapon_response_family_mask_t response_families;
} sg_rune_weapon_profile_t;

typedef struct sg_rune_weapon_response_kernel_s
{
	sg_rune_weapon_response_region_index_t region;
	uint32_t profile;
	sg_rune_weapon_response_family_t family;
	sg_rune_analytic_function_span_t functions;
} sg_rune_weapon_response_kernel_t;

typedef struct sg_rune_compact_static_s sg_rune_compact_static_t;

typedef struct sg_rune_compact_model_s
{
	uint16_t version;
	uint16_t reserved;
	uint32_t schema_tag;
	sg_rune_compact_identity_t identity;
	const sg_rune_compact_cell_t *cells;
	uint32_t cell_count;
	const sg_rune_compact_facet_t *facets;
	uint32_t facet_count;
	const sg_rune_compact_incidence_t *incidences;
	uint32_t incidence_count;
	const sg_rune_compact_incidence_index_t *cell_incidences;
	uint32_t cell_incidence_count;
	const sg_rune_q8_vec3_t *vertices;
	uint32_t vertex_count;
	const sg_rune_compact_portal_t *portals;
	uint32_t portal_count;
	const sg_rune_movement_field_attachment_t *movement_fields;
	uint32_t movement_field_count;
	const sg_rune_weapon_response_region_t *weapon_regions;
	uint32_t weapon_region_count;
	const sg_rune_weapon_profile_t *weapon_profiles;
	uint32_t weapon_profile_count;
	const sg_rune_weapon_response_kernel_t *weapon_kernels;
	uint32_t weapon_kernel_count;
	const sg_rune_analytic_function_index_t *analytic_function_refs;
	uint32_t analytic_function_ref_count;
	const sg_rune_compact_analytic_t *analytic;
	const sg_rune_compact_static_t *static_data;
} sg_rune_compact_model_t;

typedef enum sg_rune_compact_error_code_e
{
	SG_RUNE_COMPACT_ERROR_NONE = 0,
	SG_RUNE_COMPACT_ERROR_INVALID_ARGUMENT,
	SG_RUNE_COMPACT_ERROR_UNSUPPORTED_VERSION,
	SG_RUNE_COMPACT_ERROR_NONZERO_RESERVED,
	SG_RUNE_COMPACT_ERROR_LIMIT_EXCEEDED,
	SG_RUNE_COMPACT_ERROR_IDENTITY_MISMATCH,
	SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER,
	SG_RUNE_COMPACT_ERROR_INVALID_PROVENANCE,
	SG_RUNE_COMPACT_ERROR_INVALID_GEOMETRY,
	SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
	SG_RUNE_COMPACT_ERROR_INVALID_TOPOLOGY,
	SG_RUNE_COMPACT_ERROR_INVALID_STANCE,
	SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD,
	SG_RUNE_COMPACT_ERROR_INVALID_STATIC_DATA,
	SG_RUNE_COMPACT_ERROR_CODE_COUNT
} sg_rune_compact_error_code_t;

typedef enum sg_rune_compact_record_domain_e
{
	SG_RUNE_COMPACT_RECORD_MODEL = 0,
	SG_RUNE_COMPACT_RECORD_CELL,
	SG_RUNE_COMPACT_RECORD_FACET,
	SG_RUNE_COMPACT_RECORD_INCIDENCE,
	SG_RUNE_COMPACT_RECORD_PORTAL,
	SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD,
	SG_RUNE_COMPACT_RECORD_WEAPON_REGION,
	SG_RUNE_COMPACT_RECORD_WEAPON_PROFILE,
	SG_RUNE_COMPACT_RECORD_WEAPON_KERNEL
} sg_rune_compact_record_domain_t;

typedef struct sg_rune_compact_error_s
{
	sg_rune_compact_error_code_t code;
	sg_rune_compact_record_domain_t domain;
	uint32_t record;
} sg_rune_compact_error_t;

/* Compares every identity field without depending on structure padding. */
int SG_RuneCompactIdentityMatches(
	const sg_rune_compact_identity_t *actual,
	const sg_rune_compact_identity_t *expected);

/* Performs linear structural validation with O(function_count) temporary
 * storage. Facet loops start at their lexicographically least Q8 vertex and
 * wind toward the declared plane normal with strict convex turns. Plane
 * membership admits the Q8 coordinate rounding bound,
 * 0.5 * sum(abs(normal)), plus deterministic binary64 arithmetic roundoff. */
int SG_RuneCompactModelValidate(const sg_rune_compact_model_t *model,
	sg_rune_compact_error_t *error_out);

/* Performs structural validation and binds the model to an expected identity. */
int SG_RuneCompactModelValidateBound(const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	sg_rune_compact_error_t *error_out);
const char *SG_RuneCompactModelErrorString(sg_rune_compact_error_code_t code);

#endif
