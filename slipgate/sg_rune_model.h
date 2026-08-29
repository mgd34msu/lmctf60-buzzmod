/* Static RUNE v2 model contract. */
#ifndef SG_RUNE_MODEL_H
#define SG_RUNE_MODEL_H

#include <stdint.h>

#define SG_RUNE_MODEL_VERSION UINT16_C(2)
#define SG_RUNE_MODEL_SCHEMA_TAG UINT32_C(0x32554e52)
#define SG_RUNE_MODEL_INDEX_NONE UINT32_MAX

#define SG_RUNE_MODEL_MAX_CELLS UINT32_C(1048576)
#define SG_RUNE_MODEL_MAX_PLANES UINT32_C(4194304)
#define SG_RUNE_MODEL_MAX_PORTALS UINT32_C(2097152)
#define SG_RUNE_MODEL_MAX_PORTAL_VERTICES UINT32_C(8388608)
#define SG_RUNE_MODEL_MAX_PORTAL_VERTICES_PER_PORTAL UINT32_C(64)
#define SG_RUNE_MODEL_MAX_PHASES UINT32_C(262144)
#define SG_RUNE_MODEL_MAX_PHASE_TRANSITIONS UINT32_C(4194304)
#define SG_RUNE_MODEL_MAX_SURFACES UINT32_C(2097152)
#define SG_RUNE_MODEL_MAX_AFFORDANCES UINT32_C(2097152)
#define SG_RUNE_MODEL_MAX_KERNELS UINT32_C(4194304)
#define SG_RUNE_MODEL_MAX_LANDMARKS UINT32_C(65536)
#define SG_RUNE_MODEL_MAX_MECHANISMS UINT32_C(65536)
#define SG_RUNE_MODEL_MAX_CELL_PLANES UINT32_C(64)
#define SG_RUNE_MODEL_MAX_CELL_PHASES UINT32_C(32)
#define SG_RUNE_MODEL_MAX_CELL_SURFACES UINT32_C(128)
#define SG_RUNE_MODEL_MAX_CELL_AFFORDANCES UINT32_C(128)
#define SG_RUNE_MODEL_MAX_CELL_KERNELS UINT32_C(128)
#define SG_RUNE_MODEL_MAX_CELL_LANDMARKS UINT32_C(64)
#define SG_RUNE_MODEL_MAX_CELL_MECHANISMS UINT32_C(64)
#define SG_RUNE_MODEL_MAX_PORTAL_PHASES UINT32_C(16)
#define SG_RUNE_MODEL_MAX_AFFORDANCE_SURFACES UINT32_C(64)
#define SG_RUNE_MODEL_MAX_AFFORDANCE_PHASES UINT32_C(32)
#define SG_RUNE_MODEL_MAX_MECHANISM_TOPOLOGY UINT32_C(64)

typedef struct sg_rune_vec3_s
{
	float value[3];
} sg_rune_vec3_t;

typedef struct sg_rune_bounds_s
{
	sg_rune_vec3_t mins;
	sg_rune_vec3_t maxs;
} sg_rune_bounds_t;

typedef struct sg_rune_hull_profile_s
{
	sg_rune_vec3_t mins;
	sg_rune_vec3_t maxs;
} sg_rune_hull_profile_t;

typedef struct sg_rune_interval_s
{
	float min_value;
	float max_value;
} sg_rune_interval_t;

typedef struct sg_rune_interval3_s
{
	sg_rune_interval_t x;
	sg_rune_interval_t y;
	sg_rune_interval_t z;
} sg_rune_interval3_t;

/* Stable IDs carry the source-set identity without hashing it away. */
typedef struct sg_rune_stable_id_s
{
	uint64_t source_set_identity;
	uint64_t high;
	uint64_t low;
} sg_rune_stable_id_t;

#define SG_RUNE_STABLE_ID_NONE \
	((sg_rune_stable_id_t){ UINT64_MAX, UINT64_MAX, UINT64_MAX })

typedef struct sg_rune_cell_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_cell_id_t;
typedef sg_rune_cell_id_t sg_rune_cell_ref_t;
#define SG_RUNE_CELL_REF_NONE \
	((sg_rune_cell_ref_t){ SG_RUNE_STABLE_ID_NONE })

typedef struct sg_rune_portal_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_portal_id_t;
typedef sg_rune_portal_id_t sg_rune_portal_ref_t;
#define SG_RUNE_PORTAL_REF_NONE \
	((sg_rune_portal_ref_t){ SG_RUNE_STABLE_ID_NONE })

