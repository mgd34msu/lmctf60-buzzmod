#ifndef SG_RUNE_COMPACT_GAME_H
#define SG_RUNE_COMPACT_GAME_H

/* The offline generator game module builds and atomically publishes the
 * accepted compact artifact for the authenticated level. The shipped runtime
 * module keeps the same command boundary but fails closed instead of linking
 * construction or solver code. */
int SG_RuneCompactGameGenerate(const char *mapname);

#endif /* SG_RUNE_COMPACT_GAME_H */
