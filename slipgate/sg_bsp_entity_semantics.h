#ifndef SG_BSP_ENTITY_SEMANTICS_H
#define SG_BSP_ENTITY_SEMANTICS_H

/* Quake II's g_local.h defines `world` as an edict expression.  This public
 * schema historically exposes a world fact and accepts world parameters, so
 * shield the declaration boundary instead of depending on include order. */
#ifdef world
#define SG_BSP_ENTITY_SEMANTICS_RESTORE_WORLD_MACRO
#undef world
#endif

#include <stddef.h>
#include <stdint.h>

#include "sg_bsp_world.h"
#include "sg_mechanism_kinds.h"
#include "sg_rune_model.h"
#include "sg_rune_source_authority.h"

#define SG_BSP_ENTITY_STRING_NONE UINT32_MAX
#define SG_BSP_ENTITY_MODEL_NONE UINT32_MAX

typedef enum sg_bsp_entity_semantics_error_code_e
{
	SG_BSP_ENTITY_SEMANTICS_ERROR_NONE = 0,
	SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_ARGUMENT,
	SG_BSP_ENTITY_SEMANTICS_ERROR_MALFORMED_TEXT,
	SG_BSP_ENTITY_SEMANTICS_ERROR_DUPLICATE_KEY,
	SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_VALUE,
	SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_MODEL,
	SG_BSP_ENTITY_SEMANTICS_ERROR_DUPLICATE_MODEL,
	SG_BSP_ENTITY_SEMANTICS_ERROR_AMBIGUOUS_IDENTITY,
	SG_BSP_ENTITY_SEMANTICS_ERROR_SIZE_OVERFLOW,
	SG_BSP_ENTITY_SEMANTICS_ERROR_OUT_OF_MEMORY,
	SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_RECORD_ORDER,
	SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_RECORD_RANGE,
	SG_BSP_ENTITY_SEMANTICS_ERROR_MISSING_WORLD_RECORD
} sg_bsp_entity_semantics_error_code_t;

typedef struct sg_bsp_entity_semantics_error_s
{
	sg_bsp_entity_semantics_error_code_t code;
	uint32_t entity_ordinal;
	uint32_t detail_ordinal;
} sg_bsp_entity_semantics_error_t;

typedef uint32_t sg_bsp_entity_semantic_flags_t;
enum
{
	SG_BSP_ENTITY_HAS_LANDMARK = UINT32_C(1) << 0,
	SG_BSP_ENTITY_HAS_MECHANISM = UINT32_C(1) << 1,
	SG_BSP_ENTITY_HAS_BRUSH_MODEL = UINT32_C(1) << 2,
	SG_BSP_ENTITY_HAS_BOUNDS = UINT32_C(1) << 3,
	SG_BSP_ENTITY_FLAG_RED = UINT32_C(1) << 4,
	SG_BSP_ENTITY_FLAG_BLUE = UINT32_C(1) << 5,
	SG_BSP_ENTITY_TOUCH_ACTIVATED = UINT32_C(1) << 6,
	SG_BSP_ENTITY_USE_ACTIVATED = UINT32_C(1) << 7,
	SG_BSP_ENTITY_DWELL_DEFINED = UINT32_C(1) << 8,
	SG_BSP_ENTITY_DELAY_DEFINED = UINT32_C(1) << 9,
	SG_BSP_ENTITY_ANGLES_DEFINED = UINT32_C(1) << 10,
	SG_BSP_ENTITY_LIP_DEFINED = UINT32_C(1) << 11,
	SG_BSP_ENTITY_HEIGHT_DEFINED = UINT32_C(1) << 12,
	SG_BSP_ENTITY_DISTANCE_DEFINED = UINT32_C(1) << 13,
	SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND = UINT32_C(1) << 14,
	SG_BSP_ENTITY_GRAVITY_DEFINED = UINT32_C(1) << 15,
	SG_BSP_ENTITY_DAMAGE_DEFINED = UINT32_C(1) << 16,
	SG_BSP_ENTITY_COUNT_DEFINED = UINT32_C(1) << 17,
	SG_BSP_ENTITY_HEALTH_DEFINED = UINT32_C(1) << 18,
	SG_BSP_ENTITY_RANDOM_DEFINED = UINT32_C(1) << 19,
	SG_BSP_ENTITY_MOVE_ORIGIN_DEFINED = UINT32_C(1) << 20,
	SG_BSP_ENTITY_MOVE_ANGLES_DEFINED = UINT32_C(1) << 21,
	SG_BSP_ENTITY_STYLE_DEFINED = UINT32_C(1) << 22,
	SG_BSP_ENTITY_SPEED_DEFINED = UINT32_C(1) << 23,
	SG_BSP_ENTITY_ACCELERATION_DEFINED = UINT32_C(1) << 24,
	SG_BSP_ENTITY_DECELERATION_DEFINED = UINT32_C(1) << 25,
	SG_BSP_ENTITY_SPAWNFLAGS_DEFINED = UINT32_C(1) << 26,
	SG_BSP_ENTITY_PAUSE_DEFINED = UINT32_C(1) << 27,
	SG_BSP_ENTITY_AUTO_ACTIVATED = UINT32_C(1) << 28,
	SG_BSP_ENTITY_DAMAGE_ACTIVATED = UINT32_C(1) << 29,
	SG_BSP_ENTITY_INVENTORY_GATED = UINT32_C(1) << 30
};
#define SG_BSP_ENTITY_INITIALLY_ACTIVE (UINT32_C(1) << 31)

