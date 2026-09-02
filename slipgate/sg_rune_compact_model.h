/* Compact shared static RUNE model. This is the successor to sg_rune_model_t. */
#ifndef SG_RUNE_COMPACT_MODEL_H
#define SG_RUNE_COMPACT_MODEL_H

#include <stdint.h>

#include "sg_bsp_entity_semantics.h"
#include "sg_host_engine_pmove.h"
#include "sg_host_hook_law.h"
#include "sg_rune_compact_analytic.h"

#define SG_RUNE_COMPACT_MODEL_VERSION UINT16_C(12)
#define SG_RUNE_COMPACT_MODEL_SCHEMA_TAG UINT32_C(0x4d434e54)
#define SG_RUNE_COMPACT_INDEX_NONE UINT32_MAX

#define SG_RUNE_COMPACT_MAX_CELLS UINT32_C(1048576)
#define SG_RUNE_COMPACT_MAX_FACETS UINT32_C(4194304)
#define SG_RUNE_COMPACT_MAX_INCIDENCES UINT32_C(8388608)
#define SG_RUNE_COMPACT_MAX_VERTICES UINT32_C(16777216)
#define SG_RUNE_COMPACT_MAX_PORTALS UINT32_C(2097152)
#define SG_RUNE_COMPACT_MAX_SOURCE_SURFACES UINT32_C(4194304)
#define SG_RUNE_COMPACT_MAX_SOURCE_SURFACE_VERTICES UINT32_C(16777216)
#define SG_RUNE_COMPACT_MAX_MOVEMENT_FIELDS UINT32_C(4194304)
#define SG_RUNE_COMPACT_MAX_MOVEMENT_STATES UINT32_C(4194304)
#define SG_RUNE_COMPACT_MAX_WEAPON_KERNELS UINT32_C(8388608)
#define SG_RUNE_COMPACT_MAX_WEAPON_ATTACHMENTS UINT32_C(16777216)
#define SG_RUNE_COMPACT_MAX_WEAPON_RELATION_SPANS UINT32_C(16777216)
#define SG_RUNE_COMPACT_MAX_WEAPON_RELATION_REFS UINT32_C(33554432)
#define SG_RUNE_COMPACT_MAX_WEAPON_PROFILES UINT32_C(256)
#define SG_RUNE_COMPACT_MAX_WEAPON_FUNCTION_REFS UINT32_C(33554432)
#define SG_RUNE_COMPACT_MAX_STATIC_OCCLUDERS UINT32_C(4194304)
#define SG_RUNE_COMPACT_MAX_RESPONSE_FRAGMENTS \
	SG_RUNE_COMPACT_MAX_INCIDENCES
#define SG_RUNE_COMPACT_MAX_RESPONSE_HALFSPACES \
	SG_RUNE_COMPACT_MAX_VERTICES
#define SG_RUNE_COMPACT_MAX_RESPONSE_PATCHES \
	SG_RUNE_COMPACT_MAX_FACETS
#define SG_RUNE_COMPACT_MAX_RESPONSE_PATCH_VERTICES \
	SG_RUNE_COMPACT_MAX_VERTICES
#define SG_RUNE_COMPACT_MAX_RESPONSE_SPLITS \
	SG_RUNE_COMPACT_MAX_FACETS
#define SG_RUNE_COMPACT_MAX_RESPONSE_FACTS \
	SG_RUNE_COMPACT_MAX_CELLS
#define SG_RUNE_COMPACT_MAX_RESPONSE_CANDIDATE_GROUPS \
	SG_RUNE_COMPACT_MAX_CELLS
#define SG_RUNE_COMPACT_MAX_RESPONSE_ENDPOINT_GROUPS \
	SG_RUNE_COMPACT_MAX_RESPONSE_FRAGMENTS
#define SG_RUNE_COMPACT_MAX_RESPONSE_ENDPOINT_MEMBERS \
	SG_RUNE_COMPACT_MAX_RESPONSE_FRAGMENTS
#define SG_RUNE_COMPACT_MAX_MOVEMENT_FIBERS UINT32_C(16777216)
#define SG_RUNE_COMPACT_MAX_MOVEMENT_HOOK_TARGETS UINT32_C(33554432)
#define SG_RUNE_COMPACT_MAX_MOVEMENT_FIBER_FUNCTION_REFS UINT32_C(33554432)
#define SG_RUNE_COMPACT_MAX_MOVEMENT_ANGULAR_SCHEDULES UINT32_C(4194304)
#define SG_RUNE_COMPACT_MAX_MECHANISM_AUTHORITIES UINT32_C(1048576)
#define SG_RUNE_COMPACT_MAX_MECHANISM_AUTHORITY_CONTROLLERS UINT32_C(4194304)
#define SG_RUNE_COMPACT_MAX_MECHANISM_AUTHORITY_TOPOLOGY_EDGES UINT32_C(4194304)
#define SG_RUNE_COMPACT_MAX_MECHANISM_AUTHORITY_TRANSITIONS UINT32_C(4194304)
#define SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT UINT32_C(14)
#define SG_RUNE_COMPACT_RESPONSE_PARTITION_VERSION UINT16_C(3)
#define SG_RUNE_COMPACT_RESPONSE_INDEX_NONE UINT32_MAX

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
	uint64_t weapon_profile_catalog_id;
} sg_rune_compact_identity_t;

#define SG_RUNE_COMPACT_INDEX_TYPE(name) \
	typedef struct name##_s { uint32_t value; } name##_t

SG_RUNE_COMPACT_INDEX_TYPE(sg_rune_compact_cell_index);
SG_RUNE_COMPACT_INDEX_TYPE(sg_rune_compact_facet_index);
SG_RUNE_COMPACT_INDEX_TYPE(sg_rune_compact_incidence_index);
SG_RUNE_COMPACT_INDEX_TYPE(sg_rune_compact_portal_index);
SG_RUNE_COMPACT_INDEX_TYPE(sg_rune_movement_capability_index);
SG_RUNE_COMPACT_INDEX_TYPE(sg_rune_movement_state_index);
SG_RUNE_COMPACT_INDEX_TYPE(sg_rune_movement_fiber_index);
SG_RUNE_COMPACT_INDEX_TYPE(sg_rune_mechanism_transition_index);
SG_RUNE_COMPACT_INDEX_TYPE(sg_rune_mechanism_controller_index);
SG_RUNE_COMPACT_INDEX_TYPE(sg_rune_static_mechanism_index);
SG_RUNE_COMPACT_INDEX_TYPE(sg_rune_authority_mechanism_index);

