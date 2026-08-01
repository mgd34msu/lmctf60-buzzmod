/*
 * sg_caco.c -- CACO: the eye. Belief, not omniscience.
 *
 * Everything a SLIPGATE bot decides from goes through here. The rule is the
 * one a fair player lives under:
 *
 *   - flag ON its stand or not: common knowledge, it is on everyone's HUD
 *     (LMCTF maintains that via ctf_flagathome / matchstate on the score
 *     displays); WHERE a flag is when it is not home is not.
 *   - a dropped flag's position, an enemy carrier's position: known only if
 *     some teammate has actually seen it -- PVS plus a trace, the same
 *     visibility LMCTF's own code uses -- and the belief carries the time it
 *     was seen so it ages.
 *   - own team's carrier: teammates know who carries (HUD icon) and learn
 *     position from sightings.
 *
 * Team belief is shared per team: bots on one team pool their sightings the
 * way humans pool callouts. Aging past SG_BELIEF_STALE clears a position
 * back to unknown.
 *
 * Three things the eye does beyond recording:
 *
 *   - it TALKS. A sighting that materially changes what the team believes
 *     goes out on say_team, through the game's own chat, so the humans on
 *     the team get the callout too. Same route bl_know.c uses.
 *   - it LISTENS to the humans. We cannot read a human teammate's mind, but
 *     a human who can see the enemy carrier would call it out, so the same
 *     visibility test is run from human eyes at a lower rate and with a
 *     delay standing in for the time it takes to say it.
 *   - it lets a stale belief MOVE. A carrier running our flag home is not
 *     standing where we last saw them; the believed seed walks one link a
 *     second down the route field toward their own stand.
 */

#include "g_local.h"
#include "g_ctffunc.h"
#include "bl_redirgi.h"                 /* BotClientCommand -- the chat route */
#include "slipgate/sg_local.h"

sg_team_belief_t sg_caco_team_belief;   /* [0]=red beliefs about red flag etc */

static float caco_next_scan;
static float caco_next_human;
static float caco_next_advect;

/* ------------------------------------------------------------- callouts */

/*
 * A bot that blurts every sighting in the frame it happens reads as a
 * script, and six bots that do it read as one. The delay is reaction time
 * (and the randomisation is what keeps them out of each other's frame); the
 * per-team gap is what stops the whole team reporting the same thing.
 */
#define SG_CALL_OURFLAG		0       /* our flag, found on the floor */
#define SG_CALL_CARRIER		1       /* the enemy running off with it */
#define SG_CALL_TOPICS		2

#define SG_CALL_TEAM_GAP	8.0f    /* one per topic per team per this */
#define SG_CALL_DELAY_MIN	0.5f
#define SG_CALL_DELAY_MAX	0.9f
#define SG_CALL_MOVED		512.0f  /* belief moved this far: say it again */

typedef struct
{
	qboolean	pending;
	int			speaker;            /* client number that will say it */
	float		due;
	char		line[160];
} sg_callout_t;

static sg_callout_t	caco_callout[2][SG_CALL_TOPICS];    /* [team-1][topic] */
static float		caco_teamsaid[2][SG_CALL_TOPICS];

/* ------------------------------------------------- human teammate relay */

#define SG_HUMAN_SCAN	2.0f        /* how often human eyes are consulted */
#define SG_HUMAN_DELAY	1.0f        /* and how late their word arrives */

#define SG_RELAY_OURS	0           /* our own carrier, indexed by team-1 */
#define SG_RELAY_THEIRS	1           /* their carrier, indexed by flag */

typedef struct
{
	qboolean	waiting;
	int			client;
	int			seed;
	float		at;                 /* when the human saw it */
	float		due;                /* when the team gets to act on it */
} sg_relay_t;

static sg_relay_t caco_relay[2][2];     /* [SG_RELAY_*][index] */

/* how often a stale carrier belief takes one step down their route home */
#define SG_ADVECT_PERIOD	1.0f

/*
 * Can this bot see that entity? The same test the game itself makes for
 * sight: line from eyes to target center unobstructed by the world.
 */
static qboolean Caco_Visible(edict_t *viewer, edict_t *target)
{
	vec3_t eye, mid;
	trace_t tr;

	VectorCopy(viewer->s.origin, eye);
	eye[2] += viewer->viewheight;
	VectorAdd(target->absmin, target->absmax, mid);
	VectorScale(mid, 0.5f, mid);

	if (!gi.inPVS(eye, mid))
		return false;
	tr = gi.trace(eye, NULL, NULL, mid, viewer, MASK_OPAQUE);
	return tr.fraction >= 1.0f;
}

/*
 * Players name places by what sits there. Dropped items are skipped: "by the
 * shotgun" is no use when the shotgun belonged to a corpse and is about to
 * vanish. Same idea, and the same fallback, as bl_know.c's Know_Where.
 */
