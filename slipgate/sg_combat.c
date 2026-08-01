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

#define SG_WEIGHT_TICK		1.0f	/* item worths, same cadence as Fields_Refresh */

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

	vec3_t		err;			/* aim error direction, unit length */
	float		err_next;		/* when to resample the tremor */

	float		win_end;		/* when the current trigger window expires */
	qboolean	win_fire;		/* is the current window a firing window */

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
} sg_combat_state_t;

static sg_combat_state_t sg_combat[SG_COMBAT_MAXCLIENTS];

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
 */
static edict_t *Combat_Scan(edict_t *self, vec3_t eye, vec3_t forward)
{
	edict_t	*best = NULL;
	float	bestdist = SG_ENGAGE_RANGE;
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
			continue;
		if (theirteam == myteam)
			continue;

		Combat_Center(p, mid);
		VectorSubtract(mid, eye, delta);
		dist = VectorLength(delta);
		if (dist < 1.0f || dist >= bestdist)
			continue;

		/* forward cone: do not shoot backwards. The basis is the CURRENT
		 * v_angle, which is what the previous frame's cmd angles produced. */
		VectorScale(delta, 1.0f / dist, delta);
		dot = DotProduct(delta, forward);
		if (dot < SG_FOV_COS)
			continue;

		if (!Combat_Visible(self, p))
			continue;

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

static const int sg_ladder_mid[] = {
	SG_W_RAILGUN, SG_W_ROCKETLAUNCHER, SG_W_HYPERBLASTER, SG_W_CHAINGUN,
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
	gitem_t *it;

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

	st->switch_next = level.time + SG_SWITCH_RATE;
	it->use(self, it);			/* Use_Weapon -> client->newweapon = it */
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
static void Combat_SampleError(sg_combat_state_t *st, vec3_t dir)
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
			VectorScale(v, 1.0f / len, st->err);
			return;
		}
	}

	/* degenerate sample: no error this time rather than a fake one */
	VectorClear(st->err);
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
	 */
	{
		int i;

		for (i = 0; i < globals.num_edicts; i++)
		{
			edict_t *it = &g_edicts[i];

			if (!it->inuse || !it->classname)
				continue;
			if (strcmp(it->classname, "item_health_mega") != 0)
				continue;
			if (!Caco_ItemBelievedUp(it))
				continue;
			worth *= 2.5f;
			break;
		}
	}

	return Combat_Clamp(worth);
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
	int i;

	if (Combat_IsQuad(e))
		return 0.0f;			/* already carrying it */

	for (i = 0; i < sg_caco_num_items; i++)
	{
		sg_belief_item_t	*b = &sg_caco_items[i];
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
	int		i;

	if (e->client->rune)
		return 0.0f;

	tier = Weapon_Tier(e);

	for (i = 0; i < sg_caco_num_items; i++)
	{
		sg_belief_item_t	*b = &sg_caco_items[i];
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

/* ------------------------------------------------------------------ frame */

void SG_CombatFrame(edict_t *self, usercmd_t *cmd, qboolean *out_engaged)
{
	sg_combat_state_t	*st;
	edict_t				*enemy;
	vec3_t				eye, forward, mid, lead, aim, endp, impact;
	float				dist, held, frac, mag, len, flight;
	float				yaw, pitch;
	trace_t				tr;
	int					ci, band, want, inhand;
	qboolean			clear_shot, carrier, ballistic, vel_stable;

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

	/* eyes and current facing. v_angle is the view the last cmd produced. */
	VectorCopy(self->s.origin, eye);
	eye[2] += self->viewheight;
	AngleVectors(self->client->v_angle, forward, NULL, NULL);

	enemy = Combat_Scan(self, eye, forward);
	if (!enemy)
	{
		st->enemy = 0;

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
		}
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
		st->err_next = 0.0f;
		st->win_end = 0.0f;
		st->win_fire = false;
		st->band = -1;
		VectorClear(st->vel_dir);
		st->vel_stable = level.time;
	}

	if (out_engaged)
		*out_engaged = true;

	/* rule F1's stability clock runs every frame a target is held, not only on
	 * the frames a projectile is in hand -- a clock that only ticks while it
	 * is being read never reaches its threshold */
	vel_stable = Combat_VelStable(st, enemy);

	/* ------------------------------------------------------ the weapon */

	carrier = Combat_IsEnemyCarrier(self, enemy);
	band = Combat_Band(st, dist);
	want = Combat_Choose(self, band, dist, carrier);
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
	 */
	if (level.time >= st->err_next)
	{
		Combat_SampleError(st, aim);
		st->err_next = level.time + 0.25f + 0.25f * random();
	}

	held = level.time - st->since;
	frac = (held >= SG_SETTLE) ? 0.0f : (1.0f - held / SG_SETTLE);
	mag = (float)tan((SG_AIM_RESIDUAL_DEG
	                  + (SG_AIM_ERROR_DEG - SG_AIM_RESIDUAL_DEG) * frac)
	                 * M_PI / 180.0);

	VectorMA(aim, mag, st->err, aim);
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
			return;				/* aim is written; the trigger stays off */

		/* ----------------------------------------------------- the vetoes */

		/* rule R1: never fire a splash weapon whose impact point is inside its
		 * own d_safe of the bot's bbox centre. The cliff is hard, not a taper. */
		if (!Combat_SplashSafe(self, inhand, impact))
			return;

		/* rule F6: a hitscan weapon past its cap is worse than the blaster */
		if (sg_weapons[inhand].range_cap > 0.0f &&
		    dist > sg_weapons[inhand].range_cap)
			return;

		/* rule F1: a projectile is only fired when the lead is a prediction.
		 * The tolerance is three quarters of a 32-unit bbox
		 * (p_client.c:1833-1834). */
		if (sg_weapons[inhand].speed > 0.0f)
		{
			float drift = flight * VectorLength(enemy->velocity);

			if (drift >= SG_LEAD_TOLERANCE && !vel_stable)
				return;
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
	if (level.time >= st->win_end)
	{
		float base;

		st->win_fire = !st->win_fire;
		base = st->win_fire ? SG_FIRE_ON : SG_FIRE_OFF;
		st->win_end = level.time + base * (1.0f + SG_FIRE_JITTER * crandom());
	}

	if (!st->win_fire)
		return;

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
		return;

	cmd->buttons |= BUTTON_ATTACK;
}