#undef SG_RUNE_COMPACT_INDEX_TYPE

#define SG_RUNE_COMPACT_SPAN_TYPE(name) \
	typedef struct name##_s { uint32_t first; uint32_t count; } name##_t

SG_RUNE_COMPACT_SPAN_TYPE(sg_rune_compact_vertex_span);
SG_RUNE_COMPACT_SPAN_TYPE(sg_rune_compact_incidence_span);
SG_RUNE_COMPACT_SPAN_TYPE(sg_rune_compact_cell_incidence_span);
SG_RUNE_COMPACT_SPAN_TYPE(sg_rune_movement_field_span);
SG_RUNE_COMPACT_SPAN_TYPE(sg_rune_movement_fiber_span);
SG_RUNE_COMPACT_SPAN_TYPE(sg_rune_movement_hook_target_span);
SG_RUNE_COMPACT_SPAN_TYPE(sg_rune_weapon_function_ref_span);
SG_RUNE_COMPACT_SPAN_TYPE(sg_rune_compact_static_occluder_span);

#undef SG_RUNE_COMPACT_SPAN_TYPE

typedef enum sg_rune_compact_response_ref_kind_e
{
	SG_RUNE_COMPACT_RESPONSE_REF_CANDIDATE_GROUP = 0,
	SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT,
	SG_RUNE_COMPACT_RESPONSE_REF_KIND_COUNT
} sg_rune_compact_response_ref_kind_t;

typedef struct sg_rune_compact_response_ref_s
{
	sg_rune_compact_response_ref_kind_t kind;
	uint32_t index;
} sg_rune_compact_response_ref_t;

typedef struct sg_rune_compact_response_ref_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_compact_response_ref_span_t;

/* One canonical certified-response subset.  Weapon attachments retain their
 * inline span for compact query consumers, and name this shared record so
 * identical source response sets are serialized once. */
typedef struct sg_rune_compact_weapon_relation_span_s
{
	sg_rune_compact_response_ref_span_t references;
} sg_rune_compact_weapon_relation_span_t;

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

typedef enum sg_rune_compact_source_surface_frame_e
{
	SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD = 0,
	SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL,
	SG_RUNE_COMPACT_SOURCE_SURFACE_FRAME_COUNT
} sg_rune_compact_source_surface_frame_t;

/* Immutable all-model BSP surface provenance. A root has no compact-cell
 * subdivision. A child references an earlier root in the same frame and plane
 * and identifies one world-cell split of that root. */
typedef struct sg_rune_compact_source_surface_s
{
	sg_rune_compact_brush_side_source_t source;
	sg_rune_compact_source_surface_frame_t frame;
	sg_rune_compact_cell_index_t cell;
	uint32_t parent_surface;
	uint32_t split_ordinal;
	sg_rune_binary32_plane_t plane;
	sg_rune_compact_vertex_span_t vertices;
} sg_rune_compact_source_surface_t;

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

typedef enum sg_rune_movement_capability_kind_e
{
	SG_RUNE_MOVEMENT_CAPABILITY_WALK = 0,
	SG_RUNE_MOVEMENT_CAPABILITY_CROUCH,
	SG_RUNE_MOVEMENT_CAPABILITY_RAMP,
	SG_RUNE_MOVEMENT_CAPABILITY_JUMP,
	SG_RUNE_MOVEMENT_CAPABILITY_DROP,
	SG_RUNE_MOVEMENT_CAPABILITY_SWIM,
	SG_RUNE_MOVEMENT_CAPABILITY_AIR_CONTROL,
	SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT,
	SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BODY,
	SG_RUNE_MOVEMENT_CAPABILITY_HOOK_PULL,
	SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELEASE,
	SG_RUNE_MOVEMENT_CAPABILITY_HOOK_COAST,
	SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELAUNCH,
	SG_RUNE_MOVEMENT_CAPABILITY_MOVER,
	SG_RUNE_MOVEMENT_CAPABILITY_EXTERNAL_FORCE,
	SG_RUNE_MOVEMENT_CAPABILITY_CONTROLLER_ACTION,
	SG_RUNE_MOVEMENT_CAPABILITY_ROCKET_JUMP,
	SG_RUNE_MOVEMENT_CAPABILITY_KIND_COUNT
} sg_rune_movement_capability_kind_t;

typedef uint32_t sg_rune_movement_state_variables_t;
enum
{
	SG_RUNE_MOVEMENT_STATE_POSITION = UINT32_C(1) << 0,
	SG_RUNE_MOVEMENT_STATE_VELOCITY = UINT32_C(1) << 1,
	SG_RUNE_MOVEMENT_STATE_STANCE = UINT32_C(1) << 2,
	SG_RUNE_MOVEMENT_STATE_SUPPORT = UINT32_C(1) << 3,
	SG_RUNE_MOVEMENT_STATE_WATER = UINT32_C(1) << 4,
	SG_RUNE_MOVEMENT_STATE_CURRENT = UINT32_C(1) << 5,
	SG_RUNE_MOVEMENT_STATE_HOOK = UINT32_C(1) << 6,
	SG_RUNE_MOVEMENT_STATE_MOVER = UINT32_C(1) << 7,
	SG_RUNE_MOVEMENT_STATE_TIME = UINT32_C(1) << 8,
	SG_RUNE_MOVEMENT_STATE_EXTERNAL_FORCE = UINT32_C(1) << 9
};

#define SG_RUNE_MOVEMENT_STATE_VARIABLES_KNOWN \
	(SG_RUNE_MOVEMENT_STATE_POSITION | SG_RUNE_MOVEMENT_STATE_VELOCITY | \
	 SG_RUNE_MOVEMENT_STATE_STANCE | SG_RUNE_MOVEMENT_STATE_SUPPORT | \
	 SG_RUNE_MOVEMENT_STATE_WATER | SG_RUNE_MOVEMENT_STATE_CURRENT | \
	 SG_RUNE_MOVEMENT_STATE_HOOK | SG_RUNE_MOVEMENT_STATE_MOVER | \
	 SG_RUNE_MOVEMENT_STATE_TIME | SG_RUNE_MOVEMENT_STATE_EXTERNAL_FORCE)