typedef struct sg_rune_plane_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_plane_id_t;
typedef sg_rune_plane_id_t sg_rune_plane_ref_t;
#define SG_RUNE_PLANE_REF_NONE \
	((sg_rune_plane_ref_t){ SG_RUNE_STABLE_ID_NONE })

typedef struct sg_rune_phase_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_phase_id_t;
typedef sg_rune_phase_id_t sg_rune_phase_ref_t;
#define SG_RUNE_PHASE_REF_NONE \
	((sg_rune_phase_ref_t){ SG_RUNE_STABLE_ID_NONE })

typedef struct sg_rune_phase_transition_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_phase_transition_id_t;
typedef sg_rune_phase_transition_id_t sg_rune_phase_transition_ref_t;
#define SG_RUNE_PHASE_TRANSITION_REF_NONE \
	((sg_rune_phase_transition_ref_t){ SG_RUNE_STABLE_ID_NONE })

typedef struct sg_rune_surface_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_surface_id_t;
typedef sg_rune_surface_id_t sg_rune_surface_ref_t;
#define SG_RUNE_SURFACE_REF_NONE \
	((sg_rune_surface_ref_t){ SG_RUNE_STABLE_ID_NONE })

typedef struct sg_rune_affordance_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_affordance_id_t;
typedef sg_rune_affordance_id_t sg_rune_affordance_ref_t;
#define SG_RUNE_AFFORDANCE_REF_NONE \
	((sg_rune_affordance_ref_t){ SG_RUNE_STABLE_ID_NONE })

typedef struct sg_rune_kernel_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_kernel_id_t;
typedef sg_rune_kernel_id_t sg_rune_kernel_ref_t;
#define SG_RUNE_KERNEL_REF_NONE \
	((sg_rune_kernel_ref_t){ SG_RUNE_STABLE_ID_NONE })

typedef struct sg_rune_landmark_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_landmark_id_t;
typedef sg_rune_landmark_id_t sg_rune_landmark_ref_t;
#define SG_RUNE_LANDMARK_REF_NONE \
	((sg_rune_landmark_ref_t){ SG_RUNE_STABLE_ID_NONE })

typedef struct sg_rune_mechanism_id_s
{
	sg_rune_stable_id_t value;
} sg_rune_mechanism_id_t;
typedef sg_rune_mechanism_id_t sg_rune_mechanism_ref_t;
#define SG_RUNE_MECHANISM_REF_NONE \
	((sg_rune_mechanism_ref_t){ SG_RUNE_STABLE_ID_NONE })

typedef struct sg_rune_entity_ref_s
{
	uint32_t index;
	uint32_t spawn_ordinal;
} sg_rune_entity_ref_t;

#define SG_RUNE_ENTITY_REF_NONE \
	((sg_rune_entity_ref_t){ UINT32_MAX, UINT32_MAX })

typedef enum sg_rune_order_domain_e
{
	SG_RUNE_ORDER_CELL = 1,
	SG_RUNE_ORDER_PORTAL,
	SG_RUNE_ORDER_PLANE,
	SG_RUNE_ORDER_PHASE,
	SG_RUNE_ORDER_PHASE_TRANSITION,
	SG_RUNE_ORDER_SURFACE,
	SG_RUNE_ORDER_AFFORDANCE,
	SG_RUNE_ORDER_KERNEL,
	SG_RUNE_ORDER_LANDMARK,
	SG_RUNE_ORDER_MECHANISM,
	SG_RUNE_ORDER_DYNAMICS_MODEL,
	SG_RUNE_ORDER_STATE_VERTEX,
	SG_RUNE_ORDER_STATE_CHART,
	SG_RUNE_ORDER_STATE_SIMPLEX,
	SG_RUNE_ORDER_STATE_DOMAIN,
	SG_RUNE_ORDER_CONTROL_FIBER,
	SG_RUNE_ORDER_RESPONSE_PATCH,
	SG_RUNE_ORDER_BOUNDARY_TRANSFER,
	SG_RUNE_ORDER_CONTROL_DOMAIN,
	SG_RUNE_ORDER_GUARD_CONDITION,
	SG_RUNE_ORDER_DYNAMICS_PROOF,
	SG_RUNE_ORDER_FIELD_REGION,
	SG_RUNE_ORDER_FIELD_HIERARCHY,
	SG_RUNE_ORDER_FIELD_ERROR_CONTRACT,
	SG_RUNE_ORDER_FIELD_CHOICE,
	SG_RUNE_ORDER_FIELD_OUTCOME,
	SG_RUNE_ORDER_FIELD_REACH_ATOM,
	SG_RUNE_ORDER_FIELD_LOCAL_PROGRESS,
	SG_RUNE_ORDER_FIELD_REFINEMENT_VERTEX,
	SG_RUNE_ORDER_FIELD_REFINEMENT_FACE,
	SG_RUNE_ORDER_FIELD_REFINEMENT_NODE,
	SG_RUNE_ORDER_FIELD_REFINEMENT_TREE,
	SG_RUNE_ORDER_SIMPLEX_OWNERSHIP_PROOF,
	SG_RUNE_ORDER_DOMAIN_SUPPORT_PROOF,
	SG_RUNE_ORDER_DOMAIN_BOUNDARY_PROOF,
	SG_RUNE_ORDER_FIELD_OUTCOME_IMAGE,
	SG_RUNE_ORDER_FIELD_OUTCOME_IMAGE_PROOF,
	SG_RUNE_ORDER_FIELD_OUTCOME_COVER_PROOF,
	SG_RUNE_ORDER_DOMAIN_COUNT
} sg_rune_order_domain_t;

