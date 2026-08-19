/* sg_snag_repair.h -- map-local, static routing surcharges from stall census. */
#ifndef SG_SNAG_REPAIR_H
#define SG_SNAG_REPAIR_H

#include "sg_rune.h"

/* Missing input is intentionally neutral.  A present but invalid input fails
 * closed: Fields_Setup must not publish a graph with untrusted repairs. */
qboolean SG_SnagRepairLoadForLevel(const rune_t *rune, const char *game_dir);
qboolean SG_SnagRepairLoadFile(const rune_t *rune, const char *path);
int SG_SnagRepairSeedSurcharge(int seed);
int SG_SnagRepairLinkSurcharge(int link);
void SG_SnagRepairClear(void);

#endif