typedef enum sg_rune_movement_fiber_kind_e
{
	SG_RUNE_MOVEMENT_FIBER_PMOVE = 0,
	SG_RUNE_MOVEMENT_FIBER_HOOK,
	SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION,
	SG_RUNE_MOVEMENT_FIBER_ANGULAR_MOVER,
	SG_RUNE_MOVEMENT_FIBER_KIND_COUNT
} sg_rune_movement_fiber_kind_t;

typedef enum sg_rune_movement_support_kind_e
{
	SG_RUNE_MOVEMENT_SUPPORT_NONE = 0,
	SG_RUNE_MOVEMENT_SUPPORT_STATIC,
	SG_RUNE_MOVEMENT_SUPPORT_MOVER,
	SG_RUNE_MOVEMENT_SUPPORT_KIND_COUNT
} sg_rune_movement_support_kind_t;

typedef enum sg_rune_movement_water_kind_e
{
	SG_RUNE_MOVEMENT_WATER_DRY = 0,
	SG_RUNE_MOVEMENT_WATER_PARTIAL,
	SG_RUNE_MOVEMENT_WATER_SUBMERGED,
	SG_RUNE_MOVEMENT_WATER_KIND_COUNT
} sg_rune_movement_water_kind_t;

typedef uint32_t sg_rune_movement_state_flags_t;
enum
{
	SG_RUNE_MOVEMENT_STATE_AIRBORNE = UINT32_C(1) << 0,
	SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE = UINT32_C(1) << 1,
	SG_RUNE_MOVEMENT_STATE_FLAG_EXTERNAL_FORCE = UINT32_C(1) << 2
};

#define SG_RUNE_MOVEMENT_STATE_FLAGS_KNOWN \
	(SG_RUNE_MOVEMENT_STATE_AIRBORNE | \
	 SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE | \
	 SG_RUNE_MOVEMENT_STATE_FLAG_EXTERNAL_FORCE)

typedef struct sg_rune_compact_movement_state_s
{
	sg_rune_stance_validity_t stance;
	uint8_t reserved[3];
	sg_rune_movement_support_kind_t support;
	sg_rune_movement_water_kind_t water;
	sg_host_hook_phase_t hook_phase;
	sg_rune_movement_state_flags_t flags;
	/* Authority-domain index used by live mechanism snapshots. */
	uint32_t mover_mechanism;
} sg_rune_compact_movement_state_t;

typedef enum sg_rune_movement_hook_target_class_e
{
	SG_RUNE_MOVEMENT_HOOK_TARGET_BLOCKED = 0,
	SG_RUNE_MOVEMENT_HOOK_TARGET_VISIBLE,
	SG_RUNE_MOVEMENT_HOOK_TARGET_CONDITIONAL,
	SG_RUNE_MOVEMENT_HOOK_TARGET_CLASS_COUNT
} sg_rune_movement_hook_target_class_t;

typedef enum sg_rune_movement_hook_target_provenance_e
{
	SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_GENERIC = 0,
	SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_STATIC_RESPONSE,
	SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_COUNT
} sg_rune_movement_hook_target_provenance_t;

typedef uint8_t sg_rune_compact_mechanism_controller_spatiality_t;
enum
{
	SG_RUNE_COMPACT_MECHANISM_CONTROLLER_NONSPATIAL = 0,
	SG_RUNE_COMPACT_MECHANISM_CONTROLLER_PLAYER_SPATIAL,
	SG_RUNE_COMPACT_MECHANISM_CONTROLLER_SPATIALITY_COUNT
};

typedef struct sg_rune_compact_movement_fiber_s
{
	sg_rune_movement_capability_index_t capability;
	sg_rune_movement_fiber_kind_t kind;
	sg_rune_movement_state_variables_t state_variables;
	sg_rune_movement_state_index_t source_state;
	sg_rune_movement_state_index_t destination_state;
	sg_rune_analytic_function_span_t functions;
	sg_rune_movement_hook_target_span_t hook_targets;
	sg_rune_mechanism_transition_index_t mechanism_transition;
	uint32_t angular_schedule;
	/* CONTROLLER_ACTION retains the activating controller independently from
	 * the controlled transition.  Both fields are NONE for every other
	 * capability.  controller_action_target is authority-domain and must equal
	 * both the controller's target and the transition's owner. */
	sg_rune_mechanism_controller_index_t controller_action_controller;
	sg_rune_authority_mechanism_index_t controller_action_target;
} sg_rune_compact_movement_fiber_t;

typedef struct sg_rune_compact_movement_hook_functions_s
{
	sg_rune_analytic_function_span_t bolt;
	sg_rune_analytic_function_span_t body;
	sg_rune_analytic_function_span_t pull;
	sg_rune_analytic_function_span_t release;
	sg_rune_analytic_function_span_t coast;
	sg_rune_analytic_function_span_t relaunch;
} sg_rune_compact_movement_hook_functions_t;

typedef struct sg_rune_compact_movement_hook_target_s
{
	sg_rune_movement_fiber_index_t fiber;
	sg_host_hook_target_kind_t target_kind;
	sg_rune_movement_hook_target_provenance_t provenance;
	sg_rune_compact_response_ref_t response;
	sg_rune_movement_hook_target_class_t visibility_class;
	sg_rune_stance_validity_t source_stances;
	sg_rune_stance_validity_t target_stances;
	uint8_t reserved[2];
	sg_rune_compact_movement_hook_functions_t functions;
} sg_rune_compact_movement_hook_target_t;

_Static_assert(sizeof(sg_rune_compact_movement_hook_target_t) == 76U,
	"compact movement hook target layout changed");