typedef struct sg_rune_order_key_s
{
	uint64_t source_set_identity;
	uint32_t domain;
	uint32_t source_index;
	uint32_t local_ordinal;
	uint32_t variant;
} sg_rune_order_key_t;

typedef enum sg_rune_order_derivation_status_e
{
	SG_RUNE_ORDER_DERIVATION_OK = 0,
	SG_RUNE_ORDER_DERIVATION_INCOMPLETE,
	SG_RUNE_ORDER_DERIVATION_INVALID,
	SG_RUNE_ORDER_DERIVATION_STATUS_COUNT
} sg_rune_order_derivation_status_t;

/* The canonical ordinal is the rank in a complete, identity-bound source set.
 * A partial source set must return SG_RUNE_ORDER_DERIVATION_INCOMPLETE. */
typedef struct sg_rune_canonical_order_input_s
{
	uint32_t domain;
	uint32_t source_index;
	uint32_t canonical_ordinal;
	uint32_t variant;
	uint64_t source_set_identity;
	uint32_t source_set_count;
	uint32_t source_set_complete;
} sg_rune_canonical_order_input_t;

/* An independently constructed source-geometry set owns these references.
 * BSP/oracle work defines and verifies the referenced records. */
typedef struct sg_rune_source_geometry_ref_s
{
	uint64_t source_set_identity;
	uint32_t source_index;
	uint32_t source_ordinal;
} sg_rune_source_geometry_ref_t;

typedef struct sg_rune_plane_s
{
	sg_rune_plane_id_t id;
	sg_rune_order_key_t order;
	sg_rune_vec3_t normal;
	float distance;
} sg_rune_plane_t;

typedef struct sg_rune_plane_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_plane_span_t;

typedef struct sg_rune_vertex_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_vertex_span_t;

typedef struct sg_rune_phase_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_phase_span_t;

typedef struct sg_rune_surface_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_surface_span_t;

typedef struct sg_rune_affordance_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_affordance_span_t;

typedef struct sg_rune_kernel_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_kernel_span_t;

typedef struct sg_rune_landmark_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_landmark_span_t;

typedef struct sg_rune_mechanism_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_mechanism_span_t;

typedef struct sg_rune_bsp_leaf_ref_s
{
	uint32_t index;
} sg_rune_bsp_leaf_ref_t;

typedef struct sg_rune_bsp_area_ref_s
{
	uint32_t index;
} sg_rune_bsp_area_ref_t;

typedef struct sg_rune_bsp_cluster_ref_s
{
	uint32_t index;
} sg_rune_bsp_cluster_ref_t;

#define SG_RUNE_BSP_LEAF_REF_NONE \
	((sg_rune_bsp_leaf_ref_t){ UINT32_MAX })
#define SG_RUNE_BSP_AREA_REF_NONE \
	((sg_rune_bsp_area_ref_t){ UINT32_MAX })
#define SG_RUNE_BSP_CLUSTER_REF_NONE \
	((sg_rune_bsp_cluster_ref_t){ UINT32_MAX })

typedef enum sg_rune_stance_e
{
	SG_RUNE_STANCE_STANDING = 0,
	SG_RUNE_STANCE_CROUCHING,
	SG_RUNE_STANCE_COUNT
} sg_rune_stance_t;

typedef enum sg_rune_motion_e
{
	SG_RUNE_MOTION_SUPPORTED = 0,
	SG_RUNE_MOTION_AIRBORNE,
	SG_RUNE_MOTION_SWIMMING,
	SG_RUNE_MOTION_COUNT
} sg_rune_motion_t;

