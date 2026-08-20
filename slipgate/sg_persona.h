

#pragma once

/*
 * One row per sg_names entry, in sg_names order (sg_client.c). The
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
 * Bind a persona to the exact name-row selected by the join path once the
 * client index is real (sg_client.c, SG_AddBotTeam). Binding by the selected
 * row rather than reparsing the name means the table is consulted once per
 * join, and a collision skip moves the displayed name and behavior together.
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
