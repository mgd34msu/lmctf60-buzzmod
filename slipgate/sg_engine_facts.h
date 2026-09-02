/* Era-4 engine facts: the numbers the engine and the game run on, read
 * from their sources, in one place.
 *
 * Nothing in the RUNE or the bot carries a number of its own.  Every
 * value here names where it comes from; when the engine or the game
 * changes, this is the file that changes.  The law (sg_rune_law) is built
 * from these and the live gravity and travels with the artifact. */
#ifndef SG_ENGINE_FACTS_H
#define SG_ENGINE_FACTS_H

/* ---- the body (engine pmove.c, game p_client.c) --------------------------- */
#define SG_FACT_BODY_HALF_WIDTH 16.0f      /* pmove: mins/maxs x,y */
#define SG_FACT_BODY_MINS_Z (-24.0f)       /* pmove: mins z, standing and ducked */
#define SG_FACT_BODY_MAXS_Z 32.0f          /* pmove: maxs z standing */
#define SG_FACT_BODY_DUCK_MAXS_Z 4.0f      /* pmove PM_CheckDuck: maxs z ducked */
#define SG_FACT_BODY_VIEW 22.0f            /* pmove: viewheight standing */
#define SG_FACT_BODY_DUCK_VIEW (-2.0f)     /* pmove PM_CheckDuck: viewheight ducked */
#define SG_FACT_BODY_MASS 200.0f           /* p_client.c PutClientInServer: ent->mass */

/* ---- movement (engine pmove.c) --------------------------------------------- */
#define SG_FACT_MAX_SPEED 300.0f           /* pm_maxspeed */
#define SG_FACT_DUCK_SPEED 100.0f          /* pm_duckspeed */
#define SG_FACT_STOP_SPEED 100.0f          /* pm_stopspeed */
#define SG_FACT_ACCELERATE 10.0f           /* pm_accelerate */
#define SG_FACT_AIR_ACCELERATE 0.0f        /* pm_airaccelerate (sv_airaccelerate 0) */
#define SG_FACT_WATER_ACCELERATE 10.0f     /* pm_wateraccelerate */
#define SG_FACT_FRICTION 6.0f              /* pm_friction */
#define SG_FACT_WATER_FRICTION 1.0f        /* pm_waterfriction */
#define SG_FACT_WATER_SPEED 400.0f         /* pm_waterspeed */
#define SG_FACT_STEP_SIZE 18.0f            /* STEPSIZE */
#define SG_FACT_FLOOR_NORMAL_Z 0.7f        /* MIN_STEP_NORMAL: a floor's normal z */
#define SG_FACT_JUMP_VELOCITY 270.0f       /* PM_CheckJump: velocity[2] += 270 */
#define SG_FACT_FRAME_MS 100U              /* the server frame */
#define SG_FACT_PMOVE_SUBSTEP_MS 25U       /* pmove integrates in 25 ms pieces of a frame */

/* ---- damage and knockback (game g_combat.c) ------------------------------- */
#define SG_FACT_KNOCKBACK_MIN_MASS 50.0f   /* T_Damage: mass floor */
#define SG_FACT_KNOCKBACK_SCALE 500.0f     /* T_Damage: others' hits */
#define SG_FACT_SELF_KNOCKBACK_SCALE 1600.0f /* T_Damage: one's own blast */
#define SG_FACT_RADIUS_FALLOFF 0.5f        /* T_RadiusDamage: points = damage - 0.5 * distance */
#define SG_FACT_RADIUS_SELF_SCALE 0.5f     /* T_RadiusDamage: one's own blast, half */

