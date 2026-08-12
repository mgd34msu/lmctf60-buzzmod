/*
 * sg_clock.c -- clockplay: score posture from public information only.
 * Moved verbatim from sg_arach.c in the 2026-08-11 standards pass.
 */
#include "g_local.h"
#include "g_ctffunc.h"
#include "g_tourney.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_util.h"
#include "slipgate/sg_clock.h"

/* ------------------------------------------------------------- clockplay */

/*
 * MATCH CONTEXT (sg_clockplay, default 0 = off).
 *
 * No bot in this tree has ever known the score or the clock. It shows on
 * film: minute 19 is played exactly the way minute 2 was played. Two caps
 * down with ninety seconds left, the quota still parks two bodies on a
 * stand that nothing can be lost from -- losing is already decided, and
 * the garrison is guarding a result. One cap up with ninety seconds left,
 * the same quota sends the same four bodies into the enemy base to hand
 * back the game. Humans do not play either of those: a lead buys
 * insurance, a deficit spends it, and a tie in the last minute makes
 * everybody an attacker.
 *
 * Two numbers, read once a second, and one LATCHED posture per team:
 *
 *   AHEAD LATE    >= 1 cap up, under a quarter of the clock left. One more
 *                 body on defence, and the carrier prices cover 1.3x. A
 *                 lead is a thing you carry home, not a thing you spend.
 *   BEHIND LATE   >= 1 cap down, same window. One body OFF defence and
 *                 into the enemy base, and the carrier prices cover 0.8x:
 *                 the flag has to arrive before the horn, and a safe route
 *                 that lands late is worth what no route at all is worth.
 *   CLOSE LATE    level, inside the last tenth. BOTH sides push the extra
 *                 attacker -- the overtime scramble, where the only losing
 *                 move is the one that ends it nil-nil.
 *
 * The posture latches on a 15-second tick. Evaluated per frame it would
 * chatter across the 25% line and across every cap that ties the game, and
 * the defensive quota is the exact input the wave-285 flap census convicted
 * for doing that (600 flips in 600 samples on it18). A strategy that
 * changes its mind four times a second is not a strategy.
 *
 * WHERE THE SCORE COMES FROM. The team capture total is summed per player
 * from STATS_CAPTURES -- where the scoreboard gets it (p_hud.c:329 red,
 * p_hud.c:399 blue), where the match result gets it (g_tourney.c:126), and
 * where a capture is credited in the first place (p_stats.c:305). There is
 * no ctfgame struct in this mod holding a team's caps; the players ARE the
 * counter. The globals named redscore/bluescore look like the answer and
 * are not: they are summed player SCORE, rebuilt every frame from
 * STATS_SCORE (g_main.c:807-828), so they move on every frag. A lead read
 * off them would call a fragfest a rout and go turtle two caps down.
 *
 * WHERE THE CLOCK COMES FROM. timelimit against level.time, the same pair
 * that ends the map (g_main.c:704). Match mode runs its own countdown on a
 * spawned timer entity and skips that check entirely (g_main.c:689,
 * g_tourney.c:93), so level.time there is counting warmup as well as play
 * and the fraction would be a lie -- the feature reads the clock as unknown
 * and stays neutral rather than guessing. Same on a server with no
 * timelimit at all. Unknown clock, no posture, byte-identical play.
 */

enum {
	SG_CLOCK_EVEN = 0,      /* no posture: early, unknown clock, or off */
	SG_CLOCK_AHEAD_LATE,
	SG_CLOCK_BEHIND_LATE,
	SG_CLOCK_CLOSE_LATE
};

#define SG_CLOCK_LATCH   15.0f  /* seconds between posture re-evaluations */
#define SG_CLOCK_LATE    0.25f  /* "late": fraction of the match remaining */
#define SG_CLOCK_ENDGAME 0.10f  /* "the last tenth", for the level scramble */

static int		sg_clock_posture[2];    /* per team, indexed team - 1 */
static int		sg_clock_caps[2];       /* team captures, as of this second */
static float	sg_clock_left;          /* fraction of the match remaining */
static qboolean	sg_clock_known;         /* is that fraction meaningful at all */
static float	sg_clock_read_next;     /* next once-a-second context read */
static float	sg_clock_latch_next;    /* next posture latch */

static const char *sg_clock_names[4] = {
	"even", "ahead-late", "behind-late", "close-late"
};

/*
 * The team's captures, summed the way every other consumer in the tree sums
 * them. A capper who disconnects takes his captures off the board with him,
 * which is correct: the scoreboard the humans are reading does the same.
 */
static int Clock_Caps(int team)
{
	int total = 0, i;

	for (i = 0; i < game.maxclients; i++)
	{
		edict_t *e = g_edicts + 1 + i;

		if (!e->inuse || !e->client)
			continue;
		if (e->client->ctf.teamnum != team)
			continue;
		total += (int)stats_get(e, STATS_CAPTURES);
	}
	return total;
}

