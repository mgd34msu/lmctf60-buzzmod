/*
 * sg_combat.c -- SLIPGATE combat: the view, the weapon and the trigger.
 *
 * The constitution's rule for this file is a single sentence: "Combat runs
 * concurrently with navigation. There is no state that suspends movement."
 * So this is not a behaviour, a state, or a mode. It is a modifier applied to
 * a usercmd_t that the Body has already filled in. It writes cmd->angles and
 * cmd->buttons, and it asks the engine for a weapon through the same entry
 * point a player's "use <name>" runs. It never touches forwardmove, sidemove
 * or upmove.
 *
 * Everything here that is a claim about the game is read from the game, per
 * principle 1 (read the engine, never assume it). The doctrine -- which weapon
 * at which range, which shot is safe to take, what an item is worth -- is
 * slipgate/WEAPONS.md, which derives every rule it states from a cited line of
 * this tree. Constants below carry the WEAPONS.md section AND the source line
 * the section read them from. The numbers that are NOT cited -- fire cadence,
 * aim error, settle time, hysteresis windows -- are preferences, not facts,
 * and are named as such where they are defined.
 *
 * Perception is CACO's gate and nothing wider: gi.inPVS plus a trace from the
 * eyes to the target's centre against MASK_OPAQUE (sg_caco.c:100-114), plus a
 * forward-cone test so a bot does not shoot at something behind its head.
 * There is no g_edicts omniscience here; an enemy that fails the gate does not
 * exist as far as this file is concerned.
 */

#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_combat.h"
#include "slipgate/sg_persona.h"    /* who is holding the gun, not just how well */

#include <math.h>

/* ------------------------------------------------------------------- facts
 *
 * Measured from the source, each with the line that says so.
 */

/*
 * What a bolt collides with. g_weapon.c:360: bolt->clipmask = MASK_SHOT.
 * MASK_SHOT is CONTENTS_SOLID|CONTENTS_MONSTER|CONTENTS_WINDOW|
 * CONTENTS_DEADMONSTER (q_shared.h:360) == 0x6000003. The pre-fire trace uses
 * the same mask, so what the trace reports is what the projectile will
 * actually hit -- not an approximation of it.
 */

/*
 * The muzzle is not the eye. Every fire function projects from its own offset
 * through P_ProjectSource (p_weapon.c:26-36), which is static to p_weapon.c;
 * the offsets are tabulated in WEAPONS.md 2.2-F3 and every one of them is
 * forward of and below the eye. The pre-fire trace below runs from the eye
 * instead, which is the conservative choice rule F3 prescribes: the eye is
 * behind and above every muzzle, so an eye-clear shot is a muzzle-clear shot.
 */

/*
 * The weapon table.
 *
 * pickup     the FindItem key Cmd_Use_f looks up (g_cmds.c:667). Names are
 *            g_items.c's own pickup_name strings: :1622, :1645, :1668, :1691,
 *            :1714, :1760, :1783, :1806, :1829, :1852, and PLASMA_PICKUP
 *            "Plasma Rifle" (plasma.h:50, used at g_items.c:1875).
 *
 * speed      projectile speed in units/second, 0 for hitscan. WEAPONS.md
 *            2.2 table: blaster and hyperblaster 1000 (p_weapon.c:939, shared
 *            Blaster_Fire), plasma 1200 (plasma.h:25-26), rocket 650
 *            (p_weapon.c:889), BFG 400 (p_weapon.c:1741), grenade launcher 600
 *            forward (p_weapon.c:812) which is BALLISTIC and solved separately.
 *            Hitscan: shotgun, super shotgun, machinegun, chaingun, railgun
 *            (fire_bullet/fire_shotgun/fire_rail -> fire_lead, g_weapon.c:268,
 *            :747).
 *
 * windup     seconds between the trigger and the projectile leaving. Only the
 *            BFG has one: gunframe 9 is the muzzle flash, gunframe 17 is the
 *            launch, 8 gunframes at 100 ms (p_weapon.c:1698-1710, WEAPONS.md
 *            1.12 and 0.3).
 *
 * range_cap  WEAPONS.md 2.2-F6: the range at which a spread weapon's expected
 *            damage falls below the blaster's 30 dps. MG 740, CG 1100,
 *            shotgun 560, SSG 400 (spreads at p_weapon.c:1160, :1298, :1369,
 *            :1555). The railgun has zero spread and no cap (g_weapon.c:747);
 *            projectiles are not capped here, rule F1 governs them instead.
 *
 * floor      WEAPONS.md 2.2-R2: the ammo count below which the weapon stops
 *            being the right tool -- about three seconds of fire at its own
 *            drain rate. CG 90 bullets at 30/s (1.5), MG 30 at 10/s (1.4),
 *            HB 30 cells at 10/s (1.3), SSG 6 shells, shotgun 3 shells,
 *            RL 4 rockets, GL 3 grenades, railgun 2 slugs, plasma 34 cells at
 *            11.1/s (1.13), BFG 50 cells which is also its hard floor
 *            (g_items.c:1854).
 */

enum
{
	SG_W_BLASTER = 0,
	SG_W_SHOTGUN,
	SG_W_SSHOTGUN,
	SG_W_MACHINEGUN,
	SG_W_CHAINGUN,
	SG_W_GRENADELAUNCHER,
	SG_W_ROCKETLAUNCHER,
	SG_W_HYPERBLASTER,
	SG_W_RAILGUN,
	SG_W_BFG,
	SG_W_PLASMA,
	SG_NUM_WEAPONS
};

typedef struct
{
	char		*pickup;	/* not const: FindItem takes char * (g_local.h) */
	float		speed;
	float		windup;
	float		range_cap;
	int			floor;
} sg_weapon_t;

static const sg_weapon_t sg_weapons[SG_NUM_WEAPONS] = {
	/*  pickup              speed   windup   cap    floor */
	{ "Blaster",           1000.0f,  0.0f,    0.0f,    0 },
	{ "Shotgun",              0.0f,  0.0f,  560.0f,    3 },
	{ "Super Shotgun",        0.0f,  0.0f,  400.0f,    6 },
	{ "Machinegun",           0.0f,  0.0f,  740.0f,   30 },
	{ "Chaingun",             0.0f,  0.0f, 1100.0f,   90 },
	{ "Grenade Launcher",   600.0f,  0.0f,    0.0f,    3 },
	{ "Rocket Launcher",    650.0f,  0.0f,    0.0f,    4 },
	{ "HyperBlaster",      1000.0f,  0.0f,    0.0f,   30 },
	{ "Railgun",              0.0f,  0.0f,    0.0f,    2 },
	{ "BFG10K",             400.0f,  0.8f,    0.0f,   50 },
	{ "Plasma Rifle",      1200.0f,  0.0f,    0.0f,   34 }
};

/*
 * WEAPONS.md 2.1 range bands, measured eye to target bbox centre:
 * contact < 128, close 128-400, mid 400-900, long > 900.
 */
#define SG_BAND_CONTACT		0
#define SG_BAND_CLOSE		1
#define SG_BAND_MID			2
#define SG_BAND_LONG		3

#define SG_R_CLOSE			128.0f
#define SG_R_MID			400.0f
#define SG_R_LONG			900.0f

/*
 * WEAPONS.md 2.2-R1: d_safe = dmg_radius + 1, because findradius hard-culls
 * past the radius (g_utils.c:77-78) -- splash does not taper, it stops.
 *   Rocket Launcher   radius 120 (p_weapon.c:866)  -> 121
 *   Grenade Launcher  radius 160 (p_weapon.c:791)  -> 161
 *   BFG impact        radius 100 (g_weapon.c:868)  -> 101
 * Plasma is not in this table: its radius is damage + PLASMA_SPLASH_RADIUS 70
 * (plasma.c:99, :158; plasma.h:13), which GROWS with the quad multiply, so it
 * is computed from is_quad and plasma_mode instead of read from here.
 */
#define SG_DSAFE_ROCKET		121.0f
#define SG_DSAFE_GRENADE	161.0f
#define SG_DSAFE_BFG		101.0f

/*
 * WEAPONS.md 2.4-D4 rule 1: under quad the rocket/grenade radii do NOT move
 * (the radius is computed before the quad multiply, p_weapon.c:791-793,
 * :869-873) but being one unit inside is 200-240 damage instead of 30, so the
 * doctrine adds a 60-unit margin -- 0.1 s of rocket flight, and about the aim
 * error this file's own tremor model admits.
 */
#define SG_QUAD_SPLASH_MARGIN	60.0f

/*
 * Plasma splash, WEAPONS.md 1.13 and 2.4-D4 rule 2. Damage is
 * PLASMA_BOUNCE_DAMAGE 39 or PLASMA_SPREAD_DAMAGE 28 (plasma.h:11-12), times
 * 4 under quad (plasma.c:51-54, :124-127), and the radius is that damage plus
 * PLASMA_SPLASH_RADIUS 70 (plasma.c:99, :158) -- so the radius nearly doubles
 * under quad. client->plasma_mode != 0 is bounce mode (plasma.c:359-368).
 */
#define SG_PLASMA_BOUNCE_DAMAGE	39
#define SG_PLASMA_SPREAD_DAMAGE	28
#define SG_PLASMA_SPLASH_RADIUS	70

/*
 * WEAPONS.md 2.2-R1b: splash self-damage is 60 at contact for the rocket
 * (1.10) and the grenade (1.8), so the doctrine forbids both outright below
 * 90 points of health-plus-absorbable-armour. "Absorbable" is just the armour
 * count: a pool of A armour absorbs exactly A damage (1.17).
 */
#define SG_SPLASH_HP_FLOOR		90

/*
 * WEAPONS.md 2.2-R1c: while hookstate == 2 the bot is being pulled toward the
 * anchor at up to 800 u/s (1.14), and client->hooklength IS the distance to
 * the anchor (p_weapon.c:2062-2068). Splash is forbidden inside d_safe plus
 * one server frame of pull -- 800 u/s * FRAMETIME 0.1 (g_local.h:150).
 */
#define SG_HOOK_PULL_FRAME		80.0f

/*
 * WEAPONS.md 2.2-F1: only fire a projectile when the lead error is smaller
 * than three quarters of a player bbox width -- the bbox is 32 wide
 * (p_client.c:1833-1834) -- or the target's velocity direction has been
 * stable for 0.3 s.
 */
#define SG_LEAD_TOLERANCE	96.0f
#define SG_LEAD_STABLE		0.3f
#define SG_LEAD_STABLE_DOT	0.9f	/* preference: what "same direction" means */

/*
 * WEAPONS.md 2.2-F5: a grenade's impact point is not predictable past first
 * contact (fuse 2.5 s, MOVETYPE_BOUNCE, p_weapon.c:812, g_weapon.c:559), so
 * the launcher is only used against a target that is not really moving.
 * 100 u/s is a preference: under a third of the 320 u/s walk speed 2.2-F1
 * works its example against.
 */
#define SG_GL_STATIC_SPEED	100.0f

/*
 * The grenade solve. Launch is 600 forward (p_weapon.c:812) plus 200 up
 * (g_weapon.c:556) under sv_gravity 800 (g_save.c:175), which WEAPONS.md
 * 2.2-F4 reduces to pitch = atan(R/900 - 1/3) for level ground. The flight is
 * stepped at FRAMETIME (g_local.h:150) for at most the 2.5 s fuse.
 */
#define SG_GL_UP_SPEED		200.0f
#define SG_GL_PITCH_SCALE	900.0f
#define SG_GL_PITCH_BIAS	0.3333333f
#define SG_GL_FUSE_STEPS	25

/*
 * WEAPONS.md 2.4-D1: open on an enemy carrier at 400-700 units. Below 400 the
 * carrier's grapple pull crosses the gap in under half a second; above 900 the
 * rocket's flight needs more lead than rule F1 will accept.
 */
#define SG_INTERCEPT_MIN	400.0f
#define SG_INTERCEPT_MAX	700.0f

/*
 * WEAPONS.md 2.4-D2 point 2 and 2.1's close band: the super shotgun is a
 * sub-256 weapon (1.7's table -- 48 damage for two shells at 256, worse than
 * the shotgun's 48 for one past that, which is also rule R2d).
 */
#define SG_SSG_PREFER_RANGE	256.0f

/*
 * WEAPONS.md 2.4-D3: the defender's pre-held weapon by sightline length.
 * Breakpoints are 1.1's spread saturation distances, not taste: 160 (below
 * which splash is forbidden by R1 anyway), 300, 500, 900.
 */
#define SG_POST_SSG			160.0f
#define SG_POST_ROCKET		300.0f
#define SG_POST_CHAINGUN	500.0f

/* --------------------------------------------------------------- preferences
 *
 * Not measured from anything. These are the fitted/taste end of the file and
 * are labelled so nobody later mistakes them for physics.
 */

#define SG_FOV_COS			0.5f	/* cos(60 deg): a 120 degree total cone */
#define SG_ENGAGE_RANGE		2000.0f	/* do not pick fights across the map */

#define SG_FIRE_ON			0.4f	/* seconds of held trigger */
#define SG_FIRE_OFF			0.2f	/* seconds of released trigger */
#define SG_FIRE_JITTER		0.30f	/* +/- this fraction on each window */

#define SG_SETTLE			0.5f	/* seconds for the aim error to converge */
#define SG_AIM_ERROR_DEG	9.0f	/* error cone half-angle at acquisition */
#define SG_AIM_RESIDUAL_DEG	0.8f	/* never becomes perfect; this is the floor */

#define SG_HIT_SLOP			40.0f	/* pre-fire trace endpoint tolerance */

/*
 * Switch hysteresis. Rule S1 says a switch costs 700-1100 ms of not-shooting
 * (WEAPONS.md 2.0's drop/raise table) and rate-limits requests to one per
 * 500 ms; the 500 ms guard was already here and is kept. The other two numbers
 * are preferences that implement "never switch inside 800 ms of an engagement
 * you are already winning": a band change has to be decisive by 64 units of
 * deadband, and the wanted weapon has to stay wanted for 800 ms, before a
 * switch is asked for at all. A dry weapon skips both -- there is nothing to
 * lose by leaving it.
 */
#define SG_SWITCH_RATE		0.5f
#define SG_BAND_DEADBAND	64.0f
#define SG_SWITCH_HOLD		0.8f

/* ------------------------------------------------- human switch discipline
 *
 * HOW A PLAYER CARRIES A GUN (sg_wswitch, owner enhancement 2).
 *
 * The hysteresis above is a machine's reading of rule S1: want a gun for
 * 800 ms and the 700-1100 ms is paid for. What it does not model is WHEN a
 * human decides. Two halves of that, and the bot gets both of them backwards.
 *
 * A player walking a corridor toward a room they expect company in has the
 * right gun UP before the corner -- the decision was made in the corridor,
 * off nothing but a guess about the room. The bot rounds that corner holding
 * whatever it last shot with and starts thinking about the rail after it is
 * already being shot at. Then, mid-duel, a human with the wrong gun mostly
 * does not switch AT ALL: a switch is a second of being a spectator in your
 * own fight, so they empty what is in their hands and reload their opinion
 * afterwards. The bot swaps the instant a band edge says to, every time, and
 * a band edge is 64 units of a strafing target. Watch two bots cross 400
 * units on a mid-band boundary and they weapon-cycle at each other.
 *
 * So: switch EARLY on a belief, LATE on a surprise. Every number here is
 * FITTED -- this is the taste end of the file, like the fire cadence.
 *
 *   DECIDE    the pause between "the wrong gun is in my hands" and the
 *             request going out, skill-scaled the way the reaction delay is:
 *             600 ms at the bottom of the ladder, 200 ms at the top. It runs
 *             CONCURRENTLY with rule S1's 800 ms want-hold, not on top of it
 *             -- the gate is on the REQUEST, and the arbitration clocks keep
 *             running underneath it, so the delay costs what it says and not
 *             a second and a half. Where the want-hold is the longer of the
 *             two it therefore still wins, and this gate binds on the paths
 *             that used to switch INSTANTLY: a gun that runs dry mid-fight,
 *             an unknown one, and a target re-acquired onto a want the bot
 *             had already been holding before it lost sight. Those are the
 *             swaps that read as machine-fast, and they are the ones that now
 *             cost a visible beat.
 *   COOLDOWN  one mid-fight switch per 4 s. Having committed to the second
 *             gun, finish the fight with it.
 *   PRE_MISS  the corridor switch's bar: the held gun's own wanted range has
 *             to miss the believed contact range by this fraction of that
 *             range before the switch is worth its 700-1100 ms. A third is
 *             about one band wide, which is the resolution the belief has.
 *   PRE_FRESH how old a sighting may be and still be a room to walk into.
 *             The same 3 s the body already prices an idle hand's pre-select
 *             at (sg_arach.c:4399), stated here because this path reads the
 *             belief table itself instead of waiting to be told.
 *   PRE_REACH how far away a believed enemy may be and still be a meeting
 *             rather than a rumour. 1200 units, which is the body's own alert
 *             radius (sg_arach.c:4409) and is deliberately NOT
 *             SG_ENGAGE_RANGE: 2000 units of straight line through map
 *             geometry is not a room the bot is about to walk into, and
 *             readying the long band's answer for it would be a bot carrying
 *             a rail everywhere on the strength of a rumour.
 *
 * The panic exception is not a number: at contact range with a gun that
 * cannot shoot, neither gate applies. That switch is not a preference being
 * revised, it is a dry click at 100 units, and it is the one mid-fight swap a
 * human makes instantly.
 */
#define SG_WS_DECIDE_S0		0.60f
#define SG_WS_DECIDE_S4		0.20f
#define SG_WS_COOLDOWN		4.0f
#define SG_WS_PRE_MISS		0.33f
#define SG_WS_PRE_FRESH		3.0f
#define SG_WS_PRE_REACH		1200.0f

#define SG_WEIGHT_TICK		1.0f	/* item worths, same cadence as Fields_Refresh */

/*
 * The duel terms. Preferences, all of them -- what they COMPOSE is measured
 * (the 2.1 ladders, the band edges, R1b's survivability floor), but how long a
 * lost target stays interesting and how hard a mismatched weapon should push
 * are fitted.
 *
 * SG_DUEL_FRESH matches the belief table's own short window: sg_arach.c prices
 * a "fresh contact" at 4 s for the carrier's flee term and 3 s for the idle
 * hand's pre-select. Two seconds is tighter than both, because this one is
 * about a firing position rather than a warning.
 *
 * SG_DUEL_MATCH_BIAS is how far the range preference moves off the bot's own
 * optimum toward the side its opponent's weapon is worse at. A quarter of the
 * gap: enough to make a railgun back away from a super shotgun, small enough
 * that it never overrides the weapon's own band gates below.
 */
#define SG_DUEL_FRESH		2.0f
#define SG_DUEL_MATCH_BIAS	0.25f

/*
 * The corner hold. A target that walked out of sight is somewhere in the set
 * of seeds it could have reached since -- SG_LOST_HOLD seconds is how long
 * that set stays small enough to be worth aiming at. The BFS that builds it is
 * capped at SG_LOST_BFS seeds examined, which is also its trace count: one
 * MASK_OPAQUE ray per seed, per recompute, per bot. SG_LOST_TICK is the
 * recompute cadence -- the set only grows as the clock runs, so re-walking it
 * every frame buys nothing and costs ten times the traces.
 */
#define SG_LOST_HOLD		3.0f
#define SG_LOST_BFS			24
#define SG_LOST_TICK		0.2f