typedef uint32_t sg_bsp_world_semantic_flags_t;
enum
{
	SG_BSP_WORLD_GRAVITY_EXPLICIT = UINT32_C(1) << 0
};

typedef enum sg_bsp_entity_physics_kind_e
{
	SG_BSP_ENTITY_PHYSICS_NONE = 0,
	SG_BSP_ENTITY_PHYSICS_PUSH,
	SG_BSP_ENTITY_PHYSICS_MONSTER_JUMP,
	SG_BSP_ENTITY_PHYSICS_GRAVITY,
	SG_BSP_ENTITY_PHYSICS_CONVEYOR,
	SG_BSP_ENTITY_PHYSICS_DAMAGE_VOLUME,
	SG_BSP_ENTITY_PHYSICS_DAMAGE_BEAM
} sg_bsp_entity_physics_kind_t;

/* These are spawn-resolved angular pusher facts. They describe transforms
 * and frame motion only. The host remains the authority for live activation,
 * blockers, and G_Push rollback. */
typedef enum sg_bsp_entity_angular_mover_kind_e
{
	SG_BSP_ENTITY_ANGULAR_MOVER_NONE = 0,
	SG_BSP_ENTITY_ANGULAR_MOVER_FINITE_DOOR,
	SG_BSP_ENTITY_ANGULAR_MOVER_CONTINUOUS_ROTATOR,
	SG_BSP_ENTITY_ANGULAR_MOVER_KIND_COUNT
} sg_bsp_entity_angular_mover_kind_t;

typedef uint32_t sg_bsp_entity_angular_mover_flags_t;
enum
{
	SG_BSP_ENTITY_ANGULAR_MOVER_START_OPEN = UINT32_C(1) << 0,
	SG_BSP_ENTITY_ANGULAR_MOVER_START_ON = UINT32_C(1) << 1,
	SG_BSP_ENTITY_ANGULAR_MOVER_REVERSE = UINT32_C(1) << 2,
	SG_BSP_ENTITY_ANGULAR_MOVER_TOGGLE = UINT32_C(1) << 3,
	SG_BSP_ENTITY_ANGULAR_MOVER_CRUSHER = UINT32_C(1) << 4,
	/* func_rotating bit 32: MOVETYPE_STOP retries after a blocked frame. */
	SG_BSP_ENTITY_ANGULAR_MOVER_STOP_ON_BLOCK = UINT32_C(1) << 5,
	SG_BSP_ENTITY_ANGULAR_MOVER_TOUCH_DAMAGE = UINT32_C(1) << 6
};

typedef struct sg_bsp_entity_angular_door_schedule_s
{
	/* The inactive and active transforms are the exact post-spawn door states. */
	sg_rune_vec3_t inactive_angles;
	sg_rune_vec3_t active_angles;
	/* This is SP_func_door_rotating's post-START_OPEN signed movedir. */
	sg_rune_vec3_t axis;
	/* Active minus inactive, retained as binary32 values. */
	sg_rune_vec3_t angular_displacement;
	float speed;
	/* Spawn-resolved moveinfo facts, retained though AngleMove uses speed only. */
	float acceleration;
	float deceleration;
	uint32_t frame_ms;
} sg_bsp_entity_angular_door_schedule_t;

typedef struct sg_bsp_entity_continuous_angular_schedule_s
{
	/* The host advances this transform by frame_angular_delta while active.
	 * It intentionally has no destination transform. */
	sg_rune_vec3_t initial_angles;
	sg_rune_vec3_t axis;
	sg_rune_vec3_t angular_velocity;
	sg_rune_vec3_t frame_angular_delta;
	float speed;
	uint32_t frame_ms;
} sg_bsp_entity_continuous_angular_schedule_t;

typedef struct sg_bsp_entity_angular_mover_s
{
	sg_bsp_entity_angular_mover_kind_t kind;
	sg_bsp_entity_angular_mover_flags_t flags;
	union
	{
		sg_bsp_entity_angular_door_schedule_t finite_door;
		sg_bsp_entity_continuous_angular_schedule_t continuous_rotator;
	} schedule;
} sg_bsp_entity_angular_mover_t;

