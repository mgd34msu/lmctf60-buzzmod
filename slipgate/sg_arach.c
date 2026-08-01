/*
 * sg_arach.c -- ARACHNOTRON: the brain with legs. First walking cut.
 *
 * This is deliberately the minimum that PLAYS: load the rune, flood one
 * cost field per flag, spawn a real client, and every ClientThink descend
 * the field toward the enemy flag -- run home when carrying. No combat, no
 * hook, no speed tricks yet. If a bot cannot get base to base on the rune,
 * nothing fancier deserves to exist; this is the walking skeleton the rest
 * grows on, and it is A/B-able against the legacy bots from the first
 * frame.
 *
 *   sv sg add        spawn a SLIPGATE bot (team by botctfteam, like legacy)
 *   sv sg remove     remove them all
 *
 * Movement per think: pick among the current seed's outgoing links (and
 * staying put) by field value at the destination, steer at the chosen
 * link's endpoint, run at full command. Jump links jump. Arrival needs no
 * test -- the next think re-reads position and field wherever we ended up,
 * which is the whole point of descending a field instead of chasing nodes.
 */

#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_combat.h"

/*
 * The client lifecycle and the glue's edict helpers are declared where the
 * legacy bot spawner declares them -- locally, the way this tree does it
 * (see bl_spawn.c:26-28). Same functions, same signatures.
 */
void		ClientThink(edict_t *ent, usercmd_t *ucmd);
qboolean	ClientConnect(edict_t *ent, char *userinfo);
void		ClientBegin(edict_t *ent);
void		ClientUserinfoChanged(edict_t *ent, char *userinfo);
edict_t		*G_SpawnClient(void);
void		G_FreeClientEdict(edict_t *ent);

#define SG_MAXBOTS      16
#define FIELD_INF       0x3fffffff

typedef struct sg_bot_s
{
	edict_t		*ent;
	qboolean	active;
	int			seed;           /* seed we believe we are at/near */
	float		stuck_time;     /* accumulated time without progress */
	float		next_report;
	float		next_cmdlog;
	vec3_t		last_origin;

	/* hook execution, two-phase: aim this frame (ClientThink turns the cmd
	 * angles into v_angle), fire immediately after, since Weapon_Hook_Fire
	 * launches along v_angle; then release before the p_weapon.c brake band */
	int			hook_phase;     /* 0 none, 1 aimed+firing, 2 rope out,
	                             * 3 released mid-air, steering to land */
	vec3_t		hook_anchor;
	vec3_t		hook_dest;      /* the link's destination seed origin */
	float		hook_deadline;

	/* a link chosen for seconds while the bot goes nowhere is a link the
	 * body cannot execute, whatever the rune thinks -- shelve it awhile.
	 * 32 slots: one doorway feeds 25 links from a single seed (662), and
	 * an 8-slot shelf recycled them faster than they expired. */
#define SG_BL_MAX 32
	int			bl_link[SG_BL_MAX];
	float		bl_until[SG_BL_MAX];

	/* a door that would not yield from this side: a wall, for a while.
	 * bd2's triggers are all SOUTH of it -- approached from the north it
	 * simply does not open, and links proven with doors held open are
	 * runtime lies from that side (Trace: 396 of 416 seconds pinned). */
#define SG_DEAD_DOORS 4
	edict_t		*dead_door[SG_DEAD_DOORS];
	float		dead_door_until[SG_DEAD_DOORS];
	edict_t		*door_hold_ent;     /* the door currently being waited on */
	float		door_hold_since;
	int			watch_link;     /* the link under progress-watch */
	float		watch_since;
	vec3_t		watch_org;
	int			commit_link;    /* the gradient step being held */
	float		commit_until;
} sg_bot_t;

static sg_bot_t	sg_bots[SG_MAXBOTS];
static rune_t	*sg_rune;
static char		sg_rune_map[64];

/* one cost field per flag: cost_ms from every seed TO the flag seed */
static int		*sg_field_red;
static int		*sg_field_blue;

/* ------------------------------------------------------------------ rune */

rune_t *Rune_Load(const char *mapname)
{
	char path[MAX_OSPATH];
	FILE *f;
	rune_t *r;
	int i;
	cvar_t *gamedir = gi.cvar("gamedir", "", 0);

	Com_sprintf(path, sizeof(path), "%s/maps/%s.rune",
	            gamedir->string[0] ? gamedir->string : ".", mapname);
	f = fopen(path, "rb");
	if (!f)
		return NULL;

	r = gi.TagMalloc(sizeof(rune_t), TAG_LEVEL);
	if (fread(&r->hdr, sizeof(r->hdr), 1, f) != 1 ||
	    r->hdr.magic != RUNE_MAGIC || r->hdr.version != RUNE_VERSION)
	{
		fclose(f);
		return NULL;
	}
	r->seeds = gi.TagMalloc(sizeof(rune_seed_t) * r->hdr.num_seeds, TAG_LEVEL);
	r->links = gi.TagMalloc(sizeof(rune_link_t) * r->hdr.num_links, TAG_LEVEL);
	if (fread(r->seeds, sizeof(rune_seed_t), r->hdr.num_seeds, f) != (size_t)r->hdr.num_seeds ||
	    fread(r->links, sizeof(rune_link_t), r->hdr.num_links, f) != (size_t)r->hdr.num_links)
	{
		fclose(f);
		return NULL;
	}
	fclose(f);

	/* per-seed link chains */
	r->first_link = gi.TagMalloc(sizeof(int) * r->hdr.num_seeds, TAG_LEVEL);
	r->next_link = gi.TagMalloc(sizeof(int) * r->hdr.num_links, TAG_LEVEL);
	for (i = 0; i < r->hdr.num_seeds; i++)
		r->first_link[i] = -1;
	for (i = r->hdr.num_links - 1; i >= 0; i--)
	{
		r->next_link[i] = r->first_link[r->links[i].from];
		r->first_link[r->links[i].from] = i;
	}
	return r;
}

int Rune_NearestSeed(rune_t *r, vec3_t p)
{
	int i, best = -1;
	float bestd = 1e30f;

	for (i = 0; i < r->hdr.num_seeds; i++)
	{
		vec3_t d;
		float dd;

		VectorSubtract(r->seeds[i].origin, p, d);
		if (d[2] > 96.0f || d[2] < -96.0f)
			continue;
		dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2] * 0.25f;
		if (dd < bestd)
		{
			bestd = dd;
			best = i;
		}
	}
	return best;
}

/* --------------------------------------------------------------- fields */

static qboolean SG_LevelSetup(void)
{
	if (sg_rune && Q_stricmp(sg_rune_map, level.mapname) == 0)
		return true;

	sg_rune = Rune_Load(level.mapname);
	if (!sg_rune)
	{
		gi.dprintf("slipgate: no rune for %s -- run 'sv rune' first\n",
		           level.mapname);
		return false;
	}
	strncpy(sg_rune_map, level.mapname, sizeof(sg_rune_map) - 1);

	if (!Fields_Setup(sg_rune))
	{
		gi.dprintf("slipgate: field setup failed (no flags?)\n");
		sg_rune = NULL;
		return false;
	}
	Caco_Reset();

	gi.dprintf("slipgate: rune %s, %d seeds, %d links, all fields up\n",
	           sg_rune->hdr.mapname, sg_rune->hdr.num_seeds,
	           sg_rune->hdr.num_links);
	return true;
}

/* ----------------------------------------------------------------- body */

