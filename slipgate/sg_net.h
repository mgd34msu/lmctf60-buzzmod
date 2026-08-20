

#pragma once

/*
 * Install the redirection. Called from GetGameAPI (g_main.c) on the
 * statement immediately after `gi = *import`, before anything in the mod
 * has had a chance to call a gi slot or cache one. Idempotent: a second
 * call is a no-op, because a second call that ran would save the shim's own
 * functions as "the engine" and the first network write would recurse until
 * the stack ran out.
 */
void		SG_NetInstall(void);
void		SG_NetNewLevel(void);


void		SG_NoteSound(edict_t *emitter, vec3_t origin, int channel,
                             int soundindex, float volume, float attenuation);


void		SG_BotClientCommand(int clientIndex, char *arg0, ...);

/*
 * Drop any injected arguments, putting gi.argc / gi.argv / gi.args back on
 * the engine's real console line. Idempotent and safe to call when nothing
 * is pending. g_cmds.c calls it at the tail of ClientCommand's
 * unrecognized-command branch; the injector already clears on its own way
 * out, so that call is belt to this braces.
 */
void		SG_ClearBotArgs(void);

/*
 * Take a client edict for a game-side client, or NULL when the client range
 * is full. The returned edict is in use, classed "noclass", and already
 * pointed at its matching gclient_t -- but the gclient_t itself is NOT
 * cleared, so whatever the slot's previous occupant left behind is still
 * there. The caller's connect sequence is what initializes it.
 */
edict_t		*SG_SpawnClientEdict(void);

/*
 * Park a client edict: invisible, non-solid, not in use, classed
 * "disconnected", client marked not connected. Deliberately overlaps with
 * ClientDisconnect, which the caller runs first; this is the idempotent
 * tail. Faults if ent->client is NULL, which SG_SpawnClientEdict guarantees
 * it is not.
 */
void		SG_FreeClientEdict(edict_t *ent);