typedef enum sg_rune_support_e
{
	SG_RUNE_SUPPORT_NONE = 0,
	SG_RUNE_SUPPORT_SUPPORTED,
	SG_RUNE_SUPPORT_MOVER,
	SG_RUNE_SUPPORT_COUNT
} sg_rune_support_t;

typedef enum sg_rune_medium_e
{
	SG_RUNE_MEDIUM_DRY = 0,
	SG_RUNE_MEDIUM_WATER,
	SG_RUNE_MEDIUM_LAVA,
	SG_RUNE_MEDIUM_SLIME,
	SG_RUNE_MEDIUM_COUNT
} sg_rune_medium_t;

typedef enum sg_rune_void_relation_e
{
	SG_RUNE_VOID_CLEAR = 0,
	SG_RUNE_VOID_ADJACENT,
	SG_RUNE_VOID_RELATION_COUNT
} sg_rune_void_relation_t;

typedef enum sg_rune_reference_frame_e
{
	SG_RUNE_FRAME_WORLD = 0,
	SG_RUNE_FRAME_MOVER_RELATIVE,
	SG_RUNE_FRAME_COUNT
} sg_rune_reference_frame_t;

typedef uint32_t sg_rune_contents_mask_t;
enum
{
	SG_RUNE_CONTENTS_EMPTY = UINT32_C(0),
	SG_RUNE_CONTENTS_SOLID = UINT32_C(1) << 0,
	SG_RUNE_CONTENTS_WINDOW = UINT32_C(1) << 1,
	SG_RUNE_CONTENTS_WATER = UINT32_C(1) << 2,
	SG_RUNE_CONTENTS_LAVA = UINT32_C(1) << 3,
	SG_RUNE_CONTENTS_SLIME = UINT32_C(1) << 4,
	SG_RUNE_CONTENTS_PLAYER_CLIP = UINT32_C(1) << 5,
	SG_RUNE_CONTENTS_SKY = UINT32_C(1) << 6,
	SG_RUNE_CONTENTS_CURRENT_0 = UINT32_C(1) << 7,
	SG_RUNE_CONTENTS_CURRENT_90 = UINT32_C(1) << 8,
	SG_RUNE_CONTENTS_CURRENT_180 = UINT32_C(1) << 9,
	SG_RUNE_CONTENTS_CURRENT_270 = UINT32_C(1) << 10,
	SG_RUNE_CONTENTS_CURRENT_UP = UINT32_C(1) << 11,
	SG_RUNE_CONTENTS_CURRENT_DOWN = UINT32_C(1) << 12
};

#define SG_RUNE_CONTENTS_WATER_MASK \
	(SG_RUNE_CONTENTS_WATER | SG_RUNE_CONTENTS_LAVA | SG_RUNE_CONTENTS_SLIME)
#define SG_RUNE_CONTENTS_CURRENT_MASK \
	(SG_RUNE_CONTENTS_CURRENT_0 | SG_RUNE_CONTENTS_CURRENT_90 | \
	 SG_RUNE_CONTENTS_CURRENT_180 | SG_RUNE_CONTENTS_CURRENT_270 | \
	 SG_RUNE_CONTENTS_CURRENT_UP | SG_RUNE_CONTENTS_CURRENT_DOWN)
#define SG_RUNE_CONTENTS_CURRENT SG_RUNE_CONTENTS_CURRENT_MASK
#define SG_RUNE_CONTENTS_KNOWN \
	(SG_RUNE_CONTENTS_SOLID | SG_RUNE_CONTENTS_WINDOW | \
	 SG_RUNE_CONTENTS_WATER | SG_RUNE_CONTENTS_LAVA | SG_RUNE_CONTENTS_SLIME | \
	 SG_RUNE_CONTENTS_PLAYER_CLIP | SG_RUNE_CONTENTS_SKY | \
	 SG_RUNE_CONTENTS_CURRENT_MASK)

#define SG_RUNE_CURRENT_DIRECTION_COUNT UINT32_C(6)

typedef uint32_t sg_rune_cell_semantics_t;
enum
{
	SG_RUNE_CELL_SEMANTIC_HAZARD = UINT32_C(1) << 0,
	SG_RUNE_CELL_SEMANTIC_SKY_BOUNDARY = UINT32_C(1) << 1,
	SG_RUNE_CELL_SEMANTIC_VOID_BOUNDARY = UINT32_C(1) << 2,
	SG_RUNE_CELL_SEMANTIC_MOVER_VOLUME = UINT32_C(1) << 3
};