/*
 * The weight tables: the one fitted component. Rows are roles; every other
 * number in the system is a measured fact. Starting values encode the
 * owner's specification directly -- attackers live for the enemy flag and
 * take what is on the way, defenders deny armour near home, carriers value
 * health and armour and the way home, everyone values interception when our
 * flag is out and believed seen.
 */
static const sg_weights_t sg_weight_table[SG_ROLES] = {
	/* objective  weap  armr  ammo  hlth  rune  powr   support intercept */
	{ 1.00f, { 0.35f, 0.30f, 0.20f, 0.15f, 0.20f, 0.40f }, 0.10f, 0.60f },  /* attack */
	{ 1.00f, { 0.30f, 0.50f, 0.25f, 0.20f, 0.15f, 0.10f }, 0.40f, 0.80f },  /* defend */
	{ 1.00f, { 0.10f, 0.45f, 0.20f, 0.50f, 0.10f, 0.05f }, 0.30f, 0.00f },  /* carry */
	/*
	 * recover: our flag is out there. The objective is the flag where belief
	 * puts it, the shopping list is the defender's (armour near home, not
	 * powerups across the map), and the thief's believed position outweighs
	 * everything else a non-carrier can be doing -- intercept above 1.
	 */
	{ 1.00f, { 0.30f, 0.50f, 0.25f, 0.20f, 0.15f, 0.10f }, 0.00f, 1.20f },  /* recover */
	/*
	 * escort: the objective IS the carrier's field, so carrier_support is 0
	 * (it would price the same field twice). Items stay modest so the escort
	 * does not wander off the carrier's road for them; intercept keeps the
	 * escort between the carrier and whoever is believed to be hunting.
	 */
	{ 1.00f, { 0.10f, 0.25f, 0.10f, 0.25f, 0.05f, 0.10f }, 0.00f, 0.80f },  /* escort */
};

/*
 * Detour worth: an item matters by how little it takes you off your road.
 *     value = worth / (1 + (cost_to_item + item_to_goal - direct) / scale)
 * All three terms are field lookups. An item dead on the route costs
 * nothing extra and pays full worth; one far off the road decays away.
 */
static float Detour_Value(int here, int cls, const int *goal_field,
                          float worth)
{
	int *ifld = sg_fields.item[cls];
	int to_item = ifld[here];
	int direct = goal_field[here];

	if (direct >= SG_FIELD_INF)
		return 0.0f;

	/*
	 * Where per-item fields exist (powerups, runes), the triangle is exact.
	 * per_item[cls][k] was flooded FROM item k, so it reads as cost from
	 * anywhere TO that item; the far leg is the goal field sampled at the
	 * item's own seed. The item that costs the least extra road wins -- not
	 * the nearest one, which is what a class field would have answered.
	 *
	 *     detour = cost_to_item + item_to_goal - direct
	 *     value  = worth / (1 + max(0, detour) / scale)
	 */
	if (sg_fields.per_item_count[cls] > 0)
	{
		float best = 0.0f;
		int k;

		for (k = 0; k < sg_fields.per_item_count[cls]; k++)
		{
			const int *kfld = sg_fields.per_item[cls][k];
			int kseed = sg_fields.per_item_seed[cls][k];
			int cost_to, item_to_goal, detour;
			float v;

			if (!kfld || kseed < 0)
				continue;
			cost_to = kfld[here];
			item_to_goal = goal_field[kseed];
			if (cost_to >= SG_FIELD_INF || item_to_goal >= SG_FIELD_INF)
				continue;

			detour = cost_to + item_to_goal - direct;
			if (detour < 0)
				detour = 0;      /* an item on the road is free, never a bonus */
			v = worth / (1.0f + (float)detour / 1500.0f);
			if (v > best)
				best = v;
		}
		return best;
	}

	/*
	 * The other classes are flooded per class only: item_to_goal is
	 * unknowable once many interchangeable items share one field, which gives
	 * cost to the NEAREST of them, so the triangle is approximated by to_item
	 * alone against scale. Honest limitation, recorded -- it holds for the
	 * classes whose members are interchangeable (a health box is a health
	 * box), which is why identity-bearing classes got per-item fields.
	 */
	if (to_item >= SG_FIELD_INF)
		return 0.0f;
	return worth / (1.0f + (float)to_item / 1500.0f);
}

/*
 * Role assignment: the owner's quota, then the two situational roles.
 *
 * Two in five defend (nearest-rounded), carrier counts toward defence, a side
 * of one attacks. Rank is slot order among SLIPGATE bots of the team, so the
 * assignment is stable frame to frame.
 *
 * Precedence, in order:
 *
 *   CARRY     I have the flag. Nothing else applies.
 *   DEFEND    my rank falls inside the quota -- the post is kept whatever
 *             else is happening; the situational roles are drawn from the
 *             attacking share only, which is what "attackers convert" means.
 *   RECOVER   our own flag is astray. EVERY attacker converts: getting it
 *             back outranks escorting, so no escort is named this frame.
 *   ESCORT    we have a live carrier who is not me: exactly one attacker,
 *             the lowest-ranked one that is not the carrier itself.
 *   ATTACK    everyone else.
 */
static sg_role_t SG_Role(sg_bot_t *bot, qboolean carrying)
{
	int team = bot->ent->client->ctf.teamnum;
	int size = 0, defenders_wanted, my_rank = 0, i;
	int my_client = (int)(bot->ent - g_edicts) - 1;
	int carrier_rank = -1;
	sg_belief_carrier_t *own = &sg_caco_team_belief.carrier[team - 1];

	if (carrying)
		return SG_ROLE_CARRY;

	for (i = 0; i < SG_MAXBOTS; i++)
	{
		if (!sg_bots[i].active || !sg_bots[i].ent || !sg_bots[i].ent->inuse)
			continue;
		if (sg_bots[i].ent->client->ctf.teamnum != team)
			continue;
		if (&sg_bots[i] == bot)
			my_rank = size;
		/* where in the ranking our own carrier sits, if it is one of ours */
		if (own->client >= 0 &&
		    (int)(sg_bots[i].ent - g_edicts) - 1 == own->client)
			carrier_rank = size;
		size++;
	}

	defenders_wanted = (size * 2 + 2) / 5;
	if (size <= 1)
		defenders_wanted = 0;

	/* a live carrier on our side counts toward the defensive share */
	if (own->client >= 0)
		defenders_wanted--;

	if (my_rank < defenders_wanted)
		return SG_ROLE_DEFEND;

	/*
	 * Our flag is astray by common knowledge (the HUD tells everyone). The
	 * attacking share turns around: the goal becomes our flag where belief
	 * puts it, and the believed thief is priced heavily by the recover row.
	 */
	if (sg_caco_team_belief.flag[team - 1].state == SG_FLAG_ASTRAY)
		return SG_ROLE_RECOVER;

	/*
	 * One escort for a live carrier of ours. The support field has to have
	 * been flooded at least once this level for the role to mean anything --
	 * before that it is infinite everywhere and an escort would have no
	 * surface to descend, so those bots keep attacking.
	 */
	if (own->client >= 0 && own->client != my_client &&
	    sg_fields.our_carrier_valid[team - 1])
	{
		int escort_rank = (defenders_wanted > 0) ? defenders_wanted : 0;

		/* the carrier plays its own role; the escort is the next one down */
		if (escort_rank == carrier_rank)
			escort_rank++;
		if (my_rank == escort_rank)
			return SG_ROLE_ESCORT;
	}

	return SG_ROLE_ATTACK;
}