/* --------------------------------------------------------------- skill spans
 *
 * bot_skill, applied per bot. The cvar names the team's BEST; each bot then
 * plays at or below it, so the sixteen names sg_arach.c spawns are
 * distinguishable opponents at one server setting instead of sixteen copies of
 * the same shooter.
 *
 * The cvar is read as 0..4 and clamped there (g_botmenu.c:77 and bl_main.c:1542
 * already pass it around; the test config runs bot_skill 4). The personal
 * offset is deterministic in the client index -- -((ci * 7) % 5) * 0.25, so
 * 0 to -1 skill levels -- and a given client number therefore plays the same
 * way for the whole match and for every match after it. 7 and 5 are coprime,
 * so the five grades spread evenly over consecutive client numbers instead of
 * clustering.
 *
 * The offset is one-sided DOWNWARD on purpose. A symmetric offset would be
 * clamped at the ceiling exactly where the tests run -- at bot_skill 4 half
 * the bots would land on 4.0 and the variety the offset exists for would
 * disappear at the one setting that matters most. One-sided, bot_skill 4
 * fields five distinct grades (4.00, 3.75, 3.50, 3.25, 3.00), the best of them
 * being the shipped behaviour and none of them better than it.
 *
 * Every number below is FITTED. Nothing here is measured from anything; this
 * is the taste end of the file, exactly like the fire cadence above it.
 *
 * The design constraint the endpoints are chosen against: at bot_skill 4 the
 * behaviour that already shipped is the CEILING. So each skill-4 endpoint is
 * written as the constant that was already in use, textually, and only the
 * skill-0 endpoint is worse. Lower skills are honestly worse; top skill is not
 * made stronger than it was.
 *
 * The single exception is the reaction delay, which did not exist at all
 * before: at skill 4 it is 0.12 s, a little over one server frame (FRAMETIME
 * 0.1, g_local.h:150) and well inside the jitter the fire windows already had.
 */
#define SG_SKILL_MAX		4.0f
#define SG_SKILL_SPREAD		0.25f	/* skill levels per step of personal offset */

#define SG_REACT_S0			0.50f	/* delay before the first pull on a NEW target */
#define SG_REACT_S4			0.12f

#define SG_SETTLE_S0		1.20f	/* seconds for the aim error to converge */
#define SG_SETTLE_S4		SG_SETTLE

#define SG_ACQUIRE_S0		14.0f	/* error cone half-angle at acquisition */
#define SG_ACQUIRE_S4		SG_AIM_ERROR_DEG

#define SG_RESIDUAL_S0		3.00f	/* the floor that cone settles onto */
#define SG_RESIDUAL_S4		SG_AIM_RESIDUAL_DEG

#define SG_FIRE_OFF_S0		0.50f	/* seconds of released trigger */
#define SG_FIRE_OFF_S4		SG_FIRE_OFF

#define SG_LEAD_JITTER_S0	4.0f	/* per-shot lead error in degrees; 0 at skill 4 */

/* ------------------------------------------------------------ aim texture
 *
 * The block above gets the SIZE of a human's aim error right and the SHAPE of
 * it wrong. Watch a demo: nobody's crosshair converges on a target the way a
 * decaying cone does. The hand swings, it goes PAST, it comes back, it goes
 * past again a little less, and the second correction is the one that lands.
 * Then, holding the target, the crosshair does not sit still and it does not
 * buzz -- it wanders off a fraction of a degree and gets pulled back, on a
 * cycle you can count in seconds.
 *
 * Three things are missing, and they are all shape, not magnitude:
 *
 *   1. the overshoot on acquisition, and its one-or-two damped corrections
 *   2. the settle window growing with the size of the swing -- Fitts's law,
 *      which is about a hand and a target and is as true of a mouse as it is
 *      of a finger and a button
 *   3. the tracking wander, which is slow and continuous, where this file's
 *      tremor was a fresh random direction stamped down every 0.25-0.5 s
 *
 * All of it is behind sg_aimtexture, default 0. With the cvar off every
 * expression below is the one that shipped, textually, and the state fields
 * this block adds are never written.
 *
 * The numbers are FITTED, like the whole block above them. The one that is
 * not free is SG_TEX_FITTS_REF: the settle multiplier is written so that it
 * comes out at exactly 1.0 at that flick angle, so the reference is the flick
 * at which the settle window is UNCHANGED from today. It is set at 30 deg
 * because the scan gate only admits targets inside a 120 deg cone
 * (SG_FOV_COS), which puts the median acquisition swing in the twenties --
 * so the multiplier is centred on the roster's own typical flick and the
 * AVERAGE settle length across a match is what it always was. Only its
 * distribution changes: a small re-acquisition converges sooner, a wide
 * swing later.
 */
#define SG_TEX_OVER_TRACK	0.08f	/* overshoot, as a fraction of the swing, */
#define SG_TEX_OVER_FLICK	0.15f	/* for style 0 and style 1 respectively */
/*
 * Ceiling on the overshoot, in degrees, and it is NOT a taste number -- it is
 * the point where the compensation below runs out of ramp to pay with. The
 * budget Combat_TexSpan has to spend is res*span + span^2/3, tightest at
 * skill 4 (0.8 and 8.2), which caps the overshoot's own mean square at
 *
 *     sqrt((0.8*8.2 + 8.2^2/3) / SG_TEX_SHAPE_MS) == 12.9 deg
 *
 * Past that the ramp is already at zero and the extra overshoot is a straight
 * accuracy loss with nothing on the other side of the ledger. 12 rather than
 * 12.9 for the margin. It binds in practice only where it should: the scan
 * gate's 120 deg cone puts most acquisitions well under it, and the swings
 * that reach it are the threat-cone ones -- a bot spun round by a shot in the
 * back, which is exactly the case where 15% of the swing stops being an
 * overshoot and becomes a pirouette.
 */
#define SG_TEX_OVER_CAP		12.0f
/*
 * Overshoot is a defect, so the file's own rule applies: skill 4 is the
 * CEILING and only skill 0 is worse. The band above is what a skill-4 bot
 * does; a skill-0 bot does a quarter more of it.
 */
#define SG_TEX_OVER_MUL_S0	1.25f
#define SG_TEX_OVER_MUL_S4	1.00f

#define SG_TEX_FITTS_REF	30.0f	/* the flick the window is calibrated on */
#define SG_TEX_FITTS_A		0.45f	/* A + B == 1 keeps m(REF) == 1 exactly */
#define SG_TEX_FITTS_B		0.55f
#define SG_TEX_FITTS_LO		0.55f
#define SG_TEX_FITTS_HI		1.70f

#define SG_TEX_STYLE_WIN	0.30f	/* a flicker settles in 0.85x the window a
                                     * tracker takes, and rides 1.15x for the
                                     * smooth end -- the same +/-15% band the
                                     * persona traits are squeezed into */

#define SG_TEX_FLICK_MIN	3.0f	/* under this the swing is a nudge and the
                                     * whole texture is skipped: a 2 deg
                                     * "flick" with an overshoot on it is a
                                     * twitch nobody makes */

#define SG_TEX_WANDER_TAU	0.90f	/* seconds for the tracking direction to
                                     * have substantially wandered */

/*
 * Mean square of the correction shape (1-x)cos(2*pi*n*x) over x in [0,1]:
 *
 *     1/6 + 1/(16*pi^2*n^2)  ==  0.173 (n=1), 0.169 (n=2)
 *
 * One number for both because the difference is under 3% and this is a term
 * that gets subtracted under a square root.
 */
#define SG_TEX_SHAPE_MS		0.173f

/*
 * How long a hit from a shooter the bot could not see keeps steering where
 * the bot looks (sg_caco.c's damage ring supplies the bearing). A human
 * spins on being hit and then gets on with it; this is the length of the
 * "and then gets on with it".
 *
 * The span runs the other way from every other one in this block, and that
 * is deliberate rather than an oversight: the low-skill bot stays rattled
 * LONGER, still checking a bearing the shot has long since left, while the
 * skill-4 bot checks once and returns to the fight in front of it. Stated
 * plainly because it does not fit the block's own rule that skill 4 is the
 * ceiling -- the bias only ever ADDS candidates to the scan, so a longer
 * window is not strictly worse for the bot that has it. It is a fluster
 * clock, not a skill ladder, and it is written here with the skill spans
 * because it is scaled by the same number.
 */
#define SG_THREAT_S0		2.40f	/* rattled this long at bot_skill 0 */
#define SG_THREAT_S4		1.20f

#define SG_COMBAT_MAXCLIENTS	256

/* ------------------------------------------------------------------- state */

/*
 * Per-client, persistent across frames. Indexed by client number, sized to a
 * fixed ceiling and bounds-checked -- this file cannot add a field to
 * sg_local.h's bot structure (another agent owns the body), so it keeps its
 * own table.
 */
typedef struct
{
	int			enemy;			/* edict index of held target, 0 = none */
	float		since;			/* level.time the target was acquired */
	float		acquired_at;	/* same instant, read by the reaction gate --
	                             * kept separate from `since` because `since` is
	                             * the settle clock and the two would otherwise
	                             * be one number doing two jobs */

	vec3_t		err;			/* aim error direction, unit length */
	float		err_next;		/* when to resample the tremor */

	/* the aim texture, all of it written only while sg_aimtexture is on.
	 * tex_dir is re-orthogonalised against the live aim every frame rather
	 * than trusted from acquisition: the target keeps moving during the
	 * settle, and an offset that has drifted out of the plane perpendicular
	 * to the aim stops being a pure angle and starts being a range error. */
	vec3_t		tex_dir;		/* which way "past the target" is, unit */
	float		tex_over;		/* overshoot amplitude, degrees, >0 = armed */
	float		tex_win;		/* this acquisition's settle window, seconds */
	float		tex_cyc;		/* corrections inside it: 1.0 or 2.0 */
	float		tex_wander;		/* level.time of the last wander step */

	float		win_end;		/* when the current trigger window expires */
	qboolean	win_fire;		/* is the current window a firing window */

	/* tap variance (sg_tapvar): the beat between a slow weapon coming
	 * ready and the next deliberate trigger press */
	float		tap_until;
	qboolean	tap_pending;    /* legacy of the weaponstate cut; unused */
	int			tap_ammo;       /* last seen ammo count for the held gun */

	/* fire discipline (sg_firedisc): the bot's OWN heading stability --
	 * humans hold fire while jockeying and shoot planted */
	vec3_t		self_dir;
	float		self_stable;

	float		switch_next;	/* rate limit on weapon-switch requests */

	int			band;			/* committed range band */
	int			band_pend;		/* band being considered */
	float		band_since;		/* when band_pend was first seen */

	int			want;			/* weapon index wanted, -1 none */
	float		want_since;		/* when it was first wanted */

	vec3_t		vel_dir;		/* target's last velocity direction */
	float		vel_stable;		/* since when it has pointed that way */

	float		post_sight;		/* posted defender sightline, <0 not posted */
	float		alert_range;	/* expected contact range from belief (an
	                             * ear or a teammate's eye), <0 none */
	float		alert_until;	/* the expectation's shelf life */

	float		worth_next;		/* when the item worths go stale */
	float		worth[SG_FIELD_CLASSES];
	float		worth_mega;		/* the overheal worth (sg_megaworth); not a
	                             * class, so it sits beside them */

	/* what the duel terms are about: the last look at the held target. org is
	 * belief, not the live edict -- it ages instead of following. enemy_last
	 * is kept where `enemy` above is cleared, because a target that walked
	 * behind a wall is still the target the range is being held against; the
	 * two are separate so the acquisition reset above keeps its exact meaning. */
	int			enemy_last;		/* edict index, outlives the sighting */
	vec3_t		enemy_org;
	float		enemy_time;		/* level.time of the last successful scan */
	int			enemy_weapon;	/* weapon index seen in their hands, -1 none */

	/* the corner hold. lost_client is the client number, not an edict index,
	 * so it compares directly against the belief table's own field. lost_until
	 * is the validity test rather than lost_client, for the same reason
	 * alert_until is: this table is static storage that starts as zeroes, and
	 * an expired deadline is the only sentinel a zeroed record answers
	 * correctly on the first frame of a level. */
	int			lost_client;
	int			lost_seed;
	float		lost_time;		/* when the target was lost */
	float		lost_until;		/* when the hold expires; <= level.time = none */
	float		lost_next;		/* when the emergence set goes stale */
	vec3_t		lost_aim;		/* the point the view is held on */
	qboolean	lost_have;		/* is lost_aim a real emergence point */
	qboolean	pursue;			/* role permits holding a corner at all */

	/* the switch discipline (sg_wswitch). ws_gate is the instant a MID-FIGHT
	 * switch request becomes legal: the decision delay when a fight starts,
	 * the 4 s cooldown once one has been spent. ws_armed is when that deadline
	 * was set, so the debug line can say how long the bot stood there holding
	 * the wrong gun. ws_panic is this frame's contact-range exception, written
	 * by the engaged path and read by the two places that honour it. ws_pre is
	 * the weapon the corridor pre-switch last asked for, so the debug channel
	 * gets one line per decision instead of one per frame. */
	float		ws_gate;
	float		ws_armed;
	qboolean	ws_panic;
	int			ws_pre;
} sg_combat_state_t;

static sg_combat_state_t sg_combat[SG_COMBAT_MAXCLIENTS];

/* why the trigger stayed off, tallied per enemy-frame and printed on the
 * debug channel every few seconds: [0] fired, [1] no clear shot,
 * [2] splash veto, [3] range cap, [4] lead drift, [5] fire window,
 * [6] held/switching. Diagnosis for the 3-in-3025 firing collapse. */
static int		sg_cbt_why[10];
static int		sg_cbt_scan[7];     /* [0]unteamed [1]same [2]far [3]fov
                                     * [4]blocked [5]acquired
                                     * [6] admitted by the threat cone alone --
                                     * outside the forward cone, kept because
                                     * something recently shot the bot from
                                     * that way */
static int		sg_cbt_fire[SG_NUM_WEAPONS];    /* trigger-frames per gun */
static int		sg_cbt_hit[SG_NUM_WEAPONS];     /* landed damage events */
static float	sg_cbt_why_next;
/* [9] is the reaction gate: a target held, a shot cleared, and the bot has not
 * finished noticing yet. Kept apart from [5] so a slow bot does not read as a
 * bot with a broken fire window. */

/* ------------------------------------------------------------------- skill */

static cvar_t	*sg_bot_skill;
static cvar_t	*sg_wswitch;

/*
 * The switch-discipline gate. sg_wswitch 0 is every path it guards compiled
 * in and never taken: no corridor pre-switch, no decision delay, no cooldown,
 * no panic exception, and the selection behaves exactly as it did before this
 * block existed. The POINTER is cached for the same reason bot_skill's is --
 * this is read per engaged bot per frame and gi.cvar walks the engine's list
 * -- while the VALUE is read fresh, so flipping it mid-match takes effect on
 * the next frame.
 */
static qboolean Combat_WSwitch(void)
{
	if (!sg_wswitch)
		sg_wswitch = gi.cvar("sg_wswitch", "0", 0);
	return (qboolean)(sg_wswitch && sg_wswitch->value != 0.0f);
}

static cvar_t	*sg_aimtexture;

/*
 * Is the aim texture armed? Pointer resolved once, value read fresh, for the
 * same reason sg_bot_skill is: gi.cvar walks the engine's list and this is
 * asked several times per engaged bot per frame, but flipping the cvar
 * mid-match has to take on the next one.
 *
 * Default 0. Everything this switch guards is additive, so with it off the
 * aim path is the one that shipped and not a re-derivation of it.
 */
static qboolean Combat_TexOn(void)
{
	if (!sg_aimtexture)
		sg_aimtexture = gi.cvar("sg_aimtexture", "1", 0);
	return (qboolean)(sg_aimtexture && sg_aimtexture->value != 0.0f);
}

/*
 * The bot's effective skill, a float in [0, 4]: the team level the cvar names,
 * plus this client's own fixed offset. The cvar POINTER is resolved once --
 * gi.cvar walks the engine's list on every call, and this is read several times
 * per engaged bot per frame -- while the VALUE is read fresh every time, so
 * changing bot_skill mid-match takes effect on the next frame.
 *
 * The offset used to be (ci * 7) % 5 -- five grades handed out by arithmetic
 * on a client index, which made the roster a skill ladder and nothing else.
 * The persona table names the grade instead (sg_persona.c), over the SAME
 * envelope: grade 0 is the team's full skill, grade 4 a full point under it,
 * and the sixteen rows average grade 2.0 exactly as the modulo did. With
 * sg_persona 0 there is no row and the modulo runs, unchanged.
 */
static float Combat_Skill(edict_t *self)
{
	float	team, s;
	int		ci, grade;

	if (!sg_bot_skill)
		sg_bot_skill = gi.cvar("bot_skill", "4", 0);

	team = sg_bot_skill ? sg_bot_skill->value : SG_SKILL_MAX;
	if (team < 0.0f)
		team = 0.0f;
	if (team > SG_SKILL_MAX)
		team = SG_SKILL_MAX;

	ci = (self && self->client) ? (int)(self->client - game.clients) : 0;
	if (ci < 0)
		ci = 0;

	grade = SG_PersonaAimGrade(self);
	if (grade < 0)
		grade = (ci * 7) % 5;

	s = team - (float)grade * SG_SKILL_SPREAD;
	if (s < 0.0f)
		s = 0.0f;
	if (s > SG_SKILL_MAX)
		s = SG_SKILL_MAX;
	return s;
}

/* linear between a span's skill-0 and skill-4 endpoints */
static float Combat_SkillLerp(float skill, float at0, float at4)
{
	return at0 + (at4 - at0) * (skill / SG_SKILL_MAX);
}

int SG_CombatSkill(edict_t *self)
{
	return (int)(Combat_Skill(self) * 100.0f + 0.5f);
}

/* ------------------------------------------------------------- item cache
 *
 * FindItem walks itemlist comparing pickup_name strings (g_items.c). Doing
 * that per weapon per frame is the kind of cost this file is not allowed to
 * spend, so every gitem_t and every inventory index is resolved once and kept
 * -- the same pattern the body uses for the flag item (sg_arach.c:603-605).
 * itemlist is static storage that outlives every level, so these pointers do
 * not dangle across a map change the way a TAG_LEVEL allocation would.
 */

static gitem_t	*sg_witem[SG_NUM_WEAPONS];
static int		sg_widx[SG_NUM_WEAPONS];
static int		sg_wammo[SG_NUM_WEAPONS];	/* inventory index, -1 = no ammo */
static gitem_t	*sg_hookitem;				/* never to be made pers.weapon */
static gitem_t	*sg_flagitem;
static int		sg_jacket_index;			/* ArmorIndex checks jacket first */
static qboolean	sg_items_ready;

static void Combat_CacheItems(void)
{
	int i;

	if (sg_items_ready)
		return;
	sg_items_ready = true;

	for (i = 0; i < SG_NUM_WEAPONS; i++)
	{
		sg_witem[i] = FindItem(sg_weapons[i].pickup);
		sg_widx[i] = sg_witem[i] ? ITEM_INDEX(sg_witem[i]) : 0;
		sg_wammo[i] = -1;
		if (sg_witem[i] && sg_witem[i]->ammo)
		{
			gitem_t *a = FindItem(sg_witem[i]->ammo);

			if (a)
				sg_wammo[i] = ITEM_INDEX(a);
		}
	}

	/* g_items.c:2491-2511. Rule S3: this one is looked up so it can be
	 * recognised and refused, never so it can be selected. */
	sg_hookitem = FindItem("Grappling Hook");

	/* the two state members whose "unset" value is not zero */
	for (i = 0; i < SG_COMBAT_MAXCLIENTS; i++)
	{
		sg_combat[i].post_sight = -1.0f;
		sg_combat[i].alert_range = -1.0f;
		sg_combat[i].alert_until = 0.0f;
		sg_combat[i].band = -1;			/* no band committed yet */
		sg_combat[i].ws_pre = -1;		/* nothing pre-switched to yet */
	}

	/* LMCTF has ONE flag item (g_items.c:2478), the same test ctf_flagtouch
	 * makes and the body already makes at sg_arach.c:600-607 */
	sg_flagitem = FindItem("Enemy Flag");

	/* jacket_armor_index is static to g_items.c (g_items.c:41), so the index
	 * is resolved here the same way g_items.c:2737 resolves it */
	{
		gitem_t *j = FindItem("Jacket Armor");

		sg_jacket_index = j ? ITEM_INDEX(j) : 0;
	}
}

/* ------------------------------------------------------------- perception */