typedef uint32_t sg_rune_surface_semantics_t;
enum
{
	SG_RUNE_SURFACE_SEMANTIC_HOOKABLE = UINT32_C(1) << 0,
	SG_RUNE_SURFACE_SEMANTIC_SKY = UINT32_C(1) << 1,
	SG_RUNE_SURFACE_SEMANTIC_COVER_BOUNDARY = UINT32_C(1) << 2,
	SG_RUNE_SURFACE_SEMANTIC_EXPOSURE_BOUNDARY = UINT32_C(1) << 3,
	SG_RUNE_SURFACE_SEMANTIC_BOUNCE = UINT32_C(1) << 4
};

typedef struct sg_rune_phase_basis_s
{
	sg_rune_phase_id_t id;
	sg_rune_order_key_t order;
	sg_rune_stance_t stance;
	sg_rune_motion_t motion;
	sg_rune_support_t support;
	sg_rune_medium_t medium;
	sg_rune_void_relation_t void_relation;
	sg_rune_reference_frame_t reference_frame;
	/* NONE for world-relative phases. */
	sg_rune_mechanism_ref_t mover;
	sg_rune_interval3_t velocity;
	sg_rune_interval_t elapsed_ms;
	uint32_t time_quantum_ms;
	uint32_t time_horizon_ms;
} sg_rune_phase_basis_t;

typedef enum sg_rune_phase_transition_kind_e
{
	SG_RUNE_PHASE_TRANSITION_NONE = 0,
	SG_RUNE_PHASE_TRANSITION_STANCE,
	SG_RUNE_PHASE_TRANSITION_ACCELERATION,
	SG_RUNE_PHASE_TRANSITION_TIME,
	SG_RUNE_PHASE_TRANSITION_MOVER_DWELL,
	SG_RUNE_PHASE_TRANSITION_TAKEOFF,
	SG_RUNE_PHASE_TRANSITION_RELAUNCH,
	SG_RUNE_PHASE_TRANSITION_SUPPORT,
	SG_RUNE_PHASE_TRANSITION_KIND_COUNT
} sg_rune_phase_transition_kind_t;

typedef struct sg_rune_phase_transition_s
{
	sg_rune_phase_transition_id_t id;
	sg_rune_order_key_t order;
	sg_rune_cell_ref_t cell;
	sg_rune_phase_ref_t source_phase;
	sg_rune_phase_ref_t destination_phase;
	sg_rune_phase_transition_kind_t kind;
	sg_rune_interval_t duration_ms;
	uint32_t flags;
} sg_rune_phase_transition_t;

typedef enum sg_rune_portal_direction_e
{
	SG_RUNE_PORTAL_BIDIRECTIONAL = 0,
	SG_RUNE_PORTAL_FROM_TO,
	SG_RUNE_PORTAL_TO_FROM,
	SG_RUNE_PORTAL_DIRECTION_COUNT
} sg_rune_portal_direction_t;

typedef uint32_t sg_rune_portal_flags_t;
enum
{
	SG_RUNE_PORTAL_HULL_VALID = UINT32_C(1) << 0,
	SG_RUNE_PORTAL_CONTENTS_CHANGE = UINT32_C(1) << 1,
	SG_RUNE_PORTAL_VOID_EDGE = UINT32_C(1) << 2,
	SG_RUNE_PORTAL_MOVER_BOUNDARY = UINT32_C(1) << 3
};

typedef struct sg_rune_cell_s
{
	sg_rune_cell_id_t id;
	sg_rune_order_key_t order;
	sg_rune_source_geometry_ref_t geometry;
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
} sg_rune_cell_t;

typedef struct sg_rune_portal_s
{
	sg_rune_portal_id_t id;
	sg_rune_order_key_t order;
	sg_rune_source_geometry_ref_t geometry;
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
} sg_rune_portal_t;

typedef struct sg_rune_surface_s
{
	sg_rune_surface_id_t id;
	sg_rune_order_key_t order;
	sg_rune_source_geometry_ref_t geometry;
	sg_rune_cell_ref_t owner_cell;
	sg_rune_plane_ref_t plane;
	sg_rune_vec3_t normal;
	sg_rune_contents_mask_t contents;
	sg_rune_surface_semantics_t semantics;
} sg_rune_surface_t;

