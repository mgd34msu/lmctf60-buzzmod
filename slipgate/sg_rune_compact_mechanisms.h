/* Identity-bound mechanism facts for compact RUNE construction. */
#ifndef SG_RUNE_COMPACT_MECHANISMS_H
#define SG_RUNE_COMPACT_MECHANISMS_H

#include <stddef.h>
#include <stdint.h>

#include "sg_bsp_entity_semantics.h"
#include "sg_rune_compact_builder.h"
#include "sg_rune_compact_geometry.h"

typedef struct sg_rune_compact_mechanisms_s sg_rune_compact_mechanisms_t;

typedef struct sg_rune_compact_mechanism_entity_ref_s
{
	uint32_t entity_ordinal;
} sg_rune_compact_mechanism_entity_ref_t;

typedef enum sg_rune_compact_mechanism_authority_kind_e
{
	SG_RUNE_COMPACT_MECHANISM_AUTHORITY_DOOR = 0,
	SG_RUNE_COMPACT_MECHANISM_AUTHORITY_BUTTON,
	SG_RUNE_COMPACT_MECHANISM_AUTHORITY_LIFT,
	SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN,
	SG_RUNE_COMPACT_MECHANISM_AUTHORITY_PUSH,
	SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TELEPORT,
	SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRIGGER,
	SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ROTATOR,
	SG_RUNE_COMPACT_MECHANISM_AUTHORITY_KIND_COUNT
} sg_rune_compact_mechanism_authority_kind_t;

typedef uint32_t sg_rune_compact_mechanism_activation_mask_t;
enum
{
	SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_AUTO = UINT32_C(1) << 0,
	SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_TOUCH = UINT32_C(1) << 1,
	SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_USE = UINT32_C(1) << 2,
	SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_DAMAGE = UINT32_C(1) << 3,
	SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_INVENTORY = UINT32_C(1) << 4
};

#define SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_KNOWN \
	(SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_AUTO | \
	 SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_TOUCH | \
	 SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_USE | \
	 SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_DAMAGE | \
	 SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_INVENTORY)

typedef enum sg_rune_compact_mechanism_authority_state_e
{
	SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE = 0,
	SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVATING,
	SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE,
	SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_RETURNING,
	SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_DISABLED,
	SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_COUNT
} sg_rune_compact_mechanism_authority_state_t;

typedef uint32_t sg_rune_compact_mechanism_authority_flags_t;
enum
{
	SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ONE_SHOT = UINT32_C(1) << 0,
	SG_RUNE_COMPACT_MECHANISM_AUTHORITY_MOVER_RELATIVE = UINT32_C(1) << 1
};

#define SG_RUNE_COMPACT_MECHANISM_AUTHORITY_FLAGS_KNOWN \
	(SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ONE_SHOT | \
	 SG_RUNE_COMPACT_MECHANISM_AUTHORITY_MOVER_RELATIVE)

/* Authority timing is an aggregate only when every authenticated mover
 * transition agrees.  Zero deliberately means that no scalar duration is
 * authoritative; it never stands for a zero-duration move. */
#define SG_RUNE_COMPACT_MECHANISM_TIME_UNSPECIFIED UINT32_C(0)

typedef uint32_t sg_rune_compact_mechanism_controller_flags_t;
enum
{
	SG_RUNE_COMPACT_MECHANISM_CONTROLLER_ONE_SHOT = UINT32_C(1) << 0
};

#define SG_RUNE_COMPACT_MECHANISM_CONTROLLER_FLAGS_KNOWN \
	SG_RUNE_COMPACT_MECHANISM_CONTROLLER_ONE_SHOT

typedef enum sg_rune_compact_mechanism_transition_kind_e
{
	SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE = 0,
	SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT,
	SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH,
	SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT,
	SG_RUNE_COMPACT_MECHANISM_TRANSITION_KIND_COUNT
} sg_rune_compact_mechanism_transition_kind_t;

typedef struct sg_rune_compact_mechanism_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_compact_mechanism_span_t;

typedef struct sg_rune_compact_mechanism_authority_s
{
	sg_rune_compact_mechanism_entity_ref_t source;
	sg_rune_compact_mechanism_authority_kind_t kind;
	sg_rune_compact_mechanism_activation_mask_t activation;
	sg_rune_compact_cell_index_t activation_cell;
	sg_rune_q8_vec3_t activation_witness;
	sg_rune_q8_bounds_t activation_bounds;
	sg_rune_compact_mechanism_span_t controllers;
	sg_rune_compact_mechanism_span_t topology;
	sg_rune_compact_mechanism_span_t transitions;
	uint32_t delay_ms;
	uint32_t dwell_ms;
	uint32_t pause_ms;
	uint32_t travel_ms;
	int32_t damage;
	int32_t health;
	/* Authenticated entity-string offset.  Zero is valid;
	 * SG_BSP_ENTITY_STRING_NONE means absent. */
	uint32_t required_item;
	sg_rune_compact_mechanism_authority_state_t initial_state;
	sg_rune_compact_mechanism_authority_state_t activated_state;
	sg_rune_compact_mechanism_authority_state_t reset_state;
	uint32_t recovery_ms;
	sg_rune_compact_mechanism_authority_flags_t flags;
} sg_rune_compact_mechanism_authority_t;

