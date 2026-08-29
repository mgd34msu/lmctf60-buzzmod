

#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_combat.h"
#include "slipgate/sg_combat_alert_policy.h"
#include "slipgate/sg_combat_commit_policy.h"
#include "slipgate/sg_combat_land_lead.h"
#include "slipgate/sg_combat_target_policy.h"
#include "slipgate/sg_item_route.h"
#include "slipgate/sg_persona.h"    /* who is holding the gun, not just how well */

#include <math.h>
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_util.h"
#include "slipgate/sg_hooks.h"

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
 * through P_ProjectSource (p_weapon.c:30-40), which is static to p_weapon.c.
 * The non-ballistic path below repeats only its handedness adjustment, then
 * calls G_ProjectSource with the weapon's literal firing offset. The envelope
 * and the pre-fire trace therefore use the same final packed view, origin and
 * direction as the physical weapon instead of treating an eye-clear line as a
 * muzzle-clear one.
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

/* Switch before a fresh believed contact, but rate-limit changes during a
 * duel. A dry weapon at contact range bypasses both delays. */
#define SG_WS_DECIDE_S0		0.60f
#define SG_WS_DECIDE_S4		0.20f
#define SG_WS_COOLDOWN		4.0f
#define SG_WS_PRE_MISS		0.33f
#define SG_WS_PRE_FRESH		3.0f
#define SG_WS_PRE_REACH		1200.0f

#define SG_WEIGHT_TICK		1.0f	/* item worths, same cadence as Fields_Refresh */


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


#define SG_TEX_OVER_TRACK	0.08f	/* overshoot, as a fraction of the swing, */
#define SG_TEX_OVER_FLICK	0.15f	/* for style 0 and style 1 respectively */

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
 * Aim texture is presentation only insofar as its consequences are concerned:
 * the view is the weapon ray, so the textured ray must remain a viable hit.
 * Keep it inside a target-box envelope that closes to its residual size in two
 * server ticks.  Hitscan gets the tighter centre-biased envelope; direct
 * projectiles retain a little more room for their lead without becoming wall
 * or clear-shot noise.  Grenades are excluded below because their arc has a
 * separately validated impact point.
 */
#define SG_AIM_ENVELOPE_SETTLE	0.20f
#define SG_AIM_HITSCAN_START	0.45f
#define SG_AIM_HITSCAN_RESIDUAL	0.18f
#define SG_AIM_PROJECTILE_START	0.70f
#define SG_AIM_PROJECTILE_RESIDUAL 0.40f
#define SG_AIM_MUZZLE_ITERATIONS	3

/*
 * Mean square of the correction shape (1-x)cos(2*pi*n*x) over x in [0,1]:
 *
 *     1/6 + 1/(16*pi^2*n^2)  ==  0.173 (n=1), 0.169 (n=2)
 *
 * One number for both because the difference is under 3% and this is a term
 * that gets subtracted under a square root.
 */
#define SG_TEX_SHAPE_MS		0.173f


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
	uint64_t	enemy_ctfid;	/* exact target life occupying that slot */
	float		acquired_at;	/* level.time the target was acquired */

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
	qboolean	post_defender;	/* permits the defender-only stocked fallback */
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
	uint64_t	enemy_last_ctfid; /* life identity of that sighting */
	vec3_t		enemy_org;
	float		enemy_time;		/* level.time of the last successful scan */
	int			enemy_weapon;	/* weapon index seen in their hands, -1 none */
	float		enemy_want_range; /* range terms frozen at that sighting */

	/* The corner hold. lost_client is the client number, not an edict index,
	 * so it compares directly against the belief table's own field. The
	 * deadline and lost_ctfid jointly validate it: the deadline makes zeroed
	 * process storage inert on its first frame, while ctfid prevents a later
	 * life in the same slot from inheriting the pursuit. */
	int			lost_client;
	uint64_t	lost_ctfid;		/* exact client life being pursued */
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
	uint32_t	random_state;   /* private aim/trigger sequence per client */
} sg_combat_state_t;

static sg_combat_state_t sg_combat[SG_COMBAT_MAXCLIENTS];
static qboolean sg_combat_initialized[SG_COMBAT_MAXCLIENTS];

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

/*
 * Combat state is process storage indexed by a recyclable client number.
 * Its reset is deliberately separate from Combat_CacheItems: itemlist lives
 * for the game DLL's lifetime, while these clocks and beliefs live for one
 * client on one level.  A caller may arrive before the level hook, so every
 * state-bearing public entry goes through Combat_ClientState as well.
 */
static uint32_t Combat_RandomNext(sg_combat_state_t *st)
{
	uint32_t state = st ? st->random_state : 0;

	if (state == 0)
		state = UINT32_C(0x6d2b79f5);
	state ^= state >> 16;
	state *= UINT32_C(0x7feb352d);
	state ^= state >> 15;
	state *= UINT32_C(0x846ca68b);
	state ^= state >> 16;
	if (state == 0)
		state = UINT32_C(0x27d4eb2d);
	if (st)
		st->random_state = state;
	return state;
}

static float Combat_RandomUnit(sg_combat_state_t *st)
{
	return (float)(Combat_RandomNext(st) & UINT32_C(0x00ffffff)) /
	    16777216.0f;
}

static float Combat_RandomSigned(sg_combat_state_t *st)
{
	return 2.0f * Combat_RandomUnit(st) - 1.0f;
}

static void Combat_ResetState(sg_combat_state_t *st, unsigned identity)
{
	memset(st, 0, sizeof(*st));
	st->random_state = (identity + 1u) * UINT32_C(0x9e3779b9) ^
	    UINT32_C(0x85ebca6b);
	(void)Combat_RandomNext(st);
	st->band = -1;
	st->want = -1;
	st->post_sight = -1.0f;
	st->alert_range = -1.0f;
	st->enemy_weapon = -1;
	st->ws_pre = -1;
}

static unsigned Combat_RandomIdentity(int client_index,
	uint64_t client_life)
{
	uint32_t folded_life = (uint32_t)client_life ^
	    (uint32_t)(client_life >> 32);

	return (unsigned)(((uint32_t)(client_index + 1) *
	    UINT32_C(0x9e3779b9)) ^ folded_life ^ UINT32_C(0x27d4eb2d));
}

static int Combat_ClientIndex(edict_t *self)
{
	int ci;

	if (!self || !self->client)
		return -1;
	ci = (int)(self->client - game.clients);
	if (ci < 0 || ci >= SG_COMBAT_MAXCLIENTS)
		return -1;
	return ci;
}

static sg_combat_state_t *Combat_ClientState(edict_t *self)
{
	int ci = Combat_ClientIndex(self);

	if (ci < 0)
		return NULL;
	if (!sg_combat_initialized[ci])
	{
		Combat_ResetState(&sg_combat[ci], (unsigned)ci);
		sg_combat_initialized[ci] = true;
	}
	return &sg_combat[ci];
}

void Combat_ResetClient(edict_t *self)
{
	int ci = Combat_ClientIndex(self);

	if (ci < 0)
		return;
	Combat_ResetState(&sg_combat[ci], Combat_RandomIdentity(ci,
	    self->client->ctf.ctfid));
	sg_combat_initialized[ci] = true;
}

void Combat_ResetLevel(void)
{
	int i;

	for (i = 0; i < SG_COMBAT_MAXCLIENTS; i++)
	{
		Combat_ResetState(&sg_combat[i], (unsigned)i);
		sg_combat_initialized[i] = true;
	}
	memset(sg_cbt_why, 0, sizeof(sg_cbt_why));
	memset(sg_cbt_scan, 0, sizeof(sg_cbt_scan));
	memset(sg_cbt_fire, 0, sizeof(sg_cbt_fire));
	memset(sg_cbt_hit, 0, sizeof(sg_cbt_hit));
	sg_cbt_why_next = 0.0f;
}

/* ------------------------------------------------------------------- skill */

static cvar_t	*sg_bot_skill;
static cvar_t	*sg_wswitch;

/*
 * The switch-discipline gate. sg_wswitch 0 is every path it guards compiled
 * in and never taken: no corridor pre-switch, no decision delay, no cooldown,
 * no panic exception, and the selection behaves exactly as it did before this
 * block existed. The POINTER is cached for the same reason bot_skill's is --
 * this is read per engaged bot per frame and sg_host.cvar walks the engine's list
 * -- while the VALUE is read fresh, so flipping it mid-match takes effect on
 * the next frame.
 */
static qboolean Combat_WSwitch(void)
{
	if (!sg_wswitch)
		sg_wswitch = sg_cv.wswitch;
	return (qboolean)(sg_wswitch && sg_wswitch->value != 0.0f);
}

static cvar_t	*sg_aimtexture;

/*
 * Is the aim texture armed? Pointer resolved once, value read fresh, for the
 * same reason sg_bot_skill is: sg_host.cvar walks the engine's list and this is
 * asked several times per engaged bot per frame, but flipping the cvar
 * mid-match has to take on the next one.
 *
 * Default 0. Everything this switch guards is additive, so with it off the
 * aim path is the one that shipped and not a re-derivation of it.
 */
static qboolean Combat_TexOn(void)
{
	if (!sg_aimtexture)
		sg_aimtexture = sg_cv.aimtexture;
	return (qboolean)(sg_aimtexture && sg_aimtexture->value != 0.0f);
}


