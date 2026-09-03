/* Generating a map's RUNE from inside the game: on a thread of its own,
 * from the live host's laws and the players' bites beside the map, and
 * published beside the map.  Start it, poll it once a frame; the poll
 * returns 1 the frame a build was published, -1 the frame one failed. */
#ifndef SG_RUNE_GAME_H
#define SG_RUNE_GAME_H

int SG_RuneGameGenerateStart(const char *mapname);
int SG_RuneGameGenerateBusy(void);
int SG_RuneGameGeneratePoll(void);
/* The server command's form: starts the build. */
int SG_RuneGameGenerate(const char *mapname);

#endif /* SG_RUNE_GAME_H */