static void Caco_Where(vec3_t origin, char *buf, int size)
{
	edict_t	*item, *best = NULL;
	float	dist, bestdist = 400.0f;
	vec3_t	delta;

	for (item = g_edicts; item < &g_edicts[globals.num_edicts]; item++)
	{
		if (!item->inuse || !item->item)
			continue;
		if (item->spawnflags & DROPPED_ITEM)
			continue;
		VectorSubtract(item->s.origin, origin, delta);
		dist = VectorLength(delta);
		if (dist < bestdist)
		{
			bestdist = dist;
			best = item;
		}
	}

	if (best && best->item && best->item->pickup_name)
		Com_sprintf(buf, size, "by the %s", best->item->pickup_name);
	else
		Com_sprintf(buf, size, "at gps %d %d %d",
		            (int)origin[0], (int)origin[1], (int)origin[2]);
}

/*
 * Queue a callout. Only our own bots speak -- a human teammate's client is
 * theirs to type with, and belief learned from human eyes goes in silently.
 */
static void Caco_Queue(edict_t *speaker, int team, int topic,
                       const char *what, vec3_t origin)
{
	sg_callout_t	*c;
	char			place[96];

	if (!speaker || !speaker->client || !(speaker->flags & FL_BOT))
		return;
	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return;
	if (topic < 0 || topic >= SG_CALL_TOPICS)
		return;

	c = &caco_callout[team - 1][topic];
	if (c->pending)
		return;                     /* someone is already about to say it */
	if (level.time < caco_teamsaid[team - 1][topic])
		return;                     /* the team heard this a moment ago */

	Caco_Where(origin, place, sizeof(place));
	Com_sprintf(c->line, sizeof(c->line), "%s %s", what, place);
	c->speaker = (int)(speaker - g_edicts - 1);
	c->due = level.time + SG_CALL_DELAY_MIN +
	         random() * (SG_CALL_DELAY_MAX - SG_CALL_DELAY_MIN);
	c->pending = true;
}

/*
 * Say the queued lines whose moment has come, through the game's real chat:
 * BotClientCommand(client, "say_team", line, NULL) routes the arguments into
 * the redirected gi.argv and runs ClientCommand -> Cmd_Say_f for that client,
 * exactly as bl_know.c's Know_Speak does. Teammates -- human ones included --
 * read it in their own chat.
 */
static void Caco_Speak(void)
{
	int t, k;

	for (t = 0; t < 2; t++)
		for (k = 0; k < SG_CALL_TOPICS; k++)
		{
			sg_callout_t	*c = &caco_callout[t][k];
			edict_t			*sp;

			if (!c->pending || level.time < c->due)
				continue;

			/* consumed either way: a muted line must not jam the slot */
			c->pending = false;

			if (level.time < caco_teamsaid[t][k])
				continue;
			if (c->speaker < 0 || c->speaker >= game.maxclients)
				continue;

			sp = g_edicts + 1 + c->speaker;
			if (!sp->inuse || !sp->client)
				continue;
			if (sp->client->ctf.teamnum != t + 1)
				continue;
			if (sp->deadflag == DEAD_DEAD || sp->health <= 0)
				continue;           /* the dead do not call it out */

			BotClientCommand(c->speaker, "say_team", c->line, NULL);
			caco_teamsaid[t][k] = level.time + SG_CALL_TEAM_GAP;
		}
}

/*
 * The flag entities: LMCTF spawns classname "flag" with flagteam set.
 * ctf_flagathome answers "is it sitting on its stand" -- that part is HUD
 * knowledge and read directly; positions when astray are only taken from
 * sightings below.
 */
static void Caco_ScanFlags(rune_t *r, edict_t *viewer, int viewer_team)
{
	edict_t *e = NULL;

	while ((e = G_Find(e, FOFS(classname), "flag")) != NULL)
	{
		int fi = (e->flagteam == CTF_TEAM_RED) ? 0 : 1;
		sg_belief_flag_t *b = &sg_caco_team_belief.flag[fi];
		edict_t *look;
		qboolean was_unknown;

		/* common knowledge: home or not (HUD) */
		if (ctf_flagathome(e))
		{
			b->state = SG_FLAG_HOME;
			b->where_seed = -1;
			continue;
		}

		/* not home. carried or dropped is also HUD-level in LMCTF
		 * (the score line shows taken flags), but WHERE requires sight. */
		if (b->state == SG_FLAG_HOME)
		{
			b->state = SG_FLAG_ASTRAY;
			b->where_seed = -1;         /* until someone sees it */
			b->seen_time = 0.0f;
		}

		/*
		 * A flag in somebody's hands does not move its own entity: LMCTF
		 * hides the model, hands it to the player (g_ctffunc.c
		 * ctf_flagtouch) and leaves the flag edict sitting where it was
		 * taken from. So the thing to look at, and the position worth
		 * believing, is the carrier -- not the invisible flag edict, which
		 * would hand us a spot nobody is standing in.
		 */
		look = (e->owner && e->owner->inuse && e->owner->client) ? e->owner : e;

		was_unknown = (b->where_seed < 0) ? true : false;

		if (viewer && Caco_Visible(viewer, look))
		{
			b->where_seed = Rune_NearestSeed(r, look->s.origin);
			b->seen_time = level.time;

			/*
			 * Our own flag, astray and until now unlocated, is the one the
			 * team most needs to hear about.
			 */
			if (was_unknown && b->where_seed >= 0 && fi == viewer_team - 1)
				Caco_Queue(viewer, viewer_team, SG_CALL_OURFLAG,
				           (look == e) ? "our flag is down"
				                       : "our flag is moving,",
				           look->s.origin);
		}
	}
}