/*
 * The surface at one seed, for one bot: weighted composition of every
 * basis field. LOWER is better (fields are costs); items and support
 * subtract because they add value. This is V(x | bot) from the design,
 * evaluated at the handful of seeds one step away.
 */
static float Surface_At(int seed, const sg_weights_t *w,
                        const int *goal_field, const int *support,
                        const int *intercept)
{
	float v;
	int c;

	if (goal_field[seed] >= SG_FIELD_INF)
		return 1e30f;

	v = w->objective * (float)goal_field[seed];

	for (c = 0; c < SG_FIELD_CLASSES; c++)
		if (w->item[c] > 0.0f)
			v -= 1500.0f * Detour_Value(seed, c, goal_field, w->item[c]);

	if (support && w->carrier_support > 0.0f && support[seed] < SG_FIELD_INF)
		v += w->carrier_support * (float)support[seed];

	if (intercept && w->intercept > 0.0f && intercept[seed] < SG_FIELD_INF)
		v += w->intercept * (float)intercept[seed];

	return v;
}

/*
 * ------------------------------------------------------------ the policy
 *
 * Movement, closed-form, from the engine rather than from feel. This is the
 * legacy body's proven policy (bl_main.c:92-175, BotAirStrafe and its
 * derivation) moved into the SLIPGATE body unchanged in substance; only the
 * inputs are re-expressed in SLIPGATE terms -- the route direction is the
 * heading the surface descent chose, not a botlib bi->dir.
 *
 * pm_maxspeed caps wishspeed, not velocity. PM_Accelerate adds
 *
 *     accelspeed = accel * frametime * wishspeed
 *
 * along wishdir for as long as addspeed = wishspeed - (velocity . wishdir)
 * stays positive, so an input held off the direction of travel keeps that
 * term alive and the speed climbing. The smallest angle that still leaves
 * addspeed at the cap is
 *
 *     cos(theta) = (wishspeed - accelspeed) / speed
 *
 * On the ground accel is 10 and the limit is friction, speed * 6 * frametime,
 * which scales with speed while the gain at the best angle does not: they meet
 * near 370. Driving forwardmove straight down the heading converges on 300 and
 * stops there. In the air accel is 1 -- a tenth the rate, but no friction and
 * therefore no ceiling.
 *
 * Which way to lean is not a coin flip: leaning toward the side the route
 * turns accelerates and steers at once, so the velocity is pulled onto the
 * path instead of away from it. The view is not involved -- the bot names the
 * direction and decomposes it against the view it is already holding, so none
 * of this costs any aim.
 */
static void SG_Strafe(usercmd_t *cmd, vec3_t fwd, vec3_t right,
                      vec3_t vel, vec3_t dir,
                      float speed2d, float frametime, float accel)
{
	vec3_t	vdir, d;
	float	wishspeed = 300.0f;     /* pm_maxspeed clamps wishspeed to this */
	float	accelspeed, c, th, sn, cs, cross;

	if (speed2d < 1.0f)
		return;

	accelspeed = accel * frametime * wishspeed;

	/*
	 * Below wishspeed - accelspeed there is no angle to find: addspeed is
	 * already saturated pointing straight down the route, so the input that
	 * accelerates hardest is also the one that steers, and leaning off it
	 * would only trade heading for nothing. Leave the caller's plain forward
	 * alone -- this is the whole of the low-speed case, and it is why the
	 * strafe is not a mode the bot enters and leaves.
	 */
	if (speed2d <= wishspeed - accelspeed)
		return;

	c = (wishspeed - accelspeed) / speed2d;
	if (c > 1.0f) c = 1.0f;
	if (c < -1.0f) c = -1.0f;
	th = acosf(c);

	vdir[0] = vel[0] / speed2d;
	vdir[1] = vel[1] / speed2d;
	vdir[2] = 0.0f;

	/* lean the way the route turns, so the gain also steers */
	cross = vdir[0] * dir[1] - vdir[1] * dir[0];
	if (cross < 0.0f)
		th = -th;

	sn = sinf(th);
	cs = cosf(th);
	d[0] = vdir[0] * cs - vdir[1] * sn;
	d[1] = vdir[0] * sn + vdir[1] * cs;
	d[2] = 0.0f;

	/*
	 * Decomposed against the basis pmove will actually build (pitch/3, see
	 * the caller), so the engine reconstructs the direction that was asked
	 * for. 400 on both axes before the clamp: wishvel is scaled down to
	 * pm_maxspeed anyway, and what matters is the direction.
	 */
	cmd->forwardmove = (short)(DotProduct(fwd, d) * 400.0f);
	cmd->sidemove = (short)(DotProduct(right, d) * 400.0f);
}

/*
 * One physics step of movement, decided from where the bot actually is.
 *
 * The caller has already put the plain command in place -- forward down the
 * view, no strafe -- so every early return here leaves honest, unaltered
 * running. Three things can be added to it:
 *
 *   ground strafe   accel 10, the strong half of this engine
 *   air strafe      accel 1, the same derivation, only A changes
 *   landing jump    Pmove runs PM_CheckJump before PM_Friction, and a jump
 *                   clears groundentity -- which is the condition PM_Friction
 *                   tests before applying any ground friction at all. A jump
 *                   issued on the step the bot touches down therefore pays no
 *                   friction; one issued a step late pays speed * 6 * ft.
 *                   That single step is the whole of bunny hopping here.
 */
static void SG_MovePolicy(edict_t *e, usercmd_t *cmd, vec3_t fwd,
                          vec3_t right, vec3_t dir,
                          qboolean open_ahead, qboolean run_link,
                          float frametime)
{
	float	sp2, sp, toward;
	int		pmf = e->client->ps.pmove.pm_flags;

	if (e->waterlevel > 1 || (pmf & PMF_DUCKED))
		return;

	sp2 = e->velocity[0] * e->velocity[0] + e->velocity[1] * e->velocity[1];
	if (sp2 < 200.0f * 200.0f)
		return;                 /* below this, straight ahead is the fastest
		                         * thing there is: addspeed is wide open */
	sp = sqrtf(sp2);

	/*
	 * The strafe leans off the direction of TRAVEL, so travel has to be
	 * roughly where the route wants to go before leaning off it means
	 * anything. A bot that needs to turn ninety degrees should turn, not
	 * harvest acceleration into the wall it is heading for.
	 */
	toward = (e->velocity[0] * dir[0] + e->velocity[1] * dir[1]) / sp;
	if (toward < 0.5f)
		return;

	if (e->groundentity)
	{
		/*
		 * Tap, never hold: PM_CheckJump sets PMF_JUMP_HELD when it fires and
		 * refuses every jump after it until a command arrives with upmove
		 * under 10. The caller releases after every step.
		 */
		if (run_link && open_ahead && sp > 320.0f && !(pmf & PMF_TIME_LAND))
			cmd->upmove = 400;

		SG_Strafe(cmd, fwd, right, e->velocity, dir, sp, frametime, 10.0f);
	}
	else
		SG_Strafe(cmd, fwd, right, e->velocity, dir, sp, frametime, 1.0f);
}

