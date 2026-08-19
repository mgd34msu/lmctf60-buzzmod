/*
 * sg_persona.c -- the casting table.
 *
 * The header argues why this file exists. This one holds the rows and the
 * four-line squeeze that turns an authored trait into a multiplier nobody
 * can abuse.
 */

#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_persona.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_hooks.h"

/* ------------------------------------------------------------------ table
 *
 * Sixteen rows, sg_names order (sg_client.c). The tone column in
 * sg_chat.c's chat_voice[] (sg_chat.c:188) was already a casting decision
 * made by somebody -- four names to a voice, terse / cocky / dry / mech --
 * and these rows are written to AGREE with it. A bot the chat file gives a
 * swaggering line to and the combat file plays as a cautious long-range
 * specialist is two characters wearing one name, which is the thing this
 * whole change exists to stop.
 *
 * So, by voice:
 *
 *   TERSE (arach, trace, ogre, knight)   soldiers. Few words, no theatre.
 *                                        Spread wide on aim: the voice says
 *                                        nothing about competence.
 *   COCKY (caco, slip, fiend, spawn)     the loud ones. Aggressive, short,
 *                                        rope-happy, allergic to standing
 *                                        still, and -- the joke the tone
 *                                        table set up -- not actually the
 *                                        best shots on the roster.
 *   DRY   (rune, phase, wizard, scrag)   the observers. Long, patient,
 *                                        accurate, content to hold a lane.
 *   MECH  (gate, field, vore, shal)      the machines. Methodical, posted,
 *                                        quiet, and utterly unbothered.
 *
 * aim_offset sums to ZERO across the sixteen, which is deliberate: the mean
 * grade stays 2.0, exactly where (ci * 7) % 5 put it, so turning personas
 * on redistributes the roster's skill without making the team as a whole
 * better or worse than the formula it replaced. Any future edit to this
 * column should keep that sum, or say in its commit message why the roster
 * is now meant to be stronger.
 */
/*
 * ROW ORDER IS sg_names ORDER, NOT VOICE ORDER. The bind is
 * sg_personas[slot & 15] against a name of sg_names[slot & 15], so the two
 * tables are one table written twice and any reshuffle here silently hands
 * a bot somebody else's character. This was written grouped by voice first
 * and caught before it ever ran -- Caco would have played the match as
 * Trace, a swaggering line over a patient long-range game -- so the voice
 * is a COLUMN below, and the order is left alone. SG_PersonaBind checks
 * the two names agree at every join and says so on the debug channel if
 * they ever stop agreeing.
 */
static const sg_persona_t sg_personas[16] =
{
	/*  name       aim  aggr  range  hook  camp  banter    voice / character */

	/* the standard candle -- every number the middle, on purpose, so the
	 * others have something to be different FROM */
	{ "Arach",   0, 1.00f, 1.00f, 1.00f, 0.45f, 0.60f },   /* terse */

	/* narrates his own highlight reel, hit or miss */
	{ "Caco",    0, 1.30f, 0.95f, 1.40f, 0.20f, 1.45f },   /* cocky */

	/* picks the shot the map had already offered him */
	{ "Rune",   +2, 0.75f, 1.30f, 0.85f, 0.70f, 0.85f },   /* dry   */

	/* would rather miss on a rope than hit on foot */
	{ "Slip",    0, 1.25f, 1.05f, 1.50f, 0.15f, 1.30f },   /* cocky */

	/* the door is closed; the door stays closed */
	{ "Gate",   +1, 0.70f, 1.05f, 0.65f, 0.95f, 0.55f },   /* mech  */

	/* arrives from the one direction nobody was watching */
	{ "Phase",   0, 0.95f, 1.10f, 1.25f, 0.35f, 0.90f },   /* dry   */

	/* covers the approach, not the stand -- the corpus's own doctrine */
	{ "Field",  -1, 0.85f, 1.20f, 0.80f, 0.80f, 0.50f },   /* mech  */

	/* reads the route and is already standing at the end of it */
	{ "Trace",  +2, 0.80f, 1.25f, 0.75f, 0.75f, 0.50f },   /* terse */

	/* slow, deliberate, and arrives carrying everything */
	{ "Vore",   -2, 0.90f, 1.10f, 0.55f, 0.65f, 0.60f },   /* mech  */

	/* no plan survives contact, because there was never a plan */
	{ "Fiend",  -1, 1.50f, 0.70f, 1.20f, 0.10f, 1.25f },   /* cocky */

	/* never on the floor, never quite committed */
	{ "Scrag",  -1, 1.05f, 1.15f, 1.45f, 0.25f, 0.95f },   /* dry   */

	/* walks in swinging; the rope is for people in a hurry */
	{ "Ogre",   -1, 1.35f, 0.75f, 0.60f, 0.30f, 0.55f },   /* terse */

	/* closes the distance and does not chatter about it */
	{ "Knight", -1, 1.20f, 0.80f, 1.00f, 0.35f, 0.45f },   /* terse */

	/* the lane is his and he has all night */
	{ "Wizard", +2, 0.65f, 1.35f, 0.70f, 0.85f, 0.80f },   /* dry   */

	/* loudest gun, worst aim, best mood on the server */
	{ "Spawn",  -2, 1.40f, 0.85f, 1.35f, 0.20f, 1.50f },   /* cocky */

	/* efficient to the point of rudeness */
	{ "Shal",   +2, 1.10f, 0.90f, 0.90f, 0.50f, 0.50f },   /* mech  */
};

