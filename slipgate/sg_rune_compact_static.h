#ifndef SG_RUNE_COMPACT_STATIC_H
#define SG_RUNE_COMPACT_STATIC_H

#include "sg_rune_compact_model.h"

#define SG_RUNE_COMPACT_MAX_MECHANISMS UINT32_C(1048576)
#define SG_RUNE_COMPACT_MAX_MECHANISM_EDGES UINT32_C(4194304)
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
	SG_RUNE_COMPACT_MECHANISM_MOVER_RELATIVE = UINT8_C(1) << 1
};

#define SG_RUNE_COMPACT_MECHANISM_FLAGS_KNOWN \
	(SG_RUNE_COMPACT_MECHANISM_ONE_SHOT | \
	 SG_RUNE_COMPACT_MECHANISM_MOVER_RELATIVE)

typedef enum sg_rune_compact_mechanism_edge_kind_e
{
	SG_RUNE_COMPACT_MECHANISM_EDGE_TARGET = 0,
	SG_RUNE_COMPACT_MECHANISM_EDGE_KILLTARGET,
	SG_RUNE_COMPACT_MECHANISM_EDGE_TEAM,
	SG_RUNE_COMPACT_MECHANISM_EDGE_PATH,
	SG_RUNE_COMPACT_MECHANISM_EDGE_ACTIVATES,
	SG_RUNE_COMPACT_MECHANISM_EDGE_KIND_COUNT
} sg_rune_compact_mechanism_edge_kind_t;

typedef struct sg_rune_compact_mechanism_edge_s
{
	sg_rune_compact_entity_ref_t source;
	sg_rune_compact_entity_ref_t destination;
	uint32_t fanout_ordinal;
	sg_rune_compact_mechanism_edge_kind_t kind;
} sg_rune_compact_mechanism_edge_t;

typedef enum sg_rune_compact_mechanism_activation_e
{
	SG_RUNE_COMPACT_MECHANISM_ACTIVATION_AUTOMATIC = 0,
	SG_RUNE_COMPACT_MECHANISM_ACTIVATION_TOUCH,
	SG_RUNE_COMPACT_MECHANISM_ACTIVATION_USE,
	SG_RUNE_COMPACT_MECHANISM_ACTIVATION_TRIGGER,
	SG_RUNE_COMPACT_MECHANISM_ACTIVATION_DWELL,
	SG_RUNE_COMPACT_MECHANISM_ACTIVATION_COUNT
} sg_rune_compact_mechanism_activation_t;

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
	sg_rune_compact_entity_ref_t controller;
	sg_rune_compact_cell_index_t entry_cell;
	sg_rune_compact_cell_index_t exit_cell;
	sg_rune_compact_landmark_index_t activation_landmark;
	sg_rune_q8_bounds_t bounds;
	sg_rune_compact_mechanism_edge_span_t topology;
	uint32_t delay_ms;
	uint32_t dwell_ms;
	uint32_t travel_ms;
	uint32_t wait_ms;
	uint32_t reset_ms;
	sg_rune_compact_mechanism_kind_t kind;
	sg_rune_compact_mechanism_activation_t activation;
	sg_rune_compact_mechanism_state_t initial_state;
	sg_rune_compact_mechanism_state_t activated_state;
	sg_rune_compact_mechanism_state_t reset_state;
	sg_rune_compact_mechanism_recovery_t recovery;
	sg_rune_compact_mechanism_flags_t flags;
	uint8_t reserved[3];
} sg_rune_compact_mechanism_t;

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
	const sg_rune_compact_mechanism_edge_t *mechanism_edges;
	uint32_t mechanism_edge_count;
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
	SG_RUNE_COMPACT_STATIC_ERROR_CODE_COUNT
} sg_rune_compact_static_error_code_t;

typedef enum sg_rune_compact_static_record_domain_e
{
	SG_RUNE_COMPACT_STATIC_RECORD_MODEL = 0,
	SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM,
	SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM_EDGE,
	SG_RUNE_COMPACT_STATIC_RECORD_LANDMARK,
	SG_RUNE_COMPACT_STATIC_RECORD_FACET_ANNOTATION,
	SG_RUNE_COMPACT_STATIC_RECORD_PORTAL_MECHANISM
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
const char *SG_RuneCompactStaticErrorString(
	sg_rune_compact_static_error_code_t code);

#endif