static void SG_BotThink(sg_bot_t *bot)
{
	edict_t *e = bot->ent;
	usercmd_t cmd;
	const int *goal_field, *support = NULL, *intercept = NULL;
	const sg_weights_t *w;
	sg_role_t role;
	int team, li, bestlink = -1;
	float bestval;
	vec3_t want, d;
	float yaw;
	qboolean carrying;
	static int intercept_field[SG_MAX_SEEDS];

	/* movement policy state for this frame */
	vec3_t		basis_fwd, basis_right;     /* the basis pmove will build */
	vec3_t		move_dir;                   /* heading the route wants */
	float		view_yaw = 0.0f, view_pitch = 0.0f;
	qboolean	have_move = false;          /* a direction to travel at all */
	qboolean	open_ahead = false;         /* room in front to hop into */
	qboolean	run_link = false;           /* chosen link is ground running */
	qboolean	precision = false;          /* final approach: no tricks */
	qboolean	hold_post = false;          /* defender at its stand: guard */
	float		post_yaw = 0.0f;            /* facing the likeliest approach */
	int			door_hold = 0;              /* rotating door ahead: 1 stand,
	                                         * 2 back out of its swing arc */
	edict_t		*door_ent = NULL;           /* which door is being waited on */
	qboolean	drop_yaw_locked = false;    /* executing a drop: no fan */
	float		drop_yaw = 0.0f;
	int			sub_steps = 1, sub_msec = 0;

	memset(&cmd, 0, sizeof(cmd));
	cmd.msec = 100;

	if (e->deadflag)
	{
		bot->seed = -1;
		cmd.buttons = BUTTON_ATTACK;
		ClientThink(e, &cmd);
		return;
	}

	/* my eyes feed the team belief before I decide from it */
	Caco_See(sg_rune, e);

	team = e->client->ctf.teamnum;
	/* LMCTF has ONE flag item: "Enemy Flag" (g_items.c:2478). Carrying is
	 * the same inventory test ctf_flagtouch itself makes. */
	{
		static gitem_t *flagitem;
		if (!flagitem)
			flagitem = FindItem("Enemy Flag");
		carrying = flagitem &&
		           e->client->pers.inventory[ITEM_INDEX(flagitem)] > 0;
	}

	role = SG_Role(bot, carrying);
	w = &sg_weight_table[role];

	/*
	 * The role's principal field:
	 *   carrier  -> own stand (the capture point)
	 *   defender -> own stand's surroundings (the home field IS the post)
	 *   recover  -> OUR flag where belief puts it: the -now field for our own
	 *               flag, which floods from the believed position when it is
	 *               astray and from home otherwise
	 *   escort   -> our carrier's believed position (the support field, used
	 *               here as the objective rather than as a side term)
	 *   attacker -> the enemy flag WHERE BELIEF PUTS IT (home stand, or the
	 *               spot it was last seen lying, via the -now field)
	 * When our flag is astray and its taker was seen, the intercept field
	 * (their believed position) joins the composition for non-carriers.
	 */
	if (role == SG_ROLE_CARRY || role == SG_ROLE_DEFEND)
		goal_field = (team == CTF_TEAM_RED) ? sg_fields.to_red_flag
		                                    : sg_fields.to_blue_flag;
	else if (role == SG_ROLE_RECOVER)
		goal_field = (team == CTF_TEAM_RED) ? sg_fields.to_red_flag_now
		                                    : sg_fields.to_blue_flag_now;
	else if (role == SG_ROLE_ESCORT)
		goal_field = sg_fields.our_carrier[team - 1];
	else
		goal_field = (team == CTF_TEAM_RED) ? sg_fields.to_blue_flag_now
		                                    : sg_fields.to_red_flag_now;

	if (role != SG_ROLE_CARRY)
	{
		sg_belief_carrier_t *ec = &sg_caco_team_belief.enemy_carrier[team - 1];

		support = sg_fields.our_carrier[team - 1];
		if (ec->seed >= 0)
		{
			int cost = 0;

			/* believed thief position: flood on demand, cheap at our size */
			Field_Flood(sg_rune, intercept_field, &ec->seed, &cost, 1);
			intercept = intercept_field;
		}
	}

	/* where am I on the rune? */
	VectorSubtract(e->s.origin, bot->last_origin, d);
	if (bot->seed < 0 || VectorLength(d) > 48.0f)
	{
		bot->seed = Rune_NearestSeed(sg_rune, e->s.origin);
		VectorCopy(e->s.origin, bot->last_origin);
	}
	if (bot->seed < 0)
	{
		ClientThink(e, &cmd);
		return;
	}

	/*
	 * The precision case: no tricks on the final approach.
	 *
	 * A flag is a thirty-unit box and a hop chain covers eight hundred units a
	 * second; the legacy bots arrived with a second of route left and sailed
	 * straight over the top, lap after lap. The legacy adapter suppressed the
	 * tricks within about 700 units of a must-touch goal (bl_main.c:451-464).
	 * The SLIPGATE field is denominated in real milliseconds of traversal, so
	 * the same idea is stated in time: inside a second and a half of the
	 * objective, run plainly and be able to stop. Speed serves the objective.
	 */
	precision = (goal_field[bot->seed] < 1500);

	/* descend the surface: my seed vs every seed one proven link away */
	bestval = Surface_At(bot->seed, w, goal_field, support, intercept);
	for (li = sg_rune->first_link[bot->seed]; li >= 0; li = sg_rune->next_link[li])
	{
		rune_link_t *l = &sg_rune->links[li];
		float v = Surface_At(l->to, w, goal_field, support, intercept);
		int b;

		for (b = 0; b < SG_BL_MAX; b++)
			if (bot->bl_link[b] == li && bot->bl_until[b] > level.time)
				break;
		if (b < SG_BL_MAX)
			continue;               /* shelved: the body could not run it */

		if (v < bestval)
		{
			bestval = v;
			bestlink = li;
		}
	}

	/*
	 * Commitment. The composed surface has saddles -- goal one way, a
	 * shotgun the other, health a third, the item terms refreshed every
	 * second -- and a per-frame argmin at a saddle flaps between near-equal
	 * links. Both t2 attackers churned a full match at one such point
	 * (seeds 429/430, the room north of the rotating door). A chosen step
	 * is HELD until it finishes, times out, or gets shelved; the surface
	 * proposes, the body disposes.
	 */
	if (bot->commit_link >= 0 && bot->commit_link < sg_rune->hdr.num_links)
	{
		rune_link_t *cl = &sg_rune->links[bot->commit_link];
		qboolean drop_commit = false;
		int b;

		VectorSubtract(sg_rune->seeds[cl->to].origin, e->s.origin, d);
		if (bot->seed == cl->to || VectorLength(d) < 48.0f)
			drop_commit = true;             /* arrived: step complete */
		/* or overachieved: hook landings scatter up to ~234 units from the
		 * dest seed -- if the field already prices this spot at or below
		 * the destination, the step served its purpose (holding on would
		 * re-fire the hook from its own landing zone; match 6 bounced at
		 * goal 9979 all game doing exactly that) */
		if (goal_field[bot->seed] <= goal_field[cl->to])
			drop_commit = true;
		if (level.time > bot->commit_until)
			drop_commit = true;
		for (b = 0; b < SG_BL_MAX; b++)
			if (bot->bl_link[b] == bot->commit_link &&
			    bot->bl_until[b] > level.time)
				drop_commit = true;
		if (drop_commit)
			bot->commit_link = -1;
		else
			bestlink = bot->commit_link;
	}
	if (bot->commit_link < 0 && bestlink >= 0)
	{
		bot->commit_link = bestlink;
		bot->commit_until = level.time + 3.0f;
	}

	/*
	 * Progress watch. The same link chosen for four seconds while the bot
	 * stays inside a 96-unit ball is an orbit -- a lip behind a railing,
	 * a door the rune cannot see, a ledge the feelers cannot round. The
	 * cause does not matter here: shelve the link for thirty seconds and
	 * the surface reroutes through the next-best gradient. (Field orbited
	 * one drop lip for a full match; the generator fix removes that class,
	 * this removes every class.)
	 */
	if (bestlink >= 0 && bestlink == bot->watch_link &&
	    !(role == SG_ROLE_DEFEND && goal_field[bot->seed] < 400))
	{
		VectorSubtract(e->s.origin, bot->watch_org, d);
		if (VectorLength(d) > 96.0f)
		{
			VectorCopy(e->s.origin, bot->watch_org);
			bot->watch_since = level.time;
		}
		else if (level.time - bot->watch_since > 4.0f)
		{
			int b, oldest = 0;

			for (b = 0; b < SG_BL_MAX; b++)
				if (bot->bl_until[b] < bot->bl_until[oldest])
					oldest = b;
			bot->bl_link[oldest] = bestlink;
			bot->bl_until[oldest] = level.time + 30.0f;
			if (gi.cvar("sg_debug", "0", 0)->value)
				gi.dprintf("SHELVE %s link=%d at seed=%d\n",
				           e->client->pers.netname, bestlink, bot->seed);
			bot->watch_link = -1;
		}
	}
	else
	{
		bot->watch_link = bestlink;
		bot->watch_since = level.time;
		VectorCopy(e->s.origin, bot->watch_org);
	}

	/*
	 * A defender that has reached its post stands it. The stand is the
	 * surface's minimum, so descent has nowhere left to go -- pushing
	 * forwardmove into the pedestal just grinds the wall (Caco spent 66
	 * straight seconds at spd=68 doing exactly that). Inside 400ms of the
	 * post: stop, and face the seed an attacker descending on the stand
	 * would arrive through -- the neighbor whose field value sits closest
	 * above this one. Combat still owns the view the moment anyone shows.
	 */
	if (role == SG_ROLE_DEFEND && goal_field[bot->seed] < 400)
	{
		int facev = 0x7fffffff, face = -1;

		for (li = sg_rune->first_link[bot->seed]; li >= 0;
		     li = sg_rune->next_link[li])
		{
			rune_link_t *l = &sg_rune->links[li];
			int v = goal_field[l->to];

			if (v > goal_field[bot->seed] && v < facev)
			{
				facev = v;
				face = l->to;
			}
		}
		hold_post = true;
		if (face >= 0)
		{
			VectorSubtract(sg_rune->seeds[face].origin, e->s.origin, d);
			post_yaw = atan2f(d[1], d[0]) * 180.0f / M_PI;
		}
	}

	/*
	 * The surface has a gradient EVERYWHERE. Where the rune is proven, the
	 * gradient is the best outgoing link. Where it is not -- field infinite,
	 * no improving link, graph hole -- the gradient degrades to the local
	 * one: straight at the goal, deflected around whatever the feelers hit.
	 * A player with no knowledge of the map still runs toward the enemy
	 * base; a bot that stands still because its database has a hole is not
	 * descending a surface, it is worshipping a graph.
	 */
	{
		vec3_t aim;
		qboolean have_aim = false;
		qboolean jump_now = false;

		/*
		 * Just let go of a rope: the prover steered forwardmove 400 at the
		 * destination until the phantom grounded (sg_rune.c:529-534), and
		 * the link was only recorded because that landing worked. The body
		 * flies the same approach instead of falling back down the wall it
		 * just climbed.
		 */
		if (bot->hook_phase == 3)
		{
			if (e->groundentity || level.time > bot->hook_deadline)
			{
				if (e->groundentity && gi.cvar("sg_debug", "0", 0)->value)
				{
					vec3_t ld;

					VectorSubtract(bot->hook_dest, e->s.origin, ld);
					gi.dprintf("HOOKLAND %s dist=%.0f dz=%.0f\n",
					           e->client->pers.netname,
					           sqrtf(ld[0] * ld[0] + ld[1] * ld[1]), ld[2]);
				}
				bot->hook_phase = 0;
				/* a rope ride ENDS its commitment: wherever this landing
				 * is, the next step is argued fresh from here */
				bot->commit_link = -1;
			}
			else
			{
				VectorCopy(bot->hook_dest, aim);
				have_aim = true;
			}
		}

		if (!have_aim && bestlink >= 0)
		{
			rune_link_t *l = &sg_rune->links[bestlink];

			VectorCopy(sg_rune->seeds[l->to].origin, aim);
			have_aim = true;
			if (l->action == RL_JUMP && e->groundentity)
				jump_now = true;
			/* the landing hop belongs on running ground, not on a link
			 * whose traversal is itself a jump, a drop, a rope or a swim */
			if (l->action == RL_RUN)
				run_link = true;

			/*
			 * A hook link executes the way the rune proved it: aim at the
			 * STORED anchor, fire, ride the flat-800 pull, release near
			 * the destination or inside the brake band (the p_weapon.c
			 * ladder starts at 120), then steer the fall onto the landing.
			 * The view is the aim: LMCTF's Weapon_Hook_Fire fires along
			 * v_angle.
			 */
			if (l->action == RL_HOOK && bot->hook_phase == 0)
			{
				vec3_t ad;
				float alen;

				VectorSubtract(l->anchor, e->s.origin, ad);
				alen = VectorLength(ad);
				if (alen > 1.0f)
				{
					VectorCopy(l->anchor, bot->hook_anchor);
					VectorCopy(sg_rune->seeds[l->to].origin, bot->hook_dest);
					bot->hook_phase = 1;
					bot->hook_deadline = level.time + alen / 800.0f + 1.5f;
				}
			}

			/* a drop link goes via its stored lip, not the far endpoint */
			if (l->action == RL_DROP)
			{
				vec3_t lipd;
				float liph;

				VectorSubtract(l->anchor, e->s.origin, lipd);
				lipd[2] = 0.0f;
				liph = VectorLength(lipd);
				if (liph > 24.0f)
					VectorCopy(l->anchor, aim);
				/*
				 * The whole drop executes the way the prover walked it:
				 * straight at the lip, no fan (ProveDrop's approach walk
				 * steers dead at the lip, sg_rune.c), then off along the
				 * RECORDED heading (dd_last_heading, sg_rune.c:734). The
				 * fan, given a railing beside the gap, deflects off the
				 * exact line the proof demonstrated -- Phase orbited a
				 * balcony's proven drops 60-140 units from their lips,
				 * with a 96-unit lock radius it never entered.
				 */
				drop_yaw_locked = true;
				drop_yaw = (liph > 24.0f)
				               ? atan2f(lipd[1], lipd[0]) * 180.0f / M_PI
				               : l->heading * (360.0f / 256.0f);
			}
		}

		/* while aiming to fire, the cmd angles ARE the anchor bearing --
		 * this overrides the navigation view for exactly one frame */
		if (bot->hook_phase == 1)
		{
			vec3_t ad;
			float alen, ay, ap;

			VectorSubtract(bot->hook_anchor, e->s.origin, ad);
			alen = VectorLength(ad);
			ay = atan2f(ad[1], ad[0]) * 180.0f / M_PI;
			ap = -asinf(ad[2] / (alen > 1.0f ? alen : 1.0f)) * 180.0f / M_PI;
			cmd.angles[YAW] = ANGLE2SHORT(ay)
			                - e->client->ps.pmove.delta_angles[YAW];
			cmd.angles[PITCH] = ANGLE2SHORT(ap)
			                - e->client->ps.pmove.delta_angles[PITCH];
			view_yaw = ay;
			view_pitch = ap;
		}
		else if (bot->seed >= 0)
		{
			/*
			 * Off the known surface: the field is infinite here. The right
			 * move is not the goal line -- greedy steering at a distant
			 * goal orbits concave geometry forever, and did, for four
			 * minutes straight. It is the shortest walk ONTO the surface:
			 * the nearest seed where the field turns finite. From there
			 * the links take over.
			 */
			int i, best = -1;
			float bestd = 1e30f;

			for (i = 0; i < sg_rune->hdr.num_seeds; i++)
			{
				vec3_t dd;
				float dsq;

				if (goal_field[i] >= SG_FIELD_INF)
					continue;
				VectorSubtract(sg_rune->seeds[i].origin, e->s.origin, dd);
				dsq = dd[0] * dd[0] + dd[1] * dd[1] + dd[2] * dd[2] * 4.0f;
				if (dsq < bestd)
				{
					bestd = dsq;
					best = i;
				}
			}
			if (best >= 0)
			{
				VectorCopy(sg_rune->seeds[best].origin, aim);
				have_aim = true;
			}
		}

		if (!have_aim)
		{
			/* last resort: the goal itself, by belief */
			edict_t *gf = NULL;

			if (role == SG_ROLE_ATTACK)
			{
				/* enemy stand position is common knowledge */
				gf = G_Find(NULL, FOFS(classname),
				            (team == CTF_TEAM_RED) ? "info_flag_blue"
				                                   : "info_flag_red");
			}
			else
			{
				gf = G_Find(NULL, FOFS(classname),
				            (team == CTF_TEAM_RED) ? "info_flag_red"
				                                   : "info_flag_blue");
			}
			if (gf)
			{
				VectorCopy(gf->s.origin, aim);
				have_aim = true;
			}
		}

		if (have_aim)
		{
			vec3_t fwd, probe, right2;
			trace_t tr;
			float best_open = -1.0f;
			float try_yaw, base_yaw, chosen_yaw;
			int k;

			VectorSubtract(aim, e->s.origin, want);
			base_yaw = atan2f(want[1], want[0]) * 180.0f / M_PI;
			chosen_yaw = base_yaw;

			/*
			 * Feelers: try the goal heading first, then fan out. Take the
			 * most open heading nearest the goal line. This is what makes
			 * the local gradient walk around a doorframe instead of into
			 * it.
			 */
			for (k = 0; k < 7; k++)
			{
				static const float fan[7] = { 0, -30, 30, -60, 60, -100, 100 };
				float score;

				try_yaw = (base_yaw + fan[k]) * M_PI / 180.0f;
				fwd[0] = cosf(try_yaw); fwd[1] = sinf(try_yaw); fwd[2] = 0;
				VectorMA(e->s.origin, 96.0f, fwd, probe);
				probe[2] += 8.0f;
				tr = gi.trace(e->s.origin, e->mins, e->maxs, probe,
				              e, MASK_PLAYERSOLID);
				/*
				 * A closed door is not a wall: walking into it (its
				 * auto-spawned trigger, g_func.c Think_SpawnDoorTrigger,
				 * reaches ~60 units out) is precisely how it opens. The
				 * rune proved these routes with doors held open; a feeler
				 * that deflects off a door steers away from the only
				 * action that makes the route real. Every shelve cluster
				 * in match 7 sat beside a door complex. Doors that only a
				 * button opens will fail to yield and the progress watch
				 * shelves that link -- the net below the honesty.
				 */
				if (tr.fraction < 1.0f && tr.ent && tr.ent->classname &&
				    strncmp(tr.ent->classname, "func_door", 9) == 0)
				{
					int dd;
					qboolean dead = false;

					/* a door that already refused to yield from here is a
					 * wall: no fraction override, the fan walks around */
					for (dd = 0; dd < SG_DEAD_DOORS; dd++)
						if (bot->dead_door[dd] == tr.ent &&
						    bot->dead_door_until[dd] > level.time)
							dead = true;
					if (!dead)
					{
						/*
						 * A ROTATING door swings through the space in
						 * front of it; a body pressing at it blocks the
						 * swing and the door reverses shut, forever
						 * (match 8: one bot, 75 shelves, jamming the door
						 * with its own face). Stand outside the arc, or
						 * back out of it, and let the floor trigger swing
						 * it. Sliding doors travel out of the path and
						 * are safe to press.
						 */
						if (k == 0 && strcmp(tr.ent->classname,
						                     "func_door_rotating") == 0)
						{
							door_hold = (tr.fraction * 96.0f < 64.0f) ? 2 : 1;
							door_ent = tr.ent;
						}
						tr.fraction = 1.0f;
					}
				}
				score = tr.fraction * (1.0f - 0.05f * (k > 0) * (k + 1));
				if (score > best_open)
				{
					best_open = score;
					chosen_yaw = base_yaw + fan[k];
				}
				if (tr.fraction >= 1.0f && k == 0)
					break;              /* goal line is open: take it */
			}

			/* at a drop lip the proven walk-off heading overrides the fan:
			 * the proof is a line, and the line is the record's */
			if (drop_yaw_locked)
				chosen_yaw = drop_yaw;

			cmd.angles[YAW] = ANGLE2SHORT(chosen_yaw)
			                - e->client->ps.pmove.delta_angles[YAW];
			cmd.angles[PITCH] = -e->client->ps.pmove.delta_angles[PITCH];
			cmd.forwardmove = 400;
			if (jump_now)
				cmd.upmove = 400;

			view_yaw = chosen_yaw;
			view_pitch = 0.0f;
			move_dir[0] = cosf(chosen_yaw * (float)M_PI / 180.0f);
			move_dir[1] = sinf(chosen_yaw * (float)M_PI / 180.0f);
			move_dir[2] = 0.0f;
			have_move = true;

			/*
			 * Room to hop into. A landing jump commits the bot to whatever
			 * speed and heading it left with for the whole arc, so it is only
			 * worth taking where the way ahead is actually clear -- the same
			 * player-box trace the feelers use, run further out along the
			 * heading that was chosen.
			 */
			VectorMA(e->s.origin, 160.0f, move_dir, probe);
			probe[2] += 8.0f;
			tr = gi.trace(e->s.origin, e->mins, e->maxs, probe,
			              e, MASK_PLAYERSOLID);
			/* same rule as the feelers: a door ahead is not a wall, but
			 * do NOT hop at one -- arrive on foot, inside its trigger */
			open_ahead = (tr.fraction >= 1.0f);

			VectorSubtract(e->s.origin, bot->last_origin, d);
			if (VectorLength(d) < 4.0f)
			{
				bot->stuck_time += 0.1f;
				if (bot->stuck_time > 1.0f && e->groundentity)
					cmd.upmove = 400;   /* hop what the feelers missed */
			}
			else
				bot->stuck_time = 0.0f;
		}

		/* rotating door working its arc: hold ground (or yield the arc),
		 * keep facing it, and let the trigger under our feet do the work.
		 * A door still shut after 2.5 seconds is not going to open from
		 * this side (no trigger reaches here): remember it as a wall for
		 * thirty seconds and let the surface reroute. */
		if (door_hold && have_move)
		{
			cmd.forwardmove = (door_hold == 2) ? -200 : 0;
			cmd.upmove = 0;

			if (door_ent != bot->door_hold_ent)
			{
				bot->door_hold_ent = door_ent;
				bot->door_hold_since = level.time;
			}
			else if (level.time - bot->door_hold_since > 2.5f)
			{
				int dd, oldest = 0;

				for (dd = 0; dd < SG_DEAD_DOORS; dd++)
					if (bot->dead_door_until[dd] < bot->dead_door_until[oldest])
						oldest = dd;
				bot->dead_door[oldest] = door_ent;
				bot->dead_door_until[oldest] = level.time + 30.0f;
				bot->door_hold_ent = NULL;
				if (gi.cvar("sg_debug", "0", 0)->value)
					gi.dprintf("DEADDOOR %s at (%.0f %.0f %.0f)\n",
					           e->client->pers.netname, e->s.origin[0],
					           e->s.origin[1], e->s.origin[2]);
			}
		}
		else
			bot->door_hold_ent = NULL;

		/* on post: whatever the descent wanted, guard duty overrides it */
		if (hold_post)
		{
			cmd.forwardmove = 0;
			cmd.sidemove = 0;
			cmd.upmove = 0;
			cmd.angles[YAW] = ANGLE2SHORT(post_yaw)
			                - e->client->ps.pmove.delta_angles[YAW];
			cmd.angles[PITCH] = -e->client->ps.pmove.delta_angles[PITCH];
			view_yaw = post_yaw;
			view_pitch = 0.0f;
			have_move = false;
			bot->stuck_time = 0.0f;
		}
	}

	/*
	 * The basis the engine will actually use, not a convenient one.
	 *
	 * Pmove builds it before every land move (pmove.c, PM_AirMove; quoted at
	 * bl_main.c:347-370): the view angles with PITCH divided by three, and
	 * then wishvel[i] = forward[i]*fmove + right[i]*smove for i in {0,1}. So
	 * forward's horizontal length is scaled by cos(pitch/3) while right stays
	 * fully horizontal. Solving against a pitch-zero basis makes the engine
	 * reconstruct a different direction than the one asked for -- shorter, and
	 * skewed toward the strafe axis, which lowers wishspeed and with it the
	 * acceleration. Here the navigation view is pitch zero, so the division
	 * changes nothing today; it is written the engine's way so that it stays
	 * correct the moment the body pitches.
	 */
	{
		vec3_t basis;

		basis[PITCH] = view_pitch;
		if (basis[PITCH] > 180.0f)
			basis[PITCH] -= 360.0f;
		basis[PITCH] /= 3.0f;
		basis[YAW] = view_yaw;
		basis[ROLL] = 0.0f;
		AngleVectors(basis, basis_fwd, basis_right, NULL);
	}

	/*
	 * Execute the frame in physics steps, and decide the movement again on
	 * every one of them.
	 *
	 * The angle that keeps PM_Accelerate paying depends on the current
	 * velocity, and the velocity is exactly what the previous step just
	 * changed: a command computed at 300 is the wrong command by the time the
	 * bot is doing 500. The landing jump wants the very step the bot touches
	 * down, and a bot that only gets one chance per tenth of a second spends
	 * far longer on the floor than a player whose client sends a dozen
	 * commands in the same window. A real client does this continuously; this
	 * is the bot catching up to that, not overtaking it.
	 *
	 * The clock is not touched. msec is the frame's real time, and the steps
	 * are that integer split with the remainder spread over the first few, so
	 * they sum to exactly what passed -- eight steps of 13ms for a 100ms frame
	 * would be 104ms of simulation for 100ms of play, which is free speed
	 * rather than finer movement. Finer decisions, never a longer clock.
	 */
	{
		int		total = cmd.msec;
		int		sub = (int)gi.cvar("sg_subframes", "8", 0)->value;
		int		base, rem, step;
		short	plain_forward = cmd.forwardmove;
		short	nav_jump = cmd.upmove;

		if (sub < 1)
			sub = 1;
		if (sub > total)
			sub = total;            /* a step cannot be shorter than 1ms */
		base = total / sub;
		rem = total % sub;
		sub_steps = sub;

		/*
		 * Combat rides the frame's command: view and trigger only, movement
		 * untouched. It writes the base cmd once and every subframe inherits
		 * it. Ordering rule from sg_combat.h: at hook_phase 1 the cmd angles
		 * ARE the anchor bearing (the rope fires along v_angle), so combat
		 * must not steal the view that frame.
		 */
		if (bot->hook_phase == 0)
		{
			qboolean engaged = false;

			SG_CombatFrame(e, &cmd, &engaged);
		}

		for (step = 0; step < sub; step++)
		{
			cmd.msec = (byte)(base + (step < rem ? 1 : 0));
			if (!cmd.msec)
				continue;
			sub_msec = cmd.msec;

			/*
			 * Every step starts from the plain command -- forward down the
			 * view, no strafe -- so anything the policy declines to do leaves
			 * ordinary running behind. The navigation jump (a jump LINK, or
			 * the stuck hop) belongs to the frame, not to every step, so it
			 * goes in once and is released like any other.
			 */
			cmd.forwardmove = plain_forward;
			cmd.sidemove = 0;
			if (step == 0)
				cmd.upmove = nav_jump;

			/*
			 * No tricks while a rope is out -- the hook SETS velocity, so
			 * there is nothing for an off-axis input to accumulate -- and
			 * none on the final approach, where being able to stop on the
			 * flag is worth more than the speed.
			 */
			if (have_move && !precision && bot->hook_phase == 0)
				SG_MovePolicy(e, &cmd, basis_fwd, basis_right, move_dir,
				              open_ahead, run_link,
				              (float)cmd.msec / 1000.0f);

			ClientThink(e, &cmd);

			/*
			 * Let go of the jump. PM_CheckJump clears PMF_JUMP_HELD only when
			 * a command arrives with upmove under 10 and refuses to jump at
			 * all while it is set, so holding the key buys nothing and costs
			 * the next hop. The release lands inside the same tenth of a
			 * second as the press.
			 */
			if (cmd.upmove >= 10)
				cmd.upmove = 0;
		}
		cmd.msec = (byte)sub_msec;
	}

	/*
	 * Hook lifecycle, after the think so v_angle reflects this frame's
	 * aim. Fire with the game's own Cmd_Hook_f -- the same entry the
	 * console command uses -- and release before the rope enters the
	 * p_weapon.c brake band (ladder starts at 120; 200 leaves a frame of
	 * margin at pull speed). A rope that never attached by its deadline
	 * is cut loose.
	 */
	{
		void Cmd_Hook_f(edict_t *ent);

		/*
		 * A rope this bot does not think it owns is a rope it cannot ever
		 * release: g_cmds.c's Cmd_Unhook_f, when the grapple happens to be
		 * pers.weapon, only forces -attack and NEVER aborts -- the live
		 * hook's short-rope dead-stop then overwrites velocity with ~0
		 * every frame (p_weapon.c:2099-2104) and p_client.c:2834 zeroes
		 * gravity, freezing the bot in place for good (Trace, 96 seconds,
		 * 4v4 match). The bot releases through ctf_hook_abort directly --
		 * the same unconditional abort p_weapon.c itself calls -- and this
		 * guard clears any rope left over from a path we did not arm.
		 */
		if (bot->hook_phase == 0 && e->client->hookstate != 0)
			ctf_hook_abort(e);

		if (bot->hook_phase == 1)
		{
			Cmd_Hook_f(e);
			bot->hook_phase = 2;
			if (gi.cvar("sg_debug", "0", 0)->value)
				gi.dprintf("HOOKFIRE %s at (%.0f %.0f %.0f)\n",
				           e->client->pers.netname, bot->hook_anchor[0],
				           bot->hook_anchor[1], bot->hook_anchor[2]);
		}
		else if (bot->hook_phase == 2)
		{
			vec3_t rd, td;
			float rope;
			qboolean arrived, was_pulling;

			VectorSubtract(bot->hook_anchor, e->s.origin, rd);
			rope = VectorLength(rd);
			/*
			 * Release the way the prover released (sg_rune.c:494-502):
			 * horizontally near the DESTINATION with the height nearly
			 * made, or rope inside the brake band. The old rope<200 cut
			 * every climb loose below its lip -- the bot slid back down
			 * and re-fired the same anchor forever.
			 */
			VectorSubtract(bot->hook_dest, e->s.origin, td);
			arrived = (td[0] * td[0] + td[1] * td[1] < 80.0f * 80.0f &&
			           td[2] > -96.0f && td[2] < 96.0f);
			if (arrived || rope < 130.0f ||
			    level.time > bot->hook_deadline || e->client->hookstate == 0)
			{
				was_pulling = (e->client->hookstate != 0);
				if (was_pulling)
					ctf_hook_abort(e);
				/* a cut live rope hands off to the landing steer; a rope
				 * that never attached does not */
				bot->hook_phase = was_pulling ? 3 : 0;
				bot->hook_deadline = level.time + 1.0f;
			}
		}
	}

	/* the literal emission record: what this frame's usercmd contained */
	if (gi.cvar("sg_debug", "0", 0)->value >= 2 || 
	    (gi.cvar("sg_debug", "0", 0)->value && level.time >= bot->next_cmdlog))
	{
		bot->next_cmdlog = level.time + 1.0f;
		/* the last step of the frame: fwd/side/up are that step's command,
		 * and msec x steps is how the frame's real time was spent */
		gi.dprintf("CMD %s: fwd=%d side=%d up=%d btn=%d yaw=%d pitch=%d msec=%d x%d\n",
		           e->client->pers.netname, cmd.forwardmove, cmd.sidemove,
		           cmd.upmove, cmd.buttons, cmd.angles[YAW],
		           cmd.angles[PITCH], cmd.msec, sub_steps);
	}

	/* once a second, the full body state: enough to reconstruct any stall
	 * offline without another instrumented rerun */
	if (gi.cvar("sg_debug", "0", 0)->value && level.time >= bot->next_report)
	{
		float sp = sqrtf(e->velocity[0] * e->velocity[0] +
		                 e->velocity[1] * e->velocity[1]);
		bot->next_report = level.time + 1.0f;
		gi.dprintf("SG %s: role=%d seed=%d goal=%d spd=%.0f org=(%.0f %.0f %.0f) link=%d "
		           "act=%d hp=%d dh=%d dl=%d st=%.1f gnd=%d\n",
		           e->client->pers.netname, role, bot->seed,
		           (bot->seed >= 0 && goal_field[bot->seed] < SG_FIELD_INF)
		               ? goal_field[bot->seed] : -1,
		           sp, e->s.origin[0], e->s.origin[1], e->s.origin[2],
		           bestlink,
		           (bestlink >= 0) ? sg_rune->links[bestlink].action : -1,
		           bot->hook_phase, door_hold, (int)drop_yaw_locked,
		           bot->stuck_time, e->groundentity != NULL);
	}
}

