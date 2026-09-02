/* The server command "sv rune": generate this map's RUNE from the live
 * host's laws and publish it beside the map.  The shipped module refuses;
 * the generator module does the work. */
#ifndef SG_RUNE_GAME_H
#define SG_RUNE_GAME_H

int SG_RuneGameGenerate(const char *mapname);

#endif /* SG_RUNE_GAME_H */
