//===========================================================================
//
// Name:				bl_main.c
// Function:		bot setup
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	1999-02-10
// Tab Size:		3
//===========================================================================

#include "g_local.h"

#ifdef BOT

#include "bl_main.h"
#include "bl_ctf.h"
#include "bl_spawn.h"
#include "bl_redirgi.h"
#include "bl_botcfg.h"
#include "bl_chat.h"

//#define TOURNEY

#if defined(WIN32) || defined(_WIN32)
#include <io.h>
#define PATHSEPERATOR_CHAR			'\\'
#define PATHSEPERATOR_STR			"\\"
#else
#include <dlfcn.h>
#define PATHSEPERATOR_CHAR			'/'
#define PATHSEPERATOR_STR			"/"
#endif

// OSP Tourney DM -- Start
#ifdef TOURNEY
#define MODE_TEAM	0x02
extern int m_mode;			// OSP Tourney match mode param
extern cvar_t *hook_enable;	// OSP Tourney DM hook status --JKK
extern void OSP_serverbotsRemove(void);
#endif
// OSP Tourney DM -- End


bot_globals_t botglobals;

void ClientThink (edict_t *ent, usercmd_t *ucmd);

//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void StringMakeGreen(char *str)
{
	cvar_t *v;
	//never make green on a dedicated server
	v = gi.cvar("dedicated", "0", 0);
	if (v->value) return;
	//set the last bit
	while(*str)
	{
		if (*str > ' ') *str |= 128;
		str++;
	} //end while
} //end of the function StringMakeGreen
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
int BotSwimming(vec3_t origin)
{
	vec3_t testorg;

	VectorCopy(origin, testorg);
	testorg[2] += 3;
	if (gi.pointcontents(testorg) & MASK_WATER) return true;
	return false;
} //end of the function BotSwimming
//===========================================================================
// hooked in G_RunFrame in g_main.c
// NOTE: don't call between: ClientBeginServerFrame and ClientEndServerFrames
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
cvar_t *bot_bunnyhop;      /* keep hopping to stay off the friction */
cvar_t *bot_strafejump;    /* air-steer for speed; needs sv_airaccelerate */


/*
 * Air strafing, worked out from the engine rather than by feel.
 *
 * PM_AirMove with sv_airaccelerate 0 calls PM_Accelerate(wishdir, wishspeed, 1)
 * -- so there IS air control, at a tenth of the ground rate. PM_Accelerate adds
 *
 *     accelspeed = accel * frametime * wishspeed
 *
 * along wishdir, but only while addspeed = wishspeed - (velocity . wishdir) is
 * still positive. Point the input straight down the velocity at 800 and that is
 * 300 - 800: nothing is added at all. Point it far enough off and the dot term
 * shrinks, the gate opens, and the component of that addition along the current
 * heading is real speed.
 *
 * The gain per frame is accelspeed * cos(theta), and accelspeed saturates at
 * accel * frametime * wishspeed, so the best angle is the smallest one that
 * still leaves addspeed at the cap:
 *
 *     cos(theta) = (wishspeed - accelspeed) / speed
 *
 * At 800 that is about 70 degrees for roughly 10 units a frame; at 400, 47
 * degrees for 20. Aiming for acos(300/speed) instead -- which is where I had it
 * -- lands exactly on addspeed = 0 and gains precisely nothing.
 *
 * Which way to lean is not a coin flip either. Leaning toward the side the
 * route turns accelerates the bot and steers it at the same time, so the
 * velocity is pulled onto the path instead of away from it. That is what makes
 * this safe to do continuously rather than in alternating bursts.
 *
 * The view is not involved. A player turns the mouse because eight key
 * combinations are all they can express a direction with; a bot can name the
 * direction outright and decompose it against whatever view it is already
 * holding, so none of this costs any aim.
 */
static float LibVarGetValue_stub_margin(void)
{
	static cvar_t *m;
	if (!m) m = gi.cvar("bot_strafe_margin", "0", 0);
	return m ? m->value : 0.0f;
}

static void BotAirStrafe(usercmd_t *ucmd, bot_input_t *bi,
                         vec3_t forward, vec3_t right,
                         vec3_t vel, float speed2d, float frametime)
{
	vec3_t vdir, d;
	float  wishspeed = 300.0f;
	float  accelspeed, c, th, sn, cs, cross;

	if (speed2d < 1.0f) return;

	accelspeed = 1.0f * frametime * wishspeed;      /* the air branch, accel 1 */

	c = (wishspeed - accelspeed) / speed2d;
	if (c >  1.0f) c =  1.0f;
	if (c < -1.0f) c = -1.0f;
	th = (float)acos(c);

	/*
	 * The derivation gives the angle at which addspeed is exactly saturated.
	 * Sitting right on it is optimal for one step in isolation, but the
	 * velocity moves underneath the answer between steps, so a little margin
	 * either way may do better in practice. Zero means take the derivation as
	 * it stands.
	 */
	th += (float)(LibVarGetValue_stub_margin() * (M_PI / 180.0));

	vdir[0] = vel[0] / speed2d;
	vdir[1] = vel[1] / speed2d;
	vdir[2] = 0.0f;

	/* lean the way the route turns, so the gain also steers */
	cross = vdir[0] * bi->dir[1] - vdir[1] * bi->dir[0];
	if (cross < 0.0f) th = -th;

	sn = (float)sin(th); cs = (float)cos(th);
	d[0] = vdir[0] * cs - vdir[1] * sn;
	d[1] = vdir[0] * sn + vdir[1] * cs;
	d[2] = 0.0f;

	ucmd->forwardmove = (short)(DotProduct(forward, d) * 400.0f);
	ucmd->sidemove    = (short)(DotProduct(right,   d) * 400.0f);
}