void SG_RunFrame(void)
{
	int i;
	static float last_time;

	/*
	 * Level changes are detected here rather than by a hook in the spawn
	 * code: the rune and fields were TAG_LEVEL so the engine already freed
	 * them, and level.time restarting is the tell. Same map or different,
	 * every pointer we held is stale the moment this trips.
	 */
	if (level.time < last_time ||
	    (sg_rune && Q_stricmp(sg_rune_map, level.mapname) != 0))
		SG_LevelChange();
	last_time = level.time;

	if (sg_rune)
	{
		Caco_Frame(sg_rune);
		Fields_Refresh(sg_rune);
	}

	for (i = 0; i < SG_MAXBOTS; i++)
		if (sg_bots[i].active && sg_bots[i].ent && sg_bots[i].ent->inuse)
			SG_BotThink(&sg_bots[i]);
}

/* ---------------------------------------------------------------- spawn */

static const char *sg_names[] = {
	"Arach", "Caco", "Rune", "Slip", "Gate", "Phase", "Field", "Trace",
};

/*
 * Ownership, for the legacy glue to ask. Two bot systems share the match --
 * that is the A/B harness working -- and the old code's FL_BOT loops must
 * not assume every bot is theirs.
 */
qboolean SG_OwnsBot(edict_t *ent)
{
	int i;

	for (i = 0; i < SG_MAXBOTS; i++)
		if (sg_bots[i].active && sg_bots[i].ent == ent)
			return true;
	return false;
}

