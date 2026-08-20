#ifndef CTF_SQLITE_CHARACTER_H
#define CTF_SQLITE_CHARACTER_H

// Per-player SQLite backend at <gamedir>/players/<name>.ctf.

qboolean CTF_SavePlayer(edict_t *player, const char *path, const char *playername);
qboolean CTF_LoadPlayer(edict_t *player, const char *path);

#endif
