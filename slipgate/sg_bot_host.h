/* sg_bot_host.h -- bots as clients of a game that never sees a socket.
 *
 * A bot occupies a client slot with no network side.  Everything the game
 * sends to a client goes through the import table, so this installs its
 * own functions there: a message staged for a bot alone is discarded
 * instead of reaching the engine (it would corrupt the next real client's
 * stream), prints to a bot go nowhere, sounds are heard by the bots
 * before the engine plays them, and a command a bot issues is handed to
 * the game's command handler with its own argument list.  Everything for
 * a real client passes through untouched. */
#ifndef SG_BOT_HOST_H
#define SG_BOT_HOST_H

/* Installs the functions over the import table once per module load. */
void SG_BotHostInstall(void);
/* At level spawn, before the level struct is cleared. */
void SG_BotHostNewLevel(void);

/* A free client slot as a bot's edict, or NULL; and its release. */
edict_t *SG_BotHostSpawnClient(void);
void SG_BotHostFreeClient(edict_t *ent);

/* A command from a bot, as if typed: argument list ends with NULL. */
void SG_BotHostCommand(int client_index, char *arg0, ...);
void SG_BotHostClearArgs(void);

#endif /* SG_BOT_HOST_H */
