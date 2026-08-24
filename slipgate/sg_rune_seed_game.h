/* sg_rune_seed_game.h -- exact host-backed RUNE seed canonicalization. */
#pragma once

qboolean SG_RuneSeedGround(vec3_t candidate, vec3_t out);
int SG_RuneSeedSourceWaterlevel(vec3_t origin, int *watertype);
qboolean SG_RuneSeedSourceUnstable(vec3_t origin);