void BotExecuteInput(edict_t *bot)
{
	vec3_t angles, forward, right;
	usercmd_t ucmd;
	bot_input_t *bi;
	int client;

#ifdef BOT_DEBUG
	if (botglobals.nobotinput) return;
#endif //BOT_DEBUG

	if (!bot_bunnyhop)   bot_bunnyhop   = gi.cvar("bot_bunnyhop", "1", 0);
	if (!bot_strafejump) bot_strafejump = gi.cvar("bot_strafejump", "0", 0);

	if (gi.cvar("bot_developer", "0", 0)->value)
	{
		static int n, idle, i_nogoal, i_nocmd, i_stuck, i_air, i_dead;
		static double sum; static float peak, next;
		/*
		 * Bands, and a reason for every frame that is not at running speed.
		 * "Idle" only ever counted the frames at a dead stop, which made the
		 * problem look ten times smaller than it is: a bot creeping along at
		 * 150 is not idle, but it is not playing either. What matters is how
		 * much of the match is spent below the 300 the engine will give for
		 * free, and what is holding it there.
		 */
		static int b_stop, b_crawl, b_slow, b_run, b_fast;
		static int s_dead, s_duck, s_water, s_air, s_land, s_lowcmd, s_ground;
		static double cmdsum; static int cmdn;
		float sp = sqrt(bot->velocity[0]*bot->velocity[0] +
		                bot->velocity[1]*bot->velocity[1]);
		int pmf   = bot->client->ps.pmove.pm_flags;
		int alive = !(bot->health <= 0 ||
		              bot->client->ps.pmove.pm_type == PM_DEAD);
		float want = botglobals.botinputs[DF_ENTCLIENT(bot)].speed;
		n++; sum += sp;
		if (sp > peak) peak = sp;
		if (alive) { cmdsum += want; cmdn++; }

		if (sp <= 60)       b_stop++;
		else if (sp <= 150) b_crawl++;
		else if (sp <= 280) b_slow++;
		else if (sp <= 320) b_run++;
		else                b_fast++;

		/* why is this frame not at running speed? first cause wins */
		if (sp <= 280) {
			if (!alive)                     s_dead++;
			else if (pmf & 1 /*DUCKED*/)    s_duck++;
			else if (bot->waterlevel > 1)   s_water++;
			else if (pmf & 16 /*TIME_LAND*/) s_land++;
			else if (!bot->groundentity)    s_air++;
			else if (want < 300.0f)         s_lowcmd++;
			else                            s_ground++;
		}
		if (sp <= 60) {
			int hasinput = botglobals.botnewinput[DF_ENTCLIENT(bot)];
			idle++;
			if (!alive)                                          i_dead++;
			else if (!hasinput)                                  i_nogoal++;
			else if (!want)                                      i_nocmd++;
			else if (!bot->groundentity)                         i_air++;
			else                                                 i_stuck++;
		}
		if (level.time > next) {
			next = level.time + 25.0f;
			gi.dprintf("botspeed: mean %.0f peak %.0f cmd %.0f | bands stop %.0f crawl %.0f slow %.0f run %.0f fast %.0f\n",
				sum/n, peak, cmdn ? cmdsum/cmdn : 0.0,
				100.0*b_stop/n, 100.0*b_crawl/n, 100.0*b_slow/n,
				100.0*b_run/n, 100.0*b_fast/n);
			gi.dprintf("botslow: dead %.0f duck %.0f water %.0f land %.0f air %.0f lowcmd %.0f ground %.0f\n",
				100.0*s_dead/n, 100.0*s_duck/n, 100.0*s_water/n,
				100.0*s_land/n, 100.0*s_air/n, 100.0*s_lowcmd/n,
				100.0*s_ground/n);
			gi.dprintf("botidle: idle %.0f%% (nogoal %.0f, nocmd %.0f, stuck %.0f, air %.0f, dead %.0f)\n",
				100.0*idle/n, 100.0*i_nogoal/n, 100.0*i_nocmd/n,
				100.0*i_stuck/n, 100.0*i_air/n, 100.0*i_dead/n);
		}
	}

	client = DF_ENTCLIENT(bot);
	//
	if (!bot->client)
	{
		gi.dprintf("client %d without client structure\n", client);
		return;
	} //end if
	//get the input for this client
	if (botglobals.botnewinput[client])
	{
		botglobals.botnewinput[client] = false;
		bi = &botglobals.botinputs[client];
	} //end if
	else
	{
		//there's no new input
		return;
	} //end else
	//
	// usercmd_t
	//
	// byte msec;
	//		number of milliseconds since the last user command update of the client
	//
	// byte buttons;
	//		only BUTTON_ATTACK = 1, BUTTON_USE = 2 and BUTTON_ANY = 128 are
	//		sent from the client to the server. The client command +attack
	//		sets BUTTON_ATTACK and the +use command sets BUTTON_USE
	//		all other commands like +forward, +back, +right etc. set BUTTON_ANY
	//
	// short angles[3];
	//		the current view angles of the client
	//
	// short forwardmove, sidemove, upmove;
	//		forwardmove and sidemove are relative to the given yaw
	//		upmove is not related to the given yaw
	//		for jumping upmove is set to 400
	//		for ducking upmove is set to -400
	//
	//	byte impulse;
	//		will be set by the client command impulse x, where x is a number
	//		in the range [0-255]. This impulse command isn't used anymore.
	//
	//	byte lightlevel;
	//		the current light level of the area the client is currently in
	//
	//clear the whole structure
	memset(&ucmd, 0, sizeof(usercmd_t));
	//the duration for the user command in milli seconds
	ucmd.msec = 1000 * bi->thinktime;
	//
	if (botglobals.nocldouble && (bi->actionflags & ACTION_DELAYEDJUMP))
	{
		bi->actionflags |= ACTION_JUMP;
		bi->actionflags &= ~ACTION_DELAYEDJUMP;
	} //end if
	//set the buttons
	if (bi->actionflags & ACTION_RESPAWN)
	{
		bot->client->latched_buttons |= BUTTON_ATTACK;
	} //end if
	if (bi->actionflags & ACTION_ATTACK)
	{
		ucmd.buttons |= BUTTON_ATTACK;
	} //end if
	if (bi->actionflags & ACTION_USE)
	{
		ucmd.buttons |= BUTTON_USE;
	} //end if
	/*
	 * Set the view angles.
	 *
	 * ClientThink works out the player's real view as
	 *     v_angle = SHORT2ANGLE(ucmd.angles + pmove.delta_angles)
	 * and the engine puts a value in delta_angles whenever it moves a client's
	 * view for it -- at every spawn, and on teleport. Writing the angle we
	 * want straight into ucmd.angles therefore aims the bot at that angle plus
	 * whatever the spawn left behind, so a bot's aim was off by a fixed
	 * rotation for its whole life: measured between 86 and 162 degrees of yaw
	 * away from what the library asked for. Taking the delta back out makes
	 * the bot look where it intends to.
	 */
	ucmd.angles[PITCH] = ANGLE2SHORT(bi->viewangles[PITCH]) - bot->client->ps.pmove.delta_angles[PITCH];
	ucmd.angles[YAW]   = ANGLE2SHORT(bi->viewangles[YAW])   - bot->client->ps.pmove.delta_angles[YAW];
	ucmd.angles[ROLL]  = ANGLE2SHORT(bi->viewangles[ROLL])  - bot->client->ps.pmove.delta_angles[ROLL];
	//get the horizontal forward and right vector
	//if swimming movement is true 3d
   //get the pitch in the range [-180, 180]
	if (BotSwimming(bot->s.origin)) angles[PITCH] = bi->viewangles[PITCH];
	else angles[PITCH] = 0;
	angles[YAW] = bi->viewangles[YAW];
	angles[ROLL] = 0;
	AngleVectors(angles, forward, right, NULL);
	//set the view independent movement
	ucmd.forwardmove = DotProduct(forward, bi->dir) * bi->speed;
	ucmd.sidemove = DotProduct(right, bi->dir) * bi->speed;
	/*
	 * Vertical command, for water only.
	 *
	 * This used to be scaled by abs(forward[2]) -- an integer abs() on a
	 * float, so out of water (where pitch is forced to zero) it was abs(0),
	 * and in water it was abs() of a fraction, which truncates to 0 as well.
	 * The factor was always zero, so the bots never had a vertical command at
	 * all, and climbing out of a pool only ever worked by way of the separate
	 * swim-out handling.
	 *
	 * Kept to the swimming case on purpose: on the ground any upmove above 10
	 * is a jump as far as PM_CheckJump is concerned, so feeding it a vertical
	 * direction on land would make a bot hop every time its goal sat above it.
	 * Jumping on land stays with ACTION_JUMP, which is deliberate about when.
	 */
	if (BotSwimming(bot->s.origin))
		ucmd.upmove = bi->dir[2] * bi->speed;
	else
		ucmd.upmove = 0;
	//normal keyboard movement
	if (bi->actionflags & ACTION_MOVEFORWARD) ucmd.forwardmove += 400;
	if (bi->actionflags & ACTION_MOVEBACK) ucmd.forwardmove -= 400;
	if (bi->actionflags & ACTION_MOVELEFT) ucmd.sidemove -= 400;
	if (bi->actionflags & ACTION_MOVERIGHT) ucmd.sidemove += 400;
	//jump/moveup
	if (bi->actionflags & ACTION_JUMP) ucmd.upmove += 400;

	/*
	 * Movement technique.
	 *
	 * Quake II's is specific and differs from Quake I or Source: forward stays
	 * held the whole time. The sequence is
	 *
	 *   - hold forward and one strafe, and pan the view smoothly the same way
	 *     as the strafe. That is the case the engine's air acceleration adds
	 *     raw speed for rather than clamping to the run limit
	 *   - jump again the instant the bot lands, so ground friction never gets
	 *     a frame to work
	 *   - alternate the strafe side on each jump, so the weave averages out
	 *     and the bot still travels in a straight line
	 *
	 * Two things gate it. The bot must already be near running speed: with no
	 * air control a jump locks in whatever speed it started with, and hopping
	 * at half speed just makes the bot airborne and unsteerable -- measured at
	 * 9% of all frames spent airborne going nowhere before this was gated.
	 * And the acceleration itself only exists when the server sets
	 * sv_airaccelerate above zero, which Quake II does not do by default; the
	 * humans in the recorded matches were above the run cap for 42% of frames,
	 * so the servers they played on had it on.
	 */
	{
		float speed2 = bot->velocity[0] * bot->velocity[0] +
		               bot->velocity[1] * bot->velocity[1];
		/*
		 * Do not leave the ground below running speed.
		 *
		 * Quake II gives no air acceleration on these servers and applies no
		 * air friction either, so horizontal speed is simply frozen for the
		 * whole arc. Hopping at 180 therefore does not preserve momentum, it
		 * commits to 180 until landing -- while staying on the ground would
		 * have wound up to 300 in a fraction of a second. Jumping is worth it
		 * only once there is something worth keeping, which means at or above
		 * the run cap, and the hook is what puts a bot above it.
		 *
		 * This was 180, and it is why airborne-and-slow was the largest single
		 * band in the speed breakdown: the bots were jumping their way out of
		 * the acceleration they had not finished gaining.
		 */
		float bhmin  = gi.cvar("bot_bunnyhop_minspeed", "295", 0)->value;
		int   cl     = DF_ENTCLIENT(bot);
		qboolean grounded = bot->groundentity != NULL;
		/*
		 * No tricks on the final approach.
		 *
		 * bi->precision is set by the adapter once the bot is closing on
		 * something it has to land on rather than pass through. A flag is a
		 * thirty-unit box and the hop chain reaches eight hundred a second;
		 * attackers were arriving with a second of route left and sailing
		 * straight over the top, lap after lap, until they were killed.
		 * Measured: with the tricks off entirely, lmctf01 went from no steals
		 * to four and six in a match. The speed is worth having everywhere
		 * else, so it is given up only for the last few strides -- which is
		 * what a player does to take the flag.
		 */
		qboolean canmove  = bot->waterlevel < 2 && !bi->precision &&
		                    !(bot->client->ps.pmove.pm_flags & PMF_DUCKED) &&
		                    !(bi->actionflags & ACTION_CROUCH) &&
		                    bi->dir[2] > -0.3f && bi->dir[2] < 0.3f;

		/*
		 * Circle jump.
		 *
		 * A hop chain compounds whatever speed it starts with, and a bot that
		 * starts at a walk stays at a walk. The entry is a pivot: hold forward
		 * and a strafe, sweep the view about ninety degrees the same way, and
		 * jump out of the turn -- that leaves the ground already above running
		 * speed, which is what the chain then builds on.
		 *
		 * Only from the ground, only when actually slow, and only when the bot
		 * has somewhere to be.
		 */
		if (bot_bunnyhop && bot_bunnyhop->value && canmove && grounded &&
			bi->speed > 0.0f && !botglobals.cj_phase[cl] &&
			speed2 < 200.0f * 200.0f &&
			!(bot->client->ps.pmove.pm_flags & PMF_TIME_LAND))
		{
			botglobals.cj_phase[cl] = 3;
			botglobals.cj_side[cl]  = (rand() & 1) ? 1 : -1;
		}

		if (botglobals.cj_phase[cl] > 0 && canmove)
		{
			int side = botglobals.cj_side[cl];

			/*
			 * Strafe into the pivot, but do not drag the view round with it.
			 *
			 * Sweeping the view is what makes a circle jump work in games that
			 * accelerate you in the air. These servers run sv_airaccelerate at
			 * zero, so the sweep returns exactly nothing -- while still pointing
			 * the bot's aim thirty degrees away from whatever it was shooting
			 * at, for three frames, every time it starts moving. The weapon is
			 * always available and the hook is offhand; neither should ever be
			 * paying for the legs.
			 */
			/*
			 * Jump, do not veer.
			 *
			 * sidemove is set above from the direction navigation asked for.
			 * Adding four hundred on top of a forward three hundred puts the
			 * resulting wishdir fifty-three degrees off that heading, every
			 * hop, for the whole crossing -- which is why bots were covering
			 * three to nine times the length of their own route. The lateral
			 * shove is there to harvest air acceleration and there is none to
			 * harvest at sv_airaccelerate 0, so it was buying nothing and
			 * steering the bot off its path to buy it.
			 *
			 * What preserves the momentum is leaving the ground, away from
			 * ground friction. That is kept. The heading stays navigation's.
			 */
			ucmd.forwardmove = 300;

			if (--botglobals.cj_phase[cl] == 0 && grounded)
			{
				/*
				 * Only leave the ground if the pivot actually got the bot up to
				 * speed. Jumping out of a turn that has not wound up yet freezes
				 * the half-built speed for the length of the arc, which is the
				 * opposite of what the entry is for. If it did not work, stay
				 * down and let the next one start from a faster bot.
				 */
				if (speed2 > bhmin * bhmin)
				{
					ucmd.upmove += 400;              /* jump out of the turn */
					botglobals.sj_side[cl] = side;
					botglobals.sj_pulse[cl] = 1;
				}
			}
		}
		else if (bot_bunnyhop && bot_bunnyhop->value && canmove &&
			grounded && !(bot->client->ps.pmove.pm_flags & PMF_TIME_LAND) &&
			speed2 > bhmin * bhmin)
		{
			/* Jump the moment we are on the floor again -- this is the
			 * one-frame window that keeps friction off the speed. */
			ucmd.upmove += 400;
			botglobals.sj_side[cl] = -botglobals.sj_side[cl];
			if (!botglobals.sj_side[cl]) botglobals.sj_side[cl] = 1;
			botglobals.sj_pulse[cl] = 1;   /* this ground frame only */
		}

		if (!grounded) botglobals.sj_pulse[cl] = 0;   /* window closes on takeoff */

		/* Forward is held throughout, on the ground and in the air. */
		if (bot_bunnyhop && bot_bunnyhop->value && canmove &&
			bi->speed > 0.0f && speed2 > bhmin * bhmin &&
			ucmd.forwardmove < 300)
			ucmd.forwardmove = 300;

		/*
		 * The strafe goes in on the ground, at the jump, and only there.
		 *
		 * There is no air control on these servers -- sv_airaccelerate is zero
		 * -- so an off-axis input after the bot has left the floor changes
		 * nothing about where it goes or how fast it gets there. It is worth
		 * something only on the frame the bot is still in contact, where it
		 * keeps the engine adding speed instead of leaving friction to it. And
		 * because the bot leaves the ground on that same frame, none of it has
		 * time to become travel across the route: you strafe just before
		 * leaving the ground, so you do not strafe.
		 *
		 * Held through the arc, as this was, it was neither of those things --
		 * no speed, because there is no air control to harvest, and a long
		 * lateral leg on every jump, which is what turned a route into three to
		 * nine times its own length.
		 */
		if (bot_strafejump && bot_strafejump->value && canmove && !grounded &&
			speed2 > bhmin * bhmin)
		{

			/*
			 * Strafe, and leave the view alone. The turn that goes with a
			 * strafe jump is there to harvest air acceleration, and there is
			 * none to harvest at sv_airaccelerate 0 -- what preserves the
			 * momentum is being off the ground, away from ground friction, and
			 * that costs the aim nothing.
			 */
			BotAirStrafe(&ucmd, bi, forward, right, bot->velocity,
			             (float)sqrt(speed2), (float)ucmd.msec / 1000.0f);
		}
	}

	//crouch/movedown
	if (bi->actionflags & ACTION_CROUCH) ucmd.upmove -= 400;
	//impulse always zero
	ucmd.impulse = 0;
	//light level at client location
	ucmd.lightlevel = 64;
	//
	bot->flags |= FL_BOTINPUT;
	//the Pmove function was probably designed to run at a higer frequency
	//than 10 Hz.
	//At this low frequency the movement code performs amazingly bad:
	//- The step checking fails half the time.
	//- Out of water jumping gets pretty difficult.
	//- Gravity seems to fail when walking down a stairs,
	//  which results in hoovering down the stairs...
	//To get a higer frequency of the Pmove calls the calls are doubled
	//here by calling the ClientThink twice. Note that ofcourse the
	//ucmd milli seconds are halved.
	//The AI still runs at 10 Hz (could be changed though).
	if (!botglobals.nocldouble)
	{
		/*
		 * How finely the bot's tenth of a second is simulated.
		 *
		 * A real client sends a usercmd every rendered frame -- a dozen or more
		 * inside one 100ms server frame -- and pmove runs once per command. The
		 * bot glue sent two. That is not merely cosmetic, because two of the
		 * things that decide how fast a bot travels are per-step:
		 *
		 *   friction  drop = speed * 6 * frametime, applied per grounded step,
		 *             so coarse steps overshoot and shed more than they should
		 *   landing   a hop chain wants the jump on the first step after
		 *             touching down, and a bot that only gets two chances per
		 *             tenth of a second spends far longer on the floor than a
		 *             player who gets twelve
		 *
		 * The AI still thinks at 10Hz -- this only subdivides the movement, the
		 * same way the engine does for a human with a high frame rate. It was
		 * written when doing this twice was expensive; it is not any more.
		 */
		int sub   = (int)gi.cvar("bot_subframes", "4", 0)->value;
		int total = ucmd.msec, base, rem, step;

		if (sub < 1) sub = 1;
		if (sub > total) sub = total;   /* a step cannot be shorter than 1ms */

		/*
		 * The steps have to add up to the time that actually passed.
		 *
		 * msec is an integer, so splitting a 100ms frame eight ways and
		 * rounding up gives eight steps of 13ms -- 104ms of simulation for
		 * 100ms of real time, and sixteen ways gives 112. That is not a finer
		 * simulation of the same movement, it is the bot getting more time than
		 * everyone else, and it would have arrived as free speed in the very
		 * measurement this exists to inform. The remainder is spread over the
		 * first few steps instead, so the total is always exact and the only
		 * thing that changes is how finely the tenth of a second is integrated.
		 */
		base = total / sub;
		rem  = total % sub;

		for (step = 0; step < sub; step++)
		{
			ucmd.msec = (byte)(base + (step < rem ? 1 : 0));
			if (!ucmd.msec) continue;

			/*
			 * Decide again, every step, from where the bot actually is.
			 *
			 * A player's head is not inside the engine. They watch the jump
			 * happen and move the mouse the whole way through it, so the
			 * direction they are asking for is never more than a moment old. A
			 * bot decides once per AI frame and then hands the same command to
			 * every step of the tenth of a second that follows.
			 *
			 * That is fatal for this particular technique, because the angle
			 * that keeps PM_Accelerate paying depends on the current velocity,
			 * and the velocity is exactly what the previous step just changed.
			 * A command computed at 300 is the wrong command by the time the
			 * bot is doing 500, so subdividing the frame without re-deciding
			 * just replays a stale input more precisely.
			 *
			 * Recomputing here is not something a human is denied -- it is the
			 * bot catching up to what a human already does continuously.
			 */
			if (bot_strafejump && bot_strafejump->value &&
			    !bot->groundentity && bot->waterlevel < 2 &&
			    !(bi->actionflags & ACTION_CROUCH))
			{
				float sp2 = bot->velocity[0] * bot->velocity[0] +
				            bot->velocity[1] * bot->velocity[1];
				float hmin = gi.cvar("bot_bunnyhop_minspeed", "295", 0)->value;
				if (sp2 > hmin * hmin)
					BotAirStrafe(&ucmd, bi, forward, right, bot->velocity,
					             (float)sqrt(sp2), (float)ucmd.msec / 1000.0f);
			}

			ClientThink(bot, &ucmd);

			/*
			 * Let go of the jump key.
			 *
			 * PM_CheckJump sets PMF_JUMP_HELD when it fires and then refuses
			 * every later jump until a command arrives with upmove under 10:
			 *
			 *     if (pm->cmd.upmove < 10) pm_flags &= ~PMF_JUMP_HELD;
			 *     if (pm_flags & PMF_JUMP_HELD) return;
			 *
			 * Holding the key down through the rest of the frame therefore buys
			 * nothing and costs the next hop, and subdividing made it worse
			 * rather than better, because the same held command was replayed
			 * across every step. A player taps it; so does the bot now, and the
			 * release lands inside the same tenth of a second as the press.
			 */
			if (ucmd.upmove >= 10) ucmd.upmove = 0;

			if (step == 0 && (bi->actionflags & ACTION_DELAYEDJUMP))
				ucmd.upmove += 400;
		}
	} //end if
	else
	{
		//call the client think function to execute the usercmd_t of the bot
		ClientThink(bot, &ucmd);
	} //end else
	//
	bot->flags &= ~FL_BOTINPUT;
	//set the ping of the bot
	bot->client->ping = 1000 * bi->thinktime + crandom() * 20;
} //end of the function BotExecuteInput
//===========================================================================
// set the pmove_state_t of the bot
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void BotSetPMoveState(edict_t *bot)
{
	#define PMF_DUCKED			1			//ducked
	#define PMF_JUMP_HELD		2			//jump held down
	#define PMF_ON_GROUND		4			//set when on the ground
	#define PMF_TIME_WATERJUMP	8			//pm_time is waterjump
	#define PMF_TIME_LAND		16			//pm_time is time before rejump
	#define PMF_TIME_TELEPORT	32			//pm_time is non-moving time
	#define PMF_NO_PREDICTION	64			//temporarily disables prediction (used for grappling hook)
	#define PMF_UNUSED			128		//unused
	//
	// pmove_state_t				//everything is set when calling gi.Pmove
	//
	// pmtype_t pm_type;
	//		type of movement, set in ClientThink
	//
	// short origin[3];			// 12.3
	//		origin of the edict, override in ClientThink after being copied
	//
	// short velocity[3];		// 12.3
	//		velocity of the edict, override in ClientThink after being copied
	//
	// byte pm_flags;				// ducked, jump_held, etc
	//		several movement flags, set by the Quake2 engine for real clients
	//		see above for possible flags
	//
	// byte pm_time;
	//		pmove time, works with one of the PMF_TIME_? flags
	//
	// short gravity;
	//		the current gravity, set in ClientThink
	//
	// short delta_angles[3];	// add to command angles to get view direction
	//									// changed by spawns, rotating objects, and teleporters
	//		angles are added to the client angles during one client frame and
	//		after that the delta angles are cleared by the Quake2 engine
	//
	//the Quake2 engine does this for real clients
	VectorClear(bot->client->ps.pmove.delta_angles);
} //end of the function BotSetPMoveState
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
bot_library_t *GetBotLibrary(edict_t *bot)
{
	bot_library_t *lib;

	lib = botglobals.botstates[DF_ENTCLIENT(bot)].library;
	if (!lib)
	{
		gi.dprintf("bot (client %d) without bot library\n", DF_ENTCLIENT(bot));
	} //end if
	return lib;
} //end of the function GetBotLibrary
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void ClientBegin(edict_t *ent);