static float Combat_Skill(edict_t *self)
{
	float	team, s;
	int		ci, grade;

	if (!sg_bot_skill)
		sg_bot_skill = sg_host.cvar("bot_skill", "4", 0);

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

static qboolean Combat_Visible(edict_t *viewer, edict_t *target)
{
	vec3_t	eye, mid;
	trace_t	tr;

	VectorCopy(viewer->s.origin, eye);
	eye[2] += viewer->viewheight;
	VectorAdd(target->absmin, target->absmax, mid);
	VectorScale(mid, 0.5f, mid);

	if (!sg_host.in_pvs(eye, mid))
		return false;
	tr = sg_host.trace(eye, NULL, NULL, mid, viewer, MASK_OPAQUE);
	return tr.fraction >= 1.0f;
}

static void Combat_Center(edict_t *e, vec3_t out)
{
	VectorAdd(e->absmin, e->absmax, out);
	VectorScale(out, 0.5f, out);
}

static qboolean Combat_IsEnemyCarrier(edict_t *self, edict_t *target);


static edict_t *Combat_Scan(edict_t *self, vec3_t eye, vec3_t forward,
	const float *threat, int incumbent_index, qboolean count_diagnostics)
{
	edict_t	*best = NULL;

	float	range_limit = SG_ENGAGE_RANGE * SG_PersonaAggression(self) *
	                   SG_TiltCaution(self);
	float	bestscore = range_limit;
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
		sg_combat_preview_candidate_t candidate;
		qboolean rear_cone = false;

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
		{
			if (count_diagnostics)
				sg_cbt_scan[0]++;
			continue;
		}
		if (theirteam == myteam)
		{
			if (count_diagnostics)
				sg_cbt_scan[1]++;
			continue;
		}
		Combat_Center(p, mid);
		VectorSubtract(mid, eye, delta);
		dist = VectorLength(delta);
		if (dist < 1.0f || dist >= range_limit)
		{
			if (count_diagnostics)
				sg_cbt_scan[2]++;
			continue;
		}

		/* forward cone: do not shoot backwards. The basis is the CURRENT
		 * v_angle, which is what the previous frame's cmd angles produced. */
		VectorScale(delta, 1.0f / dist, delta);
		dot = DotProduct(delta, forward);
		if (dot < SG_FOV_COS)
		{
			if (!threat || DotProduct(delta, threat) < SG_FOV_COS)
			{
				if (count_diagnostics)
					sg_cbt_scan[3]++;
				continue;
			}
			rear_cone = true;
		}

		candidate.self_team_valid = true;
		candidate.target_team_valid = true;
		candidate.same_team = false;
		candidate.same_entity = false;
		candidate.target_inuse = p->inuse;
		candidate.target_client = p->client != NULL;
		candidate.target_dead = p->deadflag == DEAD_DEAD;
		candidate.target_health = p->health;
		candidate.target_noclip = p->movetype == MOVETYPE_NOCLIP;
		candidate.distance = dist;
		candidate.range_limit = range_limit;
		candidate.forward_dot = rear_cone ? SG_FOV_COS : dot;
		candidate.visibility_known = false;
		candidate.visible = false;
		if (!SG_CombatPreviewCandidateEligible(&candidate))
			continue;
		if (rear_cone && count_diagnostics)
			sg_cbt_scan[6]++;

		candidate.visibility_known = true;
		candidate.visible = Combat_Visible(self, p);
		if (!SG_CombatPreviewCandidateEligible(&candidate))
		{
			if (count_diagnostics)
				sg_cbt_scan[4]++;
			continue;
		}
		if (count_diagnostics)
			sg_cbt_scan[5]++;

		{
			float score = SG_CombatTargetScore(dist,
			    (int)(p - g_edicts), incumbent_index,
			    Combat_IsEnemyCarrier(self, p));

			if (score >= bestscore)
				continue;
			best = p;
			bestscore = score;
		}
	}

	return best;
}

qboolean SG_CombatWouldEngage(edict_t *self)
{
	vec3_t eye, forward;

	if (!self || !self->inuse || !self->client ||
	    self->deadflag == DEAD_DEAD || self->health <= 0 ||
	    self->movetype == MOVETYPE_NOCLIP)
		return false;
	VectorCopy(self->s.origin, eye);
	eye[2] += self->viewheight;
	AngleVectors(self->client->v_angle, forward, NULL, NULL);
	return Combat_Scan(self, eye, forward, NULL, -1, false) != NULL;
}

/*
 * Is this edict the enemy flag carrier CACO believes in? WEAPONS.md 2.4-D1
 * gives the carrier its own engagement, so combat has to be able to name one.
 * enemy_carrier[team-1] is who holds OUR team's flag (sg_local.h:71).
 */