/*
 * Carriers: LMCTF marks them with EF_FLAG1/EF_FLAG2 on the player entity
 * (p_view.c G_SetClientEffects). Which is HUD knowledge as identity; the
 * position needs a sighting by a teammate of the believing team.
 */
static void Caco_ScanCarriers(rune_t *r, edict_t *viewer, int viewer_team)
{
	int i;

	for (i = 0; i < game.maxclients; i++)
	{
		edict_t *p = g_edicts + 1 + i;
		int carried, enemy_of;

		if (!p->inuse || !p->client)
			continue;
		if (!(p->s.effects & (EF_FLAG1 | EF_FLAG2)))
			continue;

		/* which team's flag does this player carry? EF_FLAG1 is red's */
		carried = (p->s.effects & EF_FLAG1) ? 0 : 1;
		/* the team whose flag is carried wants to know the carrier */
		enemy_of = carried;

		if (p->client->ctf.teamnum == viewer_team)
		{
			/* our own carrier: identity is team knowledge, position from
			 * sighting (including the carrier seeing themself) */
			sg_belief_carrier_t *c =
				&sg_caco_team_belief.carrier[viewer_team - 1];

			c->client = i;
			if (viewer && (viewer == p || Caco_Visible(viewer, p)))
			{
				c->seed = Rune_NearestSeed(r, p->s.origin);
				c->seen_time = level.time;
			}
		}
		else if (viewer && viewer->client &&
		         viewer->client->ctf.teamnum == enemy_of + 1)
		{
			/* an enemy carrying OUR flag: the belief that matters most */
			sg_belief_carrier_t *c =
				&sg_caco_team_belief.enemy_carrier[enemy_of];

			if (Caco_Visible(viewer, p))
			{
				qboolean material;

				/*
				 * Worth the airtime when it is news: a carrier the team had
				 * no fix on at all, a different player carrying, or the same
				 * one now a long way from where the team believes he is.
				 */
				material = (c->seed < 0 || c->client != i) ? true : false;
				if (!material && c->seed < r->hdr.num_seeds)
				{
					vec3_t d;

					VectorSubtract(p->s.origin, r->seeds[c->seed].origin, d);
					if (VectorLength(d) > SG_CALL_MOVED)
						material = true;
				}

				c->client = i;
				c->seed = Rune_NearestSeed(r, p->s.origin);
				c->seen_time = level.time;

				if (material)
					Caco_Queue(viewer, enemy_of + 1, SG_CALL_CARRIER,
					           "enemy has our flag,", p->s.origin);
			}
		}
	}
}

/*
 * ---------------------------------------------------- powerups and runes
 *
 * sg_fields.c prices the powerup and rune classes by walking g_edicts and
 * asking each item entity whether it is SOLID_NOT -- which is to say, asking
 * the world whether the quad is up. Nobody in the server room can do that.
 * What a player actually holds is:
 *
 *   - WHERE a powerup spawns: map knowledge, constant, free. Scanned once
 *     here at Caco_Reset and never asked of the world again.
 *   - whether it is up right now: only if somebody has looked at the pad
 *     since it last came back, or watched it get taken and counted.
 *
 * The respawn clock is fact, read from the item definition rather than
 * assumed: Pickup_Powerup calls SetRespawn(ent, ent->item->quantity)
 * (g_items.c:198), and the quantity field of item_quad is
 * LM_QUAD_DEFAULT_TIME (g_items.c:2025), which is 60 (g_local.h:1513).
 * item_invulnerability carries 300 in the same slot (g_items.c:2048). So the
 * delay comes from ent->item->quantity per entity and quad lands on 60.
 *
 * RUNES DO NOT WORK THAT WAY IN LMCTF and must not be modelled as if they
 * did. Three facts from g_runes.c:
 *
 *   - a rune is one persistent entity created at map load (SpawnRune, called
 *     from g_spawn.c:1037-1045), not a respawning map item;
 *   - it RELOCATES on its own every RUNETHINKTIME = 30 seconds (g_runes.c:10)
 *     to a randomly chosen item_health* spot (Rune_Think, g_runes.c:334-352,
 *     via SelectRuneSpawnPoint, g_runes.c:79). Its position is therefore NOT
 *     map knowledge at all, not even approximately, past 30 seconds;
 *   - when picked up it goes SOLID_NOT and stays in the carrier's hands with
 *     NO respawn scheduled (Pickup_Rune, g_runes.c:450-460, sets think=NULL
 *     and calls no SetRespawn). It comes back where and when the carrier dies
 *     or drops it (Drop_Rune, g_runes.c:628-673).
 *
 * So a rune gets no spawn location from map knowledge and no respawn clock to
 * infer from: its whereabouts are a sighting and nothing else, and a sighting
 * expires after SG_RUNE_WANDER because by then the thing has moved itself.
 */

/* g_runes.c:10, RUNETHINKTIME -- how long before a rune moves on its own */
#define SG_RUNE_WANDER	30.0f

sg_belief_item_t	sg_caco_items[SG_MAX_BELIEF_ITEMS];
int					sg_caco_num_items;