qboolean BotStarted(edict_t *bot)
{
	bot_library_t *lib;

	//if the bot already started
	if (botglobals.botstates[DF_ENTCLIENT(bot)].started) return true;
	//get the library the bot uses
	lib = GetBotLibrary(bot);
	//if the library is initialized
	if (lib->funcs.BotLibraryInitialized())
	{
		//NOTE: set the inuse flag to false because the bot isn't loaded
		//			from a savegame
		bot->inuse = false;
		/*
		 * Pick the bot's CTF team before ClientBegin, not after: ClientBegin
		 * keeps whatever team the client already has and only balances one
		 * that has none, so this is where "botctfteam" gets its say. Doing it
		 * afterwards would mean spawning the bot at the wrong base and then
		 * moving it.
		 */
		BotCTFAssignTeam(bot);
		//the Quake2 server calls this function for real clients
		ClientBegin(bot);
		//
		bot->flags |= FL_BOT;
		//the bot has started
		botglobals.botstates[DF_ENTCLIENT(bot)].started = true;
		return true;
	} //end if
	return false;
} //end of the function BotStarted

//==========================================================================
//
// usage of imported functions from library
//
//==========================================================================

//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
/*
 * Tell the library which model index the grapple hook uses.
 *
 * GrappleState recognises a hook in flight by matching an entity's model index
 * against weapindex_grapple, and model indices are per-level configstrings, so
 * this has to be set for every map. gi.modelindex doubles as the precache in
 * Quake II: LMCTF only registers the hook model when one is first fired, so
 * calling it here also stops the first grapple of a level from hitching.
 */