qboolean SG_AddBot(void)
{
	edict_t *ent;
	char userinfo[MAX_INFO_STRING];
	int i, slot = -1;

	if (!SG_LevelSetup())
		return false;

	for (i = 0; i < SG_MAXBOTS; i++)
		if (!sg_bots[i].active)
		{
			slot = i;
			break;
		}
	if (slot < 0)
		return false;

	memset(userinfo, 0, sizeof(userinfo));
	Info_SetValueForKey(userinfo, "name", va("%s[SG]", sg_names[slot & 7]));
	Info_SetValueForKey(userinfo, "skin", "male/grunt");
	Info_SetValueForKey(userinfo, "hand", "0");

	ent = G_SpawnClient();
	if (!ent)
		return false;
	ent->flags &= ~FL_BOT;
	ent->inuse = false;
	if (!ClientConnect(ent, userinfo))
	{
		G_FreeClientEdict(ent);
		return false;
	}
	ent->inuse = true;
	ent->flags |= FL_BOT;
	ClientUserinfoChanged(ent, userinfo);
	ClientBegin(ent);

	sg_bots[slot].ent = ent;
	sg_bots[slot].active = true;
	sg_bots[slot].seed = -1;
	sg_bots[slot].stuck_time = 0.0f;

	gi.dprintf("slipgate: %s entered\n",
	           Info_ValueForKey(userinfo, "name"));
	return true;
}

int SG_RemoveBots(void)
{
	int i, n = 0;
	void ClientDisconnect(edict_t *ent);

	for (i = 0; i < SG_MAXBOTS; i++)
	{
		if (!sg_bots[i].active)
			continue;
		if (sg_bots[i].ent && sg_bots[i].ent->inuse)
		{
			ClientDisconnect(sg_bots[i].ent);
			G_FreeClientEdict(sg_bots[i].ent);
		}
		sg_bots[i].active = false;
		sg_bots[i].ent = NULL;
		n++;
	}
	return n;
}

void SG_LevelChange(void)
{
	int i;

	/* rune and fields were TAG_LEVEL -- the engine freed them */
	sg_rune = NULL;
	sg_field_red = sg_field_blue = NULL;
	sg_rune_map[0] = 0;
	for (i = 0; i < SG_MAXBOTS; i++)
	{
		sg_bots[i].active = false;
		sg_bots[i].ent = NULL;
	}
}