typedef struct sg_rune_compact_mechanism_controller_s
{
	uint32_t mechanism;
	sg_rune_compact_mechanism_entity_ref_t controller;
	uint32_t topology_edge;
	/* These are facts of the source controller, never a copy of the controlled
	 * mechanism's activation or gates. */
	sg_rune_compact_mechanism_activation_mask_t activation;
	int32_t damage;
	int32_t health;
	uint32_t required_item;
	sg_rune_compact_mechanism_controller_flags_t flags;
	sg_rune_compact_mechanism_controller_spatiality_t spatiality;
	uint8_t reserved[3];
	sg_rune_compact_cell_index_t activation_cell;
	sg_rune_q8_vec3_t activation_witness;
	sg_rune_q8_bounds_t activation_bounds;
} sg_rune_compact_mechanism_controller_t;

_Static_assert(sizeof(sg_rune_compact_mechanism_controller_t) == 76U,
	"compact authority controller layout changed");
_Static_assert(offsetof(sg_rune_compact_mechanism_controller_t,
	spatiality) == 32U, "compact controller spatiality offset changed");
_Static_assert(offsetof(sg_rune_compact_mechanism_controller_t,
	activation_cell) == 36U, "compact controller location offset changed");

typedef struct sg_rune_compact_mechanism_topology_edge_s
{
	sg_rune_compact_mechanism_entity_ref_t source;
	sg_rune_compact_mechanism_entity_ref_t destination;
	sg_mech_edge_kind_t kind;
	uint32_t fanout_ordinal;
} sg_rune_compact_mechanism_topology_edge_t;

typedef struct sg_rune_compact_mechanism_portal_state_s
{
	sg_rune_compact_portal_index_t portal;
	uint32_t mover_model;
	uint32_t delay_ms;
	uint32_t dwell_ms;
	uint32_t pause_ms;
	uint32_t travel_ms;
	uint32_t recovery_ms;
	/* Exact host collision occupancy at the certified endpoint states.  The
	 * unequal pair is the portal-state transition's runtime meaning. */
	uint8_t source_blocked;
	uint8_t destination_blocked;
	uint8_t reserved[2];
} sg_rune_compact_mechanism_portal_state_t;

typedef struct sg_rune_compact_mechanism_teleport_s
{
	sg_rune_compact_mechanism_entity_ref_t destination;
	uint32_t fanout_ordinal;
	sg_rune_q8_vec3_t approach_witness;
	sg_rune_q8_vec3_t entry_witness;
	sg_rune_q8_vec3_t exit_witness;
	/* Stock teleporter_touch clears velocity to canonical positive zero. */
	uint32_t arrival_velocity_bits[3];
} sg_rune_compact_mechanism_teleport_t;

typedef struct sg_rune_compact_mechanism_push_s
{
	sg_rune_q8_vec3_t approach_witness;
	sg_rune_q8_vec3_t entry_witness;
	sg_rune_q8_vec3_t exit_witness;
	uint32_t launch_velocity_bits[3];
	uint32_t gravity_bits;
	uint32_t flight_ms;
} sg_rune_compact_mechanism_push_t;

typedef struct sg_rune_compact_mechanism_transport_s
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
	/* Exact host collision transforms used to map the local-Q8 poses above.
	 * Axes are host-derived operation-order witnesses: readers must never
	 * reconstruct them from angles with a platform math library. */
	uint32_t source_mover_origin_bits[3];
	uint32_t source_mover_axis_bits[3][3];
	uint32_t destination_mover_origin_bits[3];
	uint32_t destination_mover_axis_bits[3][3];
	sg_rune_compact_mechanism_entity_ref_t source_endpoint;
	sg_rune_compact_mechanism_entity_ref_t destination_endpoint;
	uint32_t fanout_ordinal;
	uint8_t swept_static_clear;
	uint8_t start_supported;
	uint8_t end_supported;
	/* Exact hull class checked at both SV_Push endpoints. */
	uint8_t stance;
} sg_rune_compact_mechanism_transport_t;

/* The tag exclusively selects one payload.  Inapplicable catalog references
 * inside the selected payload use SG_RUNE_COMPACT_INDEX_NONE. */
typedef struct sg_rune_compact_mechanism_transition_s
{
	uint32_t mechanism;
	sg_rune_compact_mechanism_transition_kind_t kind;
	sg_rune_compact_cell_index_t entry_cell;
	sg_rune_compact_cell_index_t exit_cell;
	sg_rune_compact_mechanism_authority_state_t source_state;
	sg_rune_compact_mechanism_authority_state_t destination_state;
	uint64_t elapsed_ms;
	union
	{
		sg_rune_compact_mechanism_portal_state_t portal_state;
		sg_rune_compact_mechanism_teleport_t teleport;
		sg_rune_compact_mechanism_push_t push;
		sg_rune_compact_mechanism_transport_t transport;
	} value;
} sg_rune_compact_mechanism_transition_t;