static const char *caco_rune_class[] = {
	"damage_rune", "haste_rune", "resist_rune", "regen_rune",
	"vampire_rune", NULL
};

static int Caco_ItemClassOf(edict_t *e)
{
	int i;

	if (!e->classname)
		return -1;
	if (strcmp(e->classname, "item_quad") == 0 ||
	    strcmp(e->classname, "item_invulnerability") == 0)
		return SG_BI_POWERUP;
	for (i = 0; caco_rune_class[i]; i++)
		if (strcmp(e->classname, caco_rune_class[i]) == 0)
			return SG_BI_RUNE;
	return -1;
}

static sg_belief_item_t *Caco_ItemBelief(edict_t *e)
{
	int i, num = (int)(e - g_edicts);

	for (i = 0; i < sg_caco_num_items; i++)
		if (sg_caco_items[i].ent == num)
			return &sg_caco_items[i];
	return NULL;
}

/*
 * Once, at setup. Spawn locations are the part that is genuinely constant, so
 * this is the only time the item entities are read for position -- and for
 * runes not even that, since a rune's position is never map knowledge.
 */
static void Caco_ScanItemSpawns(void)
{
	int i;

	memset(sg_caco_items, 0, sizeof(sg_caco_items));
	sg_caco_num_items = 0;

	for (i = 0; i < globals.num_edicts &&
	            sg_caco_num_items < SG_MAX_BELIEF_ITEMS; i++)
	{
		edict_t				*e = &g_edicts[i];
		sg_belief_item_t	*b;
		int					cls;

		if (!e->inuse)
			continue;
		cls = Caco_ItemClassOf(e);
		if (cls < 0)
			continue;

		/* somebody's dropped quad is not a place on the map */
		if (cls == SG_BI_POWERUP && (e->spawnflags & DROPPED_ITEM))
			continue;

		b = &sg_caco_items[sg_caco_num_items++];
		b->ent = i;
		b->cls = cls;
		b->seed = -1;
		b->seen_up_time = -1.0f;            /* nobody has looked yet */
		b->believed_respawn_time = 0.0f;

		if (cls == SG_BI_POWERUP)
		{
			VectorCopy(e->s.origin, b->org);
			b->respawn_delay = (e->item && e->item->quantity > 0)
			                 ? (float)e->item->quantity
			                 : (float)LM_QUAD_DEFAULT_TIME;
			/* what a player assumes walking in: the quad is on its pad
			 * until somebody has reason to think otherwise */
			b->believed_up = true;
		}
		else
		{
			VectorClear(b->org);
			b->respawn_delay = 0.0f;        /* no clock exists to infer from */
			b->believed_up = false;         /* and no location either, yet */
		}
	}
}

/*
 * One bot's eyes on the item entities. Looking at the pad is what settles it,
 * either way: the model standing there says up, an empty pad says taken. A
 * taken powerup starts its clock; a taken rune starts nothing, because there
 * is nothing to start.
 */
static void Caco_ScanItems(rune_t *r, edict_t *viewer)
{
	int i;

	if (!r || !viewer)
		return;

	for (i = 0; i < sg_caco_num_items; i++)
	{
		sg_belief_item_t	*b = &sg_caco_items[i];
		edict_t				*e = g_edicts + b->ent;

		if (!e->inuse)
			continue;
		if (!Caco_Visible(viewer, e))
			continue;

		if (e->solid != SOLID_NOT)
		{
			b->believed_up = true;
			b->seen_up_time = level.time;
			b->believed_respawn_time = 0.0f;
			VectorCopy(e->s.origin, b->org);
			b->seed = Rune_NearestSeed(r, b->org);
		}
		else
		{
			b->believed_up = false;
			b->believed_respawn_time = (b->respawn_delay > 0.0f)
				? level.time + b->respawn_delay
				: 0.0f;
			if (b->cls == SG_BI_RUNE)
				b->seed = -1;       /* it is in somebody's hands now */
		}
	}
}

static void Caco_AgeItems(rune_t *r)
{
	int i;

	for (i = 0; i < sg_caco_num_items; i++)
	{
		sg_belief_item_t *b = &sg_caco_items[i];

		/* a spawn location known from the map still needs a seed; that is
		 * arithmetic on map knowledge, not a look at the world */
		if (b->seed < 0 && b->believed_up && r)
			b->seed = Rune_NearestSeed(r, b->org);

		if (b->cls == SG_BI_RUNE)
		{
			/*
			 * We cannot know the phase of the rune's own 30-second timer from
			 * one sighting, so a sighting buys us at most that long before the
			 * thing has walked off by itself. After that we do not know where
			 * it is, and saying so is the whole point of this file.
			 */
			if (b->believed_up && b->seen_up_time >= 0.0f &&
			    level.time - b->seen_up_time > SG_RUNE_WANDER)
			{
				b->believed_up = false;
				b->seed = -1;
			}
			continue;               /* no respawn clock to run */
		}

		/*
		 * The inference a player makes with a watch rather than with eyes: he
		 * saw it taken, he counted, it is due. seen_up_time is deliberately
		 * NOT advanced -- this is a belief, not a sighting, and the next
		 * person to look at the pad may well find it already gone again.
		 */
		if (!b->believed_up && b->believed_respawn_time > 0.0f &&
		    level.time > b->believed_respawn_time)
		{
			b->believed_up = true;
			b->believed_respawn_time = 0.0f;
		}
	}
}