static qboolean Combat_IsEnemyCarrier(edict_t *self, edict_t *target)
{
	int team = self->client->ctf.teamnum;
	int target_client;
	sg_belief_carrier_t *carrier;

	if ((team != CTF_TEAM_RED && team != CTF_TEAM_BLUE) || !target ||
	    !target->client)
		return false;
	target_client = (int)(target - g_edicts) - 1;
	carrier = &sg_caco_team_belief.enemy_carrier[SG_TeamIdx(team)];
	return SG_CombatEnemyCarrierAllowed(team, game.maxclients,
	    carrier->client, target_client, ClientHasFlag(target) != NULL);
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
		return SG_CombatRocketRadius() + 1.0f +
		       (quad ? SG_QUAD_SPLASH_MARGIN : 0.0f);
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

/* A clear muzzle ray is not team safety for a splash weapon: a different
 * teammate can be beside the wall, floor, or target where the shot lands.
 * Scan the authoritative client roster so human teammates and SG teammates
 * receive the same protection. */
static qboolean Combat_TeamSplashSafe(edict_t *self, float dsafe,
	const vec3_t impact)
{
	int team, client_index;

	if (!self || !self->client || !impact || !isfinite(dsafe) ||
	    dsafe <= 0.0f || !isfinite(impact[0]) || !isfinite(impact[1]) ||
	    !isfinite(impact[2]))
		return false;
	team = self->client->ctf.teamnum;
	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return false;
	for (client_index = 1; client_index <= game.maxclients; client_index++)
	{
		edict_t *mate = &g_edicts[client_index];
		vec3_t centre, delta;

		if (mate == self || !mate->inuse || !mate->client ||
		    mate->deadflag || mate->health <= 0 ||
		    mate->movetype == MOVETYPE_NOCLIP ||
		    mate->client->ctf.teamnum != team)
			continue;
		Combat_Center(mate, centre);
		VectorSubtract(centre, impact, delta);
		if (VectorLength(delta) < dsafe)
			return false;
	}
	return true;
}


static qboolean Combat_TeamHitscanSafe(edict_t *self, int weapon,
	const vec3_t muzzle, const vec3_t shotdir, float max_forward,
	float source_pad, qboolean water_path)
{
	vec3_t angles, forward, right, up;
	float hspread, vspread, yaw_angle, scatter_scale;
	int team, client_index;

	if (!self || !self->client || !g_edicts || !muzzle || !shotdir ||
	    !isfinite(max_forward) || max_forward <= 0.0f ||
	    !isfinite(source_pad) || source_pad < 0.0f)
		return false;
	switch (weapon)
	{
	case SG_W_SHOTGUN:
		hspread = 500.0f;
		vspread = 500.0f;
		yaw_angle = 0.0f;
		break;
	case SG_W_SSHOTGUN:
		hspread = DEFAULT_SHOTGUN_HSPREAD;
		vspread = DEFAULT_SHOTGUN_VSPREAD;
		yaw_angle = 5.0f;
		break;
	case SG_W_MACHINEGUN:
		hspread = DEFAULT_BULLET_HSPREAD;
		vspread = DEFAULT_BULLET_VSPREAD;
		yaw_angle = 0.7f;
		break;
	case SG_W_CHAINGUN:
		hspread = DEFAULT_BULLET_HSPREAD;
		vspread = DEFAULT_BULLET_VSPREAD;
		yaw_angle = 0.0f;
		break;
	default:
		return true;
	}
	team = self->client->ctf.teamnum;
	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return false;
	if (!isfinite(muzzle[0]) || !isfinite(muzzle[1]) ||
	    !isfinite(muzzle[2]) || !isfinite(shotdir[0]) ||
	    !isfinite(shotdir[1]) || !isfinite(shotdir[2]))
		return false;
	vectoangles((float *)shotdir, angles);
	AngleVectors(angles, forward, right, up);
	scatter_scale = water_path ? 3.0f : 1.0f;

	for (client_index = 1; client_index <= game.maxclients; client_index++)
	{
		edict_t *mate = &g_edicts[client_index];
		vec3_t centre, delta, half;
		float along, lateral, vertical, radius;
		float hreach, vreach, hratio, vratio;
		int axis;

		if (mate == self || !mate->inuse || !mate->client ||
		    mate->deadflag || mate->health <= 0 ||
		    mate->movetype == MOVETYPE_NOCLIP ||
		    mate->client->ctf.teamnum != team)
			continue;
		Combat_Center(mate, centre);
		for (axis = 0; axis < 3; axis++)
		{
			if (!isfinite(mate->absmin[axis]) ||
			    !isfinite(mate->absmax[axis]) ||
			    mate->absmin[axis] > mate->absmax[axis])
				return false;
			half[axis] = 0.5f * (mate->absmax[axis] - mate->absmin[axis]);
		}
		VectorSubtract(centre, muzzle, delta);
		along = DotProduct(delta, forward);
		radius = VectorLength(half);
		if (!isfinite(along) || !isfinite(radius))
			return false;
		if (along + radius < 0.0f || along - radius > max_forward)
			continue;
		if (along < 0.0f)
			along = 0.0f;
		lateral = (float)fabs((double)DotProduct(delta, right));
		vertical = (float)fabs((double)DotProduct(delta, up));
		if (water_path)
		{
			/* The second scatter is expressed in the first scattered ray's
			 * basis, so tangent addition -- not merely 1x+2x -- is the
			 * conservative envelope.  Use its full radial cone on both axes. */
			float radial = (float)sqrt((double)(hspread * hspread +
			                                     vspread * vspread)) / 8192.0f;
			float denom = 1.0f - 2.0f * radial * radial;
			float water_ratio;
			float yaw_tangent = (float)tan((double)yaw_angle * M_PI / 180.0);

			if (denom <= 0.0f)
				return false;
			water_ratio = scatter_scale * radial / denom;
			denom = 1.0f - yaw_tangent * water_ratio;
			if (denom <= 0.0f)
				return false;
			hratio = (yaw_tangent + water_ratio) / denom;
			vratio = hratio;
		}
		else
		{
			/* Exact dry envelope in the nominal ray basis.  The yawed
			 * machinegun/SSG ray shortens nominal-forward progress while its
			 * own right-axis spread grows, hence the shared denominator. */
			float hscatter = hspread / 8192.0f;
			float vscatter = vspread / 8192.0f;
			float yaw_radians = yaw_angle * (float)M_PI / 180.0f;
			float yaw_sine = (float)sin((double)yaw_radians);
			float yaw_cosine = (float)cos((double)yaw_radians);
			float denom = yaw_cosine - yaw_sine * hscatter;

			if (denom <= 0.0f)
				return false;
			hratio = (yaw_sine + yaw_cosine * hscatter) / denom;
			vratio = vscatter / denom;
		}
		hreach = radius + source_pad + along * hratio;
		vreach = radius + source_pad + along * vratio;
		if (lateral <= hreach && vertical <= vreach)
			return false;
	}
	return true;
}

/*
 * Would firing weapon w right now hurt the bot or a live teammate? impact is
 * the point the pre-fire trace says the shot stops at -- rule R1's own
 * construction: tr.endpos IS the impact point.
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
	if (VectorLength(v) < dsafe)
		return false;
	return Combat_TeamSplashSafe(self, dsafe, impact);
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
 * Mid ranks the rocket over the rail based on measured accuracy: this body
 * rails at 12-14% under the skill scatter and lands
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


	if (sg_cv.wcommit->value != 0.0f)
	{
		int held = Combat_Held(self);

		if (held >= 0 && SG_CombatCommitCandidateAllowed(
		        sg_cv.wcommit->value, held == SG_W_BLASTER,
		        Combat_Stocked(self, held),
		        Combat_BandAllows(self, held, dist)))
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
static int Combat_PostWeapon(edict_t *self, float sightline,
                             qboolean stocked_fallback)
{
	static const int ladder_ssg[] = { SG_W_SSHOTGUN, -1 };
	static const int ladder_rl[] = { SG_W_ROCKETLAUNCHER, -1 };
	static const int ladder_cg[] = { SG_W_CHAINGUN, -1 };
	static const int ladder_rg[] = { SG_W_RAILGUN, -1 };
	static const int stocked_contact[] = {
		SG_W_SSHOTGUN, SG_W_CHAINGUN, SG_W_HYPERBLASTER,
		SG_W_SHOTGUN, SG_W_MACHINEGUN, SG_W_RAILGUN,
		SG_W_PLASMA, SG_W_ROCKETLAUNCHER, SG_W_GRENADELAUNCHER,
		SG_W_BFG, -1
	};
	static const int stocked_close[] = {
		SG_W_ROCKETLAUNCHER, SG_W_CHAINGUN, SG_W_HYPERBLASTER,
		SG_W_SSHOTGUN, SG_W_SHOTGUN, SG_W_RAILGUN,
		SG_W_MACHINEGUN, SG_W_GRENADELAUNCHER, SG_W_PLASMA,
		SG_W_BFG, -1
	};
	static const int stocked_mid[] = {
		SG_W_ROCKETLAUNCHER, SG_W_RAILGUN, SG_W_HYPERBLASTER,
		SG_W_CHAINGUN, SG_W_MACHINEGUN, SG_W_SHOTGUN,
		SG_W_BFG, SG_W_GRENADELAUNCHER, SG_W_PLASMA,
		SG_W_SSHOTGUN, -1
	};
	static const int stocked_long[] = {
		SG_W_RAILGUN, SG_W_HYPERBLASTER, SG_W_ROCKETLAUNCHER,
		SG_W_CHAINGUN, SG_W_MACHINEGUN, SG_W_SHOTGUN,
		SG_W_BFG, SG_W_GRENADELAUNCHER, SG_W_PLASMA,
		SG_W_SSHOTGUN, -1
	};
	const int	*want;
	const int	*stocked;
	int			w;
	int			owned;

	if (sightline < SG_POST_SSG)
		want = ladder_ssg;
	else if (sightline < SG_POST_ROCKET)
		want = ladder_rl;
	else if (sightline < SG_POST_CHAINGUN)
		want = ladder_cg;
	else
	{
		want = ladder_rg;
	}

	if (sightline < SG_R_CLOSE)
		stocked = stocked_contact;
	else if (sightline < SG_R_MID)
		stocked = stocked_close;
	else if (sightline < SG_R_LONG)
		stocked = stocked_mid;
	else
		stocked = stocked_long;

	w = Combat_WalkLadder(self, want, sightline, true);
	if (w >= 0)
		return w;

	/* the doctrine weapon is not in the pack: hold the band ladder's answer
	 * for the sightline instead of standing there with a blaster */
	if (sightline < SG_R_CLOSE)
		w = Combat_Choose(self, SG_BAND_CONTACT, sightline, false);
	else if (sightline < SG_R_MID)
		w = Combat_Choose(self, SG_BAND_CLOSE, sightline, false);
	else if (sightline < SG_R_LONG)
		w = Combat_Choose(self, SG_BAND_MID, sightline, false);
	else
		w = Combat_Choose(self, SG_BAND_LONG, sightline, false);

	if (!stocked_fallback)
		return w;

	/* Combat_Choose only sees the weapons named by the range band's ordinary
	 * ladder.  A stocked rail/RL outside that ladder is still a real owned
	 * gun, however, and a posted bot must not regress to the spawn blaster
	 * merely because the doctrine row was absent.  Prefer this exact-band
	 * stocked read only when the ordinary answer is unstocked; the doctrine
	 * answer remains authoritative when it already has a stocked gun. */
	owned = Combat_WalkLadder(self, stocked, sightline, true);
	if (owned >= 0 && (w == SG_W_BLASTER || !Combat_Stocked(self, w)))
		w = owned;
	return w;
}