void BotSetGrappleModelIndex(bot_library_t *lib)
{
	char idx[16];

	if (!lib) return;
	Com_sprintf(idx, sizeof idx, "%d",
		gi.modelindex("models/objects/ghook/tris.md2"));
	lib->funcs.BotLibVarSet("weapindex_grapple", idx);
}

void BotLib_BotLoadMap(char *mapname)
{
	bot_library_t *lib, *nextlib;
	int errnum;

	for (lib = botglobals.firstbotlib; lib; lib = nextlib)
	{
		nextlib = lib->next;
		BotSetGrappleModelIndex(lib);
		errnum = lib->funcs.BotLoadMap(mapname, MAX_MODELINDEXES, modelindexes,
												MAX_SOUNDINDEXES, soundindexes,
												MAX_IMAGEINDEXES, imageindexes);
		if (errnum != BLERR_NOERROR)
		{
			int i;
			edict_t *cl_ent;

			//remove all bots using this library
			for (i = 0; i < game.maxclients; i++)
			{
				cl_ent = g_edicts + 1 + i;
				if (!cl_ent->inuse) continue;
				if (!(cl_ent->flags & FL_BOT)) continue;
				if (GetBotLibrary(cl_ent) == lib)
				{
					BotDestroy(cl_ent);
				} //end if
			} //end for
		} //end if
	} //end for
} //end of the function BotLib_BotLoadMap
//==========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//==========================================================================
int BotLib_BotSetupClient(edict_t *ent, char *userinfo)
{
	char *s;
	bot_library_t *lib;
	bot_settings_t settings;

	lib = GetBotLibrary(ent);
	if (!lib) return false;
	memset(&settings, 0, sizeof(bot_settings_t));
	//
	s = Info_ValueForKey(userinfo, "charfile");
	strncpy(settings.characterfile, s, MAX_FILEPATH-1); //Riv++
	settings.characterfile[MAX_FILEPATH-1] = '\0';
	//
	s = Info_ValueForKey(userinfo, "charname");
	strncpy(settings.charactername, s, MAX_CHARACTERNAME-1); //Riv++
	settings.charactername[MAX_CHARACTERNAME-1] = '\0';
	//
	if (!lib->funcs.BotSetupClient(DF_ENTCLIENT(ent), &settings))
		return false;
	BotChat_OnEnterGame(ent);
	return true;
} //end of the function BotLib_BotSetupClient
//==========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//==========================================================================
void BotLib_BotShutdownClient(edict_t *client)
{
	bot_library_t *lib;

	lib = GetBotLibrary(client);
	if (!lib) return;
	if (client->flags & FL_BOT)
		BotChat_OnExitGame(client);
	lib->funcs.BotShutdownClient(DF_ENTCLIENT(client));
} //end of the function BotLib_BotShutDownClient
//==========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//==========================================================================
void BotLib_BotMoveClient(edict_t *oldclient, edict_t *newclient)
{
	bot_library_t *lib;

	lib = GetBotLibrary(oldclient);
	if (!lib) return;
	lib->funcs.BotMoveClient(DF_ENTCLIENT(oldclient), DF_ENTCLIENT(newclient));
} //end of the function BotLib_BotMoveClient
//==========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//==========================================================================
void BotLib_BotClientSettings(edict_t *ent)
{
	bot_clientsettings_t settings;
	bot_library_t *lib;

	if (!ent->client)
	{
		gi.dprintf("client %d without client structure\n", DF_ENTCLIENT(ent));
		return;
	} //end if
	//copy the client name
	strncpy(settings.netname, ent->client->pers.netname, MAX_NETNAME-1); //Riv++
	settings.netname[MAX_NETNAME-1] = '\0'; //Riv++
	//client skin
	strncpy(settings.skin,
		Info_ValueForKey(ent->client->pers.userinfo, "skin"), MAX_CLIENTSKINNAME-1); //Riv++
	settings.skin[MAX_CLIENTSKINNAME-1] = '\0'; //Riv++

	for (lib = botglobals.firstbotlib; lib; lib = lib->next)
	{
		lib->funcs.BotClientSettings(DF_ENTCLIENT(ent), &settings);
	} //end for
} //end of the function BotLib_BotClientSettings
//==========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//==========================================================================
void BotLib_UpdateAllClientSettings(void)
{
	int i;
	edict_t *ent;

	for (i = 0; i < game.maxclients; i++)
	{
		ent = DF_CLIENTENT(i);
		if (!ent->inuse) continue;
		BotLib_BotClientSettings(ent);
	} //end for
} //end of the function BotLib_UpdateAllClientSettings
//==========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//==========================================================================
void BotLib_BotSettings(edict_t *bot, bot_settings_t *settings)
{
} //end of the function BotLib_BotSettings
//==========================================================================
// sends a client (state) update to the bot library
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//==========================================================================
void BotLib_BotUpdateClient(edict_t *bot)
{
	bot_updateclient_t buc;
	bot_library_t *lib;
	int i;

	if (!bot->inuse) return;
	lib = GetBotLibrary(bot);
	if (!lib) return;

	if (!bot->client)
	{
		gi.dprintf("client %d without client structure\n", DF_ENTCLIENT(bot));
		return;
	} //end if
	//movement type
	buc.pm_type = bot->client->ps.pmove.pm_type;
	//origin of the bot
	VectorCopy(bot->s.origin, buc.origin);
	//velocity of the bot
	VectorCopy(bot->velocity, buc.velocity);
	//pm_flags
	buc.pm_flags = bot->client->ps.pmove.pm_flags;
	//pm_time
	buc.pm_time = bot->client->ps.pmove.pm_time;
	//gravity
	buc.gravity = sv_gravity->value;
	//delta_angles (NOTE: the bot->client->ps.pmove.delta_angles are of type short)
	VectorCopy(bot->client->ps.pmove.delta_angles, buc.delta_angles);
	//====================================
	//view angles
	/*
	 * The bot's real view angles.
	 *
	 * This was VectorClear, so the library was told every bot faced angle zero
	 * on every frame. Anything that compares where a bot is looking against
	 * where it should look was therefore meaningless -- the grapple, for one,
	 * only fires once the bot is aimed within two degrees of the hook point,
	 * which could never happen.
	 *
	 * player_state_t.viewangles is not the field to use: Quake II only fills
	 * it for fixed views, during intermission and death. A playing client's
	 * view lives in client->v_angle.
	 */
	VectorCopy(bot->client->v_angle, buc.viewangles);
	//view offset
	buc.runetype = bot->client->rune ? bot->client->rune->runetype : 0;
	buc.hookstate = bot->client->hookstate;
	VectorCopy(bot->client->ps.viewoffset, buc.viewoffset);
	//kick angles
	VectorCopy(bot->client->ps.kick_angles, buc.kick_angles);
	//gun angles
	VectorCopy(bot->client->ps.gunangles, buc.gunangles);
	//gun offset
	VectorCopy(bot->client->ps.gunoffset, buc.gunoffset);
	//gun index
	buc.gunindex = bot->client->ps.gunindex;
	//gun frame
	buc.gunframe = bot->client->ps.gunframe;
	//blend
	for (i = 0; i < 4; i++) buc.blend[i] = bot->client->ps.blend[i];
	//field of vision
	buc.fov = bot->client->ps.fov;
	//rdflags
	buc.rdflags = bot->client->ps.rdflags;
	//
	memcpy(buc.stats, bot->client->ps.stats, MAX_STATS * sizeof(short));
	//====================================
	//inventory
	memcpy(buc.inventory, bot->client->pers.inventory, MAX_ITEMS * sizeof(int));
	//update the client
	lib->funcs.BotUpdateClient(DF_ENTCLIENT(bot), &buc);
	//====================================
	BotSetPMoveState(bot);
} //end of the function BotLib_BotUpdateClient
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void BotLib_BotStartFrame(float time)
{
	bot_library_t *lib;

	//NOTE: not really nice to put this call here ... but it's functional
	BotLib_UpdateAllClientSettings();
	//
	for (lib = botglobals.firstbotlib; lib; lib = lib->next)
	{
		//set the dmflags
		lib->funcs.BotLibVarSet("dmflags", dmflags->string);
		//forward bot_developer cvar so it can be toggled at runtime
		{
			cvar_t *bd = gi.cvar("bot_developer", "0", 0);
			if (bd) lib->funcs.BotLibVarSet("bot_developer", bd->string);
		}
		//start the server frame
		lib->funcs.BotStartFrame(time);
	} //end for
} //end of the function BotLib_BotStartFrame
//===========================================================================
// sends an entity update to the bot library
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void BotLib_BotUpdateEntity(edict_t *ent)
{
	bot_updateentity_t bue;
	bot_library_t *lib;

	VectorCopy(ent->s.origin, bue.origin);
	VectorCopy(ent->s.angles, bue.angles);
	VectorCopy(ent->s.old_origin, bue.old_origin);
	VectorCopy(ent->mins, bue.mins);
	VectorCopy(ent->maxs, bue.maxs);
	bue.solid = ent->solid;
	bue.modelindex = ent->s.modelindex;
	bue.modelindex2 = ent->s.modelindex2;
	bue.modelindex3 = ent->s.modelindex3;
	bue.modelindex4 = ent->s.modelindex4;
	bue.frame = ent->s.frame;
	bue.skinnum = ent->s.skinnum;
	bue.effects = ent->s.effects;
	bue.renderfx = ent->s.renderfx;
	bue.sound = ent->s.sound;
	bue.event = ent->s.event;

#ifdef TOURNEY
	if (ent->item && (ent->item->flags & IT_RUNE))
	{
		if (ent->item->quantity == STAT_RUNE_RESIST) bue.modelindex = TECH1_INDEX;
		else if (ent->item->quantity == STAT_RUNE_STRENGTH) bue.modelindex = TECH2_INDEX;
		else if (ent->item->quantity == STAT_RUNE_HASTE) bue.modelindex = TECH3_INDEX;
		else if (ent->item->quantity == STAT_RUNE_REGEN) bue.modelindex = TECH4_INDEX;
		else if (ent->item->quantity == STAT_RUNE_VAMPIRE) bue.modelindex = TECH5_INDEX;
	} //end if
#endif //TOURNEY

	for (lib = botglobals.firstbotlib; lib; lib = lib->next)
	{
		if (lib->funcs.BotLibraryInitialized())
		{
			lib->funcs.BotUpdateEntity(DF_ENTNUMBER(ent), &bue);
		} //end if
	} //end for
	//if this is a client, FIXME: is this necessary?
	if (ent->client)
	{
		//if the entity has a sound
		if (ent->s.sound)
		{
			//send the sound seperately
			BotLib_BotAddSound(ent, CHAN_AUTO, ent->s.sound, 1.0, ATTN_IDLE, 0);
		} //end if
	} //end if
} //end of the function BotLib_BotUpdateEntity
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void BotLib_BotAddSound(edict_t *ent, int channel, int soundindex, float volume, float attenuation, float timeofs)
{
	bot_library_t *lib;
	int entnum;

	if (soundindex < 0 || soundindex > 255)
	{
//		gi.dprintf("soundindex %d out of range [0, %d]\n", soundindex, 255);
		return;
	} //end if
	entnum = DF_ENTNUMBER(ent);
	for (lib = botglobals.firstbotlib; lib; lib = lib->next)
	{
		if (lib->funcs.BotLibraryInitialized())
		{
			lib->funcs.BotAddSound(ent->s.origin, entnum, channel, soundindex, volume, attenuation, timeofs);
		} //end if
	} //end for
} //end of the function BotLib_BotAddSound
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void BotLib_BotAI(edict_t *bot, float thinktime)
{
	bot_library_t *lib;

#ifdef BOT_DEBUG
	if (botglobals.nobotai) return;
#endif //BOT_DEBUG

	lib = GetBotLibrary(bot);
	if (!lib) return;
	lib->funcs.BotAI(DF_ENTCLIENT(bot), thinktime);
	BotChat_Frame(bot);
} //end of the function BotLib_BotAI
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void BotLib_BotConsoleMessage(edict_t *bot, int type, char *message)
{
	bot_library_t *lib;

	lib = GetBotLibrary(bot);
	if (!lib) return;
	lib->funcs.BotConsoleMessage(DF_ENTCLIENT(bot), type, message);
} //end of the function BotLib_BotConsoleMessage
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void BotLib_BotLibVarSet(char *var_name, char *value)
{
	bot_library_t *lib;
	
	for (lib = botglobals.firstbotlib; lib; lib = lib->next)
	{
		lib->funcs.BotLibVarSet(var_name, value);
	} //end for
} //end of the function BotLib_BotLibVarSet
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
int BotLib_Test(int parm0, char *parm1, vec3_t parm2, vec3_t parm3)
{
	bot_library_t *lib;
	
	for (lib = botglobals.firstbotlib; lib; lib = lib->next)
	{
		lib->funcs.Test(parm0, parm1, parm2, parm3);
	} //end for
	return 0;
} //end of the function BotLib_Test