/*
 * What sg_fields.c asks instead of asking the entity. An item outside the
 * belief classes (weapons, armour, ammo, health) is answered exactly as the
 * field code answers it today, so wiring these in changes only the two
 * classes that are meant to change.
 */
qboolean Caco_ItemBelievedUp(edict_t *e)
{
	sg_belief_item_t *b;

	if (!e || !e->inuse || !e->classname)
		return false;
	b = Caco_ItemBelief(e);
	if (!b)
		return (e->solid != SOLID_NOT) ? true : false;
	return b->believed_up;
}

/*
 * A hash of the belief itself, for the field code's rebuild test. Class_
 * Signature's entity walk notices an item going up or down but cannot notice
 * a RUNE changing place while staying up, which happens every 30 seconds --
 * mixing this in makes the rune field rebuild when the belief moves.
 */
unsigned Caco_ItemBeliefSig(void)
{
	unsigned	sig = 2166136261u;
	int			i;

	for (i = 0; i < sg_caco_num_items; i++)
	{
		sig = (sig ^ (unsigned)sg_caco_items[i].believed_up) * 16777619u;
		sig = (sig ^ (unsigned)sg_caco_items[i].seed) * 16777619u;
	}
	return sig;
}

int Caco_ItemBeliefSeed(rune_t *r, edict_t *e)
{
	sg_belief_item_t *b;

	if (!e || !e->inuse)
		return -1;
	b = Caco_ItemBelief(e);
	if (b)
		return b->believed_up ? b->seed : -1;
	return Rune_NearestSeed(r, e->s.origin);
}

static void Caco_Age(rune_t *r)
{
	int i;

	Caco_AgeItems(r);

	for (i = 0; i < 2; i++)
	{
		if (sg_caco_team_belief.flag[i].where_seed >= 0 &&
		    level.time - sg_caco_team_belief.flag[i].seen_time > SG_BELIEF_STALE)
			sg_caco_team_belief.flag[i].where_seed = -1;

		if (sg_caco_team_belief.carrier[i].seed >= 0 &&
		    level.time - sg_caco_team_belief.carrier[i].seen_time > SG_BELIEF_STALE)
			sg_caco_team_belief.carrier[i].seed = -1;

		if (sg_caco_team_belief.enemy_carrier[i].seed >= 0 &&
		    level.time - sg_caco_team_belief.enemy_carrier[i].seen_time > SG_BELIEF_STALE)
			sg_caco_team_belief.enemy_carrier[i].seed = -1;

		/* a carrier who died stops being one */
		if (sg_caco_team_belief.carrier[i].client >= 0)
		{
			edict_t *p = g_edicts + 1 + sg_caco_team_belief.carrier[i].client;

			if (!p->inuse || !p->client ||
			    !(p->s.effects & (EF_FLAG1 | EF_FLAG2)))
				sg_caco_team_belief.carrier[i].client = -1;
		}

		/*
		 * Same for the thief: who is holding a flag is on everyone's screen,
		 * so the moment he is not holding one the team knows to stop chasing
		 * him. WHERE he was is a sighting and ages on its own; that he is no
		 * longer carrying is free.
		 */
		if (sg_caco_team_belief.enemy_carrier[i].client >= 0)
		{
			edict_t *p =
				g_edicts + 1 + sg_caco_team_belief.enemy_carrier[i].client;

			if (!p->inuse || !p->client ||
			    !(p->s.effects & (EF_FLAG1 | EF_FLAG2)))
			{
				sg_caco_team_belief.enemy_carrier[i].client = -1;
				sg_caco_team_belief.enemy_carrier[i].seed = -1;
			}
		}
	}
}

/*
 * ------------------------------------------------------------ projection
 *
 * A believed position that is only allowed to sit still is a lie that gets
 * worse every second, and a believed position advanced as a single point is
 * a lie that is merely better dressed: it claims to know which way he turned
 * at the junction. SLIPGATE.md asks for the honest object -- "other agents
 * are tracked as phase mass, not points" -- so what ages here is a SET.
 *
 * From the last sighting we hold up to SG_PROJ_MAX plausible seeds. Once a
 * second every member takes its best descending step along the carrier's own
 * route-home field, and where the route genuinely forks the next two
 * descending neighbours come along as well: a member in a corridor produces
 * one successor, a member at a junction produces three. Duplicates collapse
 * on the way in, so the set does not explode along parallel paths that meet.
 *
 * When the candidates overflow the cap, the ones kept are those DEEPEST along
 * his route -- the ones we have least time to answer. The single seed the
 * older consumers read (enemy_carrier[i].seed) is the deepest member of all,
 * which is the worst case for us; consumers that want the spread read
 * sg_caco_proj[i] directly and intersect it with their own reachable set.
 *
 * The sighting time is NOT touched. The belief still ages out at
 * SG_BELIEF_STALE; between now and then it spreads toward somewhere plausible
 * rather than staying pinned to a spot he has certainly left.
 *
 * All storage is fixed and static. Nothing here allocates on a frame.
 */

