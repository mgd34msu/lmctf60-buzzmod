/*
 * sg_net.h -- SLIPGATE's seat between the mod and the engine.
 *
 * Include AFTER g_local.h. Everything here is spelled in edict_t, int and
 * char *, so any translation unit in the tree can take it without pulling a
 * SLIPGATE-internal type along.
 *
 * WHY THIS MODULE EXISTS AT ALL
 *
 * A SLIPGATE bot is built entirely inside the game library (sg_arach.c,
 * SG_AddBotTeam). The engine is never told about it: there is no
 * svs.clients[] entry, no netchan, no reliable message buffer. That single
 * fact is the root of both halves of this file.
 *
 *   - Anything the engine would do PER RECIPIENT -- a console print, a
 *     center print, a unicast layout -- walks into a zero-sized buffer when
 *     the recipient is one of ours. Stock Quake II answers that with
 *     Com_Error and the server is gone. So the mod's prints and network
 *     writes are routed through this module, which drops per-recipient
 *     traffic aimed at an engine-less client and lets everything else
 *     through untouched.
 *
 *   - The client SLOT still has to come from somewhere, and it has to come
 *     from the end of the range the engine hands out from. That is the
 *     spawn/release pair below.
 *
 * The two halves share this file because they share that premise, not
 * because they share code.
 */

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

/*
 * Inject a console command on a client's behalf and run it through the
 * mod's own ClientCommand, the same entry point the engine uses when a
 * human types. Variadic: client index (0-based, edict 0 is the world),
 * argv(0), then the tail arguments, then a NULL terminator.
 *
 *     SG_BotClientCommand(cl, "say_team", line, NULL);
 *
 * The arguments are visible to gi.argc / gi.argv / gi.args for the duration
 * of the nested ClientCommand call and cleared on the way out, so a bot's
 * chat takes the identical route a human's does -- spam check, name
 * prefixing, SG_ChatHear, broadcast -- rather than arriving by a side
 * channel that skips all of it.
 *
 * Returns nothing: the caller cannot learn whether the command was
 * accepted, because ClientCommand does not say.
 */
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
