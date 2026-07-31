/*
 * bl_know.h -- what a bot is allowed to know, and how it tells its team.
 *
 * The bot code runs inside the game DLL, so every bot can trivially read
 * redflag->s.origin and win every chase. A human cannot. This module is the
 * filter that stands between the two: a bot may only act on a position it
 * could have obtained the way a player does -- off the HUD, with its own
 * eyes, or from a teammate's call-out.
 *
 * The split follows what the HUD actually draws (see the STAT_RED_ICON /
 * STAT_BLUE_ICON block in p_hud.c). The HUD tells everyone the *state* of
 * each flag -- home, dropped, or taken -- and nothing more. So flag state is
 * free knowledge and flag *position* is not, except in the home case where
 * the state and the position are the same fact.
 *
 * Nothing here moves a bot. These are questions the bot AI asks before it
 * decides where to go; a "false" answer means the bot has to go look.
 */
#pragma once

/* clear every bot's memory; call once per map load */
void Know_LevelInit(void);

/* drop what this client knew, and what others knew about it as a carrier */
void Know_ClientDisconnect(edict_t *ent);

/* run the sight sweeps and let bots speak; call once per server frame */
void Know_Frame(void);

/*
 * Where does this bot believe team 'teamnum's flag is? True with the
 * position in 'out', or false when the bot genuinely has no idea and would
 * have to go and look. Answers for a flag at home, a flag lying where the
 * bot saw it fall, and a carried flag whose carrier the bot has eyes on.
 */
qboolean Know_FlagPosition(edict_t *bot, int teamnum, vec3_t out);

/*
 * Where is the enemy who is holding *this bot's* flag -- the one worth
 * chasing. False when no one holds it, or when the bot has lost track.
 */
qboolean Know_EnemyCarrier(edict_t *bot, vec3_t out);

/* nearest rune the bot has seen or been told about, if any */
qboolean Know_RunePosition(edict_t *bot, vec3_t out);
