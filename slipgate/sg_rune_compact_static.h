#ifndef SG_RUNE_COMPACT_STATIC_H
#define SG_RUNE_COMPACT_STATIC_H

#include <stddef.h>

#include "sg_rune_compact_model.h"

#define SG_RUNE_COMPACT_MAX_MECHANISMS UINT32_C(1048576)
#define SG_RUNE_COMPACT_MAX_MECHANISM_CONTROLLERS UINT32_C(4194304)
#define SG_RUNE_COMPACT_MAX_MECHANISM_EDGES UINT32_C(4194304)
#define SG_RUNE_COMPACT_MAX_MECHANISM_TRANSITIONS UINT32_C(4194304)
#define SG_RUNE_COMPACT_MAX_LANDMARKS UINT32_C(4194304)
#define SG_RUNE_COMPACT_MAX_LANDMARK_CELL_REFS UINT32_C(16777216)
#define SG_RUNE_COMPACT_MAX_FACET_ANNOTATIONS UINT32_C(4194304)
#define SG_RUNE_COMPACT_MAX_PORTAL_MECHANISMS UINT32_C(4194304)

#define SG_RUNE_COMPACT_STATIC_INDEX_TYPE(name) \
	typedef struct name##_s { uint32_t value; } name##_t

SG_RUNE_COMPACT_STATIC_INDEX_TYPE(sg_rune_compact_mechanism_index);
SG_RUNE_COMPACT_STATIC_INDEX_TYPE(sg_rune_compact_landmark_index);

#undef SG_RUNE_COMPACT_STATIC_INDEX_TYPE

typedef struct sg_rune_compact_entity_ref_s
{
	uint32_t entity_ordinal;
} sg_rune_compact_entity_ref_t;

typedef struct sg_rune_compact_mechanism_edge_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_compact_mechanism_edge_span_t;

typedef struct sg_rune_compact_mechanism_controller_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_compact_mechanism_controller_span_t;

typedef struct sg_rune_compact_mechanism_transition_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_compact_mechanism_transition_span_t;

typedef struct sg_rune_compact_landmark_cell_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_compact_landmark_cell_span_t;

typedef enum sg_rune_compact_mechanism_kind_e
{
	SG_RUNE_COMPACT_MECHANISM_DOOR = 0,
	SG_RUNE_COMPACT_MECHANISM_BUTTON,
	SG_RUNE_COMPACT_MECHANISM_LIFT,
	SG_RUNE_COMPACT_MECHANISM_TRAIN,
	SG_RUNE_COMPACT_MECHANISM_PUSH,
	SG_RUNE_COMPACT_MECHANISM_TELEPORT,
	SG_RUNE_COMPACT_MECHANISM_TRIGGER,
	SG_RUNE_COMPACT_MECHANISM_ROTATOR,
	SG_RUNE_COMPACT_MECHANISM_KIND_COUNT
} sg_rune_compact_mechanism_kind_t;

typedef uint8_t sg_rune_compact_mechanism_flags_t;
enum
{
	SG_RUNE_COMPACT_MECHANISM_ONE_SHOT = UINT8_C(1) << 0,
	SG_RUNE_COMPACT_MECHANISM_MOVER_RELATIVE = UINT8_C(1) << 1,
	/* A rotating mover may publish a portal state only when the entity
	 * semantics authenticated a finite func_door_rotating schedule. */
	SG_RUNE_COMPACT_MECHANISM_FINITE_ANGULAR_DOOR = UINT8_C(1) << 2
};

#define SG_RUNE_COMPACT_MECHANISM_FLAGS_KNOWN \
	(SG_RUNE_COMPACT_MECHANISM_ONE_SHOT | \
	 SG_RUNE_COMPACT_MECHANISM_MOVER_RELATIVE | \
	 SG_RUNE_COMPACT_MECHANISM_FINITE_ANGULAR_DOOR)

