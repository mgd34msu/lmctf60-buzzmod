/* Era-4 fire relations: from which cells which cells can be hit, and how.
 *
 * For every floor cell, the floor cells in the clusters the map says it
 * may see are traced from its eye: a clear line to the other eye (rays:
 * rail, chaingun, shotguns), a clear corridor for a projectile's body to
 * the other body (blaster, rocket, hyperblaster), a rocket at the other
 * feet whose burst reaches them (blast), and a grenade arc that lands within
 * its burst of them when no line exists (lob).  The runtime reads the flags
 * for the pair it is in: which families can work from here on there, where
 * a defender should stand to cover an approach, which cells an attacker is
 * exposed in.  Every relation is an exact trace or an exact arc; none is
 * sampled or learned. */
#ifndef SG_RUNE_FIRE_H
#define SG_RUNE_FIRE_H

#include <stdint.h>

typedef enum sg_rune_fire_flag_e
{
	SG_RUNE_FIRE_LINE = 1U << 0,      /* eye to eye clear */
	SG_RUNE_FIRE_CORRIDOR = 1U << 1,  /* a projectile's body reaches the body */
	SG_RUNE_FIRE_BLAST = 1U << 2,     /* a rocket bursts within radius of the feet */
	SG_RUNE_FIRE_LOB = 1U << 3        /* a grenade arc lands within radius of the feet */
} sg_rune_fire_flag_t;

typedef struct sg_rune_fire_s
{
	uint32_t target;          /* cell */
	uint32_t flags;           /* sg_rune_fire_flag_t bits */
} sg_rune_fire_t;

/* Relations are traced between representative floor cells (those with
 * some standing room); every other floor cell borrows the row of the
 * representative nearest it, and a target resolves to its representative
 * the same way. */
typedef struct sg_rune_fire_cell_s
{
	uint32_t first;           /* into records, sorted by target */
	uint32_t count;
	uint32_t representative;  /* the cell whose row this is; INDEX_NONE for none */
} sg_rune_fire_cell_t;

typedef struct sg_rune_fire_table_s
{
	const sg_rune_fire_cell_t *cells;   /* one per complex cell */
	uint32_t cell_count;
	const sg_rune_fire_t *records;
	uint32_t record_count;
} sg_rune_fire_table_t;

typedef struct sg_rune_fire_store_s
{
	sg_rune_fire_cell_t *cells;
	uint32_t cell_count;
	sg_rune_fire_t *records;
	uint32_t record_count, record_capacity;
} sg_rune_fire_store_t;

typedef struct sg_rune_fire_report_s
{
	uint32_t sources;         /* floor cells traced from */
	uint32_t pairs;           /* pairs the map allowed */
	uint32_t records;
	uint32_t line, corridor, blast, lob;
	uint32_t traces;
	uint32_t arcs;
} sg_rune_fire_report_t;

void SG_RuneFireStoreInit(sg_rune_fire_store_t *store);
void SG_RuneFireStoreFree(sg_rune_fire_store_t *store);
void SG_RuneFireStoreView(const sg_rune_fire_store_t *store,
	sg_rune_fire_table_t *table_out);

struct sg_rune_bsp_s;
struct sg_rune_cx_s;
struct sg_rune_law_s;

typedef void (*sg_rune_fire_progress_fn)(void *context, uint32_t done,
	uint32_t total);

int SG_RuneFireEmit(const struct sg_rune_bsp_s *bsp,
	const struct sg_rune_cx_s *cx, const struct sg_rune_law_s *law,
	sg_rune_fire_store_t *store, sg_rune_fire_progress_fn progress,
	void *progress_context, sg_rune_fire_report_t *report_out);

/* The relation from cell to target, or 0 when none is recorded. */
uint32_t SG_RuneFireFlags(const sg_rune_fire_table_t *table, uint32_t cell,
	uint32_t target);

#endif /* SG_RUNE_FIRE_H */