/* This is a binary schema, not an in-memory convenience structure.  Keep
 * the transform witnesses at fixed offsets so static and wire consumers
 * preserve the host's exact local-to-world operation order. */
_Static_assert(sizeof(sg_rune_compact_mechanism_transport_t) == 216U,
	"compact transport payload layout changed");
_Static_assert(sizeof(sg_rune_compact_mechanism_portal_state_t) == 32U,
	"compact portal-state payload layout changed");
_Static_assert(offsetof(sg_rune_compact_mechanism_transport_t,
	source_mover_origin_bits) == 104U,
	"compact source mover origin offset changed");
_Static_assert(offsetof(sg_rune_compact_mechanism_transport_t,
	source_mover_axis_bits) == 116U,
	"compact source mover axis offset changed");
_Static_assert(offsetof(sg_rune_compact_mechanism_transport_t,
	destination_mover_origin_bits) == 152U,
	"compact destination mover origin offset changed");
_Static_assert(offsetof(sg_rune_compact_mechanism_transport_t,
	destination_mover_axis_bits) == 164U,
	"compact destination mover axis offset changed");
_Static_assert(offsetof(sg_rune_compact_mechanism_transport_t,
	source_endpoint) == 200U,
	"compact source endpoint offset changed");
_Static_assert(offsetof(sg_rune_compact_mechanism_transport_t,
	destination_endpoint) == 204U,
	"compact destination endpoint offset changed");
_Static_assert(offsetof(sg_rune_compact_mechanism_transport_t,
	fanout_ordinal) == 208U,
	"compact transport fanout offset changed");
_Static_assert(offsetof(sg_rune_compact_mechanism_transport_t,
	swept_static_clear) == 212U,
	"compact transport evidence offset changed");
_Static_assert(sizeof(sg_rune_compact_mechanism_transition_t) == 248U,
	"compact tagged transition layout changed");

typedef struct sg_rune_compact_mechanisms_view_s
{
	sg_rune_compact_identity_t identity;
	const sg_rune_compact_mechanism_authority_t *mechanisms;
	uint32_t mechanism_count;
	const sg_rune_compact_mechanism_controller_t *controllers;
	uint32_t controller_count;
	const sg_rune_compact_mechanism_topology_edge_t *topology_edges;
	uint32_t topology_edge_count;
	const sg_rune_compact_mechanism_transition_t *transitions;
	uint32_t transition_count;
} sg_rune_compact_mechanisms_view_t;

typedef enum sg_rune_compact_mechanisms_error_code_e
{
	SG_RUNE_COMPACT_MECHANISMS_ERROR_NONE = 0,
	SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_ARGUMENT,
	SG_RUNE_COMPACT_MECHANISMS_ERROR_IDENTITY_MISMATCH,
	SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
	SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_GEOMETRY,
	SG_RUNE_COMPACT_MECHANISMS_ERROR_AMBIGUOUS_BINDING,
	SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION,
	SG_RUNE_COMPACT_MECHANISMS_ERROR_OVERFLOW,
	SG_RUNE_COMPACT_MECHANISMS_ERROR_OUT_OF_MEMORY,
	SG_RUNE_COMPACT_MECHANISMS_ERROR_CODE_COUNT
} sg_rune_compact_mechanisms_error_code_t;

typedef enum sg_rune_compact_mechanisms_record_domain_e
{
	SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT = 0,
	SG_RUNE_COMPACT_MECHANISMS_RECORD_BUILDER,
	SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY,
	SG_RUNE_COMPACT_MECHANISMS_RECORD_EDGE,
	SG_RUNE_COMPACT_MECHANISMS_RECORD_CELL,
	SG_RUNE_COMPACT_MECHANISMS_RECORD_PORTAL,
	SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION
} sg_rune_compact_mechanisms_record_domain_t;

typedef struct sg_rune_compact_mechanisms_error_s
{
	sg_rune_compact_mechanisms_error_code_t code;
	sg_rune_compact_mechanisms_record_domain_t domain;
	uint32_t record;
} sg_rune_compact_mechanisms_error_t;

int SG_RuneCompactMechanismsMaterialize(
	const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	sg_rune_compact_mechanisms_t **mechanisms_out,
	sg_rune_compact_mechanisms_error_t *error_out);
int SG_RuneCompactMechanismsRead(const sg_rune_compact_mechanisms_t *mechanisms,
	sg_rune_compact_mechanisms_view_t *view_out);
void SG_RuneCompactMechanismsDestroy(sg_rune_compact_mechanisms_t *mechanisms);
const char *SG_RuneCompactMechanismsErrorString(
	sg_rune_compact_mechanisms_error_code_t code);

#if defined(SG_RUNE_COMPACT_MECHANISMS_TESTING)
void SG_RuneCompactMechanismsTestFailAfter(size_t allocation);
size_t SG_RuneCompactMechanismsTestAllocationCount(void);
#endif

#endif