sg_proj_t sg_caco_proj[2];

/* scratch for one step: every member's successors before dedupe and cull */
static int caco_proj_cand[SG_PROJ_MAX * SG_PROJ_BRANCH];

/*
 * The best SG_PROJ_BRANCH descending neighbours of one seed, written into
 * out[] in ascending field order (out[0] is the step the old code took).
 * Neighbours that do not descend are not steps toward his stand and are not
 * offered: a carrier who wanders backwards is not the case we must cover.
 */
static int Caco_BestSteps(rune_t *r, int seed, const int *field, int *out)
{
	int li, n = 0, i, j;
	int val[SG_PROJ_BRANCH];

	if (!r || !field || seed < 0 || seed >= r->hdr.num_seeds)
		return 0;
	if (field[seed] >= SG_FIELD_INF)
		return 0;                   /* no route priced from here */

	for (li = r->first_link[seed]; li >= 0; li = r->next_link[li])
	{
		int to = r->links[li].to;
		int v;

		if (to < 0 || to >= r->hdr.num_seeds)
			continue;
		v = field[to];
		if (v >= field[seed])
			continue;

		for (i = 0; i < n; i++)
			if (out[i] == to)
				break;
		if (i < n)
			continue;               /* two links to the same seed */

		for (i = 0; i < n && val[i] <= v; i++)
			;
		if (i >= SG_PROJ_BRANCH)
			continue;               /* worse than the three we hold */

		for (j = (n < SG_PROJ_BRANCH ? n : SG_PROJ_BRANCH - 1); j > i; j--)
		{
			out[j] = out[j - 1];
			val[j] = val[j - 1];
		}
		out[i] = to;
		val[i] = v;
		if (n < SG_PROJ_BRANCH)
			n++;
	}
	return n;
}

/* one second of travel for the whole set */
static void Caco_ProjStep(rune_t *r, sg_proj_t *p, const int *field)
{
	int nc = 0, i, k;

	for (i = 0; i < p->n; i++)
	{
		int step[SG_PROJ_BRANCH];
		int ns = Caco_BestSteps(r, p->seed[i], field, step);

		if (ns == 0)
		{
			/* home, or off the priced graph: he can also just be standing
			 * there, and that possibility must not be dropped */
			step[0] = p->seed[i];
			ns = 1;
		}

		for (k = 0; k < ns && nc < SG_PROJ_MAX * SG_PROJ_BRANCH; k++)
		{
			int j;

			for (j = 0; j < nc; j++)
				if (caco_proj_cand[j] == step[k])
					break;
			if (j == nc)
				caco_proj_cand[nc++] = step[k];
		}
	}

	/* keep the deepest SG_PROJ_MAX, ordered, so seed[0] is the worst case */
	p->n = 0;
	while (p->n < SG_PROJ_MAX && p->n < nc)
	{
		int best = -1, bestv = 0;

		for (i = 0; i < nc; i++)
		{
			int s = caco_proj_cand[i];
			int v;

			if (s < 0)
				continue;           /* already taken */
			v = (s < r->hdr.num_seeds) ? field[s] : SG_FIELD_INF;
			if (best < 0 || v < bestv)
			{
				best = i;
				bestv = v;
			}
		}
		if (best < 0)
			break;

		p->seed[p->n++] = caco_proj_cand[best];
		caco_proj_cand[best] = -1;
	}
}

static void Caco_Project(rune_t *r)
{
	int i;

	if (level.time < caco_next_advect)
		return;
	caco_next_advect = level.time + SG_ADVECT_PERIOD;

	if (!r || !sg_fields.to_red_flag || !sg_fields.to_blue_flag)
		return;

	for (i = 0; i < 2; i++)
	{
		sg_belief_carrier_t *c = &sg_caco_team_belief.enemy_carrier[i];
		sg_proj_t *p = &sg_caco_proj[i];
		const int *theirs;

		if (c->client < 0 || c->seed < 0)
		{
			p->n = 0;
			p->client = -1;
			continue;
		}

		/* a fresh sighting is certainty: the set collapses back to a point */
		if (p->n == 0 || p->client != c->client || c->seen_time > p->from_time)
		{
			p->client = c->client;
			p->from_time = c->seen_time;
			p->seed[0] = c->seed;
			p->n = 1;
		}

		if (level.time - c->seen_time < SG_ADVECT_PERIOD)
			continue;               /* someone has eyes on him right now */

		/*
		 * enemy_carrier[i] holds team i+1's flag, so he plays for the other
		 * side and is running to the other side's stand.
		 */
		theirs = i ? sg_fields.to_red_flag : sg_fields.to_blue_flag;
		Caco_ProjStep(r, p, theirs);

		if (p->n > 0)
			c->seed = p->seed[0];
	}
}

/*
 * --------------------------------------------------------- human eyes
 *
 * Human teammates see things too and we cannot read their minds. What we can
 * say honestly is that a human who has the enemy carrier in view would call
 * it out, so the same PVS-plus-trace test is run from human eyes -- but at a
 * fraction of the rate a bot looks, and the word only reaches the team
 * SG_HUMAN_DELAY later, which is roughly what saying it costs. Flag home /
 * astray state needs none of this: it is on everyone's HUD already.
 */