typedef enum sg_rune_compact_mechanism_edge_kind_e
{
	SG_RUNE_COMPACT_MECHANISM_EDGE_TARGET = 0,
	SG_RUNE_COMPACT_MECHANISM_EDGE_KILLTARGET,
	SG_RUNE_COMPACT_MECHANISM_EDGE_TEAM,
	SG_RUNE_COMPACT_MECHANISM_EDGE_PATH,
	SG_RUNE_COMPACT_MECHANISM_EDGE_ACTIVATES,
	/* Entity ownership/enemy relations are static topology facts too.  Keep
	 * them distinct on the wire instead of silently dropping them. */
	SG_RUNE_COMPACT_MECHANISM_EDGE_OWNER,
	SG_RUNE_COMPACT_MECHANISM_EDGE_ENEMY,
	/* Keep the four target relation facts distinct.  The legacy TARGET and
	 * PATH values above remain stable for already-published records; these
	 * values are the exact source semantics used by the current authority. */
	SG_RUNE_COMPACT_MECHANISM_EDGE_TARGET_ENT,
	SG_RUNE_COMPACT_MECHANISM_EDGE_MOVE_TARGET,
	SG_RUNE_COMPACT_MECHANISM_EDGE_ROUTE_TARGET,
	SG_RUNE_COMPACT_MECHANISM_EDGE_PATH_TARGET,
	SG_RUNE_COMPACT_MECHANISM_EDGE_KIND_COUNT
} sg_rune_compact_mechanism_edge_kind_t;

typedef struct sg_rune_compact_mechanism_edge_s
{
	sg_rune_compact_entity_ref_t source;
	sg_rune_compact_entity_ref_t destination;
	uint32_t fanout_ordinal;
	sg_rune_compact_mechanism_edge_kind_t kind;
} sg_rune_compact_mechanism_edge_t;

typedef uint32_t sg_rune_compact_static_activation_mask_t;
enum
{
	SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_AUTO = UINT32_C(1) << 0,
	SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_TOUCH = UINT32_C(1) << 1,
	SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_USE = UINT32_C(1) << 2,
	SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_DAMAGE = UINT32_C(1) << 3,
	SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_INVENTORY = UINT32_C(1) << 4
};

#define SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_KNOWN \
	(SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_AUTO | \
	 SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_TOUCH | \
	 SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_USE | \
	 SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_DAMAGE | \
	 SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_INVENTORY)

typedef enum sg_rune_compact_mechanism_state_e
{
	SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE = 0,
	SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVATING,
	SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE,
	SG_RUNE_COMPACT_MECHANISM_STATE_RETURNING,
	SG_RUNE_COMPACT_MECHANISM_STATE_DISABLED,
	SG_RUNE_COMPACT_MECHANISM_STATE_COUNT
} sg_rune_compact_mechanism_state_t;

typedef enum sg_rune_compact_mechanism_recovery_e
{
	SG_RUNE_COMPACT_MECHANISM_RECOVERY_NONE = 0,
	SG_RUNE_COMPACT_MECHANISM_RECOVERY_WAIT_FOR_RESET,
	SG_RUNE_COMPACT_MECHANISM_RECOVERY_REACQUIRE_CONTROLLER,
	SG_RUNE_COMPACT_MECHANISM_RECOVERY_COUNT
} sg_rune_compact_mechanism_recovery_t;

typedef struct sg_rune_compact_mechanism_s
{
	sg_rune_compact_entity_ref_t source;
	sg_rune_compact_cell_index_t entry_cell;
	sg_rune_compact_cell_index_t exit_cell;
	sg_rune_compact_landmark_index_t activation_landmark;
	sg_rune_q8_bounds_t bounds;
	sg_rune_compact_mechanism_controller_span_t controllers;
	sg_rune_compact_mechanism_edge_span_t topology;
	sg_rune_compact_mechanism_transition_span_t transitions;
	uint32_t delay_ms;
	uint32_t dwell_ms;
	uint32_t travel_ms;
	uint32_t wait_ms;
	uint32_t reset_ms;
	sg_rune_compact_static_activation_mask_t activation_mask;
	int32_t damage;
	int32_t health;
	/* Entity string-table offset; UINT32_MAX means absent and zero is valid. */
	uint32_t required_item;
	sg_rune_compact_entity_ref_t transition_destination;
	uint32_t transition_fanout_ordinal;
	uint32_t launch_velocity_bits[3];
	uint32_t gravity_bits;
	uint32_t flight_ms;
	sg_rune_compact_mechanism_kind_t kind;
	sg_rune_compact_mechanism_state_t initial_state;
	sg_rune_compact_mechanism_state_t activated_state;
	sg_rune_compact_mechanism_state_t reset_state;
	sg_rune_compact_mechanism_recovery_t recovery;
	sg_rune_compact_mechanism_flags_t flags;
	uint8_t reserved[3];
} sg_rune_compact_mechanism_t;

typedef struct sg_rune_compact_static_mechanism_controller_s
{
	sg_rune_compact_entity_ref_t controller;
	/* Authenticated authority-topology ordinal.  UINT32_MAX means that this
	 * controller fact has no topology-edge provenance. */
	uint32_t topology_edge;
	sg_rune_compact_mechanism_controller_spatiality_t spatiality;
	uint8_t reserved[3];
	/* Exact activation witness retained with the controller fact.  A single
	 * entity may therefore have several independent activation/topology facts. */
	sg_rune_compact_cell_index_t activation_cell;
	sg_rune_q8_vec3_t activation_witness;
	sg_rune_q8_bounds_t activation_bounds;
} sg_rune_compact_static_mechanism_controller_t;