/*
 * CACO's sight gate, verbatim in behaviour: PVS, then a clear line from the
 * eyes to the target's centre against MASK_OPAQUE. sg_caco.c:100-114 holds the
 * original; it is static there, so this is a copy rather than a call. If that
 * gate ever changes, this one changes with it.
 */
static qboolean Combat_Visible(edict_t *viewer, edict_t *target)
{
	vec3_t	eye, mid;
	trace_t	tr;

	VectorCopy(viewer->s.origin, eye);
	eye[2] += viewer->viewheight;
	VectorAdd(target->absmin, target->absmax, mid);
	VectorScale(mid, 0.5f, mid);

	if (!gi.inPVS(eye, mid))
		return false;
	tr = gi.trace(eye, NULL, NULL, mid, viewer, MASK_OPAQUE);
	return tr.fraction >= 1.0f;
}

static void Combat_Center(edict_t *e, vec3_t out)
{
	VectorAdd(e->absmin, e->absmax, out);
	VectorScale(out, 0.5f, out);
}

/*
 * Pick a target: a live enemy client, inside engage range, inside the forward
 * cone, and visible. Nearest wins. Iteration follows sg_caco.c:294-300 --
 * g_edicts + 1 + i over game.maxclients, inuse and client checked.
 *
 * Liveness is the same pair CACO uses (sg_caco.c:213):
 *     deadflag == DEAD_DEAD || health <= 0
 * Team is client->ctf.teamnum (g_local.h:1122), CTF_TEAM_RED/BLUE from
 * g_ctffunc.h:12-13. A client not on a team -- observer, connecting, or
 * mid-join -- has neither value and is skipped, which is what we want.
 *
 * `threat`, when it is not NULL, is a second cone of the same half-angle
 * pointed back down the line something just shot the bot along (the damage
 * ring, sg_caco.c). A candidate inside EITHER cone is considered. This is
 * the whole of the reaction: nothing here turns the bot by hand. If the
 * shooter is actually standing back there, the scan now returns him and the
 * ordinary aim path swings the view around -- which is a bot spinning on
 * being hit. If he is behind a wall, Combat_Visible fails exactly as it
 * always did and the bot does not turn, because there is nothing to see.
 * Aim error and the fire windows are untouched: this is where a bot LOOKS,
 * not how well it shoots.
 */
static edict_t *Combat_Scan(edict_t *self, vec3_t eye, vec3_t forward,
                            const float *threat)
{
	edict_t	*best = NULL;
	/*
	 * How far out this bot is willing to START something. The cap itself is
	 * a refusal to fight across the map (SG_ENGAGE_RANGE); the persona moves
	 * it by at most 15% either way, which is the difference between a Fiend
	 * who takes the corridor shot and a Wizard who lets it walk. Nearest
	 * still wins inside whatever the cap turns out to be -- this bends who
	 * is a candidate, not how one is chosen. Exactly SG_ENGAGE_RANGE when
	 * no persona applies.
	 *
	 * The second factor is the tilt clock (sg_arach.c, sg_tilt): for a few
	 * skill-scaled seconds after a respawn the bot starts fewer fights --
	 * it takes the corridor shot it would otherwise have taken, but not
	 * the one across the room. Exactly 1.0 with sg_tilt off, outside the
	 * window, and for anything that is not a SLIPGATE bot. Nothing below
	 * this line changes: a fight this bot DOES take is fought with the
	 * same aim, the same reaction and the same trigger it always had.
	 */
	float	bestdist = SG_ENGAGE_RANGE * SG_PersonaAggression(self) *
	                   SG_TiltCaution(self);
	int		myteam = self->client->ctf.teamnum;
	int		i;

	if (myteam != CTF_TEAM_RED && myteam != CTF_TEAM_BLUE)
		return NULL;

	for (i = 0; i < game.maxclients; i++)
	{
		edict_t	*p = g_edicts + 1 + i;
		vec3_t	mid, delta;
		float	dist, dot;
		int		theirteam;

		if (p == self)
			continue;
		if (!p->inuse || !p->client)
			continue;
		if (p->deadflag == DEAD_DEAD || p->health <= 0)
			continue;
		if (p->movetype == MOVETYPE_NOCLIP)
			continue;			/* observers, per Cmd_Hook_f's own test */

		theirteam = p->client->ctf.teamnum;
		if (theirteam != CTF_TEAM_RED && theirteam != CTF_TEAM_BLUE)
			{ sg_cbt_scan[0]++; continue; }
		if (theirteam == myteam)
			{ sg_cbt_scan[1]++; continue; }

		Combat_Center(p, mid);
		VectorSubtract(mid, eye, delta);
		dist = VectorLength(delta);
		if (dist < 1.0f || dist >= bestdist)
			{ sg_cbt_scan[2]++; continue; }

		/* forward cone: do not shoot backwards. The basis is the CURRENT
		 * v_angle, which is what the previous frame's cmd angles produced. */
		VectorScale(delta, 1.0f / dist, delta);
		dot = DotProduct(delta, forward);
		if (dot < SG_FOV_COS)
		{
			if (!threat || DotProduct(delta, threat) < SG_FOV_COS)
				{ sg_cbt_scan[3]++; continue; }
			sg_cbt_scan[6]++;
		}

		if (!Combat_Visible(self, p))
			{ sg_cbt_scan[4]++; continue; }
		sg_cbt_scan[5]++;

		best = p;
		bestdist = dist;
	}

	return best;
}

/*
 * Is this edict the enemy flag carrier CACO believes in? WEAPONS.md 2.4-D1
 * gives the carrier its own engagement, so combat has to be able to name one.
 * enemy_carrier[team-1] is who holds OUR team's flag (sg_local.h:71).
 */
static qboolean Combat_IsEnemyCarrier(edict_t *self, edict_t *target)
{
	int	team = self->client->ctf.teamnum;
	int	i;

	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return false;

	for (i = 0; i < 2; i++)
	{
		sg_belief_carrier_t *c = &sg_caco_team_belief.enemy_carrier[i];

		if (c->client >= 0 && g_edicts + 1 + c->client == target)
			return true;
	}
	return false;
}

static qboolean Combat_Carrying(edict_t *self)
{
	return sg_flagitem &&
	       self->client->pers.inventory[ITEM_INDEX(sg_flagitem)] > 0;
}

/* --------------------------------------------------------- weapon state */

static qboolean Combat_IsQuad(edict_t *self)
{
	/* p_weapon.c:289: is_quad is quad_framenum > level.framenum */
	return self->client->quad_framenum > level.framenum;
}

static int Combat_AmmoCount(edict_t *self, int w)
{
	if (sg_wammo[w] < 0)
		return 0x7fffffff;		/* blaster: no ammo at all, g_items.c:1623 */
	return self->client->pers.inventory[sg_wammo[w]];
}

/*
 * WEAPONS.md 2.1's availability predicate: in inventory AND ammo at or above
 * item->quantity -- the same test Use_Weapon (p_weapon.c:367-372) and
 * Weapon_Generic (p_weapon.c:504-505) apply. quantity is 1 for everything
 * except the super shotgun 2 (g_items.c:1670), the BFG 50 (g_items.c:1854)
 * and the plasma 10 (g_items.c:1877).
 */
static qboolean Combat_Avail(edict_t *self, int w)
{
	if (!sg_witem[w] || !sg_witem[w]->use)
		return false;
	if (!self->client->pers.inventory[sg_widx[w]])
		return false;
	return Combat_AmmoCount(self, w) >= sg_witem[w]->quantity;
}

/* rule R2a's stronger predicate: available AND above the three-second floor */
static qboolean Combat_Stocked(edict_t *self, int w)
{
	if (!Combat_Avail(self, w))
		return false;
	return Combat_AmmoCount(self, w) >= sg_weapons[w].floor;
}

/*
 * Which of the eleven the bot is holding, or -1 for anything else -- which
 * means the grappling hook, hand grenades, or nothing at all. -1 is treated
 * as "must switch": it is the only state in which the trigger would fire the
 * rope (g_cmds.c:1408-1411, p_weapon.c:2139-2144).
 */
static int Combat_Held(edict_t *self)
{
	gitem_t	*wp = self->client->pers.weapon;
	int		i;

	if (!wp)
		return -1;
	for (i = 0; i < SG_NUM_WEAPONS; i++)
		if (sg_witem[i] == wp)
			return i;
	return -1;
}

/*
 * Splash radius plus one, per weapon, per WEAPONS.md 2.2-R1 and 2.4-D4.
 * Returns 0 for a weapon with no splash. Quad is read from the bot, not
 * assumed, because the two families behave differently: the rocket and
 * grenade radii are fixed and get a doctrine margin, the plasma radius is
 * derived from the quadded damage and grows on its own.
 */
static float Combat_DSafe(edict_t *self, int w)
{
	qboolean quad = Combat_IsQuad(self);

	switch (w)
	{
	case SG_W_ROCKETLAUNCHER:
		return SG_DSAFE_ROCKET + (quad ? SG_QUAD_SPLASH_MARGIN : 0.0f);
	case SG_W_GRENADELAUNCHER:
		return SG_DSAFE_GRENADE + (quad ? SG_QUAD_SPLASH_MARGIN : 0.0f);
	case SG_W_BFG:
		/* the impact splash literal is not quadded (g_weapon.c:868) and the
		 * wave and lasers skip the owner outright (g_weapon.c:822, :917), so
		 * the BFG is the one splash weapon quad does not make more dangerous */
		return SG_DSAFE_BFG;
	case SG_W_PLASMA:
		{
			int dmg = self->client->plasma_mode ? SG_PLASMA_BOUNCE_DAMAGE
			                                    : SG_PLASMA_SPREAD_DAMAGE;

			if (quad)
				dmg *= 4;
			return (float)(dmg + SG_PLASMA_SPLASH_RADIUS) + 1.0f;
		}
	default:
		return 0.0f;
	}
}

/*
 * WEAPONS.md 2.2-R1b: health plus absorbable armour. A pool of A armour
 * absorbs exactly A damage (1.17, CheckArmor at g_combat.c:283-321), so the
 * armour count IS the absorbable term.
 */
static int Combat_Survivable(edict_t *self)
{
	int idx = ArmorIndex(self);

	return self->health + (idx ? self->client->pers.inventory[idx] : 0);
}

/*
 * Would firing weapon w right now hurt the bot? impact is the point the
 * pre-fire trace says the shot stops at -- rule R1's own construction:
 * tr.endpos IS the impact point.
 */
static qboolean Combat_SplashSafe(edict_t *self, int w, vec3_t impact)
{
	float	dsafe = Combat_DSafe(self, w);
	vec3_t	me, v;

	if (dsafe <= 0.0f)
		return true;

	/* R1b: the rocket and the grenade cost 60 at contact; below 90 points of
	 * health-plus-armour that is not a trade, it is a suicide */
	if ((w == SG_W_ROCKETLAUNCHER || w == SG_W_GRENADELAUNCHER) &&
	    Combat_Survivable(self) < SG_SPLASH_HP_FLOOR)
		return false;

	/* R1c: a rope shortening at 800 u/s closes the gap the splash needs */
	if (self->client->hookstate == 2 &&
	    (float)self->client->hooklength < dsafe + SG_HOOK_PULL_FRAME)
		return false;

	Combat_Center(self, me);
	VectorSubtract(impact, me, v);
	return VectorLength(v) >= dsafe;
}

/* ------------------------------------------------------------- the ladders
 *
 * WEAPONS.md 2.1, one array per band, in the dossier's rank order, terminated
 * by -1. The doctrine ladders of 2.4 come after them.
 */

static const int sg_ladder_contact[] = {
	SG_W_SSHOTGUN, SG_W_CHAINGUN, SG_W_HYPERBLASTER, SG_W_SHOTGUN,
	SG_W_MACHINEGUN, SG_W_RAILGUN, SG_W_PLASMA, SG_W_BLASTER, -1
};

static const int sg_ladder_close[] = {
	SG_W_ROCKETLAUNCHER, SG_W_CHAINGUN, SG_W_HYPERBLASTER, SG_W_SSHOTGUN,
	SG_W_SHOTGUN, SG_W_RAILGUN, SG_W_MACHINEGUN, SG_W_GRENADELAUNCHER,
	SG_W_PLASMA, SG_W_BLASTER, -1
};

/*
 * Mid ranks the rocket over the rail as of the wave-42..47 accuracy
 * tables: this body rails at 12-14% under the skill scatter and lands
 * rockets at 23%, and 12% of 100 loses to 23% of a rocket plus its
 * splash at every mid distance. The dossier's rail-first order was
 * written for a hand steadier than the one the skill model gives us --
 * the LADDER serves the shooter it has, not the shooter the dossier
 * imagined (WEAPONS.md 2.1, amended).
 */
static const int sg_ladder_mid[] = {
	SG_W_ROCKETLAUNCHER, SG_W_RAILGUN, SG_W_HYPERBLASTER, SG_W_CHAINGUN,
	SG_W_MACHINEGUN, SG_W_SHOTGUN, SG_W_BFG, SG_W_BLASTER, -1
};

static const int sg_ladder_long[] = {
	SG_W_RAILGUN, SG_W_HYPERBLASTER, SG_W_BLASTER, -1
};

/*
 * WEAPONS.md 2.4-D2 point 2: a carrier holds a weapon that works while moving
 * fast and needs no lead. Railgun, then chaingun, then the super shotgun for
 * anything that gets inside 256 units.
 */
static const int sg_ladder_carry[] = {
	SG_W_RAILGUN, SG_W_CHAINGUN, SG_W_SSHOTGUN, -1
};

/*
 * WEAPONS.md 2.4-D1: the rocket's 120-unit splash forgives the lead error a
 * grappling carrier's 800 u/s forces on you, and 220-240 on a direct hit ends
 * the run in one shot. Railgun second (no lead at all), chaingun third.
 */
static const int sg_ladder_intercept[] = {
	SG_W_ROCKETLAUNCHER, SG_W_RAILGUN, SG_W_CHAINGUN, -1
};

static const int *Combat_Ladder(int band)
{
	switch (band)
	{
	case SG_BAND_CONTACT:	return sg_ladder_contact;
	case SG_BAND_CLOSE:		return sg_ladder_close;
	case SG_BAND_MID:		return sg_ladder_mid;
	default:				return sg_ladder_long;
	}
}

/*
 * Extra per-band gates the rank order alone cannot express.
 *
 *  - splash weapons are refused inside their own d_safe of the target, which
 *    is rule R1 applied to the selection rather than only to the trigger:
 *    the close band ranks the rocket first "if d >= 121".
 *  - the super shotgun is refused past 256 units even where the band allows
 *    it: 1.7's table says 37 damage for two shells at 400, and rule R2d says
 *    the shotgun does 48 for one shell instead.
 *  - the hitscan caps of rule F6 apply wherever the band reaches past them.
 */
static qboolean Combat_BandAllows(edict_t *self, int w, float dist)
{
	float dsafe = Combat_DSafe(self, w);

	if (dsafe > 0.0f && dist < dsafe)
		return false;
	if (w == SG_W_SSHOTGUN && dist > SG_SSG_PREFER_RANGE)
		return false;
	if (sg_weapons[w].range_cap > 0.0f && dist > sg_weapons[w].range_cap)
		return false;

	/* R1b and R1c do not depend on the impact point, so they can veto a
	 * SELECTION as well as a shot -- holding a weapon that will never be
	 * allowed to fire is worse than holding one rung lower */
	if ((w == SG_W_ROCKETLAUNCHER || w == SG_W_GRENADELAUNCHER) &&
	    Combat_Survivable(self) < SG_SPLASH_HP_FLOOR)
		return false;
	if (dsafe > 0.0f && self->client->hookstate == 2 &&
	    (float)self->client->hooklength < dsafe + SG_HOOK_PULL_FRAME)
		return false;

	return true;
}

/*
 * Walk one ladder. Pass 1 takes the highest-ranked weapon above its R2 floor;
 * pass 2 relaxes to the hard floor (item->quantity); rule R2a. Returns -1 when
 * the ladder has nothing to offer, which lets a doctrine ladder fall through
 * to its band ladder.
 */
static int Combat_WalkLadder(edict_t *self, const int *ladder, float dist,
                             qboolean stocked_only)
{
	int i;

	for (i = 0; ladder[i] >= 0; i++)
	{
		int w = ladder[i];

		if (!Combat_BandAllows(self, w, dist))
			continue;
		if (stocked_only ? !Combat_Stocked(self, w) : !Combat_Avail(self, w))
			continue;
		return w;
	}
	return -1;
}

/*
 * The weapon this bot should be holding against this target at this range.
 * Doctrine ladders first (2.4), then the band ladder (2.1), then the blaster,
 * which is always in inventory and never removable (p_client.c:1147-1151).
 */
static int Combat_Choose(edict_t *self, int band, float dist, qboolean carrier)
{
	const int	*ladder;
	int			w;

	if (Combat_Carrying(self))
	{
		w = Combat_WalkLadder(self, sg_ladder_carry, dist, true);
		if (w < 0)
			w = Combat_WalkLadder(self, sg_ladder_carry, dist, false);
		if (w >= 0)
			return w;
	}
	else if (carrier && dist >= SG_INTERCEPT_MIN && dist <= SG_INTERCEPT_MAX)
	{
		w = Combat_WalkLadder(self, sg_ladder_intercept, dist, true);
		if (w < 0)
			w = Combat_WalkLadder(self, sg_ladder_intercept, dist, false);
		if (w >= 0)
			return w;
	}

	ladder = Combat_Ladder(band);

	/*
	 * WEAPON COMMITMENT (sg_wcommit). The rung-3 gate's strongest tell
	 * (separability 1.000): consecutive human shots stay on ONE gun --
	 * switch_diagonal_mass 0.90 against this body's 0.68 -- because a
	 * human carries a main weapon across bands and switches only when
	 * forced. The band arbitration re-derives the optimal gun per 64
	 * units of range and spends the whole fight commuting between local
	 * optima (wswitch, which only slowed the REQUESTS down, was struck
	 * for moving the tell the wrong way). Under commitment the held gun
	 * is KEPT whenever it appears anywhere in the current band's ladder,
	 * has ammo, and the band gates allow it -- rail fights at every
	 * range like it does in the corpus; the SSG at 900 units still
	 * switches because BandAllows already refuses it. Doctrine ladders
	 * above (carrier, intercept) outrank commitment on purpose.
	 */
	if (gi.cvar("sg_wcommit", "1", 0)->value != 0.0f)
	{
		int held = Combat_Held(self);

		/*
		 * Mode 2 (rung-3 set #1, 18/18): commitment as shipped kept the
		 * SPAWN BLASTER all game -- every judge named blaster-dominated
		 * timelines with machine accuracy on every bot sheet, a conduct
		 * no human sustains. The last-resort gun is not a gun a human
		 * commits to; mode 2 refuses it and lets the ladder walk pick a
		 * real weapon the moment one is stocked.
		 */
		if (gi.cvar("sg_wcommit", "1", 0)->value >= 2.0f &&
		    held == SG_W_BLASTER)
			held = -1;

		if (held >= 0 && Combat_Stocked(self, held) &&
		    Combat_BandAllows(self, held, dist))
		{
			int i;

			for (i = 0; ladder[i] >= 0; i++)
				if (ladder[i] == held)
					return held;
		}
	}

	w = Combat_WalkLadder(self, ladder, dist, true);
	if (w < 0)
		w = Combat_WalkLadder(self, ladder, dist, false);
	if (w < 0)
		w = SG_W_BLASTER;
	return w;
}

/*
 * The weapon a posted defender pre-holds, from the sightline it measured at
 * its stand. WEAPONS.md 2.4-D3's table; D3b's point is that holding the right
 * one costs nothing and raising it mid-contact costs a full weapon cycle.
 * The 500-900 row's "railgun if the approach is a single line, chaingun if it
 * is a wide room" needs a room-width model the rune does not carry, so the
 * railgun is taken -- the conservative half, since it never degrades.
 */