static void Caco_Relay(int kind, int idx, int client, int seed)
{
	sg_relay_t *rl;

	if (idx < 0 || idx > 1 || seed < 0)
		return;

	rl = &caco_relay[kind][idx];
	rl->waiting = true;
	rl->client = client;
	rl->seed = seed;
	rl->at = level.time;            /* stamped when SEEN, not when heard */
	rl->due = level.time + SG_HUMAN_DELAY;
}

void Caco_HumanEyes(rune_t *r, int team)
{
	int i, j;

	if (!r)
		return;
	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return;

	for (i = 0; i < game.maxclients; i++)
	{
		edict_t *h = g_edicts + 1 + i;

		if (!h->inuse || !h->client)
			continue;
		if (h->flags & FL_BOT)
			continue;               /* bots feed belief through Caco_See */
		if (h->client->ctf.teamnum != team)
			continue;
		if (h->deadflag == DEAD_DEAD || h->health <= 0)
			continue;

		for (j = 0; j < game.maxclients; j++)
		{
			edict_t *p = g_edicts + 1 + j;
			int carried;

			if (!p->inuse || !p->client)
				continue;
			if (!(p->s.effects & (EF_FLAG1 | EF_FLAG2)))
				continue;

			carried = (p->s.effects & EF_FLAG1) ? 0 : 1;

			if (p->client->ctf.teamnum == team)
			{
				/* our own carrier, or this human IS the carrier */
				if (p == h || Caco_Visible(h, p))
					Caco_Relay(SG_RELAY_OURS, team - 1, j,
					           Rune_NearestSeed(r, p->s.origin));
			}
			else if (carried == team - 1)
			{
				/* an enemy running with OUR flag */
				if (Caco_Visible(h, p))
					Caco_Relay(SG_RELAY_THEIRS, carried, j,
					           Rune_NearestSeed(r, p->s.origin));
			}
		}
	}
}

/*
 * The delayed word arrives. It loses to anything a bot saw later -- a
 * teammate's shout does not overwrite what you are looking at.
 */
static void Caco_RelayFlush(void)
{
	int k, i;

	for (k = 0; k < 2; k++)
		for (i = 0; i < 2; i++)
		{
			sg_relay_t *rl = &caco_relay[k][i];
			sg_belief_carrier_t *c;
			edict_t *p;

			if (!rl->waiting || level.time < rl->due)
				continue;
			rl->waiting = false;

			if (rl->client < 0 || rl->client >= game.maxclients)
				continue;
			p = g_edicts + 1 + rl->client;
			if (!p->inuse || !p->client ||
			    !(p->s.effects & (EF_FLAG1 | EF_FLAG2)))
				continue;           /* not carrying any more: nothing to tell */

			c = (k == SG_RELAY_THEIRS)
				? &sg_caco_team_belief.enemy_carrier[i]
				: &sg_caco_team_belief.carrier[i];

			if (rl->at <= c->seen_time)
				continue;           /* the team already knows something newer */

			c->client = rl->client;
			c->seed = rl->seed;
			c->seen_time = rl->at;
		}
}

/*
 * Each SLIPGATE bot contributes its sight each frame; the shared scan of
 * flag home-state runs on a cadence with no viewer at all (HUD knowledge
 * needs no eyes).
 */
sg_belief_enemy_t sg_caco_enemies[2][SG_MAX_ENEMY_TRACK];

/*
 * Every enemy a teammate lays eyes on, not just carriers: the defender's
 * pre-spin and the rune-threat weighting both need "someone is out there
 * and roughly where". Upsert by client number; a full table evicts the
 * stalest sighting. The RF_GLOW test is the rune tell (p_view.c:792-794).
 */
static void Caco_ScanEnemies(rune_t *r, edict_t *viewer, int viewer_team)
{
	int i;

	for (i = 0; i < game.maxclients; i++)
	{
		edict_t *p = g_edicts + 1 + i;
		sg_belief_enemy_t *tab = sg_caco_enemies[viewer_team - 1];
		int s, slot = -1;

		qboolean seen, heard = false;

		if (!p->inuse || !p->client || p->deadflag)
			continue;
		if (p->client->ctf.teamnum == viewer_team ||
		    p->client->ctf.teamnum <= CTF_TEAM_UNDEFINED)
			continue;
		seen = Caco_Visible(viewer, p);
		if (!seen)
		{
			/*
			 * The ear. PHS is the engine's own "could a sound from there
			 * reach here" set (gi.inPHS, game.h:112), and the loud states
			 * are server-visible facts: a firing weapon (WEAPON_FIRING,
			 * g_local.h:173) or a rope out. No sight line, but noise in
			 * the PHS places the maker COARSELY -- enough to warn a post,
			 * never to aim, which is what heard_only means downstream.
			 */
			if ((p->client->weaponstate == WEAPON_FIRING ||
			     p->client->hookstate != 0) &&
			    gi.inPHS(viewer->s.origin, p->s.origin))
				heard = true;
			if (!heard)
				continue;
		}

		/* this client's slot, else an empty one, else the stalest */
		for (s = 0; s < SG_MAX_ENEMY_TRACK && slot < 0; s++)
			if (tab[s].client == i)
				slot = s;
		for (s = 0; s < SG_MAX_ENEMY_TRACK && slot < 0; s++)
			if (tab[s].client < 0)
				slot = s;
		if (slot < 0)
		{
			slot = 0;
			for (s = 1; s < SG_MAX_ENEMY_TRACK; s++)
				if (tab[s].seen_time < tab[slot].seen_time)
					slot = s;
		}
		/* a fresh eye entry outranks an ear: don't degrade it */
		if (!seen && tab[slot].client == i && !tab[slot].heard_only &&
		    level.time - tab[slot].seen_time < 2.0f)
			continue;

		tab[slot].client = i;
		tab[slot].seed = Rune_NearestSeed(r, p->s.origin);
		tab[slot].seen_time = level.time;
		tab[slot].heard_only = !seen;
		if (seen)
			tab[slot].runed = (p->s.renderfx & RF_GLOW) != 0;
	}
}