//==========================================================================
//
// functions exported to the bot library
//
//==========================================================================

//===========================================================================
// stores the new bot input
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void BotLibImport_BotInput(int client, bot_input_t *bi)
{
	if (client < 0 || client >= game.maxclients)
	{
		gi.dprintf("BotInput: client number out of range\n");
		return;
	} //end if
	memcpy(&botglobals.botinputs[client], bi, sizeof(bot_input_t));
	botglobals.botnewinput[client] = true;
} //end of the function BotLibImport_BotInput
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void BotLibImport_Print(int type, char *fmt, ...)
{
	char str[2048];
	char warning[64] = "Warning: ";
	char error[64] = "Error: ";
	char fatal[64] = "Fatal: ";
	va_list ap;

	va_start(ap, fmt);
	vsprintf(str, fmt, ap);
	va_end(ap);

	switch(type)
	{
		case PRT_MESSAGE:
		{
			gi.dprintf("%s", str);
			//gi.bprintf(PRINT_HIGH, str);
			break;
		} //end case
		case PRT_WARNING:
		{
			StringMakeGreen(warning);
			gi.dprintf("%s%s", warning, str);
			break;
		} //end case
		case PRT_ERROR:
		{
			StringMakeGreen(error);
			gi.dprintf("%s%s", error, str);
			break;
		} //end case
		case PRT_FATAL:
		{
			StringMakeGreen(fatal);
			//gi.error("%s%s", fatal, str);
			gi.dprintf("%s%s", fatal, str);
			break;
		} //end case
		case PRT_EXIT:
		{
			StringMakeGreen(str);
			gi.error("Exit: %s", str);
			break;
		} //end case
		default:
		{
			gi.dprintf("unknown print type\n");
			break;
		} //end case
	} //end switch
} //end of the function BotLibImport_Print
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void *BotLibImport_GetMemory(int size)
{
	void *ptr;
	//NOTE: don't use TAG_LEVEL, because all that memory will be freed
	//      at level changes. The game library doesn't change during level
	//      changes except for a LoadMap call. The game library assumes
	//      the allocated memory will stay during level changes, so the
	//      memory should not be freed in the game dll.
	ptr = gi.TagMalloc(size, TAG_GAME);
	if (!ptr)
	{
		gi.error("out of memory\n");
	} //end if
	return ptr;
} //end of the function BotLibImport_GetMemory
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void BotLibImport_FreeMemory(void *ptr)
{
	gi.TagFree(ptr);
} //end of the function BotLibImport_FreeMemory
//===========================================================================
// CTF/teamplay: check if two entities are on the same team.
// Wraps Q2's OnSameTeam() for the botlib import table.
// Entity numbers are 1-indexed (g_edicts[ent]).
//===========================================================================
static int BotLibImport_OnSameTeam(int ent1, int ent2)
{
	edict_t *e1, *e2;
	if (ent1 < 1 || ent1 >= game.maxentities) return 0;
	if (ent2 < 1 || ent2 >= game.maxentities) return 0;
	e1 = &g_edicts[ent1];
	e2 = &g_edicts[ent2];
	if (!e1->inuse || !e2->inuse) return 0;
	if (!e1->client || !e2->client) return 0;
	return OnSameTeam(e1, e2);
} //end of the function BotLibImport_OnSameTeam
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
bsp_trace_t BotLibImport_Trace(vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, int passent, int contentmask)
{
	bsp_trace_t bsptrace;
	trace_t trace;
	edict_t *p;

	//and another dirty LCC warning prevention
	memset(&trace, 0, sizeof(trace_t));
	//just for the errors
	memset(&bsptrace, 0, sizeof(bsp_trace_t));
	//check for valid passent entity number
	//-1 means "skip no entity" (used by Q3 botlib code, e.g. be_ai_goal.c)
	if (passent < -1 || passent >= game.maxentities)
	{
		gi.dprintf("BotLibTrace: invalid passent %d\n", passent);
		return bsptrace;
	} //end if
	p = (passent >= 0) ? DF_NUMBERENT(passent) : NULL;
	//
	trace = gi.trace(start, mins, maxs, end, p, contentmask);
	memcpy(bsptrace.surface.name, trace.surface->name, 16);
	bsptrace.surface.flags = trace.surface->flags;
	bsptrace.surface.value = trace.surface->value;
	bsptrace.allsolid = trace.allsolid;
	bsptrace.startsolid = trace.startsolid;
	bsptrace.fraction = trace.fraction;
	VectorCopy(trace.endpos, bsptrace.endpos);
	bsptrace.ent = DF_ENTNUMBER(trace.ent);
	bsptrace.contents = trace.contents;
	memcpy(&bsptrace.plane, &trace.plane, sizeof(cplane_t));
#ifdef __LCC__ //Riv++ Prevent dll from crashing, heh... Some issues remain though
  gi.dprintf("");
#endif
	return bsptrace;
} //end of the function BotLibImport_Trace

