#pragma once

//===========================================================================
//
// Name:         sg_redirgi.h
// Function:     bot setup
// Programmer:   Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:  1999-02-10
// Tab Size:     3
//===========================================================================
//
// SLIPGATE port of bl_redirgi.h. See sg_redirgi.c for why this layer outlived
// the legacy Quake II bot it shipped with: GetGameAPI installs it over the
// global gi, so the whole game runs through it, and SLIPGATE relies on its
// argv redirection (bot chat), its FL_BOT unicast drop and its FL_BOT print
// suppression.
//
// G_SpawnClient and G_FreeClientEdict came from bl_spawn.c. sg_arach.c calls
// both to seat and release its bots -- it does not merely mirror them, despite
// what its own header comment says -- so they moved here rather than going
// away with the rest of the legacy bot.
//===========================================================================

#define MAX_MODELINDEXES				256
#define MAX_SOUNDINDEXES				256
#define MAX_IMAGEINDEXES				256

//global botimport structure
extern game_import_t newgameimport;
//model index
extern char *modelindexes[MAX_MODELINDEXES];
//sound index
extern char *soundindexes[MAX_SOUNDINDEXES];
//image index
extern char *imageindexes[MAX_IMAGEINDEXES];

//initializes the newgameimport structure
void BotRedirectGameImport(void);
//execute a client command but now for a bot
void BotClientCommand(int client, char *str, ...);
//execute a server command
void BotServerCommand(char *str, ...);
//only stores a client command, does not execute it
void BotStoreClientCommand(char *str, ...);
//clears the bot command arguments
void BotClearCommandArguments(void);
//get the cvar with the given name
//cvar_t *BotGet_cvar(char *var_name);
//clears the model and sound index
void ClearIndexes(void);
//initializes the muzzleflash to sound index table
void BotInitMuzzleFlashToSoundindex(void);
//dumps the model index
void BotDumpModelindex(void);
//dumps the sound index
void BotDumpSoundindex(void);
//dumps the image index
void BotDumpImageindex(void);

//spawns a free client edict and hooks up its gclient_t (was bl_spawn.c)
edict_t *G_SpawnClient(void);
//releases a client edict spawned by G_SpawnClient (was bl_spawn.c)
void G_FreeClientEdict(edict_t *ent);

#ifdef TOURNEY
#define TECH1_MODEL	"models/ctf/resistance/tris.md2"
#define TECH2_MODEL	"models/ctf/strength/tris.md2"
#define TECH3_MODEL	"models/ctf/haste/tris.md2"
#define TECH4_MODEL	"models/ctf/regeneration/tris.md2"
#define TECH5_MODEL	"models/ctf/vampire/tris.md2"

#define TECH1_INDEX	251
#define TECH2_INDEX	252
#define TECH3_INDEX	253
#define TECH4_INDEX	254
#define TECH4_INDEX	255
#endif //TOURNEY