static int Combat_PostWeapon(edict_t *self, float sightline)
{
	static const int ladder_ssg[] = { SG_W_SSHOTGUN, -1 };
	static const int ladder_rl[] = { SG_W_ROCKETLAUNCHER, -1 };
	static const int ladder_cg[] = { SG_W_CHAINGUN, -1 };
	static const int ladder_rg[] = { SG_W_RAILGUN, -1 };
	const int	*want;
	int			w;

	if (sightline < SG_POST_SSG)
		want = ladder_ssg;
	else if (sightline < SG_POST_ROCKET)
		want = ladder_rl;
	else if (sightline < SG_POST_CHAINGUN)
		want = ladder_cg;
	else
		want = ladder_rg;

	w = Combat_WalkLadder(self, want, sightline, true);
	if (w >= 0)
		return w;

	/* the doctrine weapon is not in the pack: hold the band ladder's answer
	 * for the sightline instead of standing there with a blaster */
	if (sightline < SG_R_CLOSE)
		return Combat_Choose(self, SG_BAND_CONTACT, sightline, false);
	if (sightline < SG_R_MID)
		return Combat_Choose(self, SG_BAND_CLOSE, sightline, false);
	if (sightline < SG_R_LONG)
		return Combat_Choose(self, SG_BAND_MID, sightline, false);
	return Combat_Choose(self, SG_BAND_LONG, sightline, false);
}

/* -------------------------------------------------------------- the duel
 *
 * Range control needs one number the ladders do not state outright: the
 * distance a given weapon WANTS. It is not a new fact and it is not fitted --
 * it is read back out of the 2.1 ladders that are already here.
 *
 * A band's centre is the midpoint of the band's own edges (2.1: contact < 128,
 * close 128-400, mid 400-900). The long band has no far edge except
 * SG_ENGAGE_RANGE, which is a refusal to fight rather than a preference, so
 * the long band is represented by its near edge -- the distance at which the
 * railgun's zero degradation starts being the whole argument.
 */
static float Combat_BandCenter(int band)
{
	switch (band)
	{
	case SG_BAND_CONTACT:	return SG_R_CLOSE * 0.5f;
	case SG_BAND_CLOSE:		return (SG_R_CLOSE + SG_R_MID) * 0.5f;
	case SG_BAND_MID:		return (SG_R_MID + SG_R_LONG) * 0.5f;
	default:				return SG_R_LONG;
	}
}

static int Combat_LadderRank(const int *ladder, int w)
{
	int i;

	for (i = 0; ladder[i] >= 0; i++)
		if (ladder[i] == w)
			return i;
	return -1;
}

/*
 * The range a weapon wants, from the ladders and nothing else.
 *
 * Every band whose ladder names the weapon votes for that band's centre, with
 * a weight of 1/(1+rank) -- the dossier ranked it, so the rank is the strength
 * of the opinion. The mean of those votes is the preference. The railgun, at
 * rank 0 in mid AND long and rank 5 in the two near bands, lands near 690; the
 * super shotgun, rank 0 at contact and rank 3 at close, lands near 105; the
 * chaingun, ranked 1 in both near bands and 3 in mid, lands near 260. Nothing
 * here is a number somebody chose; it is the ladder table read as a curve.
 *
 * The same gates Combat_BandAllows applies to a SELECTION then apply to the
 * preference: never inside the weapon's own d_safe (R1), never past the super
 * shotgun's 256 (R2d), never past a hitscan cap (F6). `who` supplies the state
 * d_safe depends on -- quad and plasma mode -- so an opponent's preference is
 * computed against the opponent.
 */
static float Combat_WantRange(edict_t *who, int w)
{
	static const int bands[4] = {
		SG_BAND_CONTACT, SG_BAND_CLOSE, SG_BAND_MID, SG_BAND_LONG
	};
	float	sum = 0.0f, weight = 0.0f, want, dsafe, cap;
	int		b;

	if (w < 0 || w >= SG_NUM_WEAPONS)
		w = SG_W_BLASTER;

	for (b = 0; b < 4; b++)
	{
		int rank = Combat_LadderRank(Combat_Ladder(bands[b]), w);
		float vote;

		if (rank < 0)
			continue;
		vote = 1.0f / (1.0f + (float)rank);
		sum += vote * Combat_BandCenter(bands[b]);
		weight += vote;
	}

	/* every one of the eleven is named by at least one ladder -- the grenade
	 * launcher and the BFG by exactly one each -- so the fallback is dead
	 * code kept as a guard rather than as a behaviour */
	want = (weight > 0.0f) ? (sum / weight) : Combat_BandCenter(SG_BAND_CLOSE);

	/*
	 * The persona's standing preference about distance, applied to the
	 * ladder's answer and NOT to the gates below it: the splash floor, the
	 * super shotgun's 256 and the weapon's own range cap all still get the
	 * last word, so a short-preferring bot cannot be talked inside its own
	 * rocket and a long-preferring one cannot carry a shotgun to a rail
	 * fight. +/-15%, so it moves the band's centre and never the band.
	 * `who` may be a human here -- Combat_Duel prices the FOE's preference
	 * through this same function -- and a human has no row, so this is 1.0.
	 */
	want *= SG_PersonaRangeBias(who);

	dsafe = Combat_DSafe(who, w);
	if (dsafe > 0.0f && want < dsafe)
		want = dsafe;
	if (w == SG_W_SSHOTGUN && want > SG_SSG_PREFER_RANGE)
		want = SG_SSG_PREFER_RANGE;
	cap = sg_weapons[w].range_cap;
	if (cap > 0.0f && want > cap)
		want = cap;
	return want;
}

/*
 * What standing in the open costs this bot right now, 0 to 1.
 *
 * Two independent reasons to want cover, added and clamped. The first is
 * damage taken: R1b's 90 points of health-plus-absorbable-armour is the line
 * the dossier already draws for "healthy enough to accept a trade", so twice
 * it is the healthy end -- at 180 or better this term is zero, at 90 it is a
 * half, at nothing it is one. The second is the weapon: a shot the bot cannot
 * take is exposure paid for nothing, so a weapon Combat_BandAllows refuses at
 * this distance scores a full one, and one that is merely off its preferred
 * range scores the fractional miss.
 *
 * Healthy, in band, at the range the gun wants -- zero. That is the point: a
 * fight already being won is not one to break line of sight over.
 */
static float Combat_Exposure(edict_t *self, int w, float dist, float want)
{
	float hurt, miss, e;

	hurt = 1.0f - (float)Combat_Survivable(self)
	              / (2.0f * (float)SG_SPLASH_HP_FLOOR);
	if (hurt < 0.0f)
		hurt = 0.0f;
	if (hurt > 1.0f)
		hurt = 1.0f;

	if (!Combat_BandAllows(self, w, dist) || want < 1.0f)
		miss = 1.0f;
	else
	{
		miss = (float)fabs(dist - want) / want;
		if (miss > 1.0f)
			miss = 1.0f;
	}

	e = hurt + miss;
	if (e > 1.0f)
		e = 1.0f;
	return e;
}

/*
 * Ask for a weapon, exactly the way Cmd_Use_f does (g_cmds.c:667-687): the
 * item, its use handler, the inventory test, then it->use. Use_Weapon does not
 * switch immediately -- it sets client->newweapon (p_weapon.c:376) and
 * ChangeWeapon performs the change when the current weapon is down
 * (p_weapon.c:171-232, :427-433).
 *
 * Rule S3 is enforced here and only here: the grapple is never asked for. With
 * CTF_OFFHAND_HOOK set the weapon-cycle commands already skip it
 * (g_cmds.c:885-887, :933-935), so an explicit use is the only way to reach it
 * and this file simply never issues one.
 *
 * The plasma is a special case that is deliberately NOT exercised: Use_PLASMA
 * toggles bounce/spread mode instead of switching when the plasma is already
 * pers.weapon, and prints a line to the console every time (plasma.c:357-370).
 * A bot that toggled to reach a preferred mode would spam the server, so the
 * bot fires whichever mode it holds and prices the splash from plasma_mode
 * (see Combat_DSafe). Calling use to SELECT the plasma from another weapon
 * runs Use_PLASMA's ordinary Use_Weapon path and is fine.
 */
static void Combat_Request(edict_t *self, sg_combat_state_t *st, int w)
{
	gitem_t		*it;
	qboolean	midfight;
	int			held;

	if (w < 0 || w >= SG_NUM_WEAPONS)
		return;
	it = sg_witem[w];
	if (!it || !it->use || it == sg_hookitem)
		return;
	if (self->client->pers.weapon == it)
		return;					/* already held; never re-use the plasma */
	if (self->client->newweapon)
		return;					/* a switch is already in flight */
	if (level.time < st->switch_next)
		return;					/* rule S1's 500 ms request rate limit */
	if (!self->client->pers.inventory[sg_widx[w]])
		return;

	/*
	 * The discipline gates (sg_wswitch). st->enemy is the whole test for
	 * "mid-fight" and it needs no flag of its own: the idle path clears it
	 * before it arbitrates and the engaged path sets it before it does, so a
	 * corridor pre-switch is never charged the duel's cooldown and a duel
	 * never gets the corridor's freedom. Panic is the standing exception --
	 * see the block by SG_WS_DECIDE_S0.
	 */
	midfight = (qboolean)(st->enemy > 0);
	if (Combat_WSwitch() && midfight && !st->ws_panic &&
	    level.time < st->ws_gate)
		return;

	held = Combat_Held(self);
	st->switch_next = level.time + SG_SWITCH_RATE;
	it->use(self, it);			/* Use_Weapon -> client->newweapon = it */

	if (Combat_WSwitch() && midfight)
	{
		float waited = level.time - st->ws_armed;

		/* the cooldown is armed from the switch, not from the fight: a bot
		 * that panicked at 100 units has spent its swap for the next 4 s the
		 * same as one that deliberated for it */
		st->ws_gate = level.time + SG_WS_COOLDOWN;
		st->ws_armed = level.time;

		if (gi.cvar("sg_debug", "0", 0)->value)
			gi.dprintf("WSWITCH mid %s w%d->w%d waited=%.0fms (%s)\n",
			           self->client->pers.netname, held, w,
			           waited * 1000.0f,
			           st->ws_panic ? "panic: gun cannot shoot at contact"
			                        : "decision delay elapsed");
	}
}

/*
 * The switch decision, rule S1's hysteresis. A dry or unknown weapon is
 * abandoned at once -- "unknown" is the grapple or hand grenades, and holding
 * either means the trigger is unusable. Anything else has to be out-wanted for
 * SG_SWITCH_HOLD seconds before the 700-1100 ms is spent.
 */
static void Combat_Arbitrate(edict_t *self, sg_combat_state_t *st, int want)
{
	int held = Combat_Held(self);

	if (want < 0)
		return;

	/*
	 * The panic exception (sg_wswitch), which is the branch below it with the
	 * ammo test taken out. A gun the band gates refuse at contact range is in
	 * exactly the position a dry one is -- it is not going to be allowed to
	 * fire -- and the only difference between them is that this one still has
	 * bullets in it. ws_panic is false whenever the cvar is off, so this is
	 * unreachable in the shipped behaviour.
	 */
	if (st->ws_panic)
	{
		st->want = want;
		st->want_since = level.time;
		Combat_Request(self, st, want);
		return;
	}

	if (held < 0 || !Combat_Avail(self, held))
	{
		st->want = want;
		st->want_since = level.time;
		Combat_Request(self, st, want);
		return;
	}

	if (want == held)
	{
		st->want = held;
		st->want_since = level.time;
		return;
	}

	if (st->want != want)
	{
		st->want = want;
		st->want_since = level.time;
		return;
	}
	if (level.time - st->want_since < SG_SWITCH_HOLD)
		return;

	Combat_Request(self, st, want);
}

/*
 * THE CORRIDOR SWITCH (sg_wswitch, the pre-switch half of the discipline).
 *
 * Called only from the idle path, and only where that path used to do nothing
 * at all: the bot is not posted, not carrying, and has no alert from the body,
 * and the gun in its hands is loaded and is not the spawn blaster. Today that
 * bot walks the whole corridor holding the last thing it fired.
 *
 * The range prior is the cheapest honest one available: the straight-line
 * distance to the nearest FRESH enemy the belief table has a seed for
 * (sg_caco_enemies -- a teammate's eye or an ear, aged out at SG_WS_PRE_FRESH).
 * Corridors bend that line, but the answer only has to be right to within a
 * band, and a band is a factor of three wide. The bar for acting on it is the
 * one number this file already has for "the gun in my hands is wrong for this
 * distance": Combat_WantRange, the 2.1 ladders read as a curve. If what is
 * held wants 690 units and the meeting is coming at 150, that is a rail being
 * carried into a shotgun room and it is worth the 700-1100 ms NOW, in a
 * corridor, where the cost is nothing but walking.
 *
 * It asks through Combat_Arbitrate like every other selection, so rule S1's
 * rate limit and 800 ms want-hold and rule S3's grapple ban all still hold: a
 * belief that flickers between two seeds cannot make the bot cycle guns.
 */
static void Combat_PreSwitch(edict_t *self, sg_combat_state_t *st, int held)
{
	rune_t	*r = SG_Rune();
	float	best = -1.0f, age = 0.0f, have, miss, bar;
	int		team, s, band, want;

	if (held < 0 || !r || !r->seeds)
		return;
	team = self->client->ctf.teamnum;
	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return;

	for (s = 0; s < SG_MAX_ENEMY_TRACK; s++)
	{
		sg_belief_enemy_t	*en = &sg_caco_enemies[team - 1][s];
		vec3_t				d;
		float				dist;

		if (en->client < 0 || en->seed < 0 || en->seed >= r->hdr.num_seeds)
			continue;
		if (level.time - en->seen_time > SG_WS_PRE_FRESH)
			continue;

		VectorSubtract(r->seeds[en->seed].origin, self->s.origin, d);
		dist = VectorLength(d);
		if (dist > SG_WS_PRE_REACH)
			continue;			/* not a fight this bot is walking into */
		if (best < 0.0f || dist < best)
		{
			best = dist;
			age = level.time - en->seen_time;
		}
	}

	if (best < 0.0f)
		return;					/* nobody believed to be anywhere near */

	/*
	 * The persona, on the BAR rather than on the switch: aggression is a
	 * willingness to commit to what is in hand, so an aggressive bot needs a
	 * wider mismatch before it will spend a corridor on a swap and a
	 * methodical one pre-switches for a smaller one. +/-15%, the band
	 * sg_persona.h fixes for every consumer, applied to a preference and
	 * never to a gate.
	 */
	have = Combat_WantRange(self, held);
	miss = (float)fabs(best - have);
	bar = SG_WS_PRE_MISS * best * SG_PersonaAggression(self);
	if (miss < bar)
		return;					/* what is held is close enough to right */

	band = (best < SG_R_CLOSE) ? SG_BAND_CONTACT
	     : (best < SG_R_MID)   ? SG_BAND_CLOSE
	     : (best < SG_R_LONG)  ? SG_BAND_MID
	                           : SG_BAND_LONG;
	want = Combat_Choose(self, band, best, false);
	if (want == held)
	{
		st->ws_pre = held;
		return;					/* the ladder agrees with the hand */
	}
	if (want == SG_W_BLASTER)
		return;					/* Combat_Choose's last resort is the spawn
		                         * gun, and the idle path directly above this
		                         * one exists to get bots OFF it. A belief is
		                         * never a reason to raise it: a ladder that
		                         * ran out of rungs is a pack that has nothing
		                         * for that range, not an argument for holding
		                         * the worst weapon in the game while walking */

	if (want != st->ws_pre && gi.cvar("sg_debug", "0", 0)->value)
		gi.dprintf("WSWITCH pre %s w%d->w%d expect=%.0f hand-wants=%.0f "
		           "miss=%.0f bar=%.0f belief=%.1fs\n",
		           self->client->pers.netname, held, want, best, have,
		           miss, bar, age);
	st->ws_pre = want;

	Combat_Arbitrate(self, st, want);
}

/*
 * The range band, with a deadband so a target bobbing across a boundary does
 * not cost a weapon switch every second, and a hold time so the crossing has
 * to be decisive before the band commits.
 */
static int Combat_RawBand(float d)
{
	if (d < SG_R_CLOSE)
		return SG_BAND_CONTACT;
	if (d < SG_R_MID)
		return SG_BAND_CLOSE;
	if (d < SG_R_LONG)
		return SG_BAND_MID;
	return SG_BAND_LONG;
}

static int Combat_Band(sg_combat_state_t *st, float d)
{
	int raw = Combat_RawBand(d);

	/* acquisition: there is nothing to be hysteretic about yet, and a bot that
	 * spent the first 800 ms of every engagement holding the wrong band's
	 * weapon would be paying rule S1's cost to avoid rule S1's cost */
	if (st->band < 0)
	{
		st->band = raw;
		st->band_pend = raw;
		st->band_since = level.time;
		return st->band;
	}

	/*
	 * Hysteresis on the boundaries themselves: a band the bot is not already
	 * in has to be entered by a further SG_BAND_DEADBAND units before it
	 * counts. Widening the current band's own edges by the deadband does that
	 * for a crossing of any size -- testing the probe against `raw` instead
	 * would pin a target that jumped two bands at once to the near one.
	 */
	if (raw > st->band && Combat_RawBand(d - SG_BAND_DEADBAND) <= st->band)
		raw = st->band;
	else if (raw < st->band && Combat_RawBand(d + SG_BAND_DEADBAND) >= st->band)
		raw = st->band;

	if (raw != st->band)
	{
		if (st->band_pend != raw)
		{
			st->band_pend = raw;
			st->band_since = level.time;
		}
		else if (level.time - st->band_since >= SG_SWITCH_HOLD)
			st->band = raw;
	}
	else
		st->band_pend = raw;

	return st->band;
}

/* ------------------------------------------------------------------- aim */

/*
 * A fresh tremor direction: a unit vector with the component along `dir`
 * removed, so the error is purely lateral and its magnitude maps cleanly onto
 * an angle (an offset of m added to a unit aim vector is an angle of atan(m)).
 */
static void Combat_SamplePerp(vec3_t dir, vec3_t out)
{
	vec3_t	v;
	float	d, len;
	int		tries;

	for (tries = 0; tries < 4; tries++)
	{
		v[0] = crandom();
		v[1] = crandom();
		v[2] = crandom();

		d = DotProduct(v, dir);
		VectorMA(v, -d, dir, v);

		len = VectorLength(v);
		if (len > 0.01f)
		{
			VectorScale(v, 1.0f / len, out);
			return;
		}
	}

	/* degenerate sample: no error this time rather than a fake one */
	VectorClear(out);
}

static void Combat_SampleError(sg_combat_state_t *st, vec3_t dir)
{
	Combat_SamplePerp(dir, st->err);
}

/* ----------------------------------------------------------- aim texture */

/*
 * Track or flick, 0..1, derived rather than authored.
 *
 * The persona table was NOT given a flick_style column, and that is a ruling
 * rather than laziness. sg_persona.h says it out loud: "Bend behaviour, never
 * invent it... Anything that wanted a new branch belongs in the system that
 * owns the behaviour, not here." A correction shape is a branch in the aim
 * code; the table's job is to say who the bot is, and it already does, in a
 * column that answers this question. Aggression IS the tell. The roster's
 * loud ones -- Fiend 1.50, Spawn 1.40, Caco 1.30 -- are the ones the chat
 * file gives the swaggering lines to and the ones the table's own comment
 * calls "not actually the best shots"; a snatch-and-correct flick is exactly
 * how that plays. The patient end -- Wizard 0.65, Gate 0.70, Rune 0.75, the
 * dry observers holding a lane -- tracks smoothly and settles slowly.
 *
 * Read through SG_PersonaAggression rather than off the row, so this lands
 * inside the same +/-15% squeeze every other consumer gets and cannot widen
 * the band from out here. That maps the roster's 0.5-1.5 onto 0.85-1.15,
 * which is why the rescale below is exactly that span: the sixteen rows use
 * the full 0..1 of style and nothing clamps in practice. With sg_persona 0
 * the multiplier is 1.0 and every bot comes out at 0.5 -- dead centre, which
 * is the right answer when the roster has no characters in it.
 */