_Static_assert(sizeof(sg_rune_compact_static_mechanism_controller_t) == 52U,
	"compact static controller layout changed");
_Static_assert(offsetof(sg_rune_compact_static_mechanism_controller_t,
	spatiality) == 8U, "compact static controller spatiality offset changed");

typedef enum sg_rune_compact_static_transition_kind_e
{
	SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE = 0,
	SG_RUNE_COMPACT_STATIC_TRANSITION_TELEPORT,
	SG_RUNE_COMPACT_STATIC_TRANSITION_PUSH,
	SG_RUNE_COMPACT_STATIC_TRANSITION_TRANSPORT,
	SG_RUNE_COMPACT_STATIC_TRANSITION_KIND_COUNT
} sg_rune_compact_static_transition_kind_t;

/* The transition is a tagged projection of one authenticated mechanism fact.
 * The common prefix and the largest transport payload deliberately match the
 * authority transition shape (248 bytes in memory, with a 216-byte transport
 * payload).  Payload fields that do
 * not belong to the selected tag do not exist in the static record, so stale
 * flat-record values cannot survive a kind conversion. */
typedef struct sg_rune_compact_static_portal_state_s
{
	sg_rune_compact_portal_index_t portal;
	uint32_t mover_model;
	uint32_t delay_ms;
	uint32_t dwell_ms;
	uint32_t pause_ms;
	uint32_t travel_ms;
	uint32_t recovery_ms;
	/* Authenticated host facts, kept independent of activation/dwell timing. */
	uint8_t source_blocked;
	uint8_t destination_blocked;
	uint8_t reserved[2];
} sg_rune_compact_static_portal_state_t;

typedef struct sg_rune_compact_static_teleport_s
{
	sg_rune_compact_entity_ref_t destination;
	uint32_t fanout_ordinal;
	sg_rune_q8_vec3_t approach_witness;
	sg_rune_q8_vec3_t entry_witness;
	sg_rune_q8_vec3_t exit_witness;
	uint32_t arrival_velocity_bits[3];
} sg_rune_compact_static_teleport_t;

typedef struct sg_rune_compact_static_push_s
{
	sg_rune_q8_vec3_t approach_witness;
	sg_rune_q8_vec3_t entry_witness;
	sg_rune_q8_vec3_t exit_witness;
	uint32_t launch_velocity_bits[3];
	uint32_t gravity_bits;
	uint32_t flight_ms;
} sg_rune_compact_static_push_t;

typedef struct sg_rune_compact_static_transport_s
{
	uint32_t mover_model;
	uint32_t source_surface_ordinal;
	sg_rune_q8_vec3_t source_player_local;
	sg_rune_q8_vec3_t destination_player_local;
	sg_rune_q8_vec3_t source_support_local;
	sg_rune_q8_vec3_t destination_support_local;
	uint32_t source_player_world_bits[3];
	uint32_t destination_player_world_bits[3];
	uint32_t source_support_world_bits[3];
	uint32_t destination_support_world_bits[3];
	/* Exact binary32 mover transforms used for local-to-world pose
	 * rederivation.  The axis matrices are host-derived operation-order
	 * witnesses; readers must not reconstruct them from angles. */
	uint32_t source_mover_origin_bits[3];
	uint32_t source_mover_axis_bits[3][3];
	uint32_t destination_mover_origin_bits[3];
	uint32_t destination_mover_axis_bits[3][3];
	sg_rune_compact_entity_ref_t source_endpoint;
	sg_rune_compact_entity_ref_t destination_endpoint;
	uint32_t fanout_ordinal;
	uint8_t swept_static_clear;
	uint8_t start_supported;
	uint8_t end_supported;
	uint8_t stance;
} sg_rune_compact_static_transport_t;

typedef struct sg_rune_compact_static_transition_s
{
	sg_rune_compact_mechanism_index_t mechanism;
	sg_rune_compact_static_transition_kind_t kind;
	sg_rune_compact_cell_index_t entry_cell;
	sg_rune_compact_cell_index_t exit_cell;
	sg_rune_compact_mechanism_state_t source_state;
	sg_rune_compact_mechanism_state_t destination_state;
	uint64_t elapsed_ms;
	union
	{
		sg_rune_compact_static_portal_state_t portal_state;
		sg_rune_compact_static_teleport_t teleport;
		sg_rune_compact_static_push_t push;
		sg_rune_compact_static_transport_t transport;
	} value;
} sg_rune_compact_static_transition_t;

