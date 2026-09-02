/* The level's RUNE in the game: loaded once per map, checked against the
 * live host's identity and laws, indexed for the bots.  One owner; the
 * driver reads it every frame. */
#ifndef SG_RUNE_LEVEL_H
#define SG_RUNE_LEVEL_H

#include <stdint.h>

#include "sg_rune_artifact.h"
#include "sg_rune_field.h"
#include "sg_rune_locate.h"

#define SG_RUNE_LEVEL_FIELDS 8

typedef struct sg_rune_level_field_s
{
	sg_rune_field_t field;
	uint32_t destination_cell;  /* INDEX_NONE when the slot is empty */
	uint32_t variant;           /* 0 plain; else 1 + exposure index */
	uint64_t last_used_frame;
} sg_rune_level_field_t;

typedef struct sg_rune_level_s
{
	int current;                /* an artifact is loaded and accepted */
	char mapname[64];
	sg_rune_artifact_t artifact;
	sg_rune_locator_t locator;
	sg_rune_router_t router;
	sg_rune_level_field_t fields[SG_RUNE_LEVEL_FIELDS];
	uint64_t frame;
	int *mechanism_edict;       /* per mechanism record: edict index or 0 */
} sg_rune_level_t;

extern sg_rune_level_t sg_rune_level;

/* Loads <gamedir>/maps/<map>.rune and accepts it when its identity and law
 * match the live host.  Returns 1 when current afterwards.  Prints one line
 * either way. */
int SG_RuneLevelBegin(const char *mapname);
void SG_RuneLevelClear(void);
int SG_RuneLevelCurrent(void);

/* The field toward a destination cell, built on first use and kept while
 * it is used.  NULL when nothing is loaded or the cell is out of range. */
const sg_rune_field_t *SG_RuneLevelField(uint32_t destination_cell);

/* The same field, with every cell the defenders of the flag in
 * enemy_flag_cell can fire into (their posts and the flag itself) costing
 * extra: the route an attacker or carrier takes past a guarded base. */
const sg_rune_field_t *SG_RuneLevelFieldExposed(uint32_t destination_cell,
	uint32_t enemy_flag_cell);
void SG_RuneLevelExposureClear(void);

/* The live entity a mechanism record describes (a door, a lift, a pad,
 * a train), bound on first use by its brush model or its position; NULL
 * when it is not in the world. */
struct edict_s *SG_RuneLevelMechanismEdict(uint32_t mechanism);

/* The fire relation from cell to target (sg_rune_fire_flag_t bits), 0 when
 * the rune records none. */
uint32_t SG_RuneLevelFire(uint32_t cell, uint32_t target);

/* A defend post for the flag in flag_cell: slot 0 is the floor cell with a
 * line of fire over the most of the flag's approaches, slot 1 the one that
 * best covers what slot 0 leaves.  point_out is the post; facing_out is
 * where the approaches it covers are, on average.  0 when the rune has no
 * fire relations or nothing covers anything. */
int SG_RuneLevelDefendPost(uint32_t flag_cell, int slot, float point_out[3],
	float facing_out[3], uint32_t *cell_out);

/* The entity text the level was spawned from (after any override file),
 * kept for the identity and for generation on this level. */
void SG_RuneLevelEntities(const char *text);
const char *SG_RuneLevelEntityText(void);

/* Point to cell under the level's locator. */
uint32_t SG_RuneLevelLocate(const float origin[3], int crouching,
	float *violation_out);

#endif /* SG_RUNE_LEVEL_H */