enum
{
	SG_RUNE_COMPACT_MOVEMENT_HOOK_ACCEPTED = UINT32_C(1) << 0,
	SG_RUNE_COMPACT_MOVEMENT_HOOK_FIRST_HIT = UINT32_C(1) << 1,
	SG_RUNE_COMPACT_MOVEMENT_HOOK_ATTACHED = UINT32_C(1) << 2,
	SG_RUNE_COMPACT_MOVEMENT_HOOK_RELEASED = UINT32_C(1) << 3,
	SG_RUNE_COMPACT_MOVEMENT_HOOK_ABORTED = UINT32_C(1) << 4,
	SG_RUNE_COMPACT_MOVEMENT_HOOK_COAST_VELOCITY = UINT32_C(1) << 5,
	SG_RUNE_COMPACT_MOVEMENT_HOOK_PULL_AFTER_PMOVE = UINT32_C(1) << 6,
	SG_RUNE_COMPACT_MOVEMENT_HOOK_GRAVITY_APPLIED = UINT32_C(1) << 7,
	SG_RUNE_COMPACT_MOVEMENT_HOOK_GRAVITY_ZEROED = UINT32_C(1) << 8,
	SG_RUNE_COMPACT_MOVEMENT_HOOK_ZERO_VELOCITY_Z = UINT32_C(1) << 9,
	SG_RUNE_COMPACT_MOVEMENT_HOOK_ZERO_OLDVELOCITY_Z = UINT32_C(1) << 10,
	SG_RUNE_COMPACT_MOVEMENT_HOOK_DAMAGE = UINT32_C(1) << 11,
	SG_RUNE_COMPACT_MOVEMENT_HOOK_RUNTIME_CONDITIONAL = UINT32_C(1) << 12
};

enum
{
	SG_RUNE_COMPACT_MOVEMENT_HOOK_CAPABILITY_BLOCKED = 0,
	SG_RUNE_COMPACT_MOVEMENT_HOOK_CAPABILITY_VISIBLE = 1,
	SG_RUNE_COMPACT_MOVEMENT_HOOK_CAPABILITY_CONDITIONAL = 2
};

typedef struct sg_rune_compact_movement_angular_schedule_s
{
	sg_rune_static_mechanism_index_t static_mechanism;
	sg_rune_authority_mechanism_index_t authority_mechanism;
	uint32_t source_entity;
	uint32_t mover_model;
	sg_bsp_entity_angular_mover_flags_t flags;
	uint32_t initial_angles_bits[3];
	uint32_t axis_bits[3];
	uint32_t angular_velocity_bits[3];
	uint32_t frame_angular_delta_bits[3];
	uint32_t speed_bits;
	uint32_t frame_ms;
} sg_rune_compact_movement_angular_schedule_t;

typedef struct sg_rune_movement_capability_s
{
	sg_rune_compact_cell_index_t cell;
	sg_rune_compact_portal_index_t boundary_portal;
	sg_rune_movement_capability_kind_t kind;
	sg_rune_stance_validity_t source_stances;
	sg_rune_stance_validity_t destination_stances;
	uint8_t reserved[2];
	sg_rune_movement_fiber_span_t fibers;
} sg_rune_movement_capability_t;

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

/* Static response certificates are shared by relation class, not duplicated
 * once for every profile-family kernel that can consume the same fact. */
typedef enum sg_rune_compact_weapon_relation_class_e
{
	SG_RUNE_COMPACT_WEAPON_RELATION_DIRECT = 0,
	SG_RUNE_COMPACT_WEAPON_RELATION_RAIL,
	SG_RUNE_COMPACT_WEAPON_RELATION_IMPACT,
	SG_RUNE_COMPACT_WEAPON_RELATION_CLASS_COUNT
} sg_rune_compact_weapon_relation_class_t;

typedef uint32_t sg_rune_compact_static_relation_flags_t;
enum
{
	SG_RUNE_COMPACT_STATIC_RELATION_DIRECT = UINT32_C(1) << 0,
	SG_RUNE_COMPACT_STATIC_RELATION_PENETRATING = UINT32_C(1) << 1,
	SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT = UINT32_C(1) << 2,
	SG_RUNE_COMPACT_STATIC_RELATION_PORTAL_CROSSING = UINT32_C(1) << 3
};

#define SG_RUNE_COMPACT_STATIC_RELATION_FLAGS_KNOWN \
	(SG_RUNE_COMPACT_STATIC_RELATION_DIRECT | \
	 SG_RUNE_COMPACT_STATIC_RELATION_PENETRATING | \
	 SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT | \
	 SG_RUNE_COMPACT_STATIC_RELATION_PORTAL_CROSSING)

typedef enum sg_rune_compact_static_visibility_class_e
{
	SG_RUNE_COMPACT_STATIC_VISIBILITY_OCCLUDED = 0,
	SG_RUNE_COMPACT_STATIC_VISIBILITY_VISIBLE,
	SG_RUNE_COMPACT_STATIC_VISIBILITY_CONDITIONAL,
	SG_RUNE_COMPACT_STATIC_VISIBILITY_CLASS_COUNT
} sg_rune_compact_static_visibility_class_t;

typedef enum sg_rune_compact_static_visibility_reason_e
{
	SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_NONE = 0,
	SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_PVS,
	SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_AREA_GRAPH,
	SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_STATIC_WORLD,
	SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_AREA_PORTAL_STATE,
	SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_EXACT_RAY_REQUIRED,
	SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_MOVING_SUBMODEL,
	SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_SKY,
	SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_COUNT
} sg_rune_compact_static_visibility_reason_t;

typedef struct sg_rune_compact_static_occluder_s
{
	uint32_t model;
	uint32_t brush;
	uint32_t contents;
	uint32_t conditional;
} sg_rune_compact_static_occluder_t;

/* The response projection is shared geometry.  It belongs to the final
 * artifact once, rather than to a weapon or movement consumer. */
typedef enum sg_rune_compact_response_split_kind_e
{
	SG_RUNE_COMPACT_RESPONSE_SPLIT_TARGET_EDGE = 0,
	SG_RUNE_COMPACT_RESPONSE_SPLIT_OCCLUDER_PLANE,
	SG_RUNE_COMPACT_RESPONSE_SPLIT_OCCLUDER_EDGE,
	SG_RUNE_COMPACT_RESPONSE_SPLIT_FIRST_HIT_TIE,
	SG_RUNE_COMPACT_RESPONSE_SPLIT_KIND_COUNT
} sg_rune_compact_response_split_kind_t;

typedef struct sg_rune_compact_response_halfspace_s
{
	sg_rune_binary32_plane_t plane;
	uint32_t split;
	uint8_t open;
	uint8_t reserved[3];
} sg_rune_compact_response_halfspace_t;

typedef struct sg_rune_compact_response_fragment_s
{
	sg_rune_compact_cell_index_t parent_cell;
	sg_rune_compact_cell_incidence_span_t boundary_incidences;
	uint64_t static_partition_id;
	uint32_t configuration_region;
	uint32_t configuration_cell;
	uint32_t first_halfspace;
	uint32_t halfspace_count;
	sg_rune_q8_bounds_t bounds;
	sg_rune_q8_vec3_t witness;
	uint32_t bsp_leaf;
	uint32_t bsp_area;
	uint32_t bsp_cluster;
	sg_rune_stance_validity_t valid_stances;
	uint8_t reserved[3];
} sg_rune_compact_response_fragment_t;

