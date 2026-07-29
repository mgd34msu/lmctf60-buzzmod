#ifndef CTF_SQLITE_UNIDB_H
#define CTF_SQLITE_UNIDB_H

// Unified backend: one <gamedir>/players.db shared by every player, all four
// tables keyed on char_idx.
//
// Function names are those recovered from Debug/ctf_sqlite_unidb.obj.

qboolean DB_Conn_Start(void);		// open the shared handle, build schema if new
void     DB_Conn_Cleanup(void);		// close it; safe to call when never opened

int      DB_GetID(const char *playername);	// existing char_idx, or -1
int      DB_NewID(const char *playername);	// allocate a char_idx with base rows

qboolean DB_LoadPlayer(edict_t *player);
qboolean DB_SavePlayer(edict_t *player);

#endif
