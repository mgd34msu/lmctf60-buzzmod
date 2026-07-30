/*
 * bl_ctf.h -- CTF integration for the bots.
 *
 * The bot glue came from a Zoid-CTF codebase, where the team a bot joins is
 * passed in the "ctfteam" userinfo key. LMCTF does not read that key, so the
 * cvar the glue exposed (botctfteam) had no effect at all -- and the write was
 * inside #ifdef ZOID, which this mod does not define, so it was not even
 * compiled.
 *
 * Bots were still getting a team, because LMCTF's own ClientBegin sends any
 * client that arrives with CTF_TEAM_UNDEFINED through TeamJoin/Team_To_Join.
 * What was missing was the ability to *direct* a bot to a particular team.
 * That is what this file adds, and it is what the bot menu drives.
 */
#pragma once

/* number of clients currently on a team, bots included */
int  BotCTFTeamSize(int team);

/* bots currently in the game, and the n'th of them (0-based) */
int  BotCountInGame(void);
edict_t *BotByListIndex(int n);

/*
 * Called from BotStarted just before ClientBegin. When botctfteam names a
 * team, that team is written straight to the client so ClientBegin's
 * "already on a team" branch keeps it. When it does not, the field is left
 * alone and LMCTF balances the bot itself.
 */
void BotCTFAssignTeam(edict_t *bot);

/* remove every bot in the game; returns how many were removed */
int  BotRemoveAll(void);

/* add a named bot from the bot file; false if the name is not in it */
qboolean BotAddNamed(char *name);

/*
 * Whether bot performance is written to the stats database. Off by default --
 * a server that runs bots to fill out a game does not usually want them in
 * the leaderboards, but a server that plays bots seriously might.
 */
qboolean BotStatsEnabled(void);