typedef uint32_t sg_rune_compact_response_patch_flags_t;
enum
{
	SG_RUNE_COMPACT_RESPONSE_PATCH_HOOKABLE = UINT32_C(1) << 0,
	SG_RUNE_COMPACT_RESPONSE_PATCH_SKY = UINT32_C(1) << 1,
	SG_RUNE_COMPACT_RESPONSE_PATCH_MOVING = UINT32_C(1) << 2
};

typedef struct sg_rune_compact_response_patch_s
{
	uint64_t visibility_surface_id;
	uint32_t model;
	uint32_t brush;
	uint32_t brush_side;
	uint32_t source_surface;
	sg_rune_compact_source_surface_frame_t source_frame;
	sg_rune_compact_facet_index_t parent_facet;
	sg_rune_compact_cell_index_t target_cell;
	sg_rune_compact_incidence_span_t boundary_incidences;
	uint64_t static_partition_id;
	uint32_t configuration_region;
	uint32_t configuration_cell;
	sg_rune_binary32_plane_t plane;
	uint32_t first_vertex;
	uint32_t vertex_count;
	sg_rune_q8_bounds_t bounds;
	uint32_t bsp_leaf;
	uint32_t bsp_area;
	uint32_t bsp_cluster;
	sg_rune_compact_response_patch_flags_t flags;
	sg_rune_stance_validity_t valid_stances;
	uint8_t reserved[3];
} sg_rune_compact_response_patch_t;

typedef struct sg_rune_compact_response_split_s
{
	sg_rune_binary32_plane_t plane;
	sg_rune_compact_response_split_kind_t kind;
	uint64_t target_surface_id;
	uint32_t occluder;
	uint32_t edge;
	/* BSP brush-side provenance for OCCLUDER_PLANE only.  `edge` remains
	 * the response partition's static-occluder-side-table ordinal. */
	uint32_t brush_side;
} sg_rune_compact_response_split_t;

typedef struct sg_rune_compact_response_endpoint_group_s
{
	uint32_t bsp_cluster;
	uint32_t bsp_area;
	uint32_t flags;
	uint32_t first_member;
	uint32_t member_count;
} sg_rune_compact_response_endpoint_group_t;

enum
{
	SG_RUNE_COMPACT_RESPONSE_ENDPOINT_MOVING = UINT32_C(1) << 0
};

typedef struct sg_rune_compact_response_candidate_group_s
{
	uint32_t source_group;
	uint32_t target_group;
	sg_rune_compact_static_visibility_class_t classification;
	sg_rune_compact_static_visibility_reason_t reason;
	uint8_t requires_exact_ray;
	uint8_t requires_area_state;
	uint8_t reserved[2];
	sg_rune_compact_static_relation_flags_t relation_flags;
} sg_rune_compact_response_candidate_group_t;

typedef uint32_t sg_rune_compact_response_seal_flags_t;
enum
{
	SG_RUNE_COMPACT_RESPONSE_SEAL_EXACT_SOURCE_VOLUMES = UINT32_C(1) << 0,
	SG_RUNE_COMPACT_RESPONSE_SEAL_PHYSICAL_TARGET_PATCHES = UINT32_C(1) << 1,
	SG_RUNE_COMPACT_RESPONSE_SEAL_CANONICAL_SPLITS = UINT32_C(1) << 2,
	SG_RUNE_COMPACT_RESPONSE_SEAL_BOUND_SURFACE_AUTHORITY = UINT32_C(1) << 3,
	SG_RUNE_COMPACT_RESPONSE_SEAL_CONSTANT_RESPONSE_PAIRS = UINT32_C(1) << 4,
	SG_RUNE_COMPACT_RESPONSE_SEAL_FAIL_CLOSED_UNRESOLVED = UINT32_C(1) << 5
};

#define SG_RUNE_COMPACT_RESPONSE_SEAL_REQUIRED \
	(SG_RUNE_COMPACT_RESPONSE_SEAL_EXACT_SOURCE_VOLUMES | \
	 SG_RUNE_COMPACT_RESPONSE_SEAL_PHYSICAL_TARGET_PATCHES | \
	 SG_RUNE_COMPACT_RESPONSE_SEAL_CANONICAL_SPLITS | \
	 SG_RUNE_COMPACT_RESPONSE_SEAL_BOUND_SURFACE_AUTHORITY | \
	 SG_RUNE_COMPACT_RESPONSE_SEAL_FAIL_CLOSED_UNRESOLVED)

#define SG_RUNE_COMPACT_RESPONSE_SEAL_KNOWN \
	(SG_RUNE_COMPACT_RESPONSE_SEAL_REQUIRED | \
	 SG_RUNE_COMPACT_RESPONSE_SEAL_CONSTANT_RESPONSE_PAIRS)

typedef struct sg_rune_compact_response_seal_s
{
	uint16_t version;
	uint16_t reserved;
	sg_rune_compact_response_seal_flags_t flags;
	uint32_t split_frontier_count;
	uint32_t source_fragment_count;
	uint32_t target_patch_count;
	uint32_t split_count;
	uint32_t response_pair_count;
	uint32_t certified_direct_pair_count;
	uint32_t certified_static_impact_pair_count;
	uint32_t unresolved_response_pair_count;
	uint32_t unresolved_candidate_group_count;
	uint32_t source_endpoint_group_count;
	uint32_t target_endpoint_group_count;
	uint32_t source_endpoint_member_count;
	uint32_t target_endpoint_member_count;
	uint32_t static_occluder_count;
	uint32_t compact_facet_count;
	uint32_t compact_cell_count;
	uint32_t compact_source_surface_count;
	uint32_t compact_source_surface_vertex_count;
	/* Binds response surface foreign keys to the canonical source catalog. */
	uint64_t source_surface_catalog_seal;
} sg_rune_compact_response_seal_t;

typedef struct sg_rune_compact_response_fact_s
{
	uint32_t source_fragment;
	uint32_t target_patch;
	sg_rune_compact_static_relation_flags_t flags;
	sg_rune_compact_static_visibility_class_t visibility;
	sg_rune_compact_static_visibility_reason_t visibility_reason;
	uint8_t requires_exact_ray;
	uint8_t requires_area_state;
	uint8_t reserved[2];
	uint32_t certificate_split;
	sg_rune_q8_vec3_t target_witness;
	sg_rune_compact_static_occluder_span_t occluders;
	sg_host_collision_trace_t trace;
} sg_rune_compact_response_fact_t;