int SG_CombatBestPostWeapon(edict_t *self, float sightline)
{
	if (!self || !self->client)
		return -1;
	Combat_CacheItems();
	return Combat_PostWeapon(self, sightline, true);
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
	 * `who` may be a human here -- Cbt_Track snapshots the visible foe's
	 * preference through this same function -- and a human has no row, so this
	 * is 1.0. The snapshot is retained after sight is lost; this function is
	 * not called on a hidden opponent.
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

		if (sg_cv.debug->value)
			sg_host.dprint("WSWITCH mid %s w%d->w%d waited=%.0fms (%s)\n",
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
		sg_belief_enemy_t	*en = &sg_caco_enemies[SG_TeamIdx(team)][s];
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

	if (want != st->ws_pre && sg_cv.debug->value)
		sg_host.dprint("WSWITCH pre %s w%d->w%d expect=%.0f hand-wants=%.0f "
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
static void Combat_SamplePerp(sg_combat_state_t *st, vec3_t dir, vec3_t out)
{
	vec3_t	v;
	float	d, len;
	int		tries;

	for (tries = 0; tries < 4; tries++)
	{
		v[0] = Combat_RandomSigned(st);
		v[1] = Combat_RandomSigned(st);
		v[2] = Combat_RandomSigned(st);

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
	Combat_SamplePerp(st, dir, st->err);
}

/* ----------------------------------------------------------- aim texture */


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
	over *= sg_cv.aimtexture->value;
	if (over > SG_TEX_OVER_CAP)
		over = SG_TEX_OVER_CAP;

	win = Combat_SkillLerp(skill, SG_SETTLE_S0, SG_SETTLE_S4)
	    * Combat_TexFitts(flick)
	    * (1.0f + SG_TEX_STYLE_WIN * (0.5f - style));

	st->tex_over = over;
	st->tex_win = win;

	st->tex_cyc = (style >= 0.5f) ? 2.0f : 1.0f;

	if (sg_cv.debug->value)
		sg_host.dprint("AIMTEX %s flick=%.1f over=%.2f settle=%dms cyc=%d "
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

	x = (level.time - st->acquired_at) / st->tex_win;
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


static void Combat_TexWander(sg_combat_state_t *st, vec3_t dir)
{
	vec3_t	kick;
	float	dt, w, len;

	/* A newer acquisition stamp invalidates the prior target's wander. */
	if (st->tex_wander < st->acquired_at)
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

	Combat_SamplePerp(st, dir, kick);
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
 * Project a composed aim ray back into the target's inner box.  `lead` is the
 * point the firing solution intends to hit (the centre for hitscan, predicted
 * centre for a projectile); the ray's lateral displacement at that range is
 * therefore bounded by the smallest target half-extent.  This runs after all
 * synthetic error has been composed, so persona, tremor and texture can vary
 * the hand-like path only inside a ray that can still plausibly land.
 */
static void Combat_ConstrainAim(edict_t *enemy, int weapon, vec3_t origin,
                                vec3_t lead, sg_combat_state_t *st,
                                vec3_t aim, float source_pad)
{
	vec3_t	clean, lateral;
	float	range, dot, lateral_len, half, start, residual, fraction;
	float	allowed, tangent;

	VectorSubtract(lead, origin, clean);
	range = VectorLength(clean);
	if (range < 1.0f)
		return;
	VectorScale(clean, 1.0f / range, clean);

	/* A ray inside this sphere is inside every axis of the target box. */
	half = 0.5f * (enemy->maxs[0] - enemy->mins[0]);
	if (0.5f * (enemy->maxs[1] - enemy->mins[1]) < half)
		half = 0.5f * (enemy->maxs[1] - enemy->mins[1]);
	if (0.5f * (enemy->maxs[2] - enemy->mins[2]) < half)
		half = 0.5f * (enemy->maxs[2] - enemy->mins[2]);
	/* Chaingun_Fire can move its source anywhere inside the conservative
	 * +/- source_pad cube below.  Keep the nominal endpoint seven units inside
	 * the inscribed sphere, so every possible source ray remains in the live
	 * target box rather than merely the centre muzzle ray. */
	if (source_pad > 0.0f)
		half -= source_pad * 1.75f;
	if (half < 1.0f)
		return;

	if (sg_weapons[weapon].speed <= 0.0f)
	{
		start = SG_AIM_HITSCAN_START;
		residual = SG_AIM_HITSCAN_RESIDUAL;
	}
	else
	{
		start = SG_AIM_PROJECTILE_START;
		residual = SG_AIM_PROJECTILE_RESIDUAL;
	}
	fraction = (level.time - st->acquired_at) / SG_AIM_ENVELOPE_SETTLE;
	if (fraction < 0.0f)
		fraction = 0.0f;
	if (fraction > 1.0f)
		fraction = 1.0f;
	allowed = half * (start + (residual - start) * fraction);

	dot = DotProduct(aim, clean);
	if (dot <= 0.0f)
	{
		VectorCopy(clean, aim);
		return;
	}
	VectorMA(aim, -dot, clean, lateral);
	lateral_len = VectorLength(lateral);
	if (lateral_len < 0.0001f)
		return;

	/* At `range`, tan(theta) is exactly the lateral endpoint displacement. */
	tangent = lateral_len / dot;
	if (tangent <= allowed / range)
		return;
	VectorScale(lateral, (allowed / range) / lateral_len, lateral);
	VectorAdd(clean, lateral, aim);
	VectorNormalize(aim);
}

/*
 * Return whether the forward ray crosses the target box whose live dimensions
 * have been translated so its centre is `centre`.  For hitscan `centre` is the
 * live absmin/absmax centre; for a direct projectile it is its predicted
 * centre.  Keeping the asymmetric mins/maxs is important for a crouched
 * player: its centre is not the entity origin.
 */
static qboolean Combat_RayHitsTargetBox(vec3_t origin, vec3_t dir,
                                        edict_t *enemy, vec3_t centre)
{
	float	enter = 0.0f, leave = 1000000.0f;
	int		i;

	for (i = 0; i < 3; i++)
	{
		float	boxcentre = 0.5f * (enemy->mins[i] + enemy->maxs[i]);
		float	lo = centre[i] + enemy->mins[i] - boxcentre;
		float	hi = centre[i] + enemy->maxs[i] - boxcentre;
		float	t0, t1;

		if ((float)fabs((double)dir[i]) < 0.000001f)
		{
			if (origin[i] < lo || origin[i] > hi)
				return false;
			continue;
		}

		t0 = (lo - origin[i]) / dir[i];
		t1 = (hi - origin[i]) / dir[i];
		if (t0 > t1)
		{
			float	tmp = t0;

			t0 = t1;
			t1 = tmp;
		}
		if (t0 > enter)
			enter = t0;
		if (t1 < leave)
			leave = t1;
		if (leave < enter)
			return false;
	}

	return leave >= 0.0f;
}

/*
 * A chaingun's source is chosen after the decision.  Its actual set is inside
 * the +/- four unit source cube, so require all eight cube corners to cross
 * the target box.  The feasible-source set is convex and the target box is
 * convex, making this a conservative proof for every source between them.
 */
static qboolean Combat_RayHitsMuzzleEnvelope(vec3_t muzzle, vec3_t dir,
                                             edict_t *enemy, vec3_t centre,
                                             float source_pad)
{
	int		x, y, z;

	if (source_pad <= 0.0f)
		return Combat_RayHitsTargetBox(muzzle, dir, enemy, centre);

	for (x = -1; x <= 1; x += 2)
		for (y = -1; y <= 1; y += 2)
			for (z = -1; z <= 1; z += 2)
			{
				vec3_t source;

				source[0] = muzzle[0] + (float)x * source_pad;
				source[1] = muzzle[1] + (float)y * source_pad;
				source[2] = muzzle[2] + (float)z * source_pad;
				if (!Combat_RayHitsTargetBox(source, dir, enemy, centre))
					return false;
			}
	return true;
}

/*
 * Pmove consumes a short command plus delta_angles, clamps pitch, then writes
 * v_angle.  Reconstruct that exact view here instead of trusting the float
 * aim that was packed into the command.  The weapon and pre-fire trace must
 * agree on this quantised direction.
 */
static void Combat_CmdView(edict_t *self, usercmd_t *cmd, vec3_t angles)
{
	short	packed;
	int		i;

	if (self->client->ps.pmove.pm_flags & PMF_TIME_TELEPORT)
	{
		packed = (short)(cmd->angles[YAW]
		                 + self->client->ps.pmove.delta_angles[YAW]);
		VectorClear(angles);
		angles[YAW] = SHORT2ANGLE(packed);
		return;
	}

	for (i = 0; i < 3; i++)
	{
		packed = (short)(cmd->angles[i]
		                 + self->client->ps.pmove.delta_angles[i]);
		angles[i] = SHORT2ANGLE(packed);
	}
	if (angles[PITCH] > 89.0f && angles[PITCH] < 180.0f)
		angles[PITCH] = 89.0f;
	else if (angles[PITCH] < 271.0f && angles[PITCH] >= 180.0f)
		angles[PITCH] = 271.0f;
}

/*
 * Machinegun_Fire overwrites kick_angles[PITCH] with this deterministic
 * recoil before it builds its firing forward (p_weapon.c:1136-1156).  The
 * random yaw/roll and fire_lead spread are physical dispersion, not a second
 * view ray; compensate the deterministic pitch here so the nominal fire ray
 * and this trace share the target bearing.
 */
static float Combat_MachinegunPitchKick(edict_t *self, int weapon)
{
	if (weapon != SG_W_MACHINEGUN || !self || !self->client)
		return 0.0f;
	return (float)self->client->machinegun_shots * -1.5f;
}

/* Pack a desired physical shot direction into the command that produces it. */
static void Combat_WriteShotCmd(edict_t *self, int weapon, vec3_t shot,
                                usercmd_t *cmd)
{
	float	yaw, pitch;

	yaw = (float)(atan2(shot[1], shot[0]) * 180.0 / M_PI);
	pitch = (float)(-asin(shot[2]) * 180.0 / M_PI);
	pitch -= Combat_MachinegunPitchKick(self, weapon);

	cmd->angles[YAW] = (short)(ANGLE2SHORT(yaw)
	                          - self->client->ps.pmove.delta_angles[YAW]);
	cmd->angles[PITCH] = (short)(ANGLE2SHORT(pitch)
	                            - self->client->ps.pmove.delta_angles[PITCH]);
	cmd->angles[ROLL] = (short)(ANGLE2SHORT(0.0f)
	                           - self->client->ps.pmove.delta_angles[ROLL]);
}

/*
 * Mirror P_ProjectSource's handedness and every combat weapon's literal
 * firing offset.  HyperBlaster_Fire adds its current rotating g_offset after
 * the (24, 8, viewheight-8) base.  Chaingun_Fire picks
 * r=7+crandom()*4 and u=crandom()*4; trace its nominal (7, 0) source and
 * report a four-unit hull pad that covers every possible source position
 * before its independent bullet spread.
 */
static qboolean Combat_WeaponRay(edict_t *self, int weapon, usercmd_t *cmd,
                                 vec3_t muzzle, vec3_t dir,
                                 float *source_pad)
{
	vec3_t	angles, forward, right, offset;

	if (!self || !self->client || weapon < 0 || weapon >= SG_NUM_WEAPONS)
		return false;

	Combat_CmdView(self, cmd, angles);
	angles[PITCH] += Combat_MachinegunPitchKick(self, weapon);
	AngleVectors(angles, forward, right, NULL);

	VectorSet(offset, 0.0f, 0.0f, self->viewheight - 8.0f);
	if (source_pad)
		*source_pad = 0.0f;
	switch (weapon)
	{
	case SG_W_BLASTER:
		offset[0] = 24.0f;
		offset[1] = 8.0f;
		break;
	case SG_W_HYPERBLASTER:
	{
		float rotation = (self->client->ps.gunframe - 5.0f)
		                 * 2.0f * (float)M_PI / 6.0f;

		offset[0] = 24.0f - 4.0f * (float)sin((double)rotation);
		offset[1] = 8.0f;
		offset[2] += 4.0f * (float)cos((double)rotation);
		break;
	}
	case SG_W_ROCKETLAUNCHER:
	case SG_W_BFG:
	case SG_W_PLASMA:
		offset[0] = 8.0f;
		offset[1] = 8.0f;
		break;
	case SG_W_RAILGUN:
		offset[1] = 7.0f;
		break;
	case SG_W_SHOTGUN:
	case SG_W_SSHOTGUN:
	case SG_W_MACHINEGUN:
		offset[1] = 8.0f;
		break;
	case SG_W_CHAINGUN:
		offset[1] = 7.0f;
		if (source_pad)
			*source_pad = 4.0f;
		break;
	default:
		return false;	/* grenade launcher stays on its ballistic path */
	}

	if (self->client->pers.hand == LEFT_HANDED)
		offset[1] *= -1.0f;
	else if (self->client->pers.hand == CENTER_HANDED)
		offset[1] = 0.0f;
	G_ProjectSource(self->s.origin, offset, forward, right, muzzle);
	VectorCopy(forward, dir);
	return true;
}

typedef enum
{
	COMBAT_RAY_INVALID = 0,
	COMBAT_RAY_MISS,
	COMBAT_RAY_HIT
} combat_ray_result_t;

/*
 * The muzzle moves with the packed view.  Three constraint/repack passes plus
 * a bounded final correction close that small fixed point, then the caller
 * receives the actual ray it will trace and fire.
 */
static combat_ray_result_t Combat_FinalizeMuzzleAim(edict_t *self,
                                                    edict_t *enemy, int weapon,
                                                    vec3_t lead,
                                                    sg_combat_state_t *st,
                                                    vec3_t aim, usercmd_t *cmd,
                                                    vec3_t muzzle,
                                                    vec3_t shotdir,
                                                    float *source_pad)
{
	int	pass;
	qboolean final_hit;

	for (pass = 0; pass < SG_AIM_MUZZLE_ITERATIONS; pass++)
	{
		Combat_WriteShotCmd(self, weapon, aim, cmd);
		if (!Combat_WeaponRay(self, weapon, cmd, muzzle, shotdir, source_pad))
			return COMBAT_RAY_INVALID;
		Combat_ConstrainAim(enemy, weapon, muzzle, lead, st, shotdir,
		                    *source_pad);
		VectorCopy(shotdir, aim);
	}

	Combat_WriteShotCmd(self, weapon, aim, cmd);
	if (!Combat_WeaponRay(self, weapon, cmd, muzzle, shotdir, source_pad))
		return COMBAT_RAY_INVALID;
	final_hit = Combat_RayHitsMuzzleEnvelope(muzzle, shotdir, enemy, lead,
	                                         *source_pad);
	if (final_hit)
		return COMBAT_RAY_HIT;

	/* Quantisation can move an edge ray by one short.  Spend one final bounded
	 * correction rather than accepting a view ray that the physical shot misses. */
	Combat_ConstrainAim(enemy, weapon, muzzle, lead, st, shotdir,
	                    *source_pad);
	VectorCopy(shotdir, aim);
	Combat_WriteShotCmd(self, weapon, aim, cmd);
	if (!Combat_WeaponRay(self, weapon, cmd, muzzle, shotdir, source_pad))
		return COMBAT_RAY_INVALID;
	return Combat_RayHitsMuzzleEnvelope(muzzle, shotdir, enemy, lead,
	                                    *source_pad)
	       ? COMBAT_RAY_HIT : COMBAT_RAY_MISS;
}

/* Aim a physical non-ballistic ray at an intentional point (carrier splash). */
static qboolean Combat_FinalizePointRay(edict_t *self, int weapon,
                                        vec3_t point, vec3_t aim,
                                        usercmd_t *cmd, vec3_t muzzle,
                                        vec3_t shotdir, float *source_pad)
{
	int	pass;

	for (pass = 0; pass < SG_AIM_MUZZLE_ITERATIONS; pass++)
	{
		Combat_WriteShotCmd(self, weapon, aim, cmd);
		if (!Combat_WeaponRay(self, weapon, cmd, muzzle, shotdir, source_pad))
			return false;
		VectorSubtract(point, muzzle, aim);
		if (VectorNormalize(aim) < 1.0f)
			return false;
	}

	Combat_WriteShotCmd(self, weapon, aim, cmd);
	return Combat_WeaponRay(self, weapon, cmd, muzzle, shotdir, source_pad);
}

/* The pre-fire decision is deliberately independent of endpoint proximity. */
static qboolean Combat_TraceClears(int weapon, qboolean enemy_hit,
                                   qboolean unobstructed,
                                   qboolean teammate_hit)
{
	if (teammate_hit)
		return false;
	if (sg_weapons[weapon].speed <= 0.0f)
		return enemy_hit;
	return enemy_hit || unobstructed;
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

typedef struct
{
	float		projectile_time;
	sg_combat_landing_t landing;
	qboolean	landing_splash;
} combat_solve_t;

static combat_solve_t Combat_Solve(edict_t *enemy, int w, const vec3_t eye,
                                   vec3_t lead)
{
	vec3_t	mid, delta;
	float	dist, speed;
	combat_solve_t solve = { 0 };

	Combat_Center(enemy, mid);
	VectorCopy(mid, lead);

	VectorSubtract(mid, eye, delta);
	dist = VectorLength(delta);

	speed = w == SG_W_ROCKETLAUNCHER ? SG_CombatRocketSpeed() :
	        sg_weapons[w].speed;
	if (speed <= 0.0f)
		return solve;

	if (w == SG_W_ROCKETLAUNCHER &&
	    SG_CombatLandingAim(enemy, lead, &solve.landing))
	{
		solve.landing_splash = true;
		return solve;
	}

	solve.projectile_time = sg_weapons[w].windup +
	                        dist / speed;
	VectorMA(mid, solve.projectile_time, enemy->velocity, lead);

	VectorSubtract(lead, eye, delta);
	dist = VectorLength(delta);
	solve.projectile_time = sg_weapons[w].windup +
	                        dist / speed;
	VectorMA(mid, solve.projectile_time, enemy->velocity, lead);

	return solve;
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
		tr = sg_host.trace(p, NULL, NULL, next, self, MASK_SHOT);
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
		const int *ents;
		int i, n = SG_MegaEntities(&ents);

		for (i = 0; i < n; i++)
		{
			edict_t *it = &g_edicts[ents[i]];

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


#define SG_MEGA_HEADROOM	170

static float Worth_Mega(edict_t *e)
{
	float		worth;
	int			h = e->health;
	int			i, n, team;
	const int	*ents;

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
	n = SG_MegaEntities(&ents);
	for (i = 0; i < n; i++)
	{
		edict_t *it = &g_edicts[ents[i]];

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
	sg_combat_state_t *st;

	st = Combat_ClientState(self);
	if (!st)
		return 0.0f;

	/* Remove mega-health pull immediately when the bot has no headroom. */
	if (self->health >= SG_MEGA_HEADROOM)
		return 0.0f;
	return st->worth_mega;
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
static int Weapon_IndexTier(int w)
{
	switch (w)
	{
	case SG_W_ROCKETLAUNCHER:
	case SG_W_CHAINGUN:
		return 5;
	case SG_W_SSHOTGUN:
	case SG_W_RAILGUN:
	case SG_W_HYPERBLASTER:
		return 4;
	case SG_W_MACHINEGUN:
	case SG_W_GRENADELAUNCHER:
	case SG_W_PLASMA:
		return 3;
	case SG_W_SHOTGUN:
		return 2;
	case SG_W_BLASTER:
		return 1;
	default:
		/* The BFG is situational combat equipment, not a rung in the
		 * ordinary acquisition ladder (WEAPONS.md 2.3). */
		return 0;
	}
}

static int Weapon_Tier(edict_t *e)
{
	int best = 1;
	int w;

	for (w = SG_W_SHOTGUN; w < SG_NUM_WEAPONS; w++)
		if (Combat_Avail(e, w) && Weapon_IndexTier(w) > best)
			best = Weapon_IndexTier(w);
	return best;
}

static int Weapon_StockedTier(edict_t *e)
{
	int best = 1;
	int w;

	for (w = SG_W_SHOTGUN; w < SG_NUM_WEAPONS; w++)
		if (Combat_Stocked(e, w) && Weapon_IndexTier(w) > best)
			best = Weapon_IndexTier(w);
	return best;
}

qboolean SG_CombatWeaponState(edict_t *self,
                              sg_combat_weapon_state_t *out)
{
	int w;

	if (!self || !self->client || !out)
		return false;
	Combat_CacheItems();
	memset(out, 0, sizeof(*out));
	out->available_tier = Weapon_Tier(self);
	out->stocked_tier = Weapon_StockedTier(self);
	out->held_weapon = Combat_Held(self);
	for (w = SG_W_SHOTGUN; w < SG_NUM_WEAPONS; w++)
	{
		if (Combat_Avail(self, w))
			out->nonblaster_available = true;
		if (Combat_Stocked(self, w))
			out->nonblaster_stocked = true;
	}
	return true;
}

int SG_CombatHeldAmmoTag(edict_t *self)
{
	int held;

	if (!self || !self->client)
		return -1;
	Combat_CacheItems();
	held = Combat_Held(self);
	if (held < 0 || held >= SG_NUM_WEAPONS || sg_wammo[held] < 0 ||
	    sg_wammo[held] >= game.num_items)
		return -1;
	return itemlist[sg_wammo[held]].tag;
}

int SG_CombatWeaponPickupTier(const edict_t *item)
{
	int w;

	if (!item || !item->item)
		return 0;
	Combat_CacheItems();
	for (w = SG_W_BLASTER; w < SG_NUM_WEAPONS; w++)
		if (item->item == sg_witem[w])
			return Weapon_IndexTier(w);
	return 0;
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
static float Worth_Quad(edict_t *e)
{
	int i, ti;

	if (Combat_IsQuad(e))
		return 0.0f;			/* already carrying it */

	/*
	 * THIS bot's team's row. The pricing is where
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

		if (!Caco_ItemBelievedRouteableFor(e->client->ctf.teamnum, it))
			continue; /* neither standing nor this exact pad's earned lead */

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
 * items, so this cache records the best one CACO believes is standing. Route
 * pricing later converts that scalar back to the exact entity's proportion
 * with SG_RuneRouteWorth; the role and live threat multipliers remain intact.
 */
static float Rune_EntityWorth(edict_t *e, edict_t *rune)
{
	float mult;
	int tier;

	if (!e || !e->client || !rune || !rune->inuse || e->client->rune)
		return 0.0f;
	tier = Weapon_Tier(e);
	switch (rune->runetype)
	{
	case RUNE_HASTE:
		/* Exactly 2x rate of fire, with the same 2x ammunition drain. */
		mult = (tier <= 2) ? 1.20f : 1.60f;
		break;
	case RUNE_DAMAGE:
		mult = 1.45f;		/* x1.75 outgoing, g_runes.c */
		break;
	case RUNE_RESIST:
		mult = 1.45f;		/* /1.75 incoming, g_runes.c */
		break;
	case RUNE_REGEN:
		mult = 1.20f;		/* 3.33 hp/s, g_runes.c */
		break;
	case RUNE_VAMP:
		mult = 1.00f;		/* half landed player damage heals, g_combat.c */
		break;
	default:
		return 0.0f;
	}
	return Combat_Clamp(0.55f * mult);
}

static float Worth_Rune(edict_t *e)
{
	float best = 0.0f;
	int i, ti;

	if (!e || !e->client || e->client->rune)
		return 0.0f;

	/*
	 * Bots on the other team know where runes are only when they see them,
	 * never from the opposing team's knowledge.
	 * A rune belief is a sighting and nothing else -- there is no clock behind
	 * it to fall back on -- so reading the wrong team's row here would be the
	 * purest form of the leak the ruling names.
	 */
	ti = SG_TeamIdx(e->client->ctf.teamnum);

	for (i = 0; i < sg_caco_num_items; i++)
	{
		sg_belief_item_t	*b = &sg_caco_items[ti][i];
		edict_t				*it;
		float				worth;

		if (b->cls != SG_BI_RUNE || !b->believed_up)
			continue;
		it = &g_edicts[b->ent];
		if (!it->inuse)
			continue;

		worth = Rune_EntityWorth(e, it);
		if (worth > best)
			best = worth;
	}
	return best;
}

float SG_RuneRouteWorth(edict_t *self, edict_t *rune, float class_worth)
{
	float best, exact;

	if (class_worth <= 0.0f)
		return 0.0f;
	best = Worth_Rune(self);
	exact = Rune_EntityWorth(self, rune);
	if (best <= 0.0f || exact <= 0.0f)
		return 0.0f;
	return class_worth * exact / best;
}

void SG_CombatWeights(edict_t *self, const sg_weights_t *role,
                      sg_weights_t *out)
{
	sg_combat_state_t	*st;
	int					c;

	if (!self || !self->client || !role || !out)
		return;

	*out = *role;

	st = Combat_ClientState(self);
	if (!st)
		return;
	Combat_CacheItems();

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

void SG_CombatPost(edict_t *self, float sightline,
                   qboolean defender_stand)
{
	sg_combat_state_t *st = Combat_ClientState(self);

	if (!st)
		return;
	st->post_sight = sightline;
	st->post_defender = defender_stand;
}

/* Pre-select for a believed contact; bounded decay prevents stale loadouts. */
void SG_CombatAlert(edict_t *self, float expect_range)
{
	sg_combat_state_t *st = Combat_ClientState(self);

	if (!st)
		return;
	st->alert_range = expect_range;
	st->alert_until = level.time + 3.0f;
}

void SG_CombatAlertFromBeliefs(edict_t *self, const int *goal_field)
{
	rune_t *rune = SG_Rune();
	int team = self->client->ctf.teamnum;
	sg_combat_alert_selection_t selected;

	if ((team != CTF_TEAM_RED && team != CTF_TEAM_BLUE) ||
	    !rune || sg_fields.item[0] == NULL)
		return;
	if (SG_CombatAlertSelect(sg_caco_enemies[SG_TeamIdx(team)],
	    SG_MAX_ENEMY_TRACK, rune, goal_field, game.maxclients,
	    self->s.origin, level.time, &selected))
		SG_CombatAlert(self, selected.range);
}

/* ------------------------------------------------------------- duel terms */

/* Current live entity, not its retained seed belief. */
static edict_t *Combat_EnemyIdentityCurrent(edict_t *self, int enemy_index,
                                            uint64_t enemy_ctfid)
{
	edict_t *en;
	int self_team;

	if (!self || !self->inuse || !self->client ||
	    self->movetype == MOVETYPE_NOCLIP)
		return NULL;
	if (enemy_index <= 0 || enemy_index > game.maxclients ||
	    enemy_index >= globals.num_edicts)
		return NULL;
	en = g_edicts + enemy_index;
	if (!en->client)
		return NULL;
	self_team = self->client->ctf.teamnum;
	if (!SG_CombatLiveEnemyIdentityAllowed(self_team,
	    en->client->ctf.teamnum, game.maxclients, globals.num_edicts,
	    enemy_index, enemy_ctfid, en->client->ctf.ctfid, en->inuse,
	    true, !en->deadflag && en->health > 0,
	    en->movetype == MOVETYPE_NOCLIP))
		return NULL;
	return en;
}

edict_t *SG_CombatLiveEnemy(edict_t *self)
{
	sg_combat_state_t *st;

	if (!self || !self->inuse || !self->client)
		return NULL;
	st = Combat_ClientState(self);
	if (!st)
		return NULL;
	return Combat_EnemyIdentityCurrent(self, st->enemy, st->enemy_ctfid);
}

void SG_CombatPursuit(edict_t *self, qboolean allowed)
{
	sg_combat_state_t *st = Combat_ClientState(self);

	if (!st)
		return;
	st->pursue = allowed;
	if (!allowed)
		st->lost_until = 0.0f;	/* a role change ends the camp */
}

qboolean SG_CombatDuel(edict_t *self, vec3_t enemy_org, float *want_range,
                       float *exposure_w)
{
	sg_combat_state_t	*st;
	vec3_t				org, eye, d;
	float				dist, want;
	int					held, team, s;

	if (want_range)
		*want_range = 0.0f;
	if (exposure_w)
		*exposure_w = 0.0f;

	if (!self || !self->inuse || !self->client)
		return false;
	if (self->deadflag == DEAD_DEAD || self->health <= 0)
		return false;
	st = Combat_ClientState(self);
	if (!st)
		return false;

	if (st->enemy_last <= 0 || level.time - st->enemy_time > SG_DUEL_FRESH)
		return false;
	if (!Combat_EnemyIdentityCurrent(self, st->enemy_last,
	                                 st->enemy_last_ctfid))
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
			sg_belief_enemy_t *en = &sg_caco_enemies[SG_TeamIdx(team)][s];

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
	if (st->enemy_weapon >= 0 && st->enemy_weapon < SG_NUM_WEAPONS)
	{
		float theirs = st->enemy_want_range;
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
		tr = sg_host.trace(eye, NULL, NULL, probe, self, MASK_OPAQUE);
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


static qboolean Combat_LostHold(edict_t *self, sg_combat_state_t *st,
                                usercmd_t *cmd, vec3_t eye)
{
	vec3_t	aim;
	float	len, yaw, pitch;
	int		held;

	if (!st->pursue || level.time >= st->lost_until)
		return false;
	if (!Combat_EnemyIdentityCurrent(self, st->lost_client + 1,
	                                 st->lost_ctfid))
	{
		st->lost_until = 0.0f;
		st->lost_have = false;
		return false;
	}

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

/* Apply fire discipline and trigger timing to a firing solution. */
static void Cbt_Trigger(edict_t *self, usercmd_t *cmd,
                        sg_combat_state_t *st, float skill, int inhand)
{
	/* Slow weapons wait a skill-scaled beat after consuming ammo. */
	if (sg_cv.tapvar->value > 0.0f)
	{
		int hw = Combat_Held(self);

		if (hw == SG_W_RAILGUN || hw == SG_W_SSHOTGUN ||
		    hw == SG_W_ROCKETLAUNCHER || hw == SG_W_GRENADELAUNCHER ||
		    hw == SG_W_SHOTGUN || hw == SG_W_BFG)
		{
			/* Detect shots by ammo decrement because READY may occur between ticks. */
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
					    sg_cv.tapvar->value *
					    Combat_SkillLerp(skill, 0.45f, 0.12f) *
					    (0.4f + 1.2f * Combat_RandomUnit(st));
					if (sg_cv.debug->value)
						sg_host.dprint("TAPDBG %s w=%d delay=%.2f\n",
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

	/* Non-carriers hold fire until their horizontal heading is stable. */
	if (sg_cv.firedisc->value > 0.0f &&
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
		st->win_end = level.time + base *
		    (1.0f + SG_FIRE_JITTER * Combat_RandomSigned(st));
	}

	if (!st->win_fire)
	{
		sg_cbt_why[5]++;
		return;
	}


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


/* Preserve the last belief and choose the no-target combat posture. */
static void Cbt_Idle(edict_t *self, sg_combat_state_t *st, usercmd_t *cmd,
                     vec3_t eye)
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
					    &sg_caco_enemies[SG_TeamIdx(team)][s];

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
			st->lost_ctfid = st->enemy_ctfid;
			st->lost_seed = seed;
			st->lost_time = level.time;
			st->lost_until = level.time + SG_LOST_HOLD;
			st->lost_next = 0.0f;
			st->lost_have = false;
		}
	}

	st->enemy = 0;
	st->enemy_ctfid = 0;
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
		Combat_Arbitrate(self, st, st->post_defender
		                 ? SG_CombatBestPostWeapon(self, st->post_sight)
		                 : Combat_PostWeapon(self, st->post_sight, false));
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


/* Update target continuity and the stability state used by aiming. */
static void Cbt_Track(edict_t *self, sg_combat_state_t *st,
                      edict_t *enemy, float skill, vec3_t eye,
                      vec3_t forward, vec3_t mid, vec3_t aim,
                      float *dist_out, qboolean *vel_stable_out,
                      qboolean *out_engaged)
{
	float dist;
	qboolean vel_stable;

	Combat_Center(enemy, mid);
	VectorSubtract(mid, eye, aim);
	dist = VectorLength(aim);

	/* target continuity: a new target restarts the settle clock, draws a
	 * fresh tremor and snaps the range band. Holding the same one lets the
	 * error decay and the band hysteresis do its work. */
	if (st->enemy != (int)(enemy - g_edicts) ||
	    st->enemy_ctfid != enemy->client->ctf.ctfid)
	{
		st->enemy = (int)(enemy - g_edicts);
		st->enemy_ctfid = enemy->client->ctf.ctfid;
		st->acquired_at = level.time;
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
	st->enemy_last_ctfid = st->enemy_ctfid;
	VectorCopy(mid, st->enemy_org);
	st->enemy_time = level.time;
	st->enemy_weapon = Combat_Held(enemy);
	st->enemy_want_range = (st->enemy_weapon >= 0)
	    ? Combat_WantRange(enemy, st->enemy_weapon) : 0.0f;

	/* re-sight clears the corner hold outright: there is nothing left to
	 * predict about a target that is standing in front of you */
	st->lost_until = 0.0f;
	st->lost_have = false;

	/* rule F1's stability clock runs every frame a target is held, not only on
	 * the frames a projectile is in hand -- a clock that only ticks while it
	 * is being read never reaches its threshold */
	vel_stable = Combat_VelStable(st, enemy);

	*dist_out = dist;
	*vel_stable_out = vel_stable;
}


/* Select and request a weapon for the current range and doctrine. */
static void Cbt_ChooseWeapon(edict_t *self, sg_combat_state_t *st,
                             edict_t *enemy, float dist,
                             qboolean *carrier_out, int *band_out)
{
	qboolean carrier;
	int band, want;

	/* ------------------------------------------------------ the weapon */

	carrier = Combat_IsEnemyCarrier(self, enemy);
	band = Combat_Band(st, dist);
	want = Combat_Choose(self, band, dist, carrier);

	/*
	 * WETWORK (sg_wetwork). The physics check that
	 * killed the wet route cuts the other way here: a swimmer moves at
	 * HALF wishspeed (pmove.c) and fire_rail's mask has no
	 * CONTENTS_WATER -- rails reach into water undegraded. A swimming
	 * target is the easiest rail shot in the game and the band ladders
	 * don't know it. Hold the rail on swimmers if it's in the pack.
	 */
	if (enemy->waterlevel > 1 &&
	    sg_cv.wetwork->value)
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

	*carrier_out = carrier;
	*band_out = band;
}

void SG_CombatFrame(edict_t *self, usercmd_t *cmd, qboolean *out_engaged)
{
	sg_combat_state_t	*st;
	edict_t				*enemy;
	edict_t				*incumbent;
	vec3_t				eye, forward, mid, lead, aim, endp, impact;
	vec3_t				muzzle, shotdir;
	vec3_t				threat;
	float				dist, held, frac, mag, len, trace_len;
	float				source_pad;
	float				yaw, pitch, skill, settle;
	float				residual, span, shape;
	trace_t				tr;
	int					band, inhand, incumbent_index;
	qboolean			clear_shot, carrier, ballistic, vel_stable, ray_hits;
	combat_ray_result_t	ray_result;
	combat_solve_t		solve;
	qboolean			rattled, textured;

	if (out_engaged)
		*out_engaged = false;

	if (!self || !self->inuse || !self->client || !cmd)
		return;
	if (self->deadflag == DEAD_DEAD || self->health <= 0)
		return;
	if (self->movetype == MOVETYPE_NOCLIP)
		return;

	st = Combat_ClientState(self);
	if (!st)
		return;
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

	/* A target slot only earns hysteresis and a lost-corner continuation while
	 * the same opposing client life still occupies it.  Death, respawn, team
	 * change and slot reuse terminate the retained fight before the scan; a
	 * new occupant must win selection on its own evidence. */
	incumbent = SG_CombatLiveEnemy(self);
	if (!incumbent && st->enemy > 0)
	{
		st->enemy = 0;
		st->enemy_ctfid = 0;
	}
	incumbent_index = incumbent ? (int)(incumbent - g_edicts) : -1;

	sg_cbt_why[7]++;                        /* frames that got this far */
	enemy = Combat_Scan(self, eye, forward, rattled ? threat : NULL,
	    incumbent_index, true);
	cmd->buttons = SG_CombatTargetClaimTrigger(cmd->buttons, enemy != NULL);
	if (enemy)
		sg_cbt_why[8]++;                    /* frames with a target */
	if (!enemy)
	{
		Cbt_Idle(self, st, cmd, eye);
		return;
	}

	Cbt_Track(self, st, enemy, skill, eye, forward, mid, aim,
	          &dist, &vel_stable, out_engaged);

	Cbt_ChooseWeapon(self, st, enemy, dist, &carrier, &band);

	inhand = Combat_Held(self);
	if (inhand < 0)
		inhand = SG_W_BLASTER;
	ballistic = false;

	solve = Combat_Solve(enemy, inhand, eye, lead);

	VectorSubtract(lead, eye, aim);
	len = VectorLength(aim);
	if (len < 1.0f)
		return;
	VectorScale(aim, 1.0f / len, aim);

	if (sg_weapons[inhand].speed > 0.0f &&
	    inhand != SG_W_GRENADELAUNCHER && skill < SG_SKILL_MAX)
	{
		float jit = (float)tan(SG_LEAD_JITTER_S0
		                       * (SG_SKILL_MAX - skill) / SG_SKILL_MAX
		                       * M_PI / 180.0);
		vec3_t perp;

		Combat_SamplePerp(st, aim, perp);
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
	 * solved on the clean aim.  Unlike direct fire, no synthetic error is
	 * applied after that solve: the checked arc and the visible weapon aim must
	 * remain the same trajectory for splash safety to mean anything.
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


	if (!ballistic)
	{
		textured = Combat_TexOn();

		if (textured)
			Combat_TexWander(st, aim);
		else if (level.time >= st->err_next)
		{
			Combat_SampleError(st, aim);
			st->err_next = level.time + 0.25f +
			    0.25f * Combat_RandomUnit(st);
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

		held = level.time - st->acquired_at;
		frac = (held >= settle) ? 0.0f : (1.0f - held / settle);
		mag = (float)tan((residual + span * frac) * M_PI / 180.0);

		VectorMA(aim, mag, st->err, aim);
		if (shape != 0.0f)
			VectorMA(aim, (float)tan((double)(st->tex_over * shape)
			                         * M_PI / 180.0), st->tex_dir, aim);
		VectorNormalize(aim);
	}

	ray_hits = false;
	source_pad = 0.0f;
	if (inhand < 0 || ballistic)
	{
		yaw = (float)(atan2(aim[1], aim[0]) * 180.0 / M_PI);
		pitch = (float)(-asin(aim[2]) * 180.0 / M_PI);
		cmd->angles[YAW] = (short)(ANGLE2SHORT(yaw)
		                         - self->client->ps.pmove.delta_angles[YAW]);
		cmd->angles[PITCH] = (short)(ANGLE2SHORT(pitch)
		                           - self->client->ps.pmove.delta_angles[PITCH]);
		if (inhand < 0)
			return;				/* aim is written; there is no shot to take */
	}
	else
	{
		if (solve.landing_splash)
		{
			if (!Combat_FinalizePointRay(self, inhand, lead, aim, cmd, muzzle,
			                            shotdir, &source_pad) ||
			    !SG_CombatLandingSplashClear(self, enemy, muzzle, shotdir,
			                                solve.landing,
			                                &solve.projectile_time, impact))
				return;
			ray_result = COMBAT_RAY_MISS;
		}
		else
			ray_result = Combat_FinalizeMuzzleAim(self, enemy, inhand, lead, st,
			                                     aim, cmd, muzzle, shotdir,
			                                     &source_pad);
		/* A constrained physical miss still supplies the real muzzle and ray
		 * for the wall/teammate veto below.  An unsupported or otherwise
		 * invalid weapon supplies neither; never trace or trigger from
		 * uninitialised physical state. */
		if (ray_result == COMBAT_RAY_INVALID)
			return;
		ray_hits = (ray_result == COMBAT_RAY_HIT);
	}

	/*
	 * The ballistic case never runs the straight-line wall check: the shot
	 * does not travel in a straight line, and Combat_GrenadeImpact already
	 * walked the real flight to its first contact. What is left to ask is
	 * rule F5's question -- is that contact close enough to the target to be
	 * worth a grenade? damage_radius is damage + 40 = 160 (p_weapon.c:791),
	 * and T_RadiusDamage pays 120 - 0.5*d from the impact point out to it
	 * (g_combat.c:742).
	 */
	if (solve.landing_splash)
	{
		float drift = solve.projectile_time * VectorLength(enemy->velocity);

		if (!Combat_SplashSafe(self, inhand, impact))
			return;
		if (drift >= SG_LEAD_TOLERANCE && !vel_stable)
		{
			sg_cbt_why[4]++;
			return;
		}
	}
	else if (ballistic)
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
		 * This starts at the exact final muzzle, not the eye.  Hitscan needs a
		 * real target hit.  A direct projectile may either hit the current enemy
		 * or have an unobstructed line through its predicted lead point; a wall
		 * or teammate is never promoted to a clear shot by endpoint proximity.
		 */
		VectorSubtract(lead, muzzle, endp);
		trace_len = VectorLength(endp) + 64.0f;
		VectorMA(muzzle, trace_len, shotdir, endp);
		tr = sg_host.trace(muzzle, NULL, NULL, endp, self, MASK_SHOT);
		if (tr.ent == enemy)
			VectorCopy(tr.endpos, impact);
		else
			VectorCopy(lead, impact);

		clear_shot = ray_hits && Combat_TraceClears(inhand,
		                                             tr.ent == enemy,
		                                             tr.fraction >= 1.0f,
		                                             tr.ent && tr.ent->client &&
		                                             tr.ent != enemy &&
		                                             OnSameTeam(self, tr.ent));

		/* Chaingun_Fire randomises its source by four units in both lateral
		 * axes.  Keep the nominal ray for its target test, then sweep that whole
		 * source box as a guard: any possible muzzle that would meet a wall or a
		 * teammate vetoes the trigger. */
		if (clear_shot && source_pad > 0.0f)
		{
			vec3_t padmins, padmaxs;
			trace_t guard;

			VectorSet(padmins, -source_pad, -source_pad, -source_pad);
			VectorSet(padmaxs, source_pad, source_pad, source_pad);
			guard = sg_host.trace(muzzle, padmins, padmaxs, endp, self,
			                      MASK_SHOT);
			if (guard.fraction < 1.0f && guard.ent != enemy)
				clear_shot = false;
			if (guard.ent && guard.ent->client && guard.ent != enemy &&
			    OnSameTeam(self, guard.ent))
				clear_shot = false;
		}

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
			fl = sg_host.trace(lead, NULL, NULL, down, enemy, MASK_SHOT);
			if (fl.fraction < 1.0f)
			{
				trace_t path;

				VectorSubtract(fl.endpos, muzzle, aim);
				if (VectorNormalize(aim) >= 1.0f &&
				    Combat_FinalizePointRay(self, inhand, fl.endpos, aim, cmd,
				                            muzzle, shotdir, &source_pad))
				{
					path = sg_host.trace(muzzle, NULL, NULL, fl.endpos, self,
					                     MASK_SHOT);
					VectorSubtract(path.endpos, fl.endpos, v);
					if (VectorLength(v) <= 1.0f &&
					    !(path.ent && path.ent->client &&
					      path.ent != enemy && OnSameTeam(self, path.ent)))
					{
						VectorCopy(fl.endpos, impact);
						clear_shot = true;
					}
				}
			}
		}

		if (!clear_shot)
		{
			sg_cbt_why[1]++;
			return;				/* aim is written; the trigger stays off */
		}

		/* ----------------------------------------------------- the vetoes */

		/* The physical bullet/pellet rays fan out after the nominal trace.
		 * Refuse a spread hitscan shot if any live teammate occupies that
		 * envelope before the target range. */
		if (sg_weapons[inhand].speed <= 0.0f)
		{
			qboolean water_path;
			trace_t water_trace;

			water_path = (sg_host.pointcontents(muzzle) & MASK_WATER) != 0;
			water_trace = sg_host.trace(muzzle, NULL, NULL, endp, self,
			                            MASK_WATER);
			if (water_trace.fraction < 1.0f)
				water_path = true;
			if (!Combat_TeamHitscanSafe(self, inhand, muzzle, shotdir,
			                            trace_len, source_pad, water_path))
			{
				sg_cbt_why[1]++;
				return;
			}
		}

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
			float drift = solve.projectile_time * VectorLength(enemy->velocity);

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

	Cbt_Trigger(self, cmd, st, skill, inhand);
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
	if (!sg_cv.debug->value || level.time < sg_cbt_why_next)
		return;
	sg_cbt_why_next = level.time + 5.0f;
	sg_host.dprint("CBTWHY frames=%d seen=%d fire=%d noclear=%d splash=%d cap=%d lead=%d win=%d held=%d react=%d\n",
	           sg_cbt_why[7], sg_cbt_why[8],
	           sg_cbt_why[0], sg_cbt_why[1], sg_cbt_why[2], sg_cbt_why[3],
	           sg_cbt_why[4], sg_cbt_why[5], sg_cbt_why[6], sg_cbt_why[9]);
	sg_host.dprint("CBTSCAN unteamed=%d same=%d far=%d fov=%d blocked=%d acquired=%d threat=%d\n",
	           sg_cbt_scan[0], sg_cbt_scan[1], sg_cbt_scan[2],
	           sg_cbt_scan[3], sg_cbt_scan[4], sg_cbt_scan[5],
	           sg_cbt_scan[6]);
	{
		int w;

		for (w = 0; w < SG_NUM_WEAPONS; w++)
			if (sg_cbt_fire[w])
				sg_host.dprint("ACC w%d fires=%d hits=%d\n",
				           w, sg_cbt_fire[w], sg_cbt_hit[w]);
	}
}

#ifdef SG_COMBAT_AIM_TEST
int SG_CombatAimTestFinalize(int weapon, int hand, int machinegun_shots,
                             int gunframe, float viewheight,
                             const vec3_t origin,
                             const vec3_t lead, const vec3_t mins,
                             const vec3_t maxs, const vec3_t requested,
                             float elapsed, vec3_t muzzle_out,
                             vec3_t dir_out, float *source_pad_out)
{
	edict_t		self, enemy;
	gclient_t	client;
	sg_combat_state_t st;
	usercmd_t	cmd;
	vec3_t		aim, lead_copy;
	float		saved_time = level.time;
	combat_ray_result_t result;

	memset(&self, 0, sizeof(self));
	memset(&enemy, 0, sizeof(enemy));
	memset(&client, 0, sizeof(client));
	memset(&st, 0, sizeof(st));
	memset(&cmd, 0, sizeof(cmd));
	self.client = &client;
	self.viewheight = viewheight;
	VectorCopy(origin, self.s.origin);
	client.pers.hand = hand;
	client.machinegun_shots = machinegun_shots;
	client.ps.gunframe = gunframe;
	VectorCopy(mins, enemy.mins);
	VectorCopy(maxs, enemy.maxs);
	VectorCopy(requested, aim);
	if (VectorNormalize(aim) <= 0.0001f)
		return 0;
	VectorCopy(lead, lead_copy);
	st.acquired_at = 0.0f;
	level.time = elapsed;
	result = Combat_FinalizeMuzzleAim(&self, &enemy, weapon, lead_copy, &st,
	                                 aim, &cmd, muzzle_out, dir_out,
	                                 source_pad_out);
	level.time = saved_time;
	return (int)result;
}

int SG_CombatAimTestWeaponRay(int weapon, int hand, int machinegun_shots,
                              int gunframe, float viewheight,
                              const vec3_t origin,
                              const vec3_t shot, vec3_t muzzle_out,
                              vec3_t dir_out, float *source_pad_out)
{
	edict_t		self;
	gclient_t	client;
	usercmd_t	cmd;
	vec3_t		shot_copy;

	memset(&self, 0, sizeof(self));
	memset(&client, 0, sizeof(client));
	memset(&cmd, 0, sizeof(cmd));
	self.client = &client;
	self.viewheight = viewheight;
	VectorCopy(origin, self.s.origin);
	client.pers.hand = hand;
	client.machinegun_shots = machinegun_shots;
	client.ps.gunframe = gunframe;
	VectorCopy(shot, shot_copy);
	if (VectorNormalize(shot_copy) <= 0.0001f)
		return 0;
	Combat_WriteShotCmd(&self, weapon, shot_copy, &cmd);
	return Combat_WeaponRay(&self, weapon, &cmd, muzzle_out, dir_out,
	                        source_pad_out) ? 1 : 0;
}

int SG_CombatAimTestTraceClear(int weapon, int enemy_hit, int unobstructed,
                               int teammate_hit)
{
	return Combat_TraceClears(weapon, enemy_hit != 0, unobstructed != 0,
	                          teammate_hit != 0) ? 1 : 0;
}

int SG_CombatAimTestTeamSplashSafe(edict_t *self, float safe_radius,
                                   const vec3_t impact)
{
	return Combat_TeamSplashSafe(self, safe_radius, impact) ? 1 : 0;
}

int SG_CombatAimTestTeamHitscanSafe(edict_t *self, int weapon,
                                    const vec3_t muzzle,
                                    const vec3_t shotdir, float max_forward,
                                    float source_pad, int water_path)
{
	return Combat_TeamHitscanSafe(self, weapon, muzzle, shotdir, max_forward,
	                              source_pad, water_path != 0) ? 1 : 0;
}

uint32_t SG_CombatAimTestRandom(unsigned identity, unsigned steps)
{
	sg_combat_state_t st;
	uint32_t value = 0;

	Combat_ResetState(&st, identity);
	while (steps-- > 0)
		value = Combat_RandomNext(&st);
	return value ? value : st.random_state;
}

uint32_t SG_CombatAimTestClientRandom(int client_index,
                                      uint64_t client_life,
                                      unsigned steps)
{
	return SG_CombatAimTestRandom(
	    Combat_RandomIdentity(client_index, client_life), steps);
}
#endif