typedef struct sg_bsp_world_entity_semantics_s
{
	uint64_t source_set_identity;
	uint32_t source_entity_ordinal;
	sg_bsp_world_semantic_flags_t flags;
	float gravity;
} sg_bsp_world_entity_semantics_t;

typedef struct sg_bsp_entity_semantic_s
{
	uint64_t source_set_identity;
	uint32_t source_entity_ordinal;
	uint32_t canonical_ordinal;
	uint32_t classname;
	uint32_t targetname;
	uint32_t required_item;
	uint32_t spawned_classname;
	uint32_t destination_map;
	uint32_t bsp_model;
	sg_bsp_entity_semantic_flags_t flags;
	sg_rune_landmark_kind_t landmark_kind;
	sg_rune_mechanism_kind_t mechanism_kind;
	sg_mech_node_kind_t mechanism_role;
	sg_bsp_entity_physics_kind_t physics_kind;
	sg_rune_vec3_t origin;
	sg_rune_vec3_t angles;
	sg_rune_vec3_t move_direction;
	sg_rune_vec3_t move_origin;
	sg_rune_vec3_t move_angles;
	sg_bsp_entity_angular_mover_t angular_mover;
	sg_rune_bounds_t bounds;
	float delay_ms;
	float dwell_ms;
	float pause_ms;
	float speed;
	float acceleration;
	float deceleration;
	float lip;
	float height;
	float distance;
	float gravity;
	float random;
	int32_t damage;
	int32_t count;
	int32_t health;
	int32_t style;
	uint32_t spawnflags;
} sg_bsp_entity_semantic_t;

static inline int SG_BspEntitySemanticHasFiniteAngularDoor(
	const sg_bsp_entity_semantic_t *entity)
{
	return entity != NULL &&
		entity->mechanism_kind == SG_RUNE_MECHANISM_ROTATOR &&
		entity->angular_mover.kind ==
		SG_BSP_ENTITY_ANGULAR_MOVER_FINITE_DOOR;
}

typedef struct sg_bsp_entity_semantic_edge_s
{
	uint32_t source;
	uint32_t destination;
	sg_mech_edge_kind_t kind;
	uint32_t name;
	uint32_t fanout_ordinal;
} sg_bsp_entity_semantic_edge_t;

typedef struct sg_bsp_entity_semantics_s
{
	uint64_t source_set_identity;
	sg_bsp_world_entity_semantics_t world;
	/* Builder-issued storage; audits authenticate bases and extents before
	 * indexing, so caller-owned arrays cannot be presented as facts. */
	sg_bsp_entity_semantic_t *entities;
	uint32_t entity_count;
	sg_bsp_entity_semantic_edge_t *edges;
	uint32_t edge_count;
	/* Non-empty string storage is builder-owned and audited by extent identity. */
	char *strings;
	uint32_t string_bytes;
} sg_bsp_entity_semantics_t;

int SG_BspEntitySemanticsBuild(const sg_bsp_world_t *world,
	uint64_t source_set_identity, sg_bsp_entity_semantics_t **semantics_out,
	sg_bsp_entity_semantics_error_t *error_out);
/* selected_entity_text_bytes includes the terminating NUL.  The survivor
 * records are borrowed for the call, strictly increasing by source ordinal,
 * and describe every declaration that survived host spawning. */
int SG_BspEntitySemanticsBuildEffective(const sg_bsp_world_t *world,
	const char *selected_entity_text, size_t selected_entity_text_bytes,
	const sg_rune_source_entity_record_t *survivors, size_t survivor_count,
	uint64_t source_set_identity, sg_bsp_entity_semantics_t **semantics_out,
	sg_bsp_entity_semantics_error_t *error_out);
void SG_BspEntitySemanticsDestroy(sg_bsp_entity_semantics_t *semantics);
const char *SG_BspEntitySemanticsString(
	const sg_bsp_entity_semantics_t *semantics, uint32_t offset);
/* Returns the builder-issued spawn-resolved schedule for a canonical entity
 * ordinal. NONE and records without matching source provenance return NULL. */
const sg_bsp_entity_angular_mover_t *SG_BspEntitySemanticsAngularMover(
	const sg_bsp_entity_semantics_t *semantics, uint32_t canonical_ordinal);
int SG_BspEntitySemanticsCountsRepresentable(size_t entity_count,
	size_t edge_count, size_t string_bytes);
const char *SG_BspEntitySemanticsErrorString(
	sg_bsp_entity_semantics_error_code_t code);

#ifdef SG_BSP_ENTITY_SEMANTICS_RESTORE_WORLD_MACRO
#define world (&g_edicts[0])
#undef SG_BSP_ENTITY_SEMANTICS_RESTORE_WORLD_MACRO
#endif

#endif