typedef struct sg_rune_compact_response_projection_s
{
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
	const sg_rune_compact_response_fact_t *facts;
	uint32_t fact_count;
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
	const sg_rune_compact_static_occluder_t *occluders;
	uint32_t occluder_count;
	sg_rune_compact_response_seal_t seal;
	uint8_t exact_live_prefire_trace_required;
	uint8_t reserved[3];
} sg_rune_compact_response_projection_t;

typedef struct sg_rune_weapon_profile_s
{
	uint32_t source_profile;
	sg_rune_weapon_response_family_mask_t response_families;
	uint16_t projectile_count_min;
	uint16_t projectile_count_max;
	uint16_t auxiliary_trace_count;
	uint8_t direct_response_count;
	uint8_t reserved;
} sg_rune_weapon_profile_t;

typedef enum sg_rune_weapon_effect_channel_e
{
	SG_RUNE_WEAPON_EFFECT_CHANNEL_PRIMARY = 0,
	SG_RUNE_WEAPON_EFFECT_CHANNEL_SPLASH,
	SG_RUNE_WEAPON_EFFECT_CHANNEL_SECONDARY_SPLASH,
	SG_RUNE_WEAPON_EFFECT_CHANNEL_AUXILIARY_TRACE,
	SG_RUNE_WEAPON_EFFECT_CHANNEL_PERIODIC_RAY,
	SG_RUNE_WEAPON_EFFECT_CHANNEL_ATTACHED_EFFECT,
	/* Immutable scalar supplied by the sealed host weapon profile. The instance
	 * is a sg_rune_weapon_static_law_slot_t, never runtime player state. */
	SG_RUNE_WEAPON_EFFECT_CHANNEL_STATIC_LAW,
	SG_RUNE_WEAPON_EFFECT_CHANNEL_COUNT
} sg_rune_weapon_effect_channel_t;

typedef enum sg_rune_weapon_static_law_slot_e
{
	SG_RUNE_WEAPON_STATIC_LAW_DIRECT_DAMAGE = 0,
	SG_RUNE_WEAPON_STATIC_LAW_DIRECT_DAMAGE_MAX,
	SG_RUNE_WEAPON_STATIC_LAW_RAY_DISTANCE,
	SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_SPEED,
	SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_SPEED_MAX,
	SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_RETIRE_DISTANCE,
	SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_HALF_EXTENT,
	SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_LIFETIME_MS,
	SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_COUNT_MIN,
	SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_COUNT_MAX,
	SG_RUNE_WEAPON_STATIC_LAW_LAUNCH_VERTICAL_SPEED,
	SG_RUNE_WEAPON_STATIC_LAW_LAUNCH_JITTER,
	SG_RUNE_WEAPON_STATIC_LAW_GRAVITY_SCALE,
	SG_RUNE_WEAPON_STATIC_LAW_FUSE_MS,
	SG_RUNE_WEAPON_STATIC_LAW_COOK_MS,
	SG_RUNE_WEAPON_STATIC_LAW_HORIZONTAL_SPREAD,
	SG_RUNE_WEAPON_STATIC_LAW_VERTICAL_SPREAD,
	SG_RUNE_WEAPON_STATIC_LAW_YAW_SPREAD_DEGREES,
	SG_RUNE_WEAPON_STATIC_LAW_AUXILIARY_TRACE_DAMAGE,
	SG_RUNE_WEAPON_STATIC_LAW_AUXILIARY_HORIZONTAL_SPREAD,
	SG_RUNE_WEAPON_STATIC_LAW_AUXILIARY_VERTICAL_SPREAD,
	SG_RUNE_WEAPON_STATIC_LAW_SPLASH_RADIUS,
	SG_RUNE_WEAPON_STATIC_LAW_SPLASH_RADIUS_MAX,
	SG_RUNE_WEAPON_STATIC_LAW_SPLASH_DAMAGE,
	SG_RUNE_WEAPON_STATIC_LAW_SPLASH_DAMAGE_MAX,
	SG_RUNE_WEAPON_STATIC_LAW_SELF_DAMAGE_SCALE,
	SG_RUNE_WEAPON_STATIC_LAW_TEAMMATE_RISK_SCALE,
	SG_RUNE_WEAPON_STATIC_LAW_SPLASH_KERNEL,
	SG_RUNE_WEAPON_STATIC_LAW_SPLASH_OWNER,
	SG_RUNE_WEAPON_STATIC_LAW_SPLASH_OWNER_SCALE,
	SG_RUNE_WEAPON_STATIC_LAW_SECONDARY_SPLASH_RADIUS,
	SG_RUNE_WEAPON_STATIC_LAW_SECONDARY_SPLASH_DAMAGE,
	SG_RUNE_WEAPON_STATIC_LAW_SECONDARY_SPLASH_KERNEL,
	SG_RUNE_WEAPON_STATIC_LAW_SECONDARY_SPLASH_OWNER,
	SG_RUNE_WEAPON_STATIC_LAW_SECONDARY_SPLASH_OWNER_SCALE,
	SG_RUNE_WEAPON_STATIC_LAW_PERIODIC_RAY_DAMAGE,
	SG_RUNE_WEAPON_STATIC_LAW_PERIODIC_RAY_RADIUS,
	SG_RUNE_WEAPON_STATIC_LAW_PERIODIC_RAY_DISTANCE,
	SG_RUNE_WEAPON_STATIC_LAW_PERIODIC_RAY_INTERVAL_MS,
	SG_RUNE_WEAPON_STATIC_LAW_WINDUP_MS,
	SG_RUNE_WEAPON_STATIC_LAW_CADENCE_MS,
	SG_RUNE_WEAPON_STATIC_LAW_CADENCE_KIND,
	SG_RUNE_WEAPON_STATIC_LAW_AMMO_READY_MINIMUM,
	SG_RUNE_WEAPON_STATIC_LAW_AMMO_LIVE_FIRE_MINIMUM,
	SG_RUNE_WEAPON_STATIC_LAW_AMMO_DEBIT,
	SG_RUNE_WEAPON_STATIC_LAW_AMMO_DEBIT_MAXIMUM,
	SG_RUNE_WEAPON_STATIC_LAW_AMMO_INFINITE_DEBIT,
	SG_RUNE_WEAPON_STATIC_LAW_HOOK_INITIAL_DAMAGE,
	SG_RUNE_WEAPON_STATIC_LAW_HOOK_ATTACHED_DAMAGE,
	SG_RUNE_WEAPON_STATIC_LAW_HOOK_PULL_SPEED,
	SG_RUNE_WEAPON_STATIC_LAW_HOOK_HEALTH,
	SG_RUNE_WEAPON_STATIC_LAW_COUNT
} sg_rune_weapon_static_law_slot_t;

