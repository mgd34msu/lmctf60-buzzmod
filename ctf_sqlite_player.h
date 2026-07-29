#ifndef CTF_SQLITE_CHARACTER_H
#define CTF_SQLITE_CHARACTER_H

qboolean CTF_SavePlayer(edict_t *player, char *path, qboolean fileexists, char *playername);
qboolean CTF_LoadPlayer(edict_t *player, char *path);

#endif
