/* sg_snag_repair.h -- map-local, static routing surcharges from stall census. */
#ifndef SG_SNAG_REPAIR_H
#define SG_SNAG_REPAIR_H

#include "sg_rune.h"

/* Every published production graph has an explicit RUNE-bound snag file.  A
 * zero-repair file states that no authenticated repair evidence was accepted;
 * it does not claim that the map is clean.  Missing or invalid input fails
 * closed before Fields_Setup publishes any fields. */
qboolean SG_SnagRepairLoadForLevel(const rune_t *rune, const char *game_dir);
qboolean SG_SnagRepairLoadFile(const rune_t *rune, const char *path);
int SG_SnagRepairSeedSurcharge(int seed);
int SG_SnagRepairLinkSurcharge(int link);
void SG_SnagRepairClear(void);

#endif