typedef struct sg_rune_weapon_function_ref_s
{
	sg_rune_analytic_function_index_t function;
	sg_rune_weapon_effect_channel_t channel;
	/* Pellet, lane, bounce, or periodic ordinal in canonical order. */
	uint32_t instance;
} sg_rune_weapon_function_ref_t;

typedef uint32_t sg_rune_weapon_runtime_requirement_mask_t;
enum
{
	SG_RUNE_WEAPON_RUNTIME_PREFIRE_TRACE = UINT32_C(1) << 0,
	SG_RUNE_WEAPON_RUNTIME_RANDOM_U15 = UINT32_C(1) << 1,
	SG_RUNE_WEAPON_RUNTIME_DAMAGE_SCALE = UINT32_C(1) << 2,
	SG_RUNE_WEAPON_RUNTIME_WEAPON_FRAME = UINT32_C(1) << 3,
	SG_RUNE_WEAPON_RUNTIME_ATTACK_HELD = UINT32_C(1) << 4,
	SG_RUNE_WEAPON_RUNTIME_AMMO_COUNT = UINT32_C(1) << 5,
	SG_RUNE_WEAPON_RUNTIME_FUSE_DEADLINE = UINT32_C(1) << 6,
	SG_RUNE_WEAPON_RUNTIME_COLLISION_EVENT = UINT32_C(1) << 7,
	SG_RUNE_WEAPON_RUNTIME_IMPACT_STATE = UINT32_C(1) << 8,
	SG_RUNE_WEAPON_RUNTIME_PROJECTILE_ORIGIN = UINT32_C(1) << 9,
	SG_RUNE_WEAPON_RUNTIME_ENTITY_QUERY = UINT32_C(1) << 10,
	SG_RUNE_WEAPON_RUNTIME_TARGET_RELATION = UINT32_C(1) << 11,
	SG_RUNE_WEAPON_RUNTIME_EVENT_FRAME = UINT32_C(1) << 12
};

#define SG_RUNE_WEAPON_RUNTIME_REQUIREMENTS_KNOWN \
	(SG_RUNE_WEAPON_RUNTIME_PREFIRE_TRACE | \
	 SG_RUNE_WEAPON_RUNTIME_RANDOM_U15 | \
	 SG_RUNE_WEAPON_RUNTIME_DAMAGE_SCALE | \
	 SG_RUNE_WEAPON_RUNTIME_WEAPON_FRAME | \
	 SG_RUNE_WEAPON_RUNTIME_ATTACK_HELD | \
	 SG_RUNE_WEAPON_RUNTIME_AMMO_COUNT | \
	 SG_RUNE_WEAPON_RUNTIME_FUSE_DEADLINE | \
	 SG_RUNE_WEAPON_RUNTIME_COLLISION_EVENT | \
	 SG_RUNE_WEAPON_RUNTIME_IMPACT_STATE | \
	 SG_RUNE_WEAPON_RUNTIME_PROJECTILE_ORIGIN | \
	 SG_RUNE_WEAPON_RUNTIME_ENTITY_QUERY | \
	 SG_RUNE_WEAPON_RUNTIME_TARGET_RELATION | \
	 SG_RUNE_WEAPON_RUNTIME_EVENT_FRAME)

/* A sealed host profile and the compact identity's weapon_law_id define the
 * numeric law. This tag names the required live event chronology. */
typedef enum sg_rune_weapon_event_law_kind_e
{
	SG_RUNE_WEAPON_EVENT_HITSCAN_RAY = 0,
	SG_RUNE_WEAPON_EVENT_RAIL_PENETRATION,
	SG_RUNE_WEAPON_EVENT_SPREAD_RAYS,
	SG_RUNE_WEAPON_EVENT_STRAIGHT_PROJECTILE,
	SG_RUNE_WEAPON_EVENT_PROJECTILE_IMPACT,
	SG_RUNE_WEAPON_EVENT_LINEAR_SPLASH,
	SG_RUNE_WEAPON_EVENT_GRENADE_BOUNCE_FUSE,
	SG_RUNE_WEAPON_EVENT_HYPERBLASTER,
	SG_RUNE_WEAPON_EVENT_BFG_COMPOSITE,
	SG_RUNE_WEAPON_EVENT_PLASMA_REFLECT,
	SG_RUNE_WEAPON_EVENT_PLASMA_SPREAD,
	SG_RUNE_WEAPON_EVENT_HOOK_DAMAGE,
	SG_RUNE_WEAPON_EVENT_LAW_KIND_COUNT
} sg_rune_weapon_event_law_kind_t;

typedef struct sg_rune_weapon_event_law_s
{
	sg_rune_weapon_event_law_kind_t kind;
	sg_rune_weapon_runtime_requirement_mask_t requirements;
} sg_rune_weapon_event_law_t;

typedef struct sg_rune_weapon_response_kernel_s
{
	uint32_t profile;
	sg_rune_weapon_response_family_t family;
	sg_rune_weapon_function_ref_span_t functions;
	sg_rune_weapon_event_law_t event_law;
} sg_rune_weapon_response_kernel_t;

/* The compact artifact carries the sealed stock profile catalog by stable
 * profile ID. These helpers define its complete mask and live-event contract. */
sg_rune_weapon_response_family_mask_t
SG_RuneCompactWeaponCanonicalProfileMask(uint32_t source_profile);
int SG_RuneCompactWeaponCanonicalEventLaw(uint32_t source_profile,
	sg_rune_weapon_response_family_t family,
	sg_rune_weapon_event_law_t *law_out);
int SG_RuneCompactWeaponRelationClassForProfile(uint32_t source_profile,
	sg_rune_weapon_response_family_t family,
	sg_rune_compact_weapon_relation_class_t *class_out);