//==========================================================================
//
// bot library loading, initialization etc.
//
//==========================================================================

//===========================================================================
// initialize the bot library
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
int BotInitLibrary(bot_library_t *lib)
{
	int errnum;
	cvar_t *cvar;
	char buf[144];

	//set the maxclients and maxentities library variables before calling BotSetupLibrary
	lib->funcs.BotLibVarSet("maxclients", maxclients->string);
	lib->funcs.BotLibVarSet("maxentities", maxentities->string);
	//maximum number of aas links
	cvar = gi.cvar("max_aaslinks", "", 0);
	if (cvar && cvar->value > 0) lib->funcs.BotLibVarSet("max_aaslinks", cvar->string);
	//maximum number of bsp links
	cvar = gi.cvar("max_bsplinks", "", 0);
	if (cvar && cvar->value > 0) lib->funcs.BotLibVarSet("max_bsplinks", cvar->string);
	//maximum number of items in a level
	cvar = gi.cvar("max_levelitems", "", 0);
	if (cvar && cvar->value > 0) lib->funcs.BotLibVarSet("max_levelitems", cvar->string);
	//automatically launch WinBSPC if AAS file not available
	cvar = gi.cvar("autolaunchbspc", "", 0);
	if (cvar && cvar->value) lib->funcs.BotLibVarSet("autolaunchbspc", "1");
	/* Before BotSetupLibrary: the library latches bot_developer into a C
	 * variable during setup, so setting it afterwards left every diagnostic
	 * switched off no matter what the cvar said. */
	cvar = gi.cvar("bot_developer", "0", 0);
	if (cvar) lib->funcs.BotLibVarSet("bot_developer", cvar->string);
	//bot skill level (1=beginner, 5=expert)
	cvar = gi.cvar("bot_skill", "4", 0);
	if (cvar) lib->funcs.BotLibVarSet("bot_skill", cvar->string);
	//deathmatch flags
	lib->funcs.BotLibVarSet("dmflags", dmflags->string);
	sprintf(buf, "DMFLAGS %s", dmflags->string);
	lib->funcs.BotDefine(buf);
	/*
	 * This block used to sit inside #ifdef ZOID, which this mod does not
	 * define -- so the library was never told CTF was running. The adapter
	 * reads the "ctf" libvar to set g_gametype to GT_CTF, without which the
	 * bots behave as if this were a free-for-all and ignore the flags.
	 * LMCTF is always CTF, so it is unconditional.
	 */
	lib->funcs.BotLibVarSet("ctf", "1");
	lib->funcs.BotLibVarSet("usehook", "1");
	lib->funcs.BotLibVarSet("runes", "1");

	/*
	 * LMCTF's grapple is offhand: you fire it with a console command and keep
	 * whatever weapon you are holding, unlike Zoid CTF where the grapple
	 * occupies a weapon slot and has to be selected. The movement code
	 * supports both -- with offhandgrapple set it issues cmd_grappleon /
	 * cmd_grappleoff instead of asking for a weapon switch, so the bot can
	 * shoot while swinging, the same as a player.
	 */
	lib->funcs.BotLibVarSet("offhandgrapple", "1");
	lib->funcs.BotLibVarSet("cmd_grappleon", "hook");
	lib->funcs.BotLibVarSet("cmd_grappleoff", "unhook");

	/* Route over TRAVEL_GRAPPLEHOOK reachabilities; see Q2BotTravelFlags.
	 * On by default because LMCTF's hook is always in hand, but exposed as a
	 * cvar so it can be turned off without a rebuild. */
	cvar = gi.cvar("bot_grapple", "1", 0);
	lib->funcs.BotLibVarSet("bot_grapple", cvar ? cvar->string : "1");

	/* The library reads libvars, not cvars. Anything the server sets that the
	 * bot code consults has to be handed across explicitly or it silently
	 * reads zero -- which is what kept the ground hook from ever firing. */
	cvar = gi.cvar("bot_groundhook", "1", 0);
	lib->funcs.BotLibVarSet("bot_groundhook", cvar ? cvar->string : "1");
	/*
	 * Fifteen degrees below the line of travel. Swept across a match at 3, 7,
	 * 10, 15, 20 and 30: shallower anchors pay more per pull (61 units at 3
	 * against 48 at 15) but find far fewer surfaces, and the total speed banked
	 * over a match -- catches times gain -- peaks here. Past 20 the ray meets
	 * the floor inside the hook's minimum reach and is refused, so at 30 only a
	 * hundred hooks fire all match and the mean falls back to what it is with
	 * no hook at all.
	 */
	cvar = gi.cvar("bot_groundhook_pitch", "15", 0);
	lib->funcs.BotLibVarSet("bot_groundhook_pitch", cvar ? cvar->string : "15");
	cvar = gi.cvar("bot_groundhook_dist", "300", 0);
	lib->funcs.BotLibVarSet("bot_groundhook_dist", cvar ? cvar->string : "300");

	/*
	 * The overhead leg of the chain. A short anchor ahead is how a slow bot
	 * gets going, but once it is moving that anchor arrives too soon to be
	 * worth much -- the pull ends before it has paid. A long hook overhead
	 * stays taut far longer and turns speed into distance instead of spending
	 * it on the floor, so above bot_ceilhook_minspeed the bots aim up instead.
	 */
	/*
	 * How far the bot will look for an anchor, and the shortest rope worth
	 * taking. The rope sets velocity to a flat 800 until it is under 120 units
	 * long, so rope length is simply how long the bot gets to travel at 800 --
	 * a short rope is a brake with extra steps.
	 */
	cvar = gi.cvar("bot_hook_reach", "1400", 0);
	lib->funcs.BotLibVarSet("bot_hook_reach", cvar ? cvar->string : "1400");
	/*
	 * Swept 200 / 400 / 550 / 700 / 900 / 1200 at three matches each. 700 is a
	 * real peak on both mean speed and the share of the match spent above the
	 * run cap, and it falls away on either side: shorter ropes fire three times
	 * as often and are worth much less each, longer ones are worth more but
	 * grow too rare to find. Rope length is time held at 800, so this is the
	 * point where the two effects cross.
	 */
	cvar = gi.cvar("bot_hook_minrope", "700", 0);
	lib->funcs.BotLibVarSet("bot_hook_minrope", cvar ? cvar->string : "700");

	cvar = gi.cvar("bot_ceilhook_dist", "900", 0);
	lib->funcs.BotLibVarSet("bot_ceilhook_dist", cvar ? cvar->string : "900");
	cvar = gi.cvar("bot_ceilhook_pitch", "30", 0);
	lib->funcs.BotLibVarSet("bot_ceilhook_pitch", cvar ? cvar->string : "30");
	cvar = gi.cvar("bot_ceilhook_minspeed", "300", 0);
	lib->funcs.BotLibVarSet("bot_ceilhook_minspeed", cvar ? cvar->string : "300");
	cvar = gi.cvar("bot_know_range", "1200", 0);
	lib->funcs.BotLibVarSet("bot_know_range", cvar ? cvar->string : "1200");

	/* CTF offence, handed across the same way. How much of a side holds the
	 * base out of five; how many seconds an unarmed attacker may spend picking
	 * up a weapon on the way; and whether an attacker keeps the hop chain and
	 * the ground hook while it runs. */
	cvar = gi.cvar("bot_defend_share", "2", 0);
	lib->funcs.BotLibVarSet("bot_defend_share", cvar ? cvar->string : "2");
	cvar = gi.cvar("bot_flagrun_pickup", "6", 0);
	lib->funcs.BotLibVarSet("bot_flagrun_pickup", cvar ? cvar->string : "6");
	/*
	 * On by default. Carrying the flag does not stop a player using the hook --
	 * it is how they get home alive. The tricks were switched off during a run
	 * because they were dragging bots off their route, but that was the hop
	 * chain steering, not the hook, and it is fixed where it was broken.
	 */
	cvar = gi.cvar("bot_flagrun_tricks", "1", 0);
	lib->funcs.BotLibVarSet("bot_flagrun_tricks", cvar ? cvar->string : "1");
#ifdef CH
	lib->funcs.BotLibVarSet("ch", ch->string);
#endif //CH
#ifdef ROCKETARENA
	lib->funcs.BotLibVarSet("ra", ra->string);
#endif //ROCKETARENA
#ifdef XATRIX
	lib->funcs.BotLibVarSet("xatrix", xatrix->string);
#endif //XATRIX
#ifdef ROGUE
	lib->funcs.BotLibVarSet("rogue", rogue->string);
#endif //ROGUE
	//log file
	cvar = gi.cvar("log", "1", 0);
	lib->funcs.BotLibVarSet("log", cvar->string);
	//no chatting
	cvar = gi.cvar("nochat", "", 0);
	if (cvar && cvar->value) lib->funcs.BotLibVarSet("nochat", "1");
	//fast chatting
	cvar = gi.cvar("fastchat", "", 0);
	if (cvar && cvar->value) lib->funcs.BotLibVarSet("fastchat", "0");
	//alternative names
	cvar = gi.cvar("altnames", "", 0);
	if (cvar && cvar->value) lib->funcs.BotLibVarSet("altnames", "1");
	//enable rocket jumping
	cvar = gi.cvar("rocketjump", "1", 0);
	if (cvar && cvar->value) lib->funcs.BotLibVarSet("rocketjump", "1");
	//forced clustering calculations
	cvar = gi.cvar("forceclustering", "", 0);
	if (cvar && cvar->value) lib->funcs.BotLibVarSet("forceclustering", "1");
	//forced reachability calculations
	cvar = gi.cvar("forcereachability", "", 0);
	if (cvar && cvar->value) lib->funcs.BotLibVarSet("forcereachability", "1");
	//force writing of AAS to file
	cvar = gi.cvar("forcewrite", "", 0);
	if (cvar && cvar->value) lib->funcs.BotLibVarSet("forcewrite", "1");
	//no AAS optimization
	cvar = gi.cvar("nooptimize", "", 0);
	if (cvar && cvar->value) lib->funcs.BotLibVarSet("nooptimize", "1");
	//number of reachabilities to calculate each frame
	cvar = gi.cvar("framereachability", "20", 0);
	lib->funcs.BotLibVarSet("framereachability", cvar->string);
	//base directory
	cvar = gi.cvar("basedir", "", 0);
	if (cvar) lib->funcs.BotLibVarSet("basedir", cvar->string);
	else lib->funcs.BotLibVarSet("basedir", "");
	//game directory
	cvar = gi.cvar("gamedir", "", 0);
	if (cvar) lib->funcs.BotLibVarSet("gamedir", cvar->string);
	else lib->funcs.BotLibVarSet("gamedir", "");
	//cd directory
	cvar = gi.cvar("cddir", "", 0);
	if (cvar) lib->funcs.BotLibVarSet("cddir", cvar->string);
	else lib->funcs.BotLibVarSet("cddir", "");
	//setup the bot library
	errnum = lib->funcs.BotSetupLibrary();
	if (errnum != BLERR_NOERROR) return false;
	//load the map
	/* This is where a freshly loaded library first reads the map -- the
	 * per-map hook above has not run for it yet. */
	BotSetGrappleModelIndex(lib);
	errnum = lib->funcs.BotLoadMap(level.mapname, MAX_MODELINDEXES, modelindexes,
																MAX_SOUNDINDEXES, soundindexes,
																MAX_IMAGEINDEXES, imageindexes);
	if (errnum != BLERR_NOERROR) return false;
#ifdef TOURNEY
	// Handle the Tourney hook --JKK
	if((int)hook_enable->value)
	{
		lib->funcs.BotLibVarSet("usehook", "1");
		lib->funcs.BotLibVarSet("laserhook", "1");
	} //end if
	else
	{
		lib->funcs.BotLibVarSet("usehook", "0");
		lib->funcs.BotLibVarSet("laserhook", "0");
	} //end else

	if(m_mode == MODE_TEAM)
		lib->funcs.BotLibVarSet("teamplay", "1");
	else
		lib->funcs.BotLibVarSet("teamplay", "0");

	lib->funcs.BotLibVarSet("log", "0");
#endif //TOURNEY
	return true;
} //end of the function BotInitLibrary
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void BotUnloadLibrary(bot_library_t *lib)
{
	//unlink library from list
	if (lib->prev) lib->prev->next = lib->next;
	else botglobals.firstbotlib = lib->next;
	if (lib->next) lib->next->prev = lib->prev;
	/*
	 * Cut every bot loose from this library first.
	 *
	 * The per-client bot states keep a pointer to the library that drives
	 * them, and nothing here cleared it. Once the library was dlclose'd those
	 * pointers referred to unmapped memory, and the next frame's BotAI call
	 * jumped through one -- a segfault with the instruction pointer somewhere
	 * meaningless, reached from G_RunFrame. It is intermittent because it
	 * needs the library to be freed while a bot still exists, which only
	 * happens on particular orderings of bots leaving.
	 */
	{
		int i;
		for (i = 0; i < game.maxclients; i++)
		{
			if (botglobals.botstates[i].library != lib) continue;
			botglobals.botstates[i].library = NULL;
			botglobals.botstates[i].active  = false;
			botglobals.botstates[i].started = false;
		} //end for
	}
	//shut down the library
	lib->funcs.BotShutdownLibrary();
#if defined(WIN32) || defined(_WIN32)
	//Win32 free the bot library
	FreeLibrary(lib->handle);
#else
	//free the shared object
	dlclose(lib->handle);
#endif
	//free the memory of the library structure
	gi.TagFree(lib);
} //end of the function BotUnloadLibrary
//===========================================================================
// bot library loading
//
// NOTE: this is platform dependent code
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
#if defined(WIN32) || defined(_WIN32)