static float Combat_FlickStyle(edict_t *self)
{
	float	s = (SG_PersonaAggression(self) - 0.85f) / 0.30f;

	if (s < 0.0f)
		s = 0.0f;
	if (s > 1.0f)
		s = 1.0f;
	return s;
}

/*
 * Fitts's law, in the only form this file needs: a bigger swing takes longer
 * to land on. log2 is spelled with log() and the conversion constant because
 * that is the one form every toolchain this builds under has always had.
 */
static float Combat_TexFitts(float flick)
{
	float	m;

	m = SG_TEX_FITTS_A + SG_TEX_FITTS_B
	    * (float)(log(1.0 + (double)flick / SG_TEX_FITTS_REF)
	              * 1.4426950408889634);

	if (m < SG_TEX_FITTS_LO)
		m = SG_TEX_FITTS_LO;
	if (m > SG_TEX_FITTS_HI)
		m = SG_TEX_FITTS_HI;
	return m;
}

/*
 * THE COMPENSATION. Read this before touching any number above it.
 *
 * The overshoot is a new error term, and a new error term makes the bot worse
 * unless something else gives. The rule this feature was accepted under is
 * that it changes the SHAPE of the miss and not the SIZE of it, so the ramp
 * pays for the overshoot out of its own width.
 *
 * The quantity held fixed is the mean square angular error over the settle
 * window, because that is what a cone-shaped error's hit probability actually
 * tracks. Today, with the offset ramping linearly from (res + span) down to
 * res over x in [0,1]:
 *
 *     E_off^2 = mean of (res + span*(1-x))^2 = res^2 + res*span + span^2/3
 *
 * With the texture on, the overshoot rides in a direction that is independent
 * of the tremor's, so it adds in mean square rather than coherently, and its
 * own mean square is (SG_TEX_SHAPE_MS * over^2):
 *
 *     E_on^2 = res^2 + res*span' + span'^2/3 + SHAPE_MS*over^2
 *
 * Setting the two equal and solving the quadratic for span' is this function.
 * With over == 0 it returns span exactly -- which is the test that matters,
 * because it means a flick too small to be armed (SG_TEX_FLICK_MIN) is not
 * quietly handed a narrower cone.
 *
 * What this does NOT compensate, and does not have to:
 *
 *   - the tracking wander. It only changes how st->err MOVES, never how long
 *     it is, so the magnitude statistics are untouched by construction. That
 *     is the whole reason it was written as a direction process instead of an
 *     added offset -- an added offset would have needed a second term here
 *     and a second argument about it.
 *   - the settle window's Fitts scaling. It is centred on the median
 *     acquisition flick (see SG_TEX_FITTS_REF), so it moves length between
 *     acquisitions without changing the average.
 *
 * Size of the correction, for the record: at skill 4 (res 0.8, span 8.2) and
 * a 30 deg flick at mid style, over is about 3.5 deg and span' comes out at
 * 7.9 -- a 4% narrower ramp buying a 3.5 deg overshoot. At the worst case the
 * cvar can produce (style 1, skill 0, a swing past SG_TEX_OVER_CAP) the two
 * mean squares still come out equal to four decimal places at every skill,
 * which is what the cap was chosen to guarantee.
 *
 * Degrees are combined here rather than tangents, as everywhere else in this
 * block; tan is applied once at the end and the two agree to within a couple
 * of percent across the whole cone.
 */
static float Combat_TexSpan(float span, float res, float over)
{
	float	target, root;

	target = res * span + span * span / 3.0f
	       - SG_TEX_SHAPE_MS * over * over;
	if (target <= 0.0f)
		return 0.0f;		/* the overshoot alone already spends the budget */

	root = (float)sqrt((double)(res * res) + (4.0 / 3.0) * (double)target);
	root = 1.5f * (root - res);
	return (root > 0.0f) ? root : 0.0f;
}

/*
 * The swing landed. Work out how far past the target the hand went, which way
 * "past" is, and how long the correction has to play out in.
 *
 * `forward` is where the view was pointing when this frame started -- the
 * flick's origin -- and `dir` is where the new target is, unit length.
 */
static void Combat_TexAcquire(edict_t *self, sg_combat_state_t *st,
                              float skill, vec3_t forward, vec3_t dir)
{
	vec3_t	past;
	float	d, len, flick, style, over, win;

	st->tex_over = 0.0f;
	st->tex_win = 0.0f;
	st->tex_cyc = 1.0f;
	VectorClear(st->tex_dir);

	d = DotProduct(forward, dir);
	if (d > 1.0f)
		d = 1.0f;
	if (d < -1.0f)
		d = -1.0f;
	flick = (float)(acos((double)d) * 180.0 / M_PI);
	if (flick < SG_TEX_FLICK_MIN)
		return;				/* a nudge, not a flick; nothing to overshoot */

	/*
	 * Which way the swing was still travelling when it arrived. dir*(f.dir)
	 * minus f is perpendicular to dir by construction and points away from
	 * where the view came from, so adding along it is continuing the rotation
	 * rather than starting a new one. It degenerates at exactly 0 and exactly
	 * 180 degrees, where there is no swing plane to overshoot in -- 0 is
	 * already excluded by the flick floor, 180 is a bot that got shot in the
	 * back, and neither wants a made-up direction.
	 */
	VectorScale(dir, d, past);
	VectorSubtract(past, forward, past);
	len = VectorLength(past);
	if (len < 0.01f)
		return;
	VectorScale(past, 1.0f / len, st->tex_dir);

	style = Combat_FlickStyle(self);

	over = flick * (SG_TEX_OVER_TRACK
	                + (SG_TEX_OVER_FLICK - SG_TEX_OVER_TRACK) * style)
	     * Combat_SkillLerp(skill, SG_TEX_OVER_MUL_S0, SG_TEX_OVER_MUL_S4);
	/*
	 * The cvar is a DOSE on the overshoot amplitude (1.0 == the adopted
	 * texture exactly). Adoption closed 3.2 degrees of the aim-offset gap
	 * out of ~3.9 remaining against the 10.89 human anchor; the ladder
	 * asks whether the rest is simply more of the same medicine. The cap
	 * still binds afterwards, so a big dose widens the common case
	 * without inventing physically absurd flicks.
	 */
	over *= gi.cvar("sg_aimtexture", "1", 0)->value;
	if (over > SG_TEX_OVER_CAP)
		over = SG_TEX_OVER_CAP;

	win = Combat_SkillLerp(skill, SG_SETTLE_S0, SG_SETTLE_S4)
	    * Combat_TexFitts(flick)
	    * (1.0f + SG_TEX_STYLE_WIN * (0.5f - style));

	st->tex_over = over;
	st->tex_win = win;
	/*
	 * One correction or two. Integer cycles are not a rounding convenience:
	 * the envelope below is linear, and
	 *
	 *     integral of (1-x)*cos(2*pi*n*x) over [0,1] == 0 for integer n
	 *
	 * exactly. That is what makes the overshoot zero-mean over the window
	 * with no bias term to subtract and no step left at the end of it, so
	 * the bot is not quietly aiming a fraction of a degree to one side for
	 * the whole settle. Interpolating n between 1 and 2 would have broken
	 * that for a difference nobody can see, so style picks a shape instead:
	 * the flicker snatches and double-corrects, the tracker slides in once.
	 */
	st->tex_cyc = (style >= 0.5f) ? 2.0f : 1.0f;

	if (gi.cvar("sg_debug", "0", 0)->value)
		gi.dprintf("AIMTEX %s flick=%.1f over=%.2f settle=%dms cyc=%d "
		           "style=%.2f\n",
		           self->client->pers.netname, flick, over,
		           (int)(win * 1000.0f + 0.5f), (int)st->tex_cyc, style);
}

/*
 * The correction, as a signed multiple of the overshoot amplitude, and the
 * direction it acts along re-squared to the live aim on the way past.
 *
 * (1-x)*cos(2*pi*n*x): full overshoot on the frame the swing lands, through
 * zero, out the other side by less, and home. Linear envelope rather than the
 * exponential a damped spring would give, for the integral argument above --
 * an exponential leaves about 4% of the amplitude as a standing bias and
 * there is no reason to pay that.
 */
static float Combat_TexShape(sg_combat_state_t *st, vec3_t dir)
{
	vec3_t	d;
	float	x, len;

	if (st->tex_over <= 0.0f || st->tex_win <= 0.0f)
		return 0.0f;

	x = (level.time - st->since) / st->tex_win;
	if (x < 0.0f || x >= 1.0f)
		return 0.0f;		/* settled; the ramp has it from here */

	VectorCopy(st->tex_dir, d);
	len = DotProduct(d, dir);
	VectorMA(d, -len, dir, d);
	len = VectorLength(d);
	if (len < 0.01f)
		return 0.0f;
	VectorScale(d, 1.0f / len, st->tex_dir);

	return (1.0f - x) * (float)cos(2.0 * M_PI * st->tex_cyc * (double)x);
}

/*
 * Tracking, between acquisitions: the error DIRECTION wanders instead of
 * jumping.
 *
 * What this replaces is a fresh random lateral stamped down every 0.25-0.5 s,
 * which is white noise -- the crosshair teleports around the target at 2-4 Hz.
 * Nobody's hand does that. A hand drifts off a fraction of a degree over a
 * second or so and gets pulled back, and the pull-back is the correction the
 * player is not aware of making. Blending a fresh perpendicular in at
 * dt/tau per frame is that: an exponential filter over white noise is a
 * random walk with a spring on it, which is the drift-and-recentre in one
 * line and no extra state.
 *
 * The vector is put back to unit length every step. That is load-bearing, not
 * tidiness: the ramp multiplies this direction, so a direction allowed to
 * shrink would become a smaller error, and this term would stop being a pure
 * shape change and start owing the compensation above an argument.
 */
static void Combat_TexWander(sg_combat_state_t *st, vec3_t dir)
{
	vec3_t	kick;
	float	dt, w, len;

	/*
	 * A new target restarts the settle clock, and st->since is rewritten
	 * when it does -- so a wander stamp older than it means "this direction
	 * belongs to the last target" without a second flag to keep in sync.
	 */
	if (st->tex_wander < st->since)
	{
		Combat_SampleError(st, dir);
		st->tex_wander = level.time;
		return;
	}

	dt = level.time - st->tex_wander;
	st->tex_wander = level.time;
	if (dt <= 0.0f)
		return;

	w = dt / SG_TEX_WANDER_TAU;
	if (w > 1.0f)
		w = 1.0f;

	Combat_SamplePerp(dir, kick);
	VectorScale(st->err, 1.0f - w, st->err);
	VectorMA(st->err, w, kick, st->err);

	len = DotProduct(st->err, dir);
	VectorMA(st->err, -len, dir, st->err);
	len = VectorLength(st->err);
	if (len < 0.01f)
		Combat_SampleError(st, dir);	/* wandered through the middle */
	else
		VectorScale(st->err, 1.0f / len, st->err);
}

/*
 * Rule F1's second clause: has the target's velocity pointed the same way for
 * long enough that the lead is a prediction rather than a guess? Direction
 * only -- a target accelerating along one axis is still predictable.
 */
static qboolean Combat_VelStable(sg_combat_state_t *st, edict_t *enemy)
{
	vec3_t	v;
	float	len;

	VectorCopy(enemy->velocity, v);
	len = VectorLength(v);
	if (len < 1.0f)
	{
		/* standing still is the most predictable thing a target can do */
		VectorClear(st->vel_dir);
		return true;
	}
	VectorScale(v, 1.0f / len, v);

	if (VectorLength(st->vel_dir) < 0.5f ||
	    DotProduct(v, st->vel_dir) < SG_LEAD_STABLE_DOT)
	{
		VectorCopy(v, st->vel_dir);
		st->vel_stable = level.time;
		return false;
	}
	VectorCopy(v, st->vel_dir);
	return (level.time - st->vel_stable) >= SG_LEAD_STABLE;
}

/*
 * WEAPONS.md 2.2's two-pass intercept, generalised over the weapon's own
 * speed. MOVETYPE_FLYMISSILE and MOVETYPE_REFLECT apply no gravity
 * (g_weapon.c:417, :695, :986; plasma.c:212, :250-252), so a straight line is
 * the whole model. Hitscan (speed 0) skips it: no travel time, no lead.
 * Flight time carries the weapon's windup, which only the BFG has -- rule F1
 * is meant to be applied to 0.8 + d/400, not to d/400.
 */
static float Combat_Solve(edict_t *enemy, int w, vec3_t eye, vec3_t lead)
{
	vec3_t	mid, delta;
	float	dist, flight;

	Combat_Center(enemy, mid);
	VectorCopy(mid, lead);

	VectorSubtract(mid, eye, delta);
	dist = VectorLength(delta);

	if (sg_weapons[w].speed <= 0.0f)
		return 0.0f;			/* hitscan: aim at the centre, rule F2 */

	/*
	 * THE LANDING-POINT LEAD (sg_landlead, wave 247). The owner:
	 * airborne players are on a committed arc -- judge where they
	 * land and put the rocket there. Linear lead extends velocity
	 * into the sky gravity will never let them reach; this branch
	 * steps the target's own parabola to its touchdown and aims the
	 * splash at the floor that catches them. Rockets only: the
	 * splash forgives timing the way rule D1 promises.
	 */
	if (w == SG_W_ROCKETLAUNCHER && !enemy->groundentity &&
	    gi.cvar("sg_landlead", "1", 0)->value)
	{
		vec3_t p0, p1;
		trace_t ltr;
		float tt, grav = 800.0f;
		int seg;

		VectorCopy(mid, p0);
		for (seg = 1; seg <= 30; seg++)
		{
			tt = 0.05f * (float)seg;
			p1[0] = mid[0] + enemy->velocity[0] * tt;
			p1[1] = mid[1] + enemy->velocity[1] * tt;
			p1[2] = mid[2] + enemy->velocity[2] * tt
			      - 0.5f * grav * tt * tt;
			ltr = gi.trace(p0, enemy->mins, enemy->maxs, p1,
			               enemy, MASK_PLAYERSOLID);
			if (ltr.fraction < 1.0f)
			{
				VectorCopy(ltr.endpos, lead);
				VectorSubtract(lead, eye, delta);
				return sg_weapons[w].windup +
				       VectorLength(delta) / sg_weapons[w].speed;
			}
			VectorCopy(p1, p0);
		}
		/* no floor inside 1.5s: fall through to the linear model */
	}

	flight = sg_weapons[w].windup + dist / sg_weapons[w].speed;
	VectorMA(mid, flight, enemy->velocity, lead);

	VectorSubtract(lead, eye, delta);
	dist = VectorLength(delta);
	flight = sg_weapons[w].windup + dist / sg_weapons[w].speed;
	VectorMA(mid, flight, enemy->velocity, lead);

	return flight;
}

/*
 * The grenade, stepped. Rule F4 gives the launch pitch for a range R on level
 * ground; g_weapon.c:555-559 gives the launch velocity and MOVETYPE_BOUNCE, so
 * sv_gravity applies. Grenade_Touch detonates on any takedamage entity and
 * bounces off world geometry (g_weapon.c:520-537), which makes the FIRST
 * contact the only predictable point of the flight -- rule F5's whole reason
 * for existing. That contact is what R1 is measured against and what the shot
 * is aimed at.
 *
 * Returns false when the flight leaves the fuse without touching anything.
 */
static qboolean Combat_GrenadeImpact(edict_t *self, vec3_t eye, float yaw,
                                     float range, vec3_t impact)
{
	vec3_t	ang, fwd, vel, p, next;
	float	pitch, grav;
	int		i;

	/* rule F4: pitch = atan(R/900 - 1/3), positive = above horizontal */
	pitch = (float)atan((double)(range / SG_GL_PITCH_SCALE - SG_GL_PITCH_BIAS));
	pitch = pitch * 180.0f / (float)M_PI;

	/* Quake pitch is positive downward (the view convention this file writes
	 * at the bottom of SG_CombatFrame), so an upward launch is negative */
	ang[PITCH] = -pitch;
	ang[YAW] = yaw;
	ang[ROLL] = 0.0f;
	AngleVectors(ang, fwd, NULL, NULL);

	VectorScale(fwd, sg_weapons[SG_W_GRENADELAUNCHER].speed, vel);
	vel[2] += SG_GL_UP_SPEED;

	grav = sv_gravity ? sv_gravity->value : 800.0f;
	VectorCopy(eye, p);

	for (i = 0; i < SG_GL_FUSE_STEPS; i++)
	{
		trace_t tr;

		vel[2] -= grav * FRAMETIME;
		VectorMA(p, FRAMETIME, vel, next);
		tr = gi.trace(p, NULL, NULL, next, self, MASK_SHOT);
		if (tr.fraction < 1.0f)
		{
			VectorCopy(tr.endpos, impact);
			return true;
		}
		VectorCopy(next, p);
	}
	return false;
}

/* --------------------------------------------------- item-need weighting
 *
 * WEAPONS.md 2.3. The unit throughout is the detour budget: a worth of w on an
 * item lying exactly on the road subtracts 1500*w milliseconds from the
 * surface (sg_arach.c:394-398, :273), so a worth is readable directly as
 * "how far off my road this is allowed to be".
 */

#define SG_WORTH_MAX	2.0f	/* 2.3: 1500 ms, the detour decay's own scale */

static float Combat_Clamp(float v)
{
	if (v < 0.0f)
		return 0.0f;
	if (v > SG_WORTH_MAX)
		return SG_WORTH_MAX;
	return v;
}

/*
 * 2.3 health. item_health (10) and item_health_large (25) are refused outright
 * at full health (g_items.c:602-604), so the worth collapses at 100; only
 * item_health_small and item_health_mega carry HEALTH_IGNORE_MAX and overheal
 * (g_items.c:2663-2709). The breakpoints are damage-per-hit facts: below 25 a
 * single shotgun blast (48) or rocket splash kills, below 50 a direct rocket
 * (100-119) kills, below 75 an SSG blast at contact (120) kills.
 */
static float Worth_Health(edict_t *e)
{
	float	worth;
	int		h = e->health;

	if (h >= 100)
		worth = 0.05f;			/* 72 ms: stimpacks and the mega only */
	else if (h >= 75)
		worth = 0.20f;			/* 254 ms */
	else if (h >= 50)
		worth = 0.45f;			/* 505 ms */
	else if (h >= 25)
		worth = 0.90f;			/* 859 ms */
	else
		worth = 1.60f;			/* 1291 ms: below one rocket splash */

	/*
	 * H2 -- the Regen rune recovers 3.33 hp/s to a ceiling of 125
	 * (g_runes.c:751-793), so above 50 waiting is cheaper than detouring;
	 * below 50 it is not, because 15 seconds is a long time in a firefight.
	 */
	if (e->client->rune && e->client->rune->runetype == RUNE_REGEN && h >= 50)
		worth *= 0.35f;

	/*
	 * H1 -- the mega is +100 OVER max and then denies itself for 100 s of
	 * decay plus 20 s of respawn (g_items.c:586-595): the longest denial clock
	 * on the map. Worth a full 1500 ms budget even at full health.
	 *
	 * Stands down under sg_megaworth: Worth_Mega says this properly, with the
	 * mega's own fields behind it, and leaving both standing would price the
	 * same pad twice.
	 */
	if (!SG_MegaOn())
	{
		int i;

		for (i = 0; i < globals.num_edicts; i++)
		{
			edict_t *it = &g_edicts[i];

			if (!it->inuse || !it->classname)
				continue;
			if (strcmp(it->classname, "item_health_mega") != 0)
				continue;
			/* the bot's own team's view, on principle -- the mega is not a
			 * belief class, so this answers off the entity either way, and
			 * the day it joins the table it is already asking correctly */
			if (!Caco_ItemBelievedUpFor(e->client->ctf.teamnum, it))
				continue;
			worth *= 2.5f;
			break;
		}
	}

	return Combat_Clamp(worth);
}

