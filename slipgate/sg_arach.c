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
	vec3_t		last_origin;
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

	if (to_item >= SG_FIELD_INF || direct >= SG_FIELD_INF)
		return 0.0f;
	/*
	 * item_to_goal is unknowable per-item once flooded by class; the class
	 * field gives cost to the NEAREST item, and the triangle detour is
	 * approximated by to_item alone against scale. Honest limitation,
	 * recorded: per-item fields would make this exact at more memory.
	 */
	return worth / (1.0f + (float)to_item / 1500.0f);
}

/*
 * Role assignment: the owner's quota. Two in five defend (nearest-rounded),
 * carrier counts toward defence, a side of one attacks. Assigned by slot
 * order among SLIPGATE bots of the team, stable frame to frame.
 */
static sg_role_t SG_Role(sg_bot_t *bot, qboolean carrying)
{
	int team = bot->ent->client->ctf.teamnum;
	int size = 0, defenders_wanted, my_rank = 0, i;

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
		size++;
	}

	defenders_wanted = (size * 2 + 2) / 5;
	if (size <= 1)
		defenders_wanted = 0;

	/* a live carrier on our side counts toward the defensive share */
	if (sg_caco_team_belief.carrier[team - 1].client >= 0)
		defenders_wanted--;

	return (my_rank < defenders_wanted) ? SG_ROLE_DEFEND : SG_ROLE_ATTACK;
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
	 *   attacker -> the enemy flag WHERE BELIEF PUTS IT (home stand, or the
	 *               spot it was last seen lying, via the -now field)
	 * When our flag is astray and its taker was seen, the intercept field
	 * (their believed position) joins the composition for non-carriers.
	 */
	if (role == SG_ROLE_CARRY || role == SG_ROLE_DEFEND)
		goal_field = (team == CTF_TEAM_RED) ? sg_fields.to_red_flag
		                                    : sg_fields.to_blue_flag;
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

	/* descend the surface: my seed vs every seed one proven link away */
	bestval = Surface_At(bot->seed, w, goal_field, support, intercept);
	for (li = sg_rune->first_link[bot->seed]; li >= 0; li = sg_rune->next_link[li])
	{
		rune_link_t *l = &sg_rune->links[li];
		float v = Surface_At(l->to, w, goal_field, support, intercept);

		if (v < bestval)
		{
			bestval = v;
			bestlink = li;
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

		if (bestlink >= 0)
		{
			rune_link_t *l = &sg_rune->links[bestlink];

			VectorCopy(sg_rune->seeds[l->to].origin, aim);
			have_aim = true;
			if ((l->action == RL_JUMP || l->action == RL_HOOK) &&
			    e->groundentity)
				jump_now = (l->action == RL_JUMP);
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
				score = tr.fraction * (1.0f - 0.05f * (k > 0) * (k + 1));
				if (score > best_open)
				{
					best_open = score;
					chosen_yaw = base_yaw + fan[k];
				}
				if (tr.fraction >= 1.0f && k == 0)
					break;              /* goal line is open: take it */
			}

			cmd.angles[YAW] = ANGLE2SHORT(chosen_yaw)
			                - e->client->ps.pmove.delta_angles[YAW];
			cmd.angles[PITCH] = -e->client->ps.pmove.delta_angles[PITCH];
			cmd.forwardmove = 400;
			if (jump_now)
				cmd.upmove = 400;

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
	}

	ClientThink(e, &cmd);

	/* every few seconds, say where this bot is and what it wants */
	if (gi.cvar("sg_debug", "0", 0)->value && level.time >= bot->next_report)
	{
		float sp = sqrtf(e->velocity[0] * e->velocity[0] +
		                 e->velocity[1] * e->velocity[1]);
		bot->next_report = level.time + 3.0f;
		gi.dprintf("SG %s: role=%d seed=%d goal=%d spd=%.0f org=(%.0f %.0f %.0f) link=%d\n",
		           e->client->pers.netname, role, bot->seed,
		           (bot->seed >= 0 && goal_field[bot->seed] < SG_FIELD_INF)
		               ? goal_field[bot->seed] : -1,
		           sp, e->s.origin[0], e->s.origin[1], e->s.origin[2],
		           bestlink);
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