/*
 * The D4 inference chain, every link a believed or team-known fact:
 * the Damage rune's pad state comes from item belief (a rune sighting,
 * aged by its own wander clock), "not ours" from team knowledge (client
 * ->rune is server truth about teammates), and "an enemy glows" from the
 * sighting table. All three together and the ×1.80 Resist posture is
 * justified; any link missing and it is not.
 */
qboolean Caco_EnemyHasDamageRune(int team)
{
	int i;
	qboolean taken = false, glowing = false;

	for (i = 0; i < sg_caco_num_items; i++)
	{
		sg_belief_item_t *it = &sg_caco_items[i];
		edict_t *re = (it->ent > 0) ? g_edicts + it->ent : NULL;

		if (re && re->classname &&
		    strcmp(re->classname, "damage_rune") == 0)
		{
			taken = !it->believed_up;
			break;
		}
	}
	if (!taken)
		return false;

	for (i = 0; i < game.maxclients; i++)
	{
		edict_t *p = g_edicts + 1 + i;

		if (p->inuse && p->client && p->client->ctf.teamnum == team &&
		    p->client->rune && p->client->rune->classname &&
		    strcmp(p->client->rune->classname, "damage_rune") == 0)
			return false;               /* it is in OUR hands */
	}

	for (i = 0; i < SG_MAX_ENEMY_TRACK; i++)
	{
		sg_belief_enemy_t *en = &sg_caco_enemies[team - 1][i];

		if (en->client >= 0 && en->runed &&
		    level.time - en->seen_time < 20.0f)
			glowing = true;
	}
	return glowing;
}

void Caco_See(rune_t *r, edict_t *viewer)
{
	if (!viewer || !viewer->client)
		return;
	Caco_ScanFlags(r, viewer, viewer->client->ctf.teamnum);
	Caco_ScanCarriers(r, viewer, viewer->client->ctf.teamnum);
	Caco_ScanEnemies(r, viewer, viewer->client->ctf.teamnum);
	Caco_ScanItems(r, viewer);
}

void Caco_Frame(rune_t *r)
{
	if (level.time >= caco_next_scan)
	{
		caco_next_scan = level.time + 0.5f;
		Caco_ScanFlags(r, NULL, 0);     /* HUD-level state only */
		Caco_Age(r);
	}

	if (level.time >= caco_next_human)
	{
		caco_next_human = level.time + SG_HUMAN_SCAN;
		Caco_HumanEyes(r, CTF_TEAM_RED);
		Caco_HumanEyes(r, CTF_TEAM_BLUE);
	}

	/* these two are per frame: a delay measured in tenths needs the clock */
	Caco_RelayFlush();
	Caco_Project(r);
	Caco_Speak();
}

void Caco_Reset(void)
{
	int i;

	memset(&sg_caco_team_belief, 0, sizeof(sg_caco_team_belief));
	memset(sg_caco_enemies, 0, sizeof(sg_caco_enemies));
	{
		int t, s;

		for (t = 0; t < 2; t++)
			for (s = 0; s < SG_MAX_ENEMY_TRACK; s++)
				sg_caco_enemies[t][s].client = -1;
	}
	memset(caco_callout, 0, sizeof(caco_callout));
	memset(caco_teamsaid, 0, sizeof(caco_teamsaid));
	memset(caco_relay, 0, sizeof(caco_relay));
	memset(sg_caco_proj, 0, sizeof(sg_caco_proj));
	for (i = 0; i < 2; i++)
	{
		sg_caco_team_belief.flag[i].where_seed = -1;
		sg_caco_team_belief.carrier[i].client = -1;
		sg_caco_team_belief.carrier[i].seed = -1;
		sg_caco_team_belief.enemy_carrier[i].client = -1;
		sg_caco_team_belief.enemy_carrier[i].seed = -1;
		sg_caco_proj[i].client = -1;
		sg_caco_proj[i].n = 0;
		sg_caco_proj[i].from_time = -1.0f;
	}

	/* spawn locations are constant, so they are read exactly once -- and
	 * runes, which are not constant, get nothing from this scan but their
	 * entity numbers (see the commentary above Caco_ScanItemSpawns) */
	Caco_ScanItemSpawns();
	caco_next_scan = 0.0f;
	caco_next_human = 0.0f;
	caco_next_advect = 0.0f;
}
