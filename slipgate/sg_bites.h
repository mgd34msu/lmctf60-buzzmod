/* sg_bites.h -- the players' rope bites, gathered from play.
 *
 * While humans play, every rope that bites is remembered with the spot it
 * was fired from, and the map's bites file (maps/<map>.bites, the same
 * file the demo tools write) gains what it did not have.  A rune is built
 * with the bites of its day; when the file has grown enough since, the
 * next load builds it again. */
#ifndef SG_BITES_H
#define SG_BITES_H

void SG_BitesLevelBegin(const char *mapname);   /* read the map's file */
void SG_BitesNote(edict_t *ent);                /* a human's rope, each frame */
void SG_BitesFlush(int force);                  /* write when changed */
/* Whether the bites file has grown enough past what the rune was built
 * with to be worth a new build. */
int SG_BitesGrown(const char *mapname);
/* Written by the generator beside the rune: the count it was built with. */
void SG_BitesWriteCountFor(const char *rune_path, const char *bites_path);

#endif /* SG_BITES_H */