typedef enum sg_rune_affordance_kind_e
{
	SG_RUNE_AFFORDANCE_VISIBILITY_PARTITION = 0,
	SG_RUNE_AFFORDANCE_HOOKABLE_REGION,
	SG_RUNE_AFFORDANCE_COVER_BOUNDARY,
	SG_RUNE_AFFORDANCE_EXPOSURE_BOUNDARY,
	SG_RUNE_AFFORDANCE_PROJECTILE_CORRIDOR,
	SG_RUNE_AFFORDANCE_BLAST_REACH,
	SG_RUNE_AFFORDANCE_BOUNCE_SURFACE,
	SG_RUNE_AFFORDANCE_KIND_COUNT
} sg_rune_affordance_kind_t;

typedef struct sg_rune_affordance_s
{
	sg_rune_affordance_id_t id;
	sg_rune_order_key_t order;
	sg_rune_cell_ref_t owner_cell;
	sg_rune_surface_span_t surfaces;
	sg_rune_phase_span_t phases;
	sg_rune_affordance_kind_t kind;
	sg_rune_interval_t range;
	uint32_t flags;
} sg_rune_affordance_t;

typedef enum sg_rune_capability_family_e
{
	SG_RUNE_CAPABILITY_CONTINUOUS_SUPPORT = 0,
	SG_RUNE_CAPABILITY_AIRBORNE_CONTROL,
	SG_RUNE_CAPABILITY_WATER_VOLUME,
	SG_RUNE_CAPABILITY_HOOK_TRAJECTORY,
	SG_RUNE_CAPABILITY_MECHANISM_CROSSING,
	SG_RUNE_CAPABILITY_EXTERNAL_FORCE,
	SG_RUNE_CAPABILITY_FAMILY_COUNT
} sg_rune_capability_family_t;

typedef enum sg_rune_cost_law_e
{
	SG_RUNE_COST_CONSTANT_RATE = 0,
	SG_RUNE_COST_ACCELERATION_LIMITED,
	SG_RUNE_COST_BALLISTIC,
	SG_RUNE_COST_BUOYANCY_LIMITED,
	SG_RUNE_COST_TETHERED,
	SG_RUNE_COST_SCHEDULED,
	SG_RUNE_COST_LAW_COUNT
} sg_rune_cost_law_t;

typedef uint32_t sg_rune_kernel_flags_t;
enum
{
	SG_RUNE_KERNEL_DIRECTIONAL = UINT32_C(1) << 0,
	SG_RUNE_KERNEL_PHASE_AWARE = UINT32_C(1) << 1,
	SG_RUNE_KERNEL_CHANGES_MEDIUM = UINT32_C(1) << 2,
	SG_RUNE_KERNEL_REQUIRES_SUPPORT = UINT32_C(1) << 3,
	SG_RUNE_KERNEL_PROVEN = UINT32_C(1) << 4
};

typedef struct sg_rune_kernel_parameters_s
{
	sg_rune_interval3_t displacement;
	sg_rune_interval_t duration_ms;
	sg_rune_interval_t speed;
	sg_rune_interval_t acceleration;
	sg_rune_interval_t vertical_acceleration;
	float gravity;
	float drag;
	uint64_t physics_abi_id;
	uint32_t fixed_latency_ms;
	uint32_t dwell_ms;
} sg_rune_kernel_parameters_t;

typedef struct sg_rune_capability_kernel_s
{
	sg_rune_kernel_id_t id;
	sg_rune_order_key_t order;
	sg_rune_cell_ref_t source_cell;
	sg_rune_cell_ref_t destination_cell;
	sg_rune_portal_ref_t boundary;
	sg_rune_affordance_ref_t affordance;
	sg_rune_mechanism_ref_t mechanism;
	sg_rune_phase_ref_t source_phase;
	sg_rune_phase_ref_t destination_phase;
	sg_rune_phase_transition_ref_t transition;
	sg_rune_capability_family_t family;
	sg_rune_cost_law_t cost_law;
	/* source_cell to destination_cell is the cost direction. A reverse
	 * traversal is a separate kernel and must obey the portal direction. */
	sg_rune_kernel_parameters_t parameters;
	sg_rune_kernel_flags_t flags;
} sg_rune_capability_kernel_t;

typedef enum sg_rune_landmark_kind_e
{
	SG_RUNE_LANDMARK_FLAG_STAND = 0,
	SG_RUNE_LANDMARK_ITEM,
	SG_RUNE_LANDMARK_WEAPON,
	SG_RUNE_LANDMARK_ARMOR,
	SG_RUNE_LANDMARK_HEALTH,
	SG_RUNE_LANDMARK_POWERUP,
	SG_RUNE_LANDMARK_TRIGGER,
	SG_RUNE_LANDMARK_MECHANISM_ENTRY,
	SG_RUNE_LANDMARK_DEFENSIVE_POSITION,
	SG_RUNE_LANDMARK_KIND_COUNT
} sg_rune_landmark_kind_t;

