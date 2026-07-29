#ifndef CTF_SQLITE_CHARACTER_H
#define CTF_SQLITE_CHARACTER_H

// Per-player backend: one SQLite database per player at
// <gamedir>/players/<name>.ctf, each table holding exactly one row.
//
// The surviving 2020 header declared:
//     qboolean CTF_SavePlayer(edict_t *player, char *path, qboolean fileexists, char *playername);
// The `fileexists` argument is gone. The caller could not know it without
// racing the filesystem, and CTF_SavePlayer has to check for itself anyway.

qboolean CTF_SavePlayer(edict_t *player, const char *path, const char *playername);
qboolean CTF_LoadPlayer(edict_t *player, const char *path);

#endif