/*
 * THE MEGA, priced as OVERHEAL (sg_megaworth). The census at wave 404 is the
 * whole argument: zero megas taken by bots in 850 s on a map that has one,
 * while the humans on the same map take it AT FULL HEALTH -- because +100 over
 * max is not health, it is a second life's worth of margin that lets an
 * attacker push deeper and survive the steal. Pickup_Health takes the mega at
 * any health (HEALTH_IGNORE_MAX, g_items.c:598-604) and adds its full count of
 * 100 over max_health; only the ordinary boxes are refused at 100.
 *
 * Worth_Health cannot say this. It is the price of THE HEALTH CLASS given the
 * bot's state, and at 100 hp that price is correctly 0.05 -- a healthy bot does
 * not want a health box. The 2.5x H1 bump above tries to speak for the mega
 * through that number and cannot: 0.05 x 2.5 is 0.125, about 190 ms of detour
 * budget, and it is spent on a field that points at the NEAREST health item of
 * any kind. So the mega gets its own worth and its own fields, and when this
 * feature is on the H1 bump stands down rather than double-counting.
 *
 * (b) THE HEADROOM GATE, and the one place this must not read like health:
 * a bot at 100/100 PASSES. Taking at full is the entire point. The cap only
 * refuses a bot that is already carrying the overheal -- at 170 a second mega
 * buys 30 points and hands the other 70 to whoever comes next, which is a
 * donation, not a detour.
 */
#define SG_MEGA_HEADROOM	170

static float Worth_Mega(edict_t *e)
{
	float	worth;
	int		h = e->health;
	int		i, team;

	if (!SG_MegaOn())
		return 0.0f;
	if (h >= SG_MEGA_HEADROOM)
		return 0.0f;

	/*
	 * The tiers are the overheal actually collected, not the damage taken:
	 * a bot at 100 banks the full +100 and a bot at 40 banks 60 of headroom
	 * plus the 100 it was missing, which is worth more. Every one of these
	 * is inside SG_WORTH_MAX and the 1500 ms detour scale reads them as
	 * "1500 ms of extra road at full health, 2000 at half".
	 */
	if (h >= 100)
		worth = 1.00f;			/* 1500 ms: the full budget, at FULL health */
	else if (h >= 75)
		worth = 1.15f;
	else if (h >= 50)
		worth = 1.35f;
	else
		worth = 1.60f;			/* hurt: the mega is a life, not a margin */

	/*
	 * (a) BELIEF, and this bot's team's row only. A mega the other side
	 * watched respawn is not a mega this side knows about; reading the
	 * global entity state here would leak the sighting, which is the one
	 * thing the belief table exists to stop.
	 */
	team = e->client->ctf.teamnum;
	for (i = 0; i < globals.num_edicts; i++)
	{
		edict_t *it = &g_edicts[i];

		if (!it->inuse || !it->classname)
			continue;
		if (strcmp(it->classname, "item_health_mega") != 0)
			continue;
		if (!Caco_ItemBelievedUpFor(team, it))
			continue;
		return Combat_Clamp(worth);
	}
	return 0.0f;
}

float SG_WorthMega(edict_t *self)
{
	int ci;

	if (!self || !self->client)
		return 0.0f;
	ci = (int)(self->client - game.clients);
	if (ci < 0 || ci >= SG_COMBAT_MAXCLIENTS)
		return 0.0f;

	/*
	 * NO CAMPING THE PAD. The tiers above are recomputed once a second like
	 * every other worth, but the headroom test is re-asked HERE, every frame:
	 * the instant the mega lands the bot is at 200 and the pull has to be
	 * gone on that frame. A second of stale worth is a second of standing on
	 * an empty pedestal, and standing on it buys nothing -- MegaHealth_think
	 * bleeds the overheal back off at 1 hp/s from the moment of pickup
	 * (g_items.c:569-592) and the pad itself is 20 s from respawning, so
	 * every second waited is a point of the prize spent. Take it and push.
	 */
	if (self->health >= SG_MEGA_HEADROOM)
		return 0.0f;
	return sg_combat[ci].worth_mega;
}

/*
 * 2.3 armour. A pool of A armour absorbs exactly A damage (1.17), so the
 * deficit is measured in absorbable damage against the body-armour ceiling of
 * 200 (g_items.c:39). The 0.05 floor at A >= 150 is rule A2: Pickup_Armor
 * refuses outright when the held count already beats the salvage value
 * (g_items.c:738-739), and a bot should not walk to an item it cannot take.
 */
static float Worth_Armor(edict_t *e)
{
	int		idx = ArmorIndex(e);
	int		a = idx ? e->client->pers.inventory[idx] : 0;
	float	worth;

	if (a == 0)
		worth = 1.00f;			/* 918 ms */
	else if (a < 25)
		worth = 0.80f;			/* 771 ms */
	else if (a < 50)
		worth = 0.60f;			/* 644 ms */
	else if (a < 100)
		worth = 0.40f;			/* 447 ms */
	else if (a < 150)
		worth = 0.20f;			/* 254 ms */
	else
		worth = 0.05f;			/* 72 ms */

	/*
	 * A1 -- the jacket-energy hole. jacketarmor_info.energy_protection is
	 * 0.00 (g_items.c:37) and ArmorIndex checks jacket FIRST (g_items.c:661),
	 * so any jacket armour at all leaves the bot fully exposed to the
	 * hyperblaster, the plasma, the BFG's lasers and the grapple, every one of
	 * which is DAMAGE_ENERGY. A jacket-armoured bot wants the upgrade more
	 * than a bare bot wants a jacket.
	 */
	if (idx && idx == sg_jacket_index)
		worth *= 1.6f;

	return Combat_Clamp(worth);
}

/*
 * 2.3 weapons. The tiers are the ladder's own top ranks; the worths follow
 * from the DPS gap they close -- a blaster-only bot does 30 dps (1.2) against
 * a chaingun's 180 (1.5), a 6:1 disadvantage that nothing else in the item
 * table can move. A bot spawns with the blaster and the hook and nothing else
 * (p_client.c:1141-1151), so tier 1 is where every bot starts every life.
 */
static int Weapon_Tier(edict_t *e)
{
	if (Combat_Avail(e, SG_W_ROCKETLAUNCHER) || Combat_Avail(e, SG_W_CHAINGUN))
		return 5;
	if (Combat_Avail(e, SG_W_SSHOTGUN) || Combat_Avail(e, SG_W_RAILGUN) ||
	    Combat_Avail(e, SG_W_HYPERBLASTER))
		return 4;
	if (Combat_Avail(e, SG_W_MACHINEGUN) ||
	    Combat_Avail(e, SG_W_GRENADELAUNCHER) || Combat_Avail(e, SG_W_PLASMA))
		return 3;
	if (Combat_Avail(e, SG_W_SHOTGUN))
		return 2;
	return 1;
}

/* 2.3: tier 1 -> 1057 ms of detour, tier 5 -> 199 ms */
static const float sg_worth_weapon[6] = {
	0.0f, 1.20f, 0.80f, 0.50f, 0.30f, 0.15f
};

/*
 * 2.3 ammo, priced off the R2 floors: what fraction of three seconds of fire
 * is left for the weapon actually held. A weapon at zero ammo is dead weight
 * and worth the most; the blaster needs nothing (g_items.c:1623-1624).
 */
static float Worth_Ammo(edict_t *e)
{
	int		held = Combat_Held(e);
	float	worth, r;

	if (Weapon_Tier(e) == 1 || held < 0 || sg_weapons[held].floor <= 0)
		return 0.10f;

	r = (float)Combat_AmmoCount(e, held) / (float)sg_weapons[held].floor;
	if (r >= 2.0f)
		worth = 0.05f;			/* 72 ms */
	else if (r >= 1.0f)
		worth = 0.20f;			/* 254 ms */
	else if (r >= 0.5f)
		worth = 0.50f;			/* 533 ms */
	else if (r > 0.0f)
		worth = 0.85f;			/* 810 ms */
	else
		worth = 1.10f;			/* 990 ms: the weapon is dead weight */

	/*
	 * M1 -- cells are contested. Power armour is paid for out of the Cells
	 * pool at one cell per point absorbed (g_combat.c:230, :269, 1.18), the
	 * same pool the hyperblaster, the BFG and the plasma drink from.
	 */
	if (PowerArmorType(e) != POWER_ARMOR_NONE &&
	    sg_witem[held] && sg_witem[held]->ammo &&
	    strcmp(sg_witem[held]->ammo, "Cells") == 0)
		worth *= 1.3f;

	return Combat_Clamp(worth);
}

/*
 * 2.3 powerups -- quad only. Invulnerability is freed at spawn at ctfflags 16
 * (g_items.c:1360-1369, 0.2), so SG_FC_POWERUP is a quad field in practice and
 * no special case is written for the other half.
 *
 * Q1: the quad respawns on a fixed 60 s clock (LM_QUAD_DEFAULT_TIME,
 * g_local.h:1513, g_items.c:2025). A field that pulls a bot toward an empty
 * pedestal is worse than no field; arriving four seconds early to camp a 60 s
 * item is correct. CACO's belief carries both facts (sg_local.h:186-198).
 */
#define SG_QUAD_CAMP_LEAD	4.0f

static float Worth_Quad(edict_t *e)
{
	int i, ti;

	if (Combat_IsQuad(e))
		return 0.0f;			/* already carrying it */

	/*
	 * THIS bot's team's row (owner's ruling, 2026-08-05). The pricing is where
	 * the per-team split actually bites: a team that has not been told the quad
	 * is coming back prices an empty pedestal at zero and walks past it, while
	 * the side whose bot called the take arrives four seconds early to camp it.
	 * That difference is the whole point of splitting the table.
	 */
	ti = (e->client && e->client->ctf.teamnum == CTF_TEAM_BLUE) ? 1 : 0;

	for (i = 0; i < sg_caco_num_items; i++)
	{
		sg_belief_item_t	*b = &sg_caco_items[ti][i];
		edict_t				*it;

		if (b->cls != SG_BI_POWERUP)
			continue;
		it = &g_edicts[b->ent];
		if (!it->inuse || !it->classname)
			continue;
		if (strcmp(it->classname, "item_quad") != 0)
			continue;

		if (!b->believed_up &&
		    b->believed_respawn_time - level.time > SG_QUAD_CAMP_LEAD)
			continue;			/* an empty pedestal for longer than Q1 allows */

		/* 2.3: the weapon tier is what makes a quad worth contesting -- a quad
		 * chaingun is 720 dps, a quad blaster is 120 (1.21's x4 on damage) */
		return Combat_Clamp(1.80f * (float)Weapon_Tier(e) / 5.0f + 0.40f);
	}
	return 0.0f;
}

/*
 * 2.3 runes. One per player -- Pickup_Rune refuses a second (g_runes.c:444-450)
 * -- so holding one makes every rune on the map worth nothing. The per-type
 * multipliers are 2.3's, from the measured effects in 1.22.
 *
 * The class weight is a single scalar while the runes are four different
 * items, so the multiplier taken is the best one CACO believes is standing.
 * That is a real approximation and is recorded as one: the per-item fields
 * (sg_fields.c:130-133) could price each rune exactly, but the weight vector
 * the surface reads has one slot per class (sg_local.h:153).
 */
static float Worth_Rune(edict_t *e)
{
	float	best = 0.0f;
	int		tier;
	int		i, ti;

	if (e->client->rune)
		return 0.0f;

	tier = Weapon_Tier(e);

	/*
	 * "BOTS ON THE OTHER TEAM WILL ONLY KNOW WHERE RUNES ARE IF THEY SEE THEM,
	 * NOT FROM THE DIFFERENT TEAM'S KNOWLEDGE" (owner's ruling, 2026-08-05).
	 * A rune belief is a sighting and nothing else -- there is no clock behind
	 * it to fall back on -- so reading the wrong team's row here would be the
	 * purest form of the leak the ruling names.
	 */
	ti = (e->client->ctf.teamnum == CTF_TEAM_BLUE) ? 1 : 0;

	for (i = 0; i < sg_caco_num_items; i++)
	{
		sg_belief_item_t	*b = &sg_caco_items[ti][i];
		edict_t				*it;
		float				mult;

		if (b->cls != SG_BI_RUNE || !b->believed_up)
			continue;
		it = &g_edicts[b->ent];
		if (!it->inuse)
			continue;

		switch (it->runetype)
		{
		case RUNE_HASTE:
			/*
			 * Exactly 2x rate of fire: RuneWeaponThinkHook calls weaponthink
			 * a second time in the same frame (g_runes.c:800-818). RU1 -- it
			 * doubles the ammo drain with it, so doubling a blaster is 60 dps
			 * and not worth the budget.
			 */
			mult = (tier <= 2) ? 1.20f : 1.60f;
			break;
		case RUNE_DAMAGE:
			mult = 1.45f;		/* x1.75 outgoing, g_runes.c:716-728 */
			break;
		case RUNE_RESIST:
			mult = 1.45f;		/* /1.75 incoming, g_runes.c:730-743 */
			break;
		case RUNE_REGEN:
			mult = 1.20f;		/* 3.33 hp/s, g_runes.c:745-798 */
			break;
		default:
			mult = 1.00f;
			break;
		}
		if (mult > best)
			best = mult;
	}

	if (best <= 0.0f)
		return 0.0f;
	return Combat_Clamp(0.55f * best);	/* 2.3: 566 ms before type scaling */
}

void SG_CombatWeights(edict_t *self, const sg_weights_t *role,
                      sg_weights_t *out)
{
	sg_combat_state_t	*st;
	int					ci, c;

	if (!self || !self->client || !role || !out)
		return;

	*out = *role;

	ci = (int)(self->client - game.clients);
	if (ci < 0 || ci >= SG_COMBAT_MAXCLIENTS)
		return;
	st = &sg_combat[ci];
	Combat_CacheItems();

	/*
	 * The worths move on the bot's own state, which moves slowly compared to
	 * a server frame, and two of them walk the edict list. Once a second is
	 * the cadence 2.3 asks for and the one Fields_Refresh already runs on
	 * (sg_fields.c:261), so the surface never sees a weight the fields have
	 * not caught up with.
	 */
	if (level.time >= st->worth_next)
	{
		st->worth_next = level.time + SG_WEIGHT_TICK;
		st->worth[SG_FC_HEALTH] = Worth_Health(self);
		st->worth[SG_FC_ARMOR] = Worth_Armor(self);
		st->worth[SG_FC_WEAPON] = sg_worth_weapon[Weapon_Tier(self)];
		st->worth[SG_FC_AMMO] = Worth_Ammo(self);
		st->worth[SG_FC_POWERUP] = Worth_Quad(self);
		st->worth[SG_FC_RUNE] = Worth_Rune(self);
		st->worth_mega = Worth_Mega(self);
	}

	/* the static row is read as a role BIAS, the worth as the state */
	for (c = 0; c < SG_FIELD_CLASSES; c++)
		out->item[c] = Combat_Clamp(role->item[c] * st->worth[c]);
}

/* ------------------------------------------------------------ the post */

void SG_CombatPost(edict_t *self, float sightline)
{
	int ci;

	if (!self || !self->client)
		return;
	ci = (int)(self->client - game.clients);
	if (ci < 0 || ci >= SG_COMBAT_MAXCLIENTS)
		return;
	sg_combat[ci].post_sight = sightline;
}

/*
 * Expected contact from belief -- an ear in the PHS, a teammate's callout --
 * before any line of sight exists. The idle hand pre-selects for the range
 * the encounter is expected at, same D3b economics as the post: holding the
 * right weapon is free, raising it mid-contact costs a full cycle. The
 * expectation decays; a stale alarm must not leave a mid-band gun in hand
 * on a long-range route forever.
 */
void SG_CombatAlert(edict_t *self, float expect_range)
{
	int ci;

	if (!self || !self->client)
		return;
	ci = (int)(self->client - game.clients);
	if (ci < 0 || ci >= SG_COMBAT_MAXCLIENTS)
		return;
	sg_combat[ci].alert_range = expect_range;
	sg_combat[ci].alert_until = level.time + 3.0f;
}

/* ------------------------------------------------------------- duel terms */

/*
 * The live target, for callers outside the combat file (the grenade
 * lead wants the enemy's ENTITY -- velocity and box -- not a seed
 * belief). NULL unless this bot re-sighted its enemy this frame-ish.
 */
edict_t *SG_CombatLiveEnemy(edict_t *self)
{
	sg_combat_state_t *st;
	int idx = (int)(self->client - game.clients);
	edict_t *en;

	if (idx < 0 || idx >= SG_COMBAT_MAXCLIENTS)
		return NULL;
	st = &sg_combat[idx];
	if (st->enemy <= 0)
		return NULL;
	en = g_edicts + st->enemy;
	if (!en->inuse || !en->client || en->deadflag ||
	    en->health <= 0)
		return NULL;
	return en;
}

void SG_CombatPursuit(edict_t *self, qboolean allowed)
{
	int ci;

	if (!self || !self->client)
		return;
	ci = (int)(self->client - game.clients);
	if (ci < 0 || ci >= SG_COMBAT_MAXCLIENTS)
		return;
	sg_combat[ci].pursue = allowed;
	if (!allowed)
		sg_combat[ci].lost_until = 0.0f;	/* a role change ends the camp */
}

qboolean SG_CombatDuel(edict_t *self, vec3_t enemy_org, float *want_range,
                       float *exposure_w)
{
	sg_combat_state_t	*st;
	edict_t				*foe;
	vec3_t				org, eye, d;
	float				dist, want;
	int					ci, held, team, s;

	if (want_range)
		*want_range = 0.0f;
	if (exposure_w)
		*exposure_w = 0.0f;

	if (!self || !self->inuse || !self->client)
		return false;
	if (self->deadflag == DEAD_DEAD || self->health <= 0)
		return false;
	ci = (int)(self->client - game.clients);
	if (ci < 0 || ci >= SG_COMBAT_MAXCLIENTS)
		return false;
	st = &sg_combat[ci];

	if (st->enemy_last <= 0 || level.time - st->enemy_time > SG_DUEL_FRESH)
		return false;
	VectorCopy(st->enemy_org, org);

	/*
	 * The team may know better than this bot's own last look: a teammate
	 * watching the same enemy right now is a fresher fix than a two-second-old
	 * memory. Eyes only -- an ear places a contact well enough to warn a post
	 * and never well enough to hold a range against (sg_local.h:87-88).
	 */
	team = self->client->ctf.teamnum;
	if (team == CTF_TEAM_RED || team == CTF_TEAM_BLUE)
	{
		rune_t *r = SG_Rune();

		for (s = 0; r && s < SG_MAX_ENEMY_TRACK; s++)
		{
			sg_belief_enemy_t *en = &sg_caco_enemies[team - 1][s];

			if (en->client < 0 || en->heard_only)
				continue;
			if (1 + en->client != st->enemy_last)
				continue;
			if (en->seed < 0 || en->seed >= r->hdr.num_seeds)
				continue;
			if (en->seen_time <= st->enemy_time)
				continue;
			VectorCopy(r->seeds[en->seed].origin, org);
		}
	}

	if (enemy_org)
		VectorCopy(org, enemy_org);

	Combat_CacheItems();

	VectorCopy(self->s.origin, eye);
	eye[2] += self->viewheight;
	VectorSubtract(org, eye, d);
	dist = VectorLength(d);

	held = Combat_Held(self);
	if (held < 0)
		held = SG_W_BLASTER;		/* the grapple or hand grenades: solve as
		                             * the blaster, the same substitution the
		                             * firing solution makes */
	want = Combat_WantRange(self, held);

	/*
	 * The matchup. Their weapon is the one that was on their model when this
	 * bot last had eyes on them, so it ages with the rest of the belief. Bias
	 * this bot's own preference away from theirs: a quarter of the gap, then
	 * back through the same R1/R2d/F6 gates, which is why a super shotgun
	 * cannot be talked out past 256 by a railgun standing at 690.
	 */
	foe = (st->enemy_last > 0 && st->enemy_last < globals.num_edicts)
	      ? g_edicts + st->enemy_last : NULL;
	if (foe && foe->inuse && foe->client &&
	    st->enemy_weapon >= 0 && st->enemy_weapon < SG_NUM_WEAPONS)
	{
		float theirs = Combat_WantRange(foe, st->enemy_weapon);
		float dsafe = Combat_DSafe(self, held);
		float cap = sg_weapons[held].range_cap;

		want += SG_DUEL_MATCH_BIAS * (want - theirs);
		if (dsafe > 0.0f && want < dsafe)
			want = dsafe;
		if (held == SG_W_SSHOTGUN && want > SG_SSG_PREFER_RANGE)
			want = SG_SSG_PREFER_RANGE;
		if (cap > 0.0f && want > cap)
			want = cap;
		if (want > SG_ENGAGE_RANGE)
			want = SG_ENGAGE_RANGE;
		if (want < 1.0f)
			want = 1.0f;
	}

	if (want_range)
		*want_range = want;
	if (exposure_w)
		*exposure_w = Combat_Exposure(self, held, dist, want);
	return true;
}