typedef struct sg_rune_landmark_s
{
	sg_rune_landmark_id_t id;
	sg_rune_order_key_t order;
	sg_rune_source_geometry_ref_t geometry;
	sg_rune_cell_ref_t cell;
	sg_rune_entity_ref_t entity;
	sg_rune_landmark_kind_t kind;
	sg_rune_vec3_t origin;
	sg_rune_bounds_t bounds;
	sg_rune_mechanism_ref_t mechanism;
	sg_rune_surface_ref_t surface;
	uint32_t semantics;
} sg_rune_landmark_t;

typedef enum sg_rune_mechanism_kind_e
{
	SG_RUNE_MECHANISM_DOOR = 0,
	SG_RUNE_MECHANISM_BUTTON,
	SG_RUNE_MECHANISM_LIFT,
	SG_RUNE_MECHANISM_TRAIN,
	SG_RUNE_MECHANISM_PUSH,
	SG_RUNE_MECHANISM_TELEPORT,
	SG_RUNE_MECHANISM_TRIGGER,
	SG_RUNE_MECHANISM_ROTATOR,
	SG_RUNE_MECHANISM_KIND_COUNT
} sg_rune_mechanism_kind_t;

typedef struct sg_rune_mechanism_s
{
	sg_rune_mechanism_id_t id;
	sg_rune_order_key_t order;
	sg_rune_mechanism_kind_t kind;
	sg_rune_cell_ref_t entry_cell;
	sg_rune_cell_ref_t exit_cell;
	sg_rune_landmark_ref_t activation_landmark;
	sg_rune_entity_ref_t entity;
	sg_rune_interval_t dwell_ms;
	sg_rune_interval_t travel_ms;
	sg_rune_mechanism_span_t topology;
	uint32_t flags;
} sg_rune_mechanism_t;

typedef struct sg_rune_physics_parameters_s
{
	float gravity;
	/* Kernel acceleration intervals use these authoritative family units. */
	float ground_acceleration;
	float air_acceleration;
	float water_acceleration;
	float hook_acceleration;
	float external_acceleration;
	float water_drag;
	float max_velocity;
	uint32_t frame_ms;
	uint32_t substep_ms;
} sg_rune_physics_parameters_t;

typedef struct sg_rune_model_identity_s
{
	uint64_t bsp_content_id;
	uint64_t entity_semantics_id;
	uint64_t physics_abi_id;
	uint64_t source_set_identity;
	uint64_t schema_id;
	uint64_t producer_identity;
	sg_rune_hull_profile_t standing_hull;
	sg_rune_hull_profile_t crouching_hull;
	sg_rune_physics_parameters_t physics;
} sg_rune_model_identity_t;

typedef enum sg_rune_completeness_state_e
{
	SG_RUNE_COMPLETENESS_UNSEALED = 0,
	SG_RUNE_COMPLETENESS_BUILDING,
	SG_RUNE_COMPLETENESS_COMPLETE,
	SG_RUNE_COMPLETENESS_FAILED,
	SG_RUNE_COMPLETENESS_STATE_COUNT
} sg_rune_completeness_state_t;

#define SG_RUNE_VALIDATION_EVIDENCE_VERSION UINT32_C(1)
/* Supplied by the downstream independent BSP/oracle verifier. This contract
 * consumes evidence; it does not manufacture proof from the model it checks. */
typedef struct sg_rune_validation_evidence_s
{
	uint32_t version;
	uint32_t reserved;
	uint64_t verifier_identity;
	uint64_t bsp_content_id;
	uint64_t source_set_identity;
	/* Opaque identity issued by the independent fixed-point verifier. */
	uint64_t fixed_point_identity;
	uint32_t fixed_point_rounds;
	uint32_t proved_cells;
	uint32_t proved_portals;
	uint32_t omitted_cells;
	uint32_t omitted_portals;
	uint32_t invented_portals;
	uint32_t pending_work;
} sg_rune_validation_evidence_t;