/* ---- weapons (game p_weapon.c, g_weapon.c) -------------------------------- */
#define SG_FACT_MUZZLE_FORWARD 8.0f        /* launchers: offset {8, 8, view - 8} */
#define SG_FACT_MUZZLE_RIGHT 8.0f
#define SG_FACT_MUZZLE_BELOW_VIEW 8.0f
#define SG_FACT_BLASTER_SPEED 1000.0f
#define SG_FACT_BLASTER_DAMAGE 15.0f       /* deathmatch */
#define SG_FACT_BLASTER_LIFE 2.0f          /* g_weapon.c fire_blaster: nextthink + 2 */
#define SG_FACT_HYPERBLASTER_DAMAGE 15.0f  /* deathmatch */
#define SG_FACT_SHOTGUN_DAMAGE 4.0f
#define SG_FACT_SHOTGUN_PELLETS 12
#define SG_FACT_SHOTGUN_HSPREAD 1000.0f    /* random offset at 8192 units */
#define SG_FACT_SHOTGUN_VSPREAD 500.0f
#define SG_FACT_SUPER_SHOTGUN_DAMAGE 6.0f
#define SG_FACT_SUPER_SHOTGUN_PELLETS 20
#define SG_FACT_SUPER_SHOTGUN_YAW 5.0f     /* the two half-loads, degrees each way */
#define SG_FACT_BULLET_HSPREAD 300.0f
#define SG_FACT_BULLET_VSPREAD 500.0f
#define SG_FACT_MACHINEGUN_DAMAGE 8.0f
#define SG_FACT_CHAINGUN_DAMAGE 6.0f       /* deathmatch */
#define SG_FACT_CHAINGUN_MAX_SHOTS 3       /* spun up, per frame */
#define SG_FACT_GRENADE_SPEED 600.0f
#define SG_FACT_GRENADE_RISE 200.0f        /* fire_grenade: + up * 200 */
#define SG_FACT_GRENADE_DAMAGE 120.0f
#define SG_FACT_GRENADE_RADIUS 160.0f      /* damage + 40 */
#define SG_FACT_GRENADE_FUSE 2.5f
#define SG_FACT_ROCKET_SPEED 650.0f
#define SG_FACT_ROCKET_DAMAGE 110.0f       /* 100 + random 20: the mean */
#define SG_FACT_ROCKET_SPLASH_DAMAGE 120.0f
#define SG_FACT_ROCKET_SPLASH_RADIUS 120.0f
#define SG_FACT_RAILGUN_DAMAGE 100.0f      /* deathmatch */
#define SG_FACT_BFG_SPEED 400.0f
#define SG_FACT_BFG_DAMAGE 200.0f
#define SG_FACT_BFG_CORE_RADIUS 100.0f
#define SG_FACT_RAY_REACH 8192.0f          /* fire_lead, fire_rail: the trace length */

/* Trigger cadences: the weapon animations at ten frames a second. */
#define SG_FACT_BLASTER_SECONDS 0.5f
#define SG_FACT_SHOTGUN_SECONDS 1.0f
#define SG_FACT_SUPER_SHOTGUN_SECONDS 1.0f
#define SG_FACT_MACHINEGUN_SECONDS 0.1f
#define SG_FACT_CHAINGUN_SECONDS 0.1f
#define SG_FACT_GRENADE_SECONDS 1.0f
#define SG_FACT_ROCKET_SECONDS 0.8f
#define SG_FACT_HYPERBLASTER_SECONDS 0.1f
#define SG_FACT_RAILGUN_SECONDS 1.5f
#define SG_FACT_BFG_SECONDS 2.0f

/* ---- the grapple (game p_weapon.c, p_client.c) ---------------------------- */
#define SG_FACT_HOOK_FIRE_SPEED 800.0f     /* GRAPPLE_FIRE_HOOK_SPEED */
#define SG_FACT_HOOK_PULL_SPEED 800.0f     /* GRAPPLE_PULL_SPEED */
#define SG_FACT_HOOK_NEAR_BITE 120.0f      /* CTF_HookPullVelocity: full speed beyond */
#define SG_FACT_HOOK_HOLD 50.0f            /* ClientThink: gravity off within */
/* CTF_HookPullVelocity: within 120 the pull is the distance times a factor
 * by band: over 100 x5, over 80 x4, over 40 x3, over 20 x2, over 10 x1,
 * then nothing. */
#define SG_FACT_HOOK_BAND_5 100.0f
#define SG_FACT_HOOK_BAND_4 80.0f
#define SG_FACT_HOOK_BAND_3 40.0f
#define SG_FACT_HOOK_BAND_2 20.0f
#define SG_FACT_HOOK_BAND_1 10.0f
#define SG_FACT_HOOK_MUZZLE_FORWARD 8.0f   /* CTF_HookMuzzle */
#define SG_FACT_HOOK_MUZZLE_RIGHT 8.0f
#define SG_FACT_HOOK_MUZZLE_BELOW_VIEW 8.0f

#endif /* SG_ENGINE_FACTS_H */
