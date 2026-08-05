/*
 * sg_persona.h -- who each bot IS, as data.
 *
 * Include AFTER g_local.h. Everything here is spelled in edict_t and float,
 * so any translation unit can take it without pulling a SLIPGATE-internal
 * type along.
 *
 * WHY THIS MODULE EXISTS AT ALL
 *
 * Skill used to be arithmetic on a client index:
 *
 *     s = team - ((client_index * 7) % 5) * 0.25
 *
 * -- five grades of ONE trait curve. Every bot wanted the same range, took
 * the same fights, threw the same ropes and held the same posts; the only
 * thing that varied was how well it did all of it. Sixteen names on the
 * scoreboard and one bot behind them, sixteen times over. The census called
 * that gap 7, and the roster naming its own bots (sg_arach.c:6704, "names
 * are identity; identity is data") was the argument for finishing the job:
 * a name that indexes nothing is decoration.
 *
 * So the modulo is replaced by a hand-authored row per name. aim_offset
 * carries the old skill spread -- the SAME envelope, team-1.0 to team, just
 * chosen rather than computed -- and the other five fields are the traits
 * the formula never had. The rows are authored, not fitted: SLIPGATE's rule
 * is that facts are measured and only preferences are fitted, and which bot
 * is the loud one is neither. It is a casting decision, and it is made in
 * the table where it can be read.
 *
 * WHAT THE TRAITS ARE ALLOWED TO DO
 *
 * Bend behaviour, never invent it. Every consumer scales an existing number
 * by at most +/-15%, which is the band where a trait reads as character
 * across a match and never as a second, worse bot: an aggressive bot picks
 * its fights a little farther out, it does not stop taking cover. Anything
 * that wanted a new branch belongs in the system that owns the behaviour,
 * not here.
 *
 * sg_persona 0 turns the whole thing off: every consumer falls back to the
 * exact expression it used before this file existed, byte for byte.
 */

#pragma once

/*
 * One row per sg_names entry, in sg_names order (sg_arach.c:6704). The
 * bind is by SLOT, so the order of this table and the order of that one
 * are the same fact stated twice -- and stating a fact twice is how it
 * comes to be stated two different ways. The name string is carried here
 * for exactly that reason: SG_PersonaBind compares it against the name the
 * bot actually joined under and complains on the debug channel if the two
 * tables have drifted out of order.
 */
typedef struct
{
	const char	*name;				/* the sg_names entry this row is for */

	int			aim_offset;			/* -2..+2 grades off the team level;
									 * +2 is the team's full skill, -2 is a
									 * full point below it -- the same
									 * envelope the modulo spanned */
	float		aggression;			/* 0.5-1.5: willingness to start it */
	float		preferred_range;	/* <1 short, 1 mid, >1 long */
	float		hook_enthusiasm;	/* 0.5-1.5: appetite for the optional rope */
	float		camp_tendency;		/* 0-1: willingness to hold a post */
	float		banter_freq;		/* 0.5-1.5: how often it has something to say */
} sg_persona_t;

/*
 * Bind a persona to a bot's client slot. Called from the join path once the
 * client index is real (sg_arach.c, SG_AddBotTeam). Binding by slot rather
 * than by name means the table is consulted once per join instead of once
 * per read, and a roster edit moves both halves together.
 */
void SG_PersonaBind(edict_t *ent, int slot);

/*
 * The bound row, or NULL: not a bot, never bound, or sg_persona 0. Every
 * caller treats NULL as "do what the code did before", which is what makes
 * the cvar a true off switch rather than a different set of numbers.
 */
const sg_persona_t *SG_PersonaFor(edict_t *ent);

/* the persona's name, or NULL -- for the debug join print */
const char *SG_PersonaName(edict_t *ent);

/*
 * The traits, as the multipliers their consumers actually want. The +/-15%
 * squeeze lives in here rather than at the five call sites, so the band is
 * one number in one file and a site cannot quietly widen it.
 *
 * All four return exactly 1.0f when no persona applies, so
 *
 *     x * SG_PersonaAggression(self)
 *
 * is the identity when the feature is off -- no branch at the call site,
 * nothing to keep in sync with the fallback path.
 */
float SG_PersonaAggression(edict_t *ent);	/* engagement willingness */
float SG_PersonaRangeBias(edict_t *ent);	/* wanted engagement distance */
float SG_PersonaHookScale(edict_t *ent);	/* the optional rope's bar */
float SG_PersonaCampScale(edict_t *ent);	/* the post's pin radius */

/*
 * The aim grade to subtract from the team skill, 0-4, or -1 when no persona
 * applies and the caller should run its own arithmetic. Not a multiplier
 * because skill is not scaled anywhere -- it is a level, and the table names
 * which one.
 */
int SG_PersonaAimGrade(edict_t *ent);

/*
 * Raw 0.5-1.5, unsqueezed, 1.0 when no persona applies. Chat volume is not
 * a combat number -- a bot that talks 50% more is funny, a bot that shoots
 * 50% better is broken -- so this one is handed over as authored and
 * sg_chat.c decides what a "frequency" means to it. Consumed nowhere yet.
 */
float SG_PersonaBanterFreq(edict_t *ent);
float SG_PersonaBanterFreqSlot(int cl);