_Static_assert(sizeof(sg_rune_compact_static_portal_state_t) == 32U,
	"compact static portal-state payload layout changed");
_Static_assert(sizeof(sg_rune_compact_static_transport_t) == 216U,
	"compact static transport layout changed");
_Static_assert(sizeof(sg_rune_compact_static_transition_t) == 248U,
	"compact static transition layout changed");

typedef enum sg_rune_compact_landmark_kind_e
{
	SG_RUNE_COMPACT_LANDMARK_SPAWN = 0,
	SG_RUNE_COMPACT_LANDMARK_FLAG,
	SG_RUNE_COMPACT_LANDMARK_WEAPON,
	SG_RUNE_COMPACT_LANDMARK_AMMO,
	SG_RUNE_COMPACT_LANDMARK_ARMOR,
	SG_RUNE_COMPACT_LANDMARK_HEALTH,
	SG_RUNE_COMPACT_LANDMARK_POWERUP,
	SG_RUNE_COMPACT_LANDMARK_BUTTON,
	SG_RUNE_COMPACT_LANDMARK_TRIGGER,
	SG_RUNE_COMPACT_LANDMARK_MECHANISM_ENTRY,
	SG_RUNE_COMPACT_LANDMARK_TELEPORTER_DESTINATION,
	SG_RUNE_COMPACT_LANDMARK_JUMPPAD_LANDING,
	SG_RUNE_COMPACT_LANDMARK_DEFENSIVE_POSITION,
	SG_RUNE_COMPACT_LANDMARK_KIND_COUNT
} sg_rune_compact_landmark_kind_t;

typedef struct sg_rune_compact_landmark_s
{
	sg_rune_compact_entity_ref_t source;
	sg_rune_compact_landmark_cell_span_t cells;
	sg_rune_compact_mechanism_index_t mechanism;
	sg_rune_q8_vec3_t origin;
	sg_rune_q8_bounds_t bounds;
	sg_rune_compact_landmark_kind_t kind;
	uint16_t variant;
	uint16_t reserved;
} sg_rune_compact_landmark_t;

typedef uint16_t sg_rune_compact_facet_attributes_t;
enum
{
	SG_RUNE_COMPACT_FACET_HOOKABLE = UINT16_C(1) << 0,
	SG_RUNE_COMPACT_FACET_SKY = UINT16_C(1) << 1,
	SG_RUNE_COMPACT_FACET_HAZARD = UINT16_C(1) << 2,
	SG_RUNE_COMPACT_FACET_COVER_NEGATIVE = UINT16_C(1) << 3,
	SG_RUNE_COMPACT_FACET_COVER_POSITIVE = UINT16_C(1) << 4,
	SG_RUNE_COMPACT_FACET_EXPOSURE_NEGATIVE = UINT16_C(1) << 5,
	SG_RUNE_COMPACT_FACET_EXPOSURE_POSITIVE = UINT16_C(1) << 6,
	SG_RUNE_COMPACT_FACET_VISIBILITY_DISCONTINUITY = UINT16_C(1) << 7
};

#define SG_RUNE_COMPACT_FACET_ATTRIBUTES_KNOWN \
	(SG_RUNE_COMPACT_FACET_HOOKABLE | SG_RUNE_COMPACT_FACET_SKY | \
	 SG_RUNE_COMPACT_FACET_HAZARD | \
	 SG_RUNE_COMPACT_FACET_COVER_NEGATIVE | \
	 SG_RUNE_COMPACT_FACET_COVER_POSITIVE | \
	 SG_RUNE_COMPACT_FACET_EXPOSURE_NEGATIVE | \
	 SG_RUNE_COMPACT_FACET_EXPOSURE_POSITIVE | \
	 SG_RUNE_COMPACT_FACET_VISIBILITY_DISCONTINUITY)

typedef struct sg_rune_compact_facet_annotation_s
{
	sg_rune_compact_facet_index_t facet;
	sg_rune_compact_facet_attributes_t attributes;
	sg_rune_stance_validity_t hookable_stances;
	uint8_t reserved;
	/* Exact source-catalog identity for surface-owned facts.  A missing
	 * source_surface means that the annotation is derived from the world
	 * facet/contents rather than an authenticated brush-side root. */
	uint32_t source_surface;
	sg_rune_compact_source_surface_frame_t source_frame;
} sg_rune_compact_facet_annotation_t;

