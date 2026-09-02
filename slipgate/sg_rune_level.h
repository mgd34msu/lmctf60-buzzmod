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

/* Point to cell under the level's locator. */
uint32_t SG_RuneLevelLocate(const float origin[3], int crouching,
	float *violation_out);

#endif /* SG_RUNE_LEVEL_H */