/* ------------------------------------------------------------------ bind */

#define SG_PERSONA_MAXCLIENTS	256		/* as sg_combat.c:372 sizes its own */

static const sg_persona_t	*persona_of[SG_PERSONA_MAXCLIENTS];

static cvar_t				*sg_persona;

/*
 * The cvar POINTER is resolved once -- sg_host.cvar walks the engine's list on
 * every call and this is read several times per engaged bot per frame --
 * while the VALUE is read fresh, so flipping sg_persona mid-match takes
 * effect on the next frame. Same treatment sg_bot_skill gets in
 * sg_combat.c:475-478, and for the same reason.
 */
static qboolean Persona_Enabled(void)
{
	if (!sg_persona)
		sg_persona = sg_cv.persona;
	return (qboolean)(sg_persona && sg_persona->value != 0.0f);
}

static int Persona_Index(edict_t *ent)
{
	int ci;

	if (!ent || !ent->client)
		return -1;
	/*
	 * A client index is recycled: the slot a bot left can come back as a
	 * human, and a stale row read by a human would be a persona nobody
	 * cast. FL_BOT is the cheap half of the guard and the join-time rebind
	 * is the other half -- a slot that comes back as a bot is bound again
	 * before anything reads it.
	 */
	if (!(ent->flags & FL_BOT))
		return -1;
	ci = (int)(ent->client - game.clients);
	if (ci < 0 || ci >= SG_PERSONA_MAXCLIENTS)
		return -1;
	return ci;
}

void SG_PersonaBind(edict_t *ent, int slot)
{
	int ci = (ent && ent->client)
	         ? (int)(ent->client - game.clients) : -1;

	if (ci < 0 || ci >= SG_PERSONA_MAXCLIENTS)
		return;
	/* the roster is sixteen and the slot allocator is sixteen wide, but
	 * the mask is written out rather than assumed -- sg_names is indexed
	 * by this same selected row at the join (sg_client.c) and the two have to
	 * agree or the name and the character come apart */
	persona_of[ci] = &sg_personas[slot & 15];

	/*
	 * And here is the check that the two tables still agree. The netname
	 * is "[SG]" plus the sg_names entry, so the persona's name has to
	 * appear inside it; when it does not, this table has been reordered
	 * away from sg_names and every bot on the server is now playing
	 * somebody else. Once per join, on the debug channel, and it names
	 * both halves so the fix is obvious.
	 */
	if (ent->client->pers.netname[0] &&
	    !strstr(ent->client->pers.netname, persona_of[ci]->name))
		sg_host.dprint("slipgate: PERSONA MISMATCH slot %d is \"%s\" but row "
		           "%d is \"%s\" -- sg_personas[] and sg_names[] have "
		           "come out of order\n",
		           slot, ent->client->pers.netname,
		           slot & 15, persona_of[ci]->name);
}