/* Compact response relations are conditional exact-ray candidates. Their
 * optional area-state gate and direct/static-impact certificate must agree
 * with the retained occluder span. */
int SG_RuneCompactWeaponStaticRelationValid(
	sg_rune_compact_static_visibility_class_t visibility,
	sg_rune_compact_static_visibility_reason_t reason,
	sg_rune_compact_static_relation_flags_t flags,
	uint8_t requires_exact_ray, uint8_t requires_area_state,
	uint32_t static_occluder_count);

int SG_RuneCompactWeaponProfileShapeValid(
	const sg_rune_weapon_profile_t *profile);
/* Deterministic identity for the exact serialized compact profile records. */
int SG_RuneCompactWeaponProfileCatalogId(
	const sg_rune_weapon_profile_t *profiles, uint32_t profile_count,
	uint64_t *catalog_id_out);
int SG_RuneCompactWeaponKernelReferenceCount(
	const sg_rune_weapon_profile_t *profile,
	sg_rune_weapon_response_family_t family, uint32_t *count_out);
int SG_RuneCompactWeaponStaticLawSlotRequired(uint32_t source_profile,
	sg_rune_weapon_response_family_t family,
	sg_rune_weapon_static_law_slot_t slot);
int SG_RuneCompactWeaponFunctionRefAllowed(
	const sg_rune_weapon_profile_t *profile,
	sg_rune_weapon_response_family_t family,
	sg_rune_weapon_effect_channel_t channel, uint32_t instance,
	sg_rune_analytic_output_meaning_t output);
int SG_RuneCompactWeaponFunctionRefExpected(
	const sg_rune_weapon_profile_t *profile,
	sg_rune_weapon_response_family_t family, uint32_t ordinal,
	sg_rune_weapon_effect_channel_t *channel_out, uint32_t *instance_out,
	sg_rune_analytic_output_meaning_t *output_out);

typedef struct sg_rune_compact_static_s sg_rune_compact_static_t;
typedef struct sg_rune_compact_weapon_field_attachment_s
	sg_rune_compact_weapon_field_attachment_t;
typedef struct sg_rune_compact_mechanism_authority_s
	sg_rune_compact_mechanism_authority_t;
typedef struct sg_rune_compact_mechanism_controller_s
	sg_rune_compact_mechanism_controller_t;
typedef struct sg_rune_compact_mechanism_topology_edge_s
	sg_rune_compact_mechanism_topology_edge_t;
typedef struct sg_rune_compact_mechanism_transition_s
	sg_rune_compact_mechanism_transition_t;

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
	const sg_rune_compact_source_surface_t *source_surfaces;
	uint32_t source_surface_count;
	const sg_rune_q8_vec3_t *source_surface_vertices;
	uint32_t source_surface_vertex_count;
	const sg_rune_movement_capability_t *movement_capabilities;
	uint32_t movement_capability_count;
	const sg_rune_compact_movement_state_t *movement_states;
	uint32_t movement_state_count;
	const sg_rune_compact_movement_fiber_t *movement_fibers;
	uint32_t movement_fiber_count;
	const sg_rune_compact_movement_hook_target_t *movement_hook_targets;
	uint32_t movement_hook_target_count;
	const sg_rune_analytic_function_index_t *movement_fiber_function_refs;
	uint32_t movement_fiber_function_ref_count;
	const sg_rune_compact_movement_angular_schedule_t
		*movement_angular_schedules;
	uint32_t movement_angular_schedule_count;
	sg_host_engine_pmove_abi_t movement_pmove_abi;
	uint64_t movement_pmove_behavior_fingerprint;
	uint64_t movement_host_level_generation;
	uint64_t movement_physics_abi_id;
	uint64_t movement_collision_law_id;
	uint64_t movement_pmove_law_id;
	uint64_t movement_gravity_law_id;
	uint64_t movement_hook_law_id;
	uint64_t movement_mechanism_law_id;
	const sg_rune_compact_mechanism_authority_t *mechanism_authorities;
	uint32_t mechanism_authority_count;
	const sg_rune_compact_mechanism_controller_t
		*mechanism_authority_controllers;
	uint32_t mechanism_authority_controller_count;
	const sg_rune_compact_mechanism_topology_edge_t
		*mechanism_authority_topology_edges;
	uint32_t mechanism_authority_topology_edge_count;
	const sg_rune_compact_mechanism_transition_t
		*mechanism_authority_transitions;
	uint32_t mechanism_authority_transition_count;
	/* Canonical inverse indexes retain the construction-time transition
	 * provenance after independently sorted authority and static projections. */
	const uint32_t *mechanism_authority_transition_static_indices;
	const uint32_t *static_transition_authority_indices;
	sg_rune_compact_response_projection_t response;
	const sg_rune_weapon_profile_t *weapon_profiles;
	uint32_t weapon_profile_count;
	const sg_rune_weapon_response_kernel_t *weapon_kernels;
	uint32_t weapon_kernel_count;
	const sg_rune_compact_weapon_field_attachment_t *weapon_attachments;
	uint32_t weapon_attachment_count;
	const sg_rune_compact_weapon_relation_span_t *weapon_relation_spans;
	uint32_t weapon_relation_span_count;
	const sg_rune_compact_response_ref_t *weapon_relation_refs;
	uint32_t weapon_relation_ref_count;
	const sg_rune_weapon_function_ref_t *weapon_function_refs;
	uint32_t weapon_function_ref_count;
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
	SG_RUNE_COMPACT_ERROR_OUT_OF_MEMORY,
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
	SG_RUNE_COMPACT_RECORD_SOURCE_SURFACE,
	SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD,
	SG_RUNE_COMPACT_RECORD_MECHANISM_AUTHORITY,
	SG_RUNE_COMPACT_RECORD_MECHANISM_CONTROLLER,
	SG_RUNE_COMPACT_RECORD_MECHANISM_TOPOLOGY,
	SG_RUNE_COMPACT_RECORD_MECHANISM_TRANSITION,
	SG_RUNE_COMPACT_RECORD_RESPONSE,
	SG_RUNE_COMPACT_RECORD_WEAPON_PROFILE,
	SG_RUNE_COMPACT_RECORD_WEAPON_KERNEL,
	SG_RUNE_COMPACT_RECORD_WEAPON_RELATION_SPAN,
	SG_RUNE_COMPACT_RECORD_WEAPON_ATTACHMENT
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