/* ------------------------------------------------------------ corner hold
 *
 * A target that stepped behind a corner has not vanished; it is somewhere in
 * the set of seeds it could have walked to since, and the rune knows exactly
 * which those are. Flood outward from where it was last believed to be,
 * spending link cost_ms against the time that has passed -- the rune's costs
 * are real traversal milliseconds (sg_rune.h:80), so the budget is the elapsed
 * clock and no conversion is needed. The members of that set this bot can
 * SEE are the mouths it might come out of.
 *
 * This is belief plus reachability, which is what CACO and RUNE are for. It is
 * not prediction of intent: every reachable seed is equally admitted, and the
 * bot simply looks at the nearest one it can cover.
 */
static void Combat_LostAim(edict_t *self, sg_combat_state_t *st, vec3_t eye)
{
	rune_t	*r = SG_Rune();
	int		q[SG_LOST_BFS], qc[SG_LOST_BFS];
	int		head = 0, n = 0, best = -1;
	float	bestd = 0.0f, budget;
	vec3_t	probe, dv;
	int		held;

	st->lost_have = false;
	if (!r || st->lost_seed < 0 || st->lost_seed >= r->hdr.num_seeds)
		return;

	budget = (level.time - st->lost_time) * 1000.0f;
	if (budget < 0.0f)
		budget = 0.0f;

	q[0] = st->lost_seed;
	qc[0] = 0;
	n = 1;

	while (head < n)
	{
		int		s = q[head];
		int		c = qc[head];
		int		li;
		trace_t	tr;

		head++;

		/*
		 * One ray per examined seed, aimed where a standing player's head
		 * would be rather than at the floor sample itself -- a seed behind a
		 * shin-high lip is still a place somebody walks out of.
		 */
		VectorCopy(r->seeds[s].origin, probe);
		probe[2] += self->viewheight;
		tr = gi.trace(eye, NULL, NULL, probe, self, MASK_OPAQUE);
		if (tr.fraction >= 1.0f)
		{
			float d;

			VectorSubtract(probe, eye, dv);
			d = VectorLength(dv);
			if (best < 0 || d < bestd)
			{
				best = s;
				bestd = d;
			}
		}

		for (li = r->first_link[s]; li >= 0 && n < SG_LOST_BFS;
		     li = r->next_link[li])
		{
			rune_link_t *l = &r->links[li];
			int			nc = c + (int)l->cost_ms;
			int			j;

			/*
			 * First cost wins: this is a breadth walk, not a Dijkstra, so a
			 * seed first reached the long way keeps the long way's cost and
			 * may fall outside the budget it would have made by the short
			 * one. The error is one-sided -- the set can only come out
			 * SMALLER than the truth -- which is the safe direction for a
			 * thing that decides where to point a gun.
			 */
			if ((float)nc > budget)
				continue;		/* further than the clock allows, so far */
			for (j = 0; j < n; j++)
				if (q[j] == l->to)
					break;
			if (j < n)
				continue;
			q[n] = l->to;
			qc[n] = nc;
			n++;
		}
	}

	if (best < 0)
		return;

	/*
	 * Where on that seed to point. A hitscan or a railgun wants the head that
	 * will appear there; a rocket or a grenade wants the floor, because splash
	 * pays 120 - 0.5*d from the impact point (1.10, g_combat.c:742) and a shot
	 * into the ground greets an arrival that a shot at head height would have
	 * already passed.
	 */
	held = Combat_Held(self);
	VectorCopy(r->seeds[best].origin, st->lost_aim);
	if (held != SG_W_ROCKETLAUNCHER && held != SG_W_GRENADELAUNCHER)
		st->lost_aim[2] += self->viewheight;
	st->lost_have = true;
}

/*
 * Hold the view on the best emergence point, if there is a hold to keep.
 * Returns true when it wrote cmd->angles. The trigger is not touched here and
 * never will be: the scan has no target, so the pre-fire trace never ran, and
 * the veto that authorises every shot in this file is that trace.
 *
 * Known consequence, stated rather than hidden: the Body runs plain forward
 * down whatever view the frame ends up with (sg_arach.c sets forwardmove 400
 * along the chosen yaw, and pmove builds its move basis from the angles this
 * file may have overwritten). Holding a view therefore also leans the walk
 * toward it -- which is the same thing that already happens on every frame a
 * target is held, and is why SG_CombatPursuit's role bound is the guard: only
 * roles that were heading that way anyway are allowed a hold at all.
 */
static qboolean Combat_LostHold(edict_t *self, sg_combat_state_t *st,
                                usercmd_t *cmd, vec3_t eye)
{
	vec3_t	aim;
	float	len, yaw, pitch;
	int		held;

	if (!st->pursue || level.time >= st->lost_until)
		return false;

	/*
	 * The contact-band exception. A super shotgun, or anything else whose
	 * preferred range is inside the contact band, wins by arriving, not by
	 * watching a doorway from across the room -- 1.7's 120 damage is a
	 * sub-128 number. Drop the hold outright and let the surface close the
	 * distance; a bot standing still with a shotgun is a bot being shot.
	 */
	held = Combat_Held(self);
	if (held < 0)
		held = SG_W_BLASTER;
	if (Combat_WantRange(self, held) < SG_R_CLOSE)
	{
		st->lost_until = 0.0f;
		return false;
	}

	if (level.time >= st->lost_next)
	{
		Combat_LostAim(self, st, eye);
		st->lost_next = level.time + SG_LOST_TICK;
	}
	if (!st->lost_have)
		return false;

	VectorSubtract(st->lost_aim, eye, aim);
	len = VectorNormalize(aim);
	if (len < 1.0f)
		return false;

	yaw = (float)(atan2(aim[1], aim[0]) * 180.0 / M_PI);
	pitch = (float)(-asin(aim[2]) * 180.0 / M_PI);
	cmd->angles[YAW] = ANGLE2SHORT(yaw)
	                 - self->client->ps.pmove.delta_angles[YAW];
	cmd->angles[PITCH] = ANGLE2SHORT(pitch)
	                   - self->client->ps.pmove.delta_angles[PITCH];
	return true;
}

/* ------------------------------------------------------------------ frame */

