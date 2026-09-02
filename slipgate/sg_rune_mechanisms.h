/* Era-4 mechanisms: doors, lifts, buttons, teleporters, pushes, trains.
 *
 * The carve treats the world's brush models as absent, so the complex
 * already has the space a door occupies and the column a lift climbs.
 * This pass reads the map's entities and binds what moves to the cells it
 * moves through: a door's gate cells, so every crossing into them waits on
 * the door; a lift's rest top as floor, and a ride record from it to the
 * ledge at the top; a teleporter's pad to its destination; a push volume
 * to where its launch lands; a train's legs between path corners.  What a
 * mechanism does live (its state, its position) is the runtime's to read
 * from the engine; the record says which entity that is. */
#ifndef SG_RUNE_MECHANISMS_H
#define SG_RUNE_MECHANISMS_H

#include <stdint.h>

#include "sg_rune_movement.h"

typedef enum sg_rune_mech_kind_e
{
	SG_RUNE_MECH_DOOR = 0,
	SG_RUNE_MECH_PLATFORM,
	SG_RUNE_MECH_BUTTON,
	SG_RUNE_MECH_TELEPORTER,
	SG_RUNE_MECH_PUSH,
	SG_RUNE_MECH_TRAIN,
	SG_RUNE_MECH_KIND_COUNT
} sg_rune_mech_kind_t;

typedef enum sg_rune_mech_activation_e
{
	SG_RUNE_MECH_ACTIVATE_TOUCH = 0,   /* walking into it works it */
	SG_RUNE_MECH_ACTIVATE_TARGETED,    /* another mechanism works it */
	SG_RUNE_MECH_ACTIVATE_SHOT,        /* damage works it */
	SG_RUNE_MECH_ACTIVATE_NONE,        /* nothing the bot can do */
	SG_RUNE_MECH_ACTIVATE_COUNT
} sg_rune_mech_activation_t;

typedef struct sg_rune_mech_s
{
	uint8_t kind;             /* sg_rune_mech_kind_t */
	uint8_t activation;       /* sg_rune_mech_activation_t */
	uint8_t reserved[2];
	int32_t bmodel;           /* the brush model index, or -1 for a point */
	uint32_t entity;          /* canonical entity ordinal in the map */
	uint32_t activator;       /* mechanism that works it, or INDEX_NONE */
	float origin[3];          /* rest position (bmodel: its origin) */
	float mins[3];            /* rest bounds, world space */
	float maxs[3];
	float travel[3];          /* door: open offset; lift: rise; push: launch */
	float speed;
	float wait;
	uint32_t first_cell;      /* gate cells, into mechanism_cells */
	uint32_t cell_count;
} sg_rune_mech_t;

typedef struct sg_rune_mech_table_s
{
	const sg_rune_mech_t *records;
	uint32_t record_count;
	const uint32_t *cells;
	uint32_t cell_count;
} sg_rune_mech_table_t;

typedef struct sg_rune_mech_store_s
{
	sg_rune_mech_t *records;
	uint32_t record_count, record_capacity;
	uint32_t *cells;
	uint32_t cell_count, cell_capacity;
} sg_rune_mech_store_t;

void SG_RuneMechStoreInit(sg_rune_mech_store_t *store);
void SG_RuneMechStoreFree(sg_rune_mech_store_t *store);
void SG_RuneMechStoreView(const sg_rune_mech_store_t *store,
	sg_rune_mech_table_t *table_out);

struct sg_bsp_world_s;
struct sg_rune_cx_s;
struct sg_rune_law_s;

/* Pass one, before movement emission: stamps the cells (a lift's rest top
 * is floor; a door's closed volume is a gate).  Pass two, after movement
 * emission: appends the records and the crossings the mechanisms make, and
 * gates the crossings into doors.  Both read the map's entities from the
 * world. */
int SG_RuneMechMarkCells(const struct sg_bsp_world_s *bsp,
	struct sg_rune_cx_s *cx);
int SG_RuneMechEmit(const struct sg_bsp_world_s *bsp,
	const struct sg_rune_cx_s *cx, const struct sg_rune_law_s *law,
	sg_rune_move_store_t *movement, sg_rune_mech_store_t *store);

const char *SG_RuneMechKindString(sg_rune_mech_kind_t kind);

#endif /* SG_RUNE_MECHANISMS_H */