typedef enum sg_rune_failure_reason_e
{
	SG_RUNE_FAILURE_NONE = 0,
	SG_RUNE_FAILURE_INVALID_ARGUMENT,
	SG_RUNE_FAILURE_LIMIT_EXCEEDED,
	SG_RUNE_FAILURE_NONFINITE_GEOMETRY,
	SG_RUNE_FAILURE_INVALID_REFERENCE,
	SG_RUNE_FAILURE_DUPLICATE_ID,
	SG_RUNE_FAILURE_NONDETERMINISTIC_ORDER,
	SG_RUNE_FAILURE_MISSING_CONFIGURATION,
	SG_RUNE_FAILURE_MISSING_PORTAL,
	SG_RUNE_FAILURE_INVALID_PHASE,
	SG_RUNE_FAILURE_INVALID_KERNEL,
	SG_RUNE_FAILURE_INVALID_SEMANTICS,
	SG_RUNE_FAILURE_IDENTITY_MISMATCH,
	SG_RUNE_FAILURE_UNSUPPORTED_BSP,
	SG_RUNE_FAILURE_UNSUPPORTED_PHYSICS,
	SG_RUNE_FAILURE_INCOMPLETE,
	SG_RUNE_FAILURE_REASON_COUNT
} sg_rune_failure_reason_t;

typedef struct sg_rune_completeness_s
{
	sg_rune_completeness_state_t state;
	sg_rune_failure_reason_t reason;
	uint32_t expected_cells;
	uint32_t expected_portals;
	uint32_t covered_cells;
	uint32_t covered_portals;
	uint32_t failure_record;
} sg_rune_completeness_t;

typedef uint32_t sg_rune_model_flags_t;
enum
{
	SG_RUNE_MODEL_IMMUTABLE = UINT32_C(1) << 0,
	SG_RUNE_MODEL_EXACT_BOUND = UINT32_C(1) << 1,
	SG_RUNE_MODEL_NO_RUNTIME_ACTORS = UINT32_C(1) << 2
};

typedef struct sg_rune_model_s
{
	uint16_t version;
	uint16_t reserved;
	uint32_t schema_tag;
	sg_rune_model_flags_t flags;
	sg_rune_model_identity_t identity;
	sg_rune_completeness_t completeness;

	const sg_rune_plane_t *planes;
	uint32_t plane_count;
	const sg_rune_vec3_t *portal_vertices;
	uint32_t portal_vertex_count;
	const sg_rune_phase_basis_t *phases;
	uint32_t phase_count;
	union
	{
		const sg_rune_phase_transition_t *phase_transitions;
		const sg_rune_phase_transition_t *transitions;
	};
	union
	{
		uint32_t phase_transition_count;
		uint32_t transition_count;
	};
	const sg_rune_cell_t *cells;
	uint32_t cell_count;
	const sg_rune_portal_t *portals;
	uint32_t portal_count;
	const sg_rune_surface_t *surfaces;
	uint32_t surface_count;
	const sg_rune_affordance_t *affordances;
	uint32_t affordance_count;
	const sg_rune_capability_kernel_t *kernels;
	uint32_t kernel_count;
	const sg_rune_landmark_t *landmarks;
	uint32_t landmark_count;
	const sg_rune_mechanism_t *mechanisms;
	uint32_t mechanism_count;
} sg_rune_model_t;

int SG_RuneModelStableIdValid(const sg_rune_stable_id_t *id);
int SG_RuneModelStableIdEqual(const sg_rune_stable_id_t *left,
	const sg_rune_stable_id_t *right);
int SG_RuneModelOrderKeyValid(const sg_rune_order_key_t *key);
int SG_RuneModelOrderKeyCompare(const sg_rune_order_key_t *left,
	const sg_rune_order_key_t *right);
sg_rune_stable_id_t SG_RuneModelStableIdFromOrderKey(
	const sg_rune_order_key_t *key);
int SG_RuneModelStableIdToOrderKey(const sg_rune_stable_id_t *id,
	sg_rune_order_key_t *key_out);
sg_rune_order_derivation_status_t SG_RuneModelOrderKeyDerive(
	const sg_rune_canonical_order_input_t *input,
	sg_rune_order_key_t *key_out);
int SG_RuneModelCompletenessValid(
	const sg_rune_completeness_t *completeness);
int SG_RuneModelPhaseValid(const sg_rune_phase_basis_t *phase);
/* Comparisons made by the most recent validation on this thread. */
uint64_t SG_RuneModelLastLookupComparisons(void);
/* COMPLETE requires evidence supplied outside the model under validation. */
sg_rune_failure_reason_t SG_RuneModelValidate(const sg_rune_model_t *model,
	const sg_rune_validation_evidence_t *evidence);
const char *SG_RuneModelFailureReasonString(sg_rune_failure_reason_t reason);

#endif
