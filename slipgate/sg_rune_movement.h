/* Era-4 movement capabilities.
 *
 * The RUNE says a crossing exists; these records say how it may be made and
 * at what cost.  One record per portal crossing per admissible kind, from
 * the geometry the complex already carries: whether each side is supported
 * or water, the floor delta between supported sides, and whether the
 * shared facet is a vertical partition or a floor boundary.  Every record
 * points at a shared profile: a handful of analytic functions derived from
 * the host's movement law under this map's gravity.  Nothing is fitted. */
#ifndef SG_RUNE_MOVE_H
#define SG_RUNE_MOVE_H

#include <stdint.h>

#include "sg_rune_analytic.h"

typedef enum sg_rune_move_kind_e
{
	SG_RUNE_MOVE_WALK = 0,
	SG_RUNE_MOVE_CROUCH,
	SG_RUNE_MOVE_RAMP,
	SG_RUNE_MOVE_JUMP,
	SG_RUNE_MOVE_DROP,
	SG_RUNE_MOVE_SWIM,
	SG_RUNE_MOVE_AIR_CONTROL,
	SG_RUNE_MOVE_ROCKET_JUMP,
	SG_RUNE_MOVE_HOOK,
	SG_RUNE_MOVE_MOVER,
	SG_RUNE_MOVE_EXTERNAL_FORCE,
	SG_RUNE_MOVE_CONTROLLER_ACTION,
	SG_RUNE_MOVE_KIND_COUNT
} sg_rune_move_kind_t;

typedef uint8_t sg_rune_move_stances_t;
enum
{
	SG_RUNE_MOVE_STANDING = UINT8_C(1) << 0,
	SG_RUNE_MOVE_CROUCHING = UINT8_C(1) << 1
};

/* Function indices into the analytic table; INDEX_NONE where a profile has
 * no such function. */
typedef struct sg_rune_move_profile_s
{
	uint32_t cost;
	uint32_t travel_time;
	uint32_t position[3];
	uint32_t velocity[3];
	uint32_t reachability;
	float lead_seconds;       /* command to launch, for rocket jump */
} sg_rune_move_profile_t;

typedef struct sg_rune_move_capability_s
{
	uint32_t cell;
	uint32_t portal;
	uint8_t kind;             /* sg_rune_move_kind_t */
	uint8_t source_stances;
	uint8_t destination_stances;
	uint8_t reserved;
	uint32_t profile;
} sg_rune_move_capability_t;

typedef struct sg_rune_move_table_s
{
	const sg_rune_move_capability_t *capabilities;
	uint32_t capability_count;
	const sg_rune_move_profile_t *profiles;
	uint32_t profile_count;
	sg_rune_fn_table_t analytic;
} sg_rune_move_table_t;

/* Host movement law the profiles derive from. */
typedef struct sg_rune_move_law_s
{
	float gravity;
	uint32_t frame_ms;
	uint32_t substep_ms;
} sg_rune_move_law_t;

/* One crossing as the builder sees it. */
typedef struct sg_rune_move_crossing_s
{
	uint32_t cell;
	uint32_t portal;
	uint32_t other_cell;
	uint8_t cell_stances;
	uint8_t other_stances;
	uint8_t portal_stances;
	uint8_t reserved;
	int source_supported;
	int target_supported;
	int source_water;
	int target_water;
	int vertical_facet;       /* the shared facet is a partition, not a floor */
	float floor_delta;        /* target floor minus source floor */
} sg_rune_move_crossing_t;

typedef struct sg_rune_move_store_s
{
	sg_rune_move_capability_t *capabilities;
	uint32_t capability_count, capability_capacity;
	sg_rune_move_profile_t *profiles;
	uint32_t profile_count;
	sg_rune_fn_store_t analytic;
	sg_rune_move_law_t law;
	float jump_rise;
	float rocket_rise;
} sg_rune_move_store_t;

/* Derives the shared profiles under the law. */
int SG_RuneMoveStoreInit(sg_rune_move_store_t *store,
	const sg_rune_move_law_t *law);
void SG_RuneMoveStoreFree(sg_rune_move_store_t *store);
void SG_RuneMoveStoreView(const sg_rune_move_store_t *store,
	sg_rune_move_table_t *table_out);

/* Emits every capability admissible for one directed crossing. */
int SG_RuneMoveEmitCrossing(sg_rune_move_store_t *store,
	const sg_rune_move_crossing_t *crossing);

const char *SG_RuneMoveKindString(sg_rune_move_kind_t kind);

/* Every directed portal of a compact geometry, with its crossing facts
 * read from the regions (one region per cell). */
struct sg_rune_compact_geometry_view_s;
struct sg_configuration_semantics_s;
int SG_RuneMoveEmitGeometry(sg_rune_move_store_t *store,
	const struct sg_rune_compact_geometry_view_s *geometry,
	const struct sg_configuration_semantics_s *semantics);

#endif /* SG_RUNE_MOVE_H */