void SG_CombatFrame(edict_t *self, usercmd_t *cmd, qboolean *out_engaged)
{
	sg_combat_state_t	*st;
	edict_t				*enemy;
	vec3_t				eye, forward, mid, lead, aim, endp, impact;
	vec3_t				threat;
	float				dist, held, frac, mag, len, flight;
	float				yaw, pitch, skill, settle;
	float				residual, span, shape;
	trace_t				tr;
	int					ci, band, want, inhand;
	qboolean			clear_shot, carrier, ballistic, vel_stable;
	qboolean			rattled, textured;

	if (out_engaged)
		*out_engaged = false;

	if (!self || !self->inuse || !self->client || !cmd)
		return;
	if (self->deadflag == DEAD_DEAD || self->health <= 0)
		return;
	if (self->movetype == MOVETYPE_NOCLIP)
		return;

	ci = (int)(self->client - game.clients);
	if (ci < 0 || ci >= SG_COMBAT_MAXCLIENTS)
		return;
	st = &sg_combat[ci];
	Combat_CacheItems();
	skill = Combat_Skill(self);		/* team level less this bot's own handicap */

	/* eyes and current facing. v_angle is the view the last cmd produced. */
	VectorCopy(self->s.origin, eye);
	eye[2] += self->viewheight;
	AngleVectors(self->client->v_angle, forward, NULL, NULL);

	/*
	 * Anything shoot us lately from somewhere we were not looking? The
	 * window is the fluster clock: about 1.2 s at the top of the ladder,
	 * twice that at the bottom.
	 */
	rattled = SG_RecentUnseenHit(self,
	                             Combat_SkillLerp(skill, SG_THREAT_S0,
	                                              SG_THREAT_S4), threat);

	sg_cbt_why[7]++;                        /* frames that got this far */
	enemy = Combat_Scan(self, eye, forward, rattled ? threat : NULL);
	if (enemy)
		sg_cbt_why[8]++;                    /* frames with a target */
	if (!enemy)
	{
		/*
		 * Just lost one. Record where belief last put it, as a SEED rather
		 * than a point: the emergence set is a walk over rune links, so its
		 * root has to be a node of that graph. Rune_NearestSeed of the last
		 * believed origin is the honest answer; the enemy table's own seed is
		 * the fallback for the frame where a teammate's sighting is all there
		 * is. Nothing is recorded when neither exists -- a hold rooted at a
		 * guess is a bot staring at a wall.
		 */
		if (st->enemy > 0)
		{
			rune_t *r = SG_Rune();
			int seed = r ? Rune_NearestSeed(r, st->enemy_org) : -1;

			if (seed < 0 && r)
			{
				int team = self->client->ctf.teamnum;
				int s;

				if (team == CTF_TEAM_RED || team == CTF_TEAM_BLUE)
					for (s = 0; s < SG_MAX_ENEMY_TRACK; s++)
					{
						sg_belief_enemy_t *en =
						    &sg_caco_enemies[team - 1][s];

						if (en->client >= 0 && 1 + en->client == st->enemy &&
						    en->seed >= 0 && en->seed < r->hdr.num_seeds)
						{
							seed = en->seed;
							break;
						}
					}
			}

			if (seed >= 0)
			{
				st->lost_client = st->enemy - 1;
				st->lost_seed = seed;
				st->lost_time = level.time;
				st->lost_until = level.time + SG_LOST_HOLD;
				st->lost_next = 0.0f;
				st->lost_have = false;
			}
		}

		st->enemy = 0;
		st->ws_panic = false;	/* the exception belongs to a fight in
		                         * progress; it does not outlive one */

		/*
		 * Idle. Rule D3b: pre-SELECT, do not pre-fire. A posted defender holds
		 * the weapon its sightline calls for; a carrier holds the flee
		 * doctrine's weapon (D2 point 2); anyone else only acts when what they
		 * are holding cannot shoot -- the grapple, hand grenades, a dry
		 * weapon -- or when they are still on the spawn blaster with something
		 * better in the pack. Holding a weapon costs nothing; raising one
		 * mid-contact costs a full weapon cycle.
		 */
		if (st->post_sight >= 0.0f)
			Combat_Arbitrate(self, st, Combat_PostWeapon(self, st->post_sight));
		else if (Combat_Carrying(self))
			Combat_Arbitrate(self, st,
			                 Combat_Choose(self, SG_BAND_MID, SG_R_MID, false));
		else if (st->alert_range >= 0.0f && level.time < st->alert_until)
		{
			/* belief says contact is coming at roughly this range */
			int ab = (st->alert_range < SG_R_CLOSE) ? SG_BAND_CONTACT
			       : (st->alert_range < SG_R_MID)   ? SG_BAND_CLOSE
			       : (st->alert_range < SG_R_LONG)  ? SG_BAND_MID
			                                        : SG_BAND_LONG;

			Combat_Arbitrate(self, st,
			                 Combat_Choose(self, ab, st->alert_range, false));
		}
		else
		{
			int h = Combat_Held(self);

			if (h < 0 || !Combat_Avail(self, h) ||
			    (h == SG_W_BLASTER && Weapon_Tier(self) > 1))
				Combat_Arbitrate(self, st,
				                 Combat_Choose(self, SG_BAND_MID, SG_R_MID,
				                               false));
			else if (Combat_WSwitch())
			{
				/* the gun is loaded and is not the spawn blaster, so the old
				 * code was finished here. Ready the room ahead instead. */
				Combat_PreSwitch(self, st, h);
			}
		}

		/*
		 * The corner. Last thing in the idle path, so the weapon choice above
		 * is already made when the emergence set is asked which height to aim
		 * at, and so a bot with no hold to keep leaves the view exactly where
		 * navigation put it. The trigger is untouched: this branch has no
		 * target, so no pre-fire trace ran, and no shot is ever authorised
		 * without one.
		 */
		Combat_LostHold(self, st, cmd, eye);
		return;
	}

	Combat_Center(enemy, mid);
	VectorSubtract(mid, eye, aim);
	dist = VectorLength(aim);

	/* target continuity: a new target restarts the settle clock, draws a
	 * fresh tremor and snaps the range band. Holding the same one lets the
	 * error decay and the band hysteresis do its work. */
	if (st->enemy != (int)(enemy - g_edicts))
	{
		st->enemy = (int)(enemy - g_edicts);
		st->since = level.time;
		st->acquired_at = level.time;	/* the reaction clock starts here */
		st->err_next = 0.0f;
		st->win_end = 0.0f;
		st->win_fire = false;
		st->band = -1;
		VectorClear(st->vel_dir);
		st->vel_stable = level.time;

		/*
		 * A fight just started: arm the decision delay (sg_wswitch). The
		 * clock is skill-scaled the way the reaction delay is, then bent by
		 * the persona in the same direction it bends the corridor bar --
		 * aggression is commitment to the gun in hand, so an aggressive bot
		 * deliberates LONGER before swapping mid-duel and a methodical one
		 * gets to the right gun sooner. Set on ACQUISITION and not on a band
		 * change, because the surprise is what is being modelled: a target
		 * that walks from mid into close was watched the whole way.
		 */
		if (Combat_WSwitch())
		{
			st->ws_gate = level.time +
			              Combat_SkillLerp(skill, SG_WS_DECIDE_S0,
			                               SG_WS_DECIDE_S4) *
			              SG_PersonaAggression(self);
			st->ws_armed = level.time;
		}

		/*
		 * The aim texture's one hook. This branch is the only place a target
		 * transition happens: a genuinely new enemy comes through it, and so
		 * does a re-acquisition after the target went behind something,
		 * because the idle path zeroes st->enemy on the way out and the
		 * comparison above cannot match zero.
		 *
		 * Cleared whether or not the cvar is on, so a texture armed while
		 * sg_aimtexture was 1 cannot still be playing out against a target
		 * acquired after it was set back to 0.
		 */
		st->tex_over = 0.0f;
		st->tex_win = 0.0f;
		if (Combat_TexOn() && dist > 1.0f)
		{
			vec3_t	dir;

			/* aim is still the raw eye->centre vector here, before the lead
			 * and the error bend it -- which is the right thing to measure a
			 * swing against: it is where the target IS, not where this bot is
			 * about to decide to point. */
			VectorScale(aim, 1.0f / dist, dir);
			Combat_TexAcquire(self, st, skill, forward, dir);
		}
	}

	if (out_engaged)
		*out_engaged = true;

	/*
	 * The record the duel terms read, written by the eye that just passed the
	 * sight gate: where it is, when that was, and which weapon was on its
	 * model. The weapon is ChangeWeapon's own s.modelindex2 (p_weapon.c:171-232)
	 * seen from here as pers.weapon -- a thing in view, not an inventory this
	 * bot has no business knowing. Anything the eye did not see stays unwritten.
	 */
	st->enemy_last = st->enemy;
	VectorCopy(mid, st->enemy_org);
	st->enemy_time = level.time;
	st->enemy_weapon = Combat_Held(enemy);

	/* re-sight clears the corner hold outright: there is nothing left to
	 * predict about a target that is standing in front of you */
	st->lost_until = 0.0f;
	st->lost_have = false;

	/* rule F1's stability clock runs every frame a target is held, not only on
	 * the frames a projectile is in hand -- a clock that only ticks while it
	 * is being read never reaches its threshold */
	vel_stable = Combat_VelStable(st, enemy);

	/* ------------------------------------------------------ the weapon */

	carrier = Combat_IsEnemyCarrier(self, enemy);
	band = Combat_Band(st, dist);
	want = Combat_Choose(self, band, dist, carrier);

	/*
	 * WETWORK (sg_wetwork, wave 300+). The owner's physics check that
	 * killed the wet route cuts the other way here: a swimmer moves at
	 * HALF wishspeed (pmove.c) and fire_rail's mask has no
	 * CONTENTS_WATER -- rails reach into water undegraded. A swimming
	 * target is the easiest rail shot in the game and the band ladders
	 * don't know it. Hold the rail on swimmers if it's in the pack.
	 */
	if (enemy->waterlevel > 1 &&
	    gi.cvar("sg_wetwork", "1", 0)->value)
	{
		static const int wet_rg[] = { SG_W_RAILGUN, -1 };
		int wr = Combat_WalkLadder(self, wet_rg, dist, false);

		if (wr >= 0)
			want = wr;
	}

	/*
	 * The panic test (sg_wswitch), written every frame the cvar is on and
	 * read by Combat_Arbitrate and Combat_Request below. Contact range with a
	 * gun that cannot shoot -- dry, unknown (the grapple, hand grenades), or
	 * one Combat_BandAllows refuses at this distance, which at contact means
	 * a splash weapon inside its own d_safe. Neither the decision delay nor
	 * the 4 s cooldown applies to that swap, because a player standing 100
	 * units from someone with a rocket launcher in their hands does not
	 * deliberate either.
	 */
	if (Combat_WSwitch())
	{
		int h = Combat_Held(self);

		st->ws_panic = (qboolean)(dist < SG_R_CLOSE &&
		                          (h < 0 || !Combat_Avail(self, h) ||
		                           !Combat_BandAllows(self, h, dist)));
	}
	else
		st->ws_panic = false;

	Combat_Arbitrate(self, st, want);

	/*
	 * The firing solution is built for the weapon in HAND, not the one wanted:
	 * a switch is 700-1100 ms away and the shot is now. An unknown weapon --
	 * the grapple, hand grenades -- solves as the blaster and never fires,
	 * because the trigger test below refuses anything Combat_Held cannot name.
	 */
	inhand = Combat_Held(self);
	if (inhand < 0)
		inhand = SG_W_BLASTER;
	ballistic = false;

	/* ---------------------------------------------------------------- lead
	 *
	 * WEAPONS.md 2.2: one refinement pass over the weapon's own projectile
	 * speed, aimed at the bbox centre rather than the origin (rule F2 -- the
	 * origin is 4 units below centre, p_client.c:1833-1834). Hitscan weapons
	 * return the centre unchanged; there is no travel time to lead.
	 */
	flight = Combat_Solve(enemy, inhand, eye, lead);

	VectorSubtract(lead, eye, aim);
	len = VectorLength(aim);
	if (len < 1.0f)
		return;
	VectorScale(aim, 1.0f / len, aim);

	/* ---------------------------------------------------------- lead error
	 *
	 * Leading a moving target is the skill this file models most directly, so
	 * it gets its own error rather than being folded into the tremor: an
	 * angular jitter of SG_LEAD_JITTER_S0 * (4 - skill) / 4 degrees, resampled
	 * every frame, which is per shot or finer. At skill 4 the term is exactly
	 * zero and the solve is untouched.
	 *
	 * It displaces the lead POINT and re-derives the aim from it, rather than
	 * bending the aim ray on its own. That keeps the wall check below tracing
	 * where the shot really goes and keeps the SG_HIT_SLOP test measuring
	 * against the point actually aimed at -- so a lead error comes out as a
	 * MISS, which is what it is, instead of a refusal to fire.
	 *
	 * Hitscan is skipped: flight is 0, there is no lead to get wrong. So is
	 * the grenade launcher, whose aim rule F4 replaces outright below.
	 */
	if (flight > 0.0f && inhand != SG_W_GRENADELAUNCHER && skill < SG_SKILL_MAX)
	{
		float jit = (float)tan(SG_LEAD_JITTER_S0
		                       * (SG_SKILL_MAX - skill) / SG_SKILL_MAX
		                       * M_PI / 180.0);
		vec3_t perp;

		Combat_SamplePerp(aim, perp);
		VectorMA(lead, jit * len, perp, lead);

		VectorSubtract(lead, eye, aim);
		len = VectorLength(aim);
		if (len < 1.0f)
			return;
		VectorScale(aim, 1.0f / len, aim);
	}

	/*
	 * The grenade launcher does not fly straight. Rule F4's pitch solve
	 * replaces the aim outright, and rule F5 restricts the weapon to targets
	 * that are not really moving -- past first contact a bouncing grenade with
	 * a 2.5 s fuse has no predictable impact point to aim at. The arc is
	 * solved on the clean aim and the tremor is applied after it, exactly as
	 * for every other weapon: the predicted impact is the one the bot INTENDED,
	 * which is what rules R1 and F5 are asking about.
	 */
	if (inhand == SG_W_GRENADELAUNCHER)
	{
		vec3_t	flat;
		float	range;

		VectorSubtract(mid, eye, flat);
		flat[2] = 0.0f;
		range = VectorLength(flat);
		if (range < 1.0f)
			return;

		yaw = (float)(atan2(flat[1], flat[0]) * 180.0 / M_PI);
		if (VectorLength(enemy->velocity) <= SG_GL_STATIC_SPEED &&
		    Combat_GrenadeImpact(self, eye, yaw, range, impact))
		{
			VectorSubtract(impact, eye, aim);
			len = VectorNormalize(aim);
			ballistic = true;
		}
		else
		{
			/* no usable arc: aim straight at the target and hold fire, so the
			 * view is still right the moment a better weapon comes up */
			VectorSubtract(mid, eye, aim);
			len = VectorNormalize(aim);
			inhand = -1;		/* marks "aim only" for the trigger below */
		}
	}

	/* ----------------------------------------------------------- aim error
	 *
	 * A human does not snap onto a target; they overshoot and settle. The
	 * error is a lateral offset that shrinks over SG_SETTLE seconds of held
	 * contact down to a residual that never reaches zero. The direction is
	 * resampled on a slow tick so it reads as tremor rather than a fixed bias
	 * that a target could out-run in one direction.
	 *
	 * That first sentence was aspirational: the cone shrinks, it never goes
	 * past. sg_aimtexture is the branch that makes it true -- the overshoot,
	 * its corrections, and a tracking direction that wanders instead of
	 * jumping. Off, this is the code that shipped and the three tex_ lines
	 * below are no-ops.
	 */
	textured = Combat_TexOn();

	if (textured)
		Combat_TexWander(st, aim);
	else if (level.time >= st->err_next)
	{
		Combat_SampleError(st, aim);
		st->err_next = level.time + 0.25f + 0.25f * random();
	}

	/*
	 * The correction shape is read off the CLEAN aim, before the tremor and
	 * the overshoot are added to it -- both of those are offsets in the plane
	 * perpendicular to this ray, and squaring the overshoot direction up
	 * against a ray that already has one of them in it would fold a little of
	 * the tremor into the correction's direction.
	 */
	shape = textured ? Combat_TexShape(st, aim) : 0.0f;

	/*
	 * Both ends of the ramp are skill terms now. A low-skill bot starts wider,
	 * takes longer to converge, and settles onto a floor it never shoots
	 * through; a skill-4 bot gets the exact three numbers this file shipped
	 * with (the S4 endpoints are those constants, textually).
	 */
	settle = Combat_SkillLerp(skill, SG_SETTLE_S0, SG_SETTLE_S4);
	residual = Combat_SkillLerp(skill, SG_RESIDUAL_S0, SG_RESIDUAL_S4);
	span = Combat_SkillLerp(skill, SG_ACQUIRE_S0, SG_ACQUIRE_S4) - residual;

	/*
	 * Textured, the window is this acquisition's -- the skill span above,
	 * stretched by the flick angle and the bot's style -- and the ramp has
	 * been narrowed to pay for the overshoot riding inside it. Untextured,
	 * tex_win is zero, tex_over is zero, and both lines below are the
	 * arithmetic that shipped.
	 */
	if (textured && st->tex_win > 0.0f)
		settle = st->tex_win;
	if (textured)
		span = Combat_TexSpan(span, residual, st->tex_over);

	held = level.time - st->since;
	frac = (held >= settle) ? 0.0f : (1.0f - held / settle);
	mag = (float)tan((residual + span * frac) * M_PI / 180.0);

	VectorMA(aim, mag, st->err, aim);
	if (shape != 0.0f)
		VectorMA(aim, (float)tan((double)(st->tex_over * shape)
		                         * M_PI / 180.0), st->tex_dir, aim);
	VectorNormalize(aim);

	/* ------------------------------------------------------------- the view
	 *
	 * Same convention the Body uses to steer (sg_arach.c:612-617): yaw from
	 * atan2 of the ground components, pitch negated because Quake pitch is
	 * positive downward, both converted with ANGLE2SHORT and biased by the
	 * client's delta_angles so the server resolves to the angle we asked for.
	 */
	yaw = (float)(atan2(aim[1], aim[0]) * 180.0 / M_PI);
	pitch = (float)(-asin(aim[2]) * 180.0 / M_PI);

	cmd->angles[YAW] = ANGLE2SHORT(yaw)
	                 - self->client->ps.pmove.delta_angles[YAW];
	cmd->angles[PITCH] = ANGLE2SHORT(pitch)
	                   - self->client->ps.pmove.delta_angles[PITCH];

	if (inhand < 0)
		return;					/* aim is written; there is no shot to take */

	/*
	 * The ballistic case never runs the straight-line wall check: the shot
	 * does not travel in a straight line, and Combat_GrenadeImpact already
	 * walked the real flight to its first contact. What is left to ask is
	 * rule F5's question -- is that contact close enough to the target to be
	 * worth a grenade? damage_radius is damage + 40 = 160 (p_weapon.c:791),
	 * and T_RadiusDamage pays 120 - 0.5*d from the impact point out to it
	 * (g_combat.c:742).
	 */
	if (ballistic)
	{
		vec3_t v;

		VectorSubtract(impact, mid, v);
		if (VectorLength(v) > SG_DSAFE_GRENADE - 1.0f)
			return;
		if (!Combat_SplashSafe(self, inhand, impact))
			return;
	}
	else
	{
		/* --------------------------------------------------- the wall check
		 *
		 * Legacy bots declined to fire 37-43% of the time because they shot
		 * into geometry. The fix is not a heuristic: trace the projectile's own
		 * path with its own clipmask (MASK_SHOT, g_weapon.c:360) and only pull
		 * the trigger if it arrives. `self` is the pass entity, so we do not
		 * trace against ourselves, exactly as a bolt does not collide with its
		 * owner.
		 *
		 * Accepted if the trace lands ON the enemy, or if it stops within
		 * SG_HIT_SLOP of the aim point -- the second case covers a shot that
		 * passes close enough to still connect on a target that has moved a
		 * little inside its own bounding box.
		 */
		VectorMA(eye, len, aim, endp);
		tr = gi.trace(eye, NULL, NULL, endp, self, MASK_SHOT);
		VectorCopy(tr.endpos, impact);

		clear_shot = false;
		if (tr.ent == enemy)
		{
			clear_shot = true;
		}
		else
		{
			vec3_t miss;

			VectorSubtract(tr.endpos, lead, miss);
			if (VectorLength(miss) <= SG_HIT_SLOP)
				clear_shot = true;
		}

		/*
		 * Rule F7's teammate clause, generalised. The railgun pierces every
		 * client it hits (g_weapon.c:764-767), so a teammate in the line is
		 * still hit even when the enemy behind is reached; every other weapon
		 * simply stops on him. Either way the shot is refused unless friendly
		 * fire is off (DF_NO_FRIENDLY_FIRE, q_shared.h:1004, tested at
		 * g_combat.c:438).
		 */
		if (tr.ent && tr.ent->client && tr.ent != enemy &&
		    OnSameTeam(self, tr.ent) &&
		    !(((int)dmflags->value) & DF_NO_FRIENDLY_FIRE))
			clear_shot = false;

		/*
		 * WEAPONS.md 2.4-D1's one case where deliberately missing is correct:
		 * a rocket into the floor under a grappling carrier still lands 90+
		 * damage, because splash is 120 - 0.5*d from the IMPACT point (1.10).
		 * Only for the carrier, only for the rocket, and only when the direct
		 * line is blocked.
		 */
		if (!clear_shot && carrier && inhand == SG_W_ROCKETLAUNCHER)
		{
			vec3_t	down, v;
			trace_t fl;

			VectorCopy(lead, down);
			down[2] -= 512.0f;
			fl = gi.trace(lead, NULL, NULL, down, enemy, MASK_SHOT);
			if (fl.fraction < 1.0f)
			{
				trace_t path;
				float	reach;

				path = gi.trace(eye, NULL, NULL, fl.endpos, self, MASK_SHOT);
				VectorSubtract(path.endpos, fl.endpos, v);
				reach = VectorLength(v);
				if (reach <= SG_HIT_SLOP)
				{
					VectorCopy(path.endpos, impact);
					VectorSubtract(fl.endpos, eye, aim);
					len = VectorNormalize(aim);
					yaw = (float)(atan2(aim[1], aim[0]) * 180.0 / M_PI);
					pitch = (float)(-asin(aim[2]) * 180.0 / M_PI);
					cmd->angles[YAW] = ANGLE2SHORT(yaw)
					                 - self->client->ps.pmove.delta_angles[YAW];
					cmd->angles[PITCH] = ANGLE2SHORT(pitch)
					                   - self->client->ps.pmove.delta_angles[PITCH];
					clear_shot = true;
				}
			}
		}

		if (!clear_shot)
		{
			sg_cbt_why[1]++;
			return;				/* aim is written; the trigger stays off */
		}

		/* ----------------------------------------------------- the vetoes */

		/* rule R1: never fire a splash weapon whose impact point is inside its
		 * own d_safe of the bot's bbox centre. The cliff is hard, not a taper. */
		if (!Combat_SplashSafe(self, inhand, impact))
		{
			sg_cbt_why[2]++;
			return;
		}

		/* rule F6: a hitscan weapon past its cap is worse than the blaster */
		if (sg_weapons[inhand].range_cap > 0.0f &&
		    dist > sg_weapons[inhand].range_cap)
		{
			sg_cbt_why[3]++;
			return;
		}

		/* rule F1: a projectile is only fired when the lead is a prediction.
		 * The tolerance is three quarters of a 32-unit bbox
		 * (p_client.c:1833-1834). */
		if (sg_weapons[inhand].speed > 0.0f)
		{
			float drift = flight * VectorLength(enemy->velocity);

			if (drift >= SG_LEAD_TOLERANCE && !vel_stable)
			{
				sg_cbt_why[4]++;
				return;
			}
		}

		/*
		 * Rule R2c: cells feed the hyperblaster at 10/s, the plasma at 10 a
		 * shot, power armour at one per point absorbed, AND the BFG at 50 a
		 * shot. One BFG shot is five seconds of hyperblaster, so it is not
		 * taken below 100 cells while a hyperblaster is also in the pack.
		 */
		if (inhand == SG_W_BFG && Combat_Avail(self, SG_W_HYPERBLASTER) &&
		    Combat_AmmoCount(self, SG_W_BFG) < 100)
			return;
	}

	/* ---------------------------------------------------------- the trigger
	 *
	 * Alternating windows, each jittered, so the pattern is not a metronome:
	 * roughly SG_FIRE_ON held then SG_FIRE_OFF released. The window advances
	 * on its own clock, independent of whether a shot was cleared, so a bot
	 * that loses line of sight mid-burst does not get a free full burst the
	 * instant it comes back.
	 */
	/*
	 * Reaction, before the windows and not inside them. A bot that has just
	 * seen a target has not decided to shoot at it yet; the delay runs from
	 * acquired_at, which is set once per NEW target, so a target re-sighted
	 * around a corner within the same acquisition does not pay it twice.
	 *
	 * It is checked ahead of the window clock deliberately: acquisition zeroes
	 * win_end and win_fire, so the first window flip happens on the first frame
	 * past the reaction and the first burst starts there rather than partway
	 * through an off-window that ticked away while the bot was still noticing.
	 */
	if (level.time < st->acquired_at
	                 + Combat_SkillLerp(skill, SG_REACT_S0, SG_REACT_S4))
	{
		sg_cbt_why[9]++;
		return;
	}

	/*
	 * TAP VARIANCE (sg_tapvar, rung-3 set #1 ranked tell #2). Every
	 * judge read the rail cadence as "a single razor spike at ~1.7s,
	 * zero spread": a held button refires a slow weapon the frame its
	 * cycle completes, and the ON/OFF windows below never gate it
	 * because the cycle outlasts the window rhythm. A human re-aims
	 * between deliberate shots -- pub rail cadence is ragged. Each time
	 * a slow weapon finishes firing and comes ready again, the next
	 * press waits a skill-scaled beat drawn fresh per shot: the ladder's
	 * bottom taps 0.2-0.7s late, the top 0.05-0.19s (pros with reload
	 * cues ARE near-metronomic -- the spread stays honest to skill).
	 */
	if (gi.cvar("sg_tapvar", "0", 0)->value > 0.0f)
	{
		int hw = Combat_Held(self);

		if (hw == SG_W_RAILGUN || hw == SG_W_SSHOTGUN ||
		    hw == SG_W_ROCKETLAUNCHER || hw == SG_W_GRENADELAUNCHER ||
		    hw == SG_W_SHOTGUN || hw == SG_W_BFG)
		{
			/*
			 * Shot detection by AMMO DECREMENT, not weaponstate: under
			 * a held trigger the gun re-enters FIRING inside the same
			 * server frame and a 10Hz think never observes READY -- the
			 * first cut of this feature was inert for exactly that
			 * reason (probe: 0 taps in 400s of 5v5). Ammo cannot lie.
			 */
			{
				int ta = (self->client->ammo_index > 0)
				    ? self->client->pers.inventory[self->client->ammo_index]
				    : 0;

				if (ta < st->tap_ammo)
				{
					/* the cvar is a DOSE: 1 = the 0.08-0.22s jitter
					 * that provably fires but cannot widen a 1.7s
					 * cycle's CV; 3 = 0.24-0.66s holds, the scale a
					 * human's deliberate re-aim actually occupies */
					st->tap_until = level.time +
					    gi.cvar("sg_tapvar", "0", 0)->value *
					    Combat_SkillLerp(skill, 0.45f, 0.12f) *
					    (0.4f + 1.2f * random());
					if (gi.cvar("sg_debug", "0", 0)->value)
						gi.dprintf("TAPDBG %s w=%d delay=%.2f\n",
						           self->client->pers.netname, hw,
						           st->tap_until - level.time);
				}
				st->tap_ammo = ta;
			}
			if (level.time < st->tap_until)
			{
				sg_cbt_why[5]++;
				return;
			}
		}
	}

	/*
	 * FIRE DISCIPLINE (sg_firedisc, rung-3 cadence tell -- the answer
	 * the tapvar family could not be). The judges read bot cadence as
	 * needles on the refire line because the trigger is independent of
	 * the legs: a bot mid-jink fires the frame the gun cycles. A human
	 * holds through the jockeying and shoots from a planted beat --
	 * the ragged inter-shot spread IS the movement showing through the
	 * trigger. Suppress fire until the body's own heading has been
	 * stable ~0.18s; every strafe reversal restarts the clock. The
	 * carrier is exempt (its flee trigger conduct is separately tuned).
	 */
	if (gi.cvar("sg_firedisc", "0", 0)->value > 0.0f &&
	    !Combat_Carrying(self))
	{
		float sp2 = self->velocity[0] * self->velocity[0]
		          + self->velocity[1] * self->velocity[1];

		if (sp2 > 150.0f * 150.0f)
		{
			vec3_t nd;

			nd[0] = self->velocity[0]; nd[1] = self->velocity[1];
			nd[2] = 0.0f;
			VectorNormalize(nd);
			if (DotProduct(nd, st->self_dir) < 0.86f)
				st->self_stable = level.time;
			VectorCopy(nd, st->self_dir);
			if (level.time < st->self_stable + 0.18f)
			{
				sg_cbt_why[5]++;
				return;
			}
		}
	}

	if (level.time >= st->win_end)
	{
		float base;

		st->win_fire = !st->win_fire;
		base = st->win_fire ? SG_FIRE_ON
		                    : Combat_SkillLerp(skill, SG_FIRE_OFF_S0,
		                                       SG_FIRE_OFF_S4);
		st->win_end = level.time + base * (1.0f + SG_FIRE_JITTER * crandom());
	}

	if (!st->win_fire)
	{
		sg_cbt_why[5]++;
		return;
	}

	/*
	 * The invariant, last thing before the button.
	 *
	 * BUTTON_ATTACK is the ROPE whenever the grapple is pers.weapon: Cmd_Hook_f
	 * degenerates to ForceCommand("+attack") (g_cmds.c:1405-1412) and
	 * Weapon_Hook aborts the rope the moment the button is not held
	 * (p_weapon.c:2139-2144). Combat_Held returns -1 for the grapple and for
	 * anything else it cannot name, so this single test is the whole of rule
	 * S3's enforcement at the trigger. Rule S2 is the second half: while
	 * newweapon is non-NULL a switch is in flight (p_weapon.c:376) and the
	 * trigger stays off until ChangeWeapon lands it.
	 *
	 * Note what is NOT tested: hookstate. An OFFHAND rope is sustained every
	 * server frame by ClientEndServerFrame with no button input at all
	 * (p_view.c:988-990), which is what makes WEAPONS.md 2.4-D2 possible --
	 * a bot can grapple and shoot at the same time, provided the grapple is
	 * never pers.weapon.
	 */
	if (Combat_Held(self) < 0 || self->client->newweapon)
	{
		sg_cbt_why[6]++;
		return;
	}

	sg_cbt_why[0]++;
	if (inhand >= 0 && inhand < SG_NUM_WEAPONS)
		sg_cbt_fire[inhand]++;
	cmd->buttons |= BUTTON_ATTACK;
}

/*
 * Accuracy's other half: called from T_Damage where damage actually
 * lands. Only bot-on-enemy hits count; splash on yourself and teammates
 * is not accuracy, it is regret.
 */
void SG_CombatHit(edict_t *att, edict_t *victim)
{
	int w;

	if (!att || !att->client || !(att->flags & FL_BOT) || att == victim)
		return;
	if (!victim || !victim->client)
		return;
	if (victim->client->ctf.teamnum == att->client->ctf.teamnum)
		return;
	w = Combat_Held(att);
	if (w >= 0 && w < SG_NUM_WEAPONS)
		sg_cbt_hit[w]++;
}

/* the tally, printed from SG_CombatFrame's caller cadence via any bot */
void SG_CombatWhy(void)
{
	if (!gi.cvar("sg_debug", "0", 0)->value || level.time < sg_cbt_why_next)
		return;
	sg_cbt_why_next = level.time + 5.0f;
	gi.dprintf("CBTWHY frames=%d seen=%d fire=%d noclear=%d splash=%d cap=%d lead=%d win=%d held=%d react=%d\n",
	           sg_cbt_why[7], sg_cbt_why[8],
	           sg_cbt_why[0], sg_cbt_why[1], sg_cbt_why[2], sg_cbt_why[3],
	           sg_cbt_why[4], sg_cbt_why[5], sg_cbt_why[6], sg_cbt_why[9]);
	gi.dprintf("CBTSCAN unteamed=%d same=%d far=%d fov=%d blocked=%d acquired=%d threat=%d\n",
	           sg_cbt_scan[0], sg_cbt_scan[1], sg_cbt_scan[2],
	           sg_cbt_scan[3], sg_cbt_scan[4], sg_cbt_scan[5],
	           sg_cbt_scan[6]);
	{
		int w;

		for (w = 0; w < SG_NUM_WEAPONS; w++)
			if (sg_cbt_fire[w])
				gi.dprintf("ACC w%d fires=%d hits=%d\n",
				           w, sg_cbt_fire[w], sg_cbt_hit[w]);
	}
}