typedef enum sg_rune_compact_portal_mechanism_kind_e
{
	SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS = 0,
	SG_RUNE_COMPACT_PORTAL_MECHANISM_MOVES,
	SG_RUNE_COMPACT_PORTAL_MECHANISM_TELEPORTS,
	SG_RUNE_COMPACT_PORTAL_MECHANISM_LAUNCHES,
	SG_RUNE_COMPACT_PORTAL_MECHANISM_KIND_COUNT
} sg_rune_compact_portal_mechanism_kind_t;

typedef struct sg_rune_compact_portal_mechanism_s
{
	sg_rune_compact_portal_index_t portal;
	sg_rune_compact_mechanism_index_t mechanism;
	sg_rune_compact_portal_mechanism_kind_t kind;
	uint8_t reserved[3];
} sg_rune_compact_portal_mechanism_t;

struct sg_rune_compact_static_s
{
	const sg_rune_compact_mechanism_t *mechanisms;
	uint32_t mechanism_count;
	const sg_rune_compact_static_mechanism_controller_t *mechanism_controllers;
	uint32_t mechanism_controller_count;
	const sg_rune_compact_mechanism_edge_t *mechanism_edges;
	uint32_t mechanism_edge_count;
	const sg_rune_compact_static_transition_t *transitions;
	uint32_t transition_count;
	const sg_rune_compact_landmark_t *landmarks;
	uint32_t landmark_count;
	const sg_rune_compact_cell_index_t *landmark_cells;
	uint32_t landmark_cell_count;
	const sg_rune_compact_facet_annotation_t *facet_annotations;
	uint32_t facet_annotation_count;
	const sg_rune_compact_portal_mechanism_t *portal_mechanisms;
	uint32_t portal_mechanism_count;
};

typedef enum sg_rune_compact_static_error_code_e
{
	SG_RUNE_COMPACT_STATIC_ERROR_NONE = 0,
	SG_RUNE_COMPACT_STATIC_ERROR_INVALID_ARGUMENT,
	SG_RUNE_COMPACT_STATIC_ERROR_LIMIT_EXCEEDED,
	SG_RUNE_COMPACT_STATIC_ERROR_NONZERO_RESERVED,
	SG_RUNE_COMPACT_STATIC_ERROR_NONCANONICAL_ORDER,
	SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE,
	SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
	SG_RUNE_COMPACT_STATIC_ERROR_OUT_OF_MEMORY,
	SG_RUNE_COMPACT_STATIC_ERROR_CODE_COUNT
} sg_rune_compact_static_error_code_t;

typedef enum sg_rune_compact_static_record_domain_e
{
	SG_RUNE_COMPACT_STATIC_RECORD_MODEL = 0,
	SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM,
	SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM_EDGE,
	SG_RUNE_COMPACT_STATIC_RECORD_LANDMARK,
	SG_RUNE_COMPACT_STATIC_RECORD_FACET_ANNOTATION,
	SG_RUNE_COMPACT_STATIC_RECORD_PORTAL_MECHANISM,
	SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM_CONTROLLER,
	SG_RUNE_COMPACT_STATIC_RECORD_TRANSITION
} sg_rune_compact_static_record_domain_t;

typedef struct sg_rune_compact_static_error_s
{
	sg_rune_compact_static_error_code_t code;
	sg_rune_compact_static_record_domain_t domain;
	uint32_t record;
} sg_rune_compact_static_error_t;

int SG_RuneCompactStaticValidate(const sg_rune_compact_model_t *model,
	const sg_rune_compact_static_t *static_data,
	sg_rune_compact_static_error_t *error_out);
/* The single canonical order for projected transition records.  Producers
 * must use this comparator when sorting each mechanism span; validators use
 * the same key so authority ordering cannot diverge from static ordering. */
int SG_RuneCompactStaticTransitionCompareCanonical(
	const sg_rune_compact_static_transition_t *left,
	const sg_rune_compact_static_transition_t *right);
/* Reproduce the host collision model-to-world operation for one Q8-local
 * witness.  Transform and output values are exact binary32 bit patterns;
 * transform inputs reject non-finite values and negative zero, while the
 * host's final zero canonicalization is retained in world_bits_out. */
int SG_RuneCompactStaticTransportDeriveWorldPointBits(
	const sg_rune_q8_vec3_t *local,
	const uint32_t mover_origin_bits[3],
	const uint32_t mover_axis_bits[3][3], uint32_t world_bits_out[3]);
const char *SG_RuneCompactStaticErrorString(
	sg_rune_compact_static_error_code_t code);

#endif