/*
 * The defensive-share shift, in bodies. Positive protects a lead, negative
 * spends one on the enemy stand. Zero whenever the feature is off, the
 * clock is unknown, or it is not late yet -- which is what keeps the
 * default byte-identical with the pre-clockplay tree.
 */
int Clock_DefendShift(int team)
{
	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return 0;

	switch (sg_clock_posture[team - 1])
	{
	case SG_CLOCK_AHEAD_LATE:	return 1;   /* one more on the stand */
	case SG_CLOCK_BEHIND_LATE:	return -1;  /* all in */
	case SG_CLOCK_CLOSE_LATE:	return -1;  /* the overtime scramble */
	default:					return 0;
	}
}

/*
 * What a clean corner is worth to a carrier right now. The route book does
 * not change with the scoreline; the PRICE of arriving alive versus
 * arriving at all does.
 */
float Clock_CoverScale(int team)
{
	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return 1.0f;

	switch (sg_clock_posture[team - 1])
	{
	case SG_CLOCK_AHEAD_LATE:	return 1.3f;
	case SG_CLOCK_BEHIND_LATE:	return 0.8f;
	default:					return 1.0f;
	}
}

/*
 * Once per frame, cheap: the context read is gated to a second and the
 * posture latch to fifteen. Runs before any bot thinks (SG_RunFrame), so
 * every bot on a team decides from the same posture on the same frame.
 */
void Clock_Frame(void)
{
	int t;

	if (sg_cv.clockplay->value <= 0.0f)
	{
		/*
		 * Off is OFF, immediately. A latched posture must not outlive
		 * the cvar going to 0 mid-map -- an admin turning the feature
		 * off would otherwise leave a team leaning for another fifteen
		 * seconds with nothing left to explain why.
		 */
		sg_clock_posture[0] = sg_clock_posture[1] = SG_CLOCK_EVEN;
		sg_clock_latch_next = 0.0f;
		sg_clock_read_next = 0.0f;
		sg_clock_known = false;
		sg_clock_left = 0.0f;
		return;
	}

	if (SG_TimerReady(sg_clock_read_next))
	{
		SG_TimerArm(&sg_clock_read_next, 1.0f);
		sg_clock_caps[0] = Clock_Caps(CTF_TEAM_RED);
		sg_clock_caps[1] = Clock_Caps(CTF_TEAM_BLUE);

		sg_clock_known = (timelimit->value > 0.0f && !Match_Mode());
		sg_clock_left = 0.0f;
		if (sg_clock_known)
		{
			float total = timelimit->value * 60.0f;
			float left = SG_TimerRemaining(total) / total;

			sg_clock_left = (left < 0.0f) ? 0.0f
			              : (left > 1.0f) ? 1.0f : left;
		}
	}

	if (SG_TimerPending(sg_clock_latch_next))
		return;
	SG_TimerArm(&sg_clock_latch_next, SG_CLOCK_LATCH);

	for (t = 0; t < 2; t++)
	{
		int diff = sg_clock_caps[t] - sg_clock_caps[1 - t];
		int want = SG_CLOCK_EVEN;

		if (sg_clock_known)
		{
			/*
			 * The three states are disjoint by construction: ahead
			 * and behind both need a non-zero margin, the scramble
			 * needs exactly zero. No precedence to argue about.
			 */
			if (diff > 0 && sg_clock_left < SG_CLOCK_LATE)
				want = SG_CLOCK_AHEAD_LATE;
			else if (diff < 0 && sg_clock_left < SG_CLOCK_LATE)
				want = SG_CLOCK_BEHIND_LATE;
			else if (diff == 0 && sg_clock_left < SG_CLOCK_ENDGAME)
				want = SG_CLOCK_CLOSE_LATE;
		}

		if (want == sg_clock_posture[t])
			continue;
		sg_clock_posture[t] = want;

		/* the shift is READ BACK from the accessor the quota itself
		 * calls, not restated here: a debug line that describes a
		 * different lean than the one the bots take is worse than no
		 * line at all */
		if (sg_cv.debug->value)
			gi.dprintf("CLOCKPLAY %s: %s, %+d defender (caps %d-%d, "
			           "%.0f%% clock left)\n",
			           t ? "blue" : "red", sg_clock_names[want],
			           Clock_DefendShift(t + CTF_TEAM_RED),
			           sg_clock_caps[t], sg_clock_caps[1 - t],
			           sg_clock_left * 100.0f);
	}
}


/* the level-change reset, called by SG_LevelChange: a new map is a new
 * match; no posture survives it */
void Clock_LevelReset(void)
{
	sg_clock_posture[0] = sg_clock_posture[1] = SG_CLOCK_EVEN;
	sg_clock_caps[0] = sg_clock_caps[1] = 0;
	sg_clock_read_next = sg_clock_latch_next = 0.0f;
	sg_clock_known = false;
	sg_clock_left = 0.0f;
}