typedef bot_export_t *(WINAPI *PFNGetBotAPI)(bot_import_t *import);

bot_library_t *BotLoadLibrary(char *botlibdir)
{
	bot_library_t *lib = NULL;
	PFNGetBotAPI GetBotAPI = NULL;
	HANDLE botlibhandle = NULL;

	//load the library
	botlibhandle = LoadLibrary(botlibdir);
	if (botlibhandle == NULL)
	{
		gi.dprintf("couldn't load %s\n", botlibdir);
	} //end if
	else
	{
		GetBotAPI = (PFNGetBotAPI) GetProcAddress(botlibhandle, "GetBotAPI");
		if (GetBotAPI == NULL)
		{
			FreeLibrary(botlibhandle);
			gi.dprintf("couldn't find GetBotAPI in %s\n", botlibdir);
		} //end if
		else
		{
			lib = (bot_library_t *) gi.TagMalloc(sizeof(bot_library_t), TAG_GAME);
			memset(lib, 0, sizeof(bot_library_t));
			strncpy(lib->path, botlibdir, MAX_PATH-1); //Riv++
			lib->path[MAX_PATH-1] = '\0';
			lib->handle = botlibhandle;
			lib->funcs = (*GetBotAPI(&botglobals.gamebotimport));
			//add the library to the list
			lib->next = botglobals.firstbotlib;
			lib->prev = NULL;
			if (botglobals.firstbotlib) botglobals.firstbotlib->prev = lib;
			botglobals.firstbotlib = lib;
			//initialize library
			if (!BotInitLibrary(lib))
			{
				BotUnloadLibrary(lib);
				return NULL;
			} //end if
			//
			gi.dprintf("loaded %s\n", botlibdir);
		} //end else
	} //end else
	return lib;
} //end of the function BotLoadLibrary

#else //!win32