const sg_persona_t *SG_PersonaFor(edict_t *ent)
{
	int ci;

	if (!Persona_Enabled())
		return NULL;
	ci = Persona_Index(ent);
	if (ci < 0)
		return NULL;
	return persona_of[ci];
}

const char *SG_PersonaName(edict_t *ent)
{
	const sg_persona_t *p = SG_PersonaFor(ent);

	return p ? p->name : NULL;
}

/* ---------------------------------------------------------------- traits */

/*
 * The squeeze. An authored trait spans 0.5-1.5 because that is a range a
 * human can reason about while writing a row; what combat is allowed to
 * feel is +/-15%. One function, so the band is one number: widening it
 * means editing this line and admitting to it, not sneaking a fatter
 * coefficient into one call site.
 *
 * centre is the trait value that means "no opinion" -- 1.0 for the four
 * that are written as multipliers, 0.5 for camp_tendency, which is written
 * as a 0-1 willingness.
 */
static float Persona_Squeeze(float trait, float centre, float span)
{
	float f = 1.0f + (trait - centre) * (0.15f / span);

	if (f < 0.85f)
		f = 0.85f;
	if (f > 1.15f)
		f = 1.15f;
	return f;
}

float SG_PersonaAggression(edict_t *ent)
{
	const sg_persona_t *p = SG_PersonaFor(ent);

	return p ? Persona_Squeeze(p->aggression, 1.0f, 0.5f) : 1.0f;
}

float SG_PersonaRangeBias(edict_t *ent)
{
	const sg_persona_t *p = SG_PersonaFor(ent);

	return p ? Persona_Squeeze(p->preferred_range, 1.0f, 0.35f) : 1.0f;
}

float SG_PersonaHookScale(edict_t *ent)
{
	const sg_persona_t *p = SG_PersonaFor(ent);

	return p ? Persona_Squeeze(p->hook_enthusiasm, 1.0f, 0.5f) : 1.0f;
}

float SG_PersonaCampScale(edict_t *ent)
{
	const sg_persona_t *p = SG_PersonaFor(ent);

	return p ? Persona_Squeeze(p->camp_tendency, 0.5f, 0.5f) : 1.0f;
}

int SG_PersonaAimGrade(edict_t *ent)
{
	const sg_persona_t *p = SG_PersonaFor(ent);
	int g;

	if (!p)
		return -1;
	/*
	 * +2 is the team's full skill and -2 is a full point under it, which
	 * is the envelope (ci * 7) % 5 spanned -- the grades are just chosen
	 * now instead of computed. Clamped because a hand-edited row is the
	 * one input to this file a typo can reach.
	 */
	g = 2 - p->aim_offset;
	if (g < 0)
		g = 0;
	if (g > 4)
		g = 4;
	return g;
}

float SG_PersonaBanterFreq(edict_t *ent)
{
	const sg_persona_t *p = SG_PersonaFor(ent);

	return p ? p->banter_freq : 1.0f;
}

/*
 * The chat file indexes bots by client slot, not edict; 0 means "persona
 * is off, use your own fallback" so a persona-less build keeps the
 * coprime chattiness spread instead of sixteen identical 1.0s.
 */
float SG_PersonaBanterFreqSlot(int cl)
{
	if (!sg_cv.persona->value)
		return 0.0f;
	/* cl is a CLIENT index; personas are bound per-client at join
	 * (persona_of), and roster slot != client slot -- the spawn scan
	 * allocates clients high-to-low. Indexing sg_personas[cl] here was
	 * the same off-by-a-mapping the bind guard exists to catch. */
	if (cl < 0 || cl >= SG_PERSONA_MAXCLIENTS || !persona_of[cl])
		return 0.0f;
	return persona_of[cl]->banter_freq;
}