bot_library_t *BotLoadLibrary(char *botlibdir)
{
	bot_library_t *lib = NULL;
	bot_export_t *(*GetBotAPI)(bot_import_t *import);
	void *botlibhandle;
	const char *e;

	//load the library
	botlibhandle = dlopen(botlibdir, RTLD_NOW);
	if (!botlibhandle)
	{
		gi.dprintf("couldn't load %s: %s\n", botlibdir, dlerror());
	} //end if
	else
	{
		GetBotAPI = dlsym(botlibhandle, "GetBotAPI");
		e = dlerror();
		if (e)
		{
			dlclose(botlibhandle);
			gi.dprintf("couldn't find GetBotAPI in %s: %s\n", botlibdir, e);
		} //end if
		else
		{
			lib = (bot_library_t *) gi.TagMalloc(sizeof(bot_library_t), TAG_GAME);
			memset(lib, 0, sizeof(bot_library_t));
			strncpy(lib->path, botlibdir, MAX_PATH-1); //Riv++
			lib->path[MAX_PATH-1] = '\0';
			lib->handle = botlibhandle;
			lib->funcs = (*GetBotAPI(&botglobals.gamebotimport));
			//add the library to the list
			lib->next = botglobals.firstbotlib;
			lib->prev = NULL;
			if (botglobals.firstbotlib) botglobals.firstbotlib->prev = lib;
			botglobals.firstbotlib = lib;
			//initialize library
			if (!BotInitLibrary(lib))
			{
				BotUnloadLibrary(lib);
				return NULL;
			} //end if
			//
			gi.dprintf("loaded %s\n", botlibdir);
		} //end else
	} //end else
	return lib;
} //end of the function BotLoadLibrary
#endif
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void BotUnloadAllLibraries(void)
{
	bot_library_t *lib;

	for (lib = botglobals.firstbotlib; lib; lib = botglobals.firstbotlib)
	{
		BotUnloadLibrary(lib);
	} //end for
} //end of the function BotUnloadAllLibraries
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
bot_library_t *BotUseLibrary(char *path)
{
	cvar_t *cvar;
	char botlibdir[MAX_PATH] = "";
	bot_library_t *lib;

	//if the file is not directly accessable
	if (access(path, 0x04))
	{
		//get the base directory
		cvar = gi.cvar("basedir", "", 0);
		if (cvar) strncat(botlibdir, cvar->string, MAX_PATH-1); //Riv++
		botlibdir[MAX_PATH-1] = '\0'; //Riv++
		AppendPathSeperator(botlibdir, MAX_PATH);
		//user specified game directory
		cvar = gi.cvar("gamedir", "", 0);
		if (cvar) strncat(botlibdir, cvar->string, MAX_PATH - strlen(botlibdir) - 1); //Riv++
		botlibdir[MAX_PATH-1] = '\0'; //Riv++
		AppendPathSeperator(botlibdir, MAX_PATH);
	} //end if
	//the dll name
	strncat(botlibdir, path, MAX_PATH - strlen(botlibdir) - 1); //Riv++
	botlibdir[MAX_PATH-1] = '\0'; //Riv++

	/*
	 * access() above resolves against the working directory, but dlopen does
	 * not: handed a bare "botlib.so" it searches the linker paths only and
	 * never looks in the game directory. Anchor it here, before the string is
	 * compared against an already-loaded library or stored as its path, so
	 * every bot agrees on the same name.
	 */
	if (!strchr(botlibdir, '/'))
	{
		char anchored[MAX_PATH];
		Com_sprintf(anchored, sizeof anchored, "./%s", botlibdir);
		strncpy(botlibdir, anchored, MAX_PATH - 1);
		botlibdir[MAX_PATH-1] = '\0';
	}
	//check if the library is loaded already
	for (lib = botglobals.firstbotlib; lib; lib = lib->next)
	{
		if (!Q_stricmp(lib->path, botlibdir))
		{
			lib->users++;
			return lib;
		} //end if
	} //end for
	if (botglobals.firstbotlib)
	{
		return NULL;
	} //end if
	lib = BotLoadLibrary(botlibdir);
	if (lib) lib->users++;
	return lib;
} //end of the function BotUseLibrary
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void BotFreeLibrary(bot_library_t *lib)
{
	cvar_t *freebotlib;

	lib->users--;
	if (lib->users <= 0)
	{
		/*
		 * Default changed to keeping the library loaded.
		 *
		 * Unloading it the moment the last bot leaves throws away the loaded
		 * navigation data along with every goal and move state, and it all has
		 * to be read back the next time somebody adds a bot. On a server that
		 * fills with bots and empties again as players come and go, that is
		 * constant churn -- one match was seen reloading the library six times
		 * -- and it is the obvious suspect for anything holding a pointer into
		 * a library that has just been dlclose'd.
		 *
		 * Set freebotlib 1 to get the old behaviour back if the memory matters
		 * more than the reloads.
		 */
		freebotlib = gi.cvar("freebotlib", "0", 0);
		if (freebotlib->value) BotUnloadLibrary(lib);
	} //end if
} //end of the function BotFreeLibrary
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void BotLibraryDump(void)
{
	bot_library_t *lib;
	q2_botclient_t *bs;
	edict_t *ent;
	int i;

	gi.dprintf("Library Dump:\n");
	if (!botglobals.firstbotlib)
	{
		gi.dprintf("no libraries found\n");
	} //end if
	else
	{
		for (lib = botglobals.firstbotlib; lib; lib = lib->next)
		{
			gi.dprintf("-------------------------------------\n");
			gi.dprintf("%s\n", lib->path);
			for (i = 1; i <= game.maxclients; i++)
			{
				bs = &botglobals.botstates[i-1];
				if (bs->active)
				{
					if (bs->library == lib)
					{
						ent = DF_NUMBERENT(i);
						gi.dprintf("    client %3d: %s\n", i-1, ent->client->pers.netname);
					} //end if
				} //end if
			} //end for
		} //end for
	} //end else
} //end of the function BotLibraryDump
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
char *Ptr2PathWithMaxSize(char *path, int size)
{
	int length;
	char *ptr, *bestptr;

	bestptr = path;
	length = strlen(bestptr);
	if (length > size)
	{
		ptr = &bestptr[length - 1];
		bestptr = NULL;
		for (length = 0; length < size; length++)
		{
			if (*ptr == '\\' || *ptr == '/') bestptr = ptr;
			ptr--;
		} //end for
		if (!bestptr) bestptr = ptr;
	} //end if
	return bestptr;
} //end of the function Ptr2PathWithMaxSize
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void BotClientDump(void)
{
	edict_t *ent;
	int i;
	char *path;

	for (i = 0; i < game.maxclients; i++)
	{
		ent = DF_CLIENTENT(i);
		if (!ent->inuse)
		{
			gi.dprintf("%3d: -\n", i);
		} //end if
		else if (ent->flags & FL_BOT)
		{
			gi.dprintf("%3d: %-16s ", i, ent->client->pers.netname);
			path = botglobals.botstates[i].library->path;
			if (strlen(path) > 25)
			{
				//minus three for the dots
				path = Ptr2PathWithMaxSize(path, 25-3);
				gi.dprintf("...");
			} //end if
			gi.dprintf("%s\n", path);
		} //end if
		else
		{
			gi.dprintf("%3d: %-16s human\n", i, ent->client->pers.netname);
		} //end else
	} //end for
} //end of the function BotClientDump
//===========================================================================
// setup the bot library import functions
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void BotSetupBotLibImport(void)
{
	bot_import_t gamebotimport;

	gamebotimport.BotInput = BotLibImport_BotInput;
	gamebotimport.BotClientCommand = BotClientCommand;		//bl_redirgi.c
	gamebotimport.Print = BotLibImport_Print;
	//to be removed later below this
	gamebotimport.Trace = BotLibImport_Trace;
	gamebotimport.PointContents = gi.pointcontents;
	gamebotimport.GetMemory = BotLibImport_GetMemory;
	gamebotimport.FreeMemory = BotLibImport_FreeMemory;
	//debug lines
	gamebotimport.DebugLineCreate = DebugLineCreate;		//bl_debug.c
	gamebotimport.DebugLineDelete = DebugLineDelete;		//bl_debug.c
	gamebotimport.DebugLineShow = DebugLineShow;				//bl_debug.c
	//PVS check — engine's inPVS for botlib visibility culling
	gamebotimport.inPVS = gi.inPVS;
	//CTF/teamplay team check
	gamebotimport.OnSameTeam = BotLibImport_OnSameTeam;
	//
	memcpy(&botglobals.gamebotimport, &gamebotimport, sizeof(bot_import_t));
} //end of the function BotSetupLibrary
//===========================================================================
// initialize bot globals and allocate bot states
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void BotSetup(void)
{
	//allocate memory for the bot states
	botglobals.botstates = (q2_botclient_t *) gi.TagMalloc(game.maxclients * sizeof(q2_botclient_t), TAG_GAME);
	memset(botglobals.botstates, 0, game.maxclients * sizeof(q2_botclient_t));
	//allocate memory for the bot input
	botglobals.botinputs = (bot_input_t *) gi.TagMalloc(game.maxclients * sizeof(bot_input_t), TAG_GAME);
	memset(botglobals.botinputs, 0, game.maxclients * sizeof(bot_input_t));
	//allocate memory for the array with new input flags
	botglobals.botnewinput = (qboolean *) gi.TagMalloc(game.maxclients * sizeof(qboolean), TAG_GAME);
	memset(botglobals.botnewinput, 0, game.maxclients * sizeof(qboolean));
	//number of bots currently in the game
	botglobals.numbots = 0;
	//setup the bot library import structure
	BotSetupBotLibImport();
} //end of the function BotSetup

#endif //BOT
