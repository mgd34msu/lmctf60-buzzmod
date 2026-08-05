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
#include "slipgate/sg_net.h"                    /* SG_BotClientCommand -- the chat route */
#include "slipgate/sg_local.h"
#include "slipgate/sg_chat.h"           /* the one owner of the say_team channel */

sg_team_belief_t sg_caco_team_belief;   /* [0]=red beliefs about red flag etc */

/*
 * The last enemy death each team knows about, by team of the VICTIM.
 * Obituaries are broadcast text -- a kill is common knowledge the frame
 * it happens -- so this is belief, not omniscience. The attack surge
 * reads it: a defender dead near their own stand opens a respawn-wide
 * window, and the window is for sprinting, not waiting.
 */
vec3_t	sg_caco_death_org[2];
float	sg_caco_death_time[2] = { -1000.0f, -1000.0f };

void SG_NoteDeath(edict_t *victim)
{
	int t;

	if (!victim->client)
		return;
	t = victim->client->ctf.teamnum;
	if (t != CTF_TEAM_RED && t != CTF_TEAM_BLUE)
		return;
	VectorCopy(victim->s.origin, sg_caco_death_org[t - CTF_TEAM_RED]);
	sg_caco_death_time[t - CTF_TEAM_RED] = level.time;
}

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
	/* one namer for every mouth: the curated landmark table in sg_chat.c.
	 * This function's own nearest-anything scan named positions by health
	 * boxes ("by the Health", 25 times a game) and once by the carried
	 * flag itself ("enemy has our flag, by the Enemy Flag"). */
	SG_ChatLocName(origin, buf, size);
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
 * SG_BotClientCommand(client, "say_team", line, NULL) routes the arguments into
 * the redirected gi.argv and runs ClientCommand -> Cmd_Say_f for that client,
 * exactly as bl_know.c's Know_Speak does. Teammates -- human ones included --
 * read it in their own chat.
 *
 * The call itself is made by sg_chat.c: one module owns the say_team channel
 * so that the per-bot budget is counted once across every SLIPGATE line, not
 * once per emitter. The team gap below is only recorded when the line
 * actually went out -- a suppressed line the team never heard must not be
 * booked as said.
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

			if (SG_ChatSayTeam(sp, c->line, SG_CHAT_TOPIC_CACO))
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

				/*
				 * The line itself is sg_chat.c's: it names the spot by
				 * the landmark a player would name it by, and it shares
				 * the one say_team budget with every other bot line. The
				 * belief transition, and who saw it, are decided here.
				 */
				if (material)
					SG_ChatCarrierSeen(viewer, enemy_of + 1, p);
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

		/*
		 * The team's word for what this bot just looked at. sg_chat.c keeps
		 * its own record of what the team has been TOLD, which is not this
		 * belief: the respawn clock below moves the belief with nobody
		 * looking, and a bot saying "quad is up" off a clock would be
		 * claiming a look nobody took.
		 */
		SG_ChatItemSeen(viewer, i,
		                (e->solid != SOLID_NOT) ? true : false);
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
 * Which row of the sighting table this client gets: his own if he already
 * has one, else an empty one, else the stalest -- a full table forgets the
 * oldest thing it knows, which is the one least likely to still be true.
 * Split out because the eye, the ear and the damage ring all write this
 * table and must agree on where; the eviction rule is not a thing to have
 * three of.
 */
static int Caco_EnemySlot(sg_belief_enemy_t *tab, int client)
{
	int s, slot = -1;

	for (s = 0; s < SG_MAX_ENEMY_TRACK && slot < 0; s++)
		if (tab[s].client == client)
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
	return slot;
}

/*
 * The shared writer over that slot rule, used by the eye
 * (Caco_ScanEnemies, below) and the ear (SG_NoteSound, further down).
 * `seen` is what separates the two callers: an eye entry is exact and
 * carries the rune tell, an ear entry is a region and carries neither.
 */
static void Caco_EnemyPlace(int team1, int client, int seed, qboolean seen,
                            qboolean runed)
{
	sg_belief_enemy_t *tab = sg_caco_enemies[team1];
	int slot = Caco_EnemySlot(tab, client);

	/* a fresh eye entry outranks an ear: don't degrade it */
	if (!seen && tab[slot].client == client && !tab[slot].heard_only &&
	    level.time - tab[slot].seen_time < 2.0f)
		return;

	tab[slot].client = client;
	tab[slot].seed = seed;
	tab[slot].seen_time = level.time;
	tab[slot].heard_only = !seen;
	if (seen)
		tab[slot].runed = runed;
}

/*
 * Every enemy a teammate lays eyes on, not just carriers: the defender's
 * pre-spin and the rune-threat weighting both need "someone is out there
 * and roughly where". The RF_GLOW test is the rune tell (p_view.c:792-794).
 *
 * EYES ONLY. This used to grow an "ear" here by polling two server-private
 * fields -- client->weaponstate == WEAPON_FIRING and client->hookstate --
 * and, when either was set inside the PHS, filing the enemy's EXACT origin
 * as a heard_only belief. That was fabricated hearing twice over: it heard
 * states rather than sounds (so it was deaf to every other noise a player
 * makes, and it "heard" a held-down trigger continuously rather than per
 * shot), and having no sound to measure it had no distance or volume to
 * degrade the position with, so the ear was quietly as accurate as the eye.
 * Hearing now enters through SG_NoteSound, off the real sound calls.
 */
static void Caco_ScanEnemies(rune_t *r, edict_t *viewer, int viewer_team)
{
	int i;

	for (i = 0; i < game.maxclients; i++)
	{
		edict_t *p = g_edicts + 1 + i;

		if (!p->inuse || !p->client || p->deadflag)
			continue;
		if (p->client->ctf.teamnum == viewer_team ||
		    p->client->ctf.teamnum <= CTF_TEAM_UNDEFINED)
			continue;
		if (!Caco_Visible(viewer, p))
			continue;

		Caco_EnemyPlace(viewer_team - 1, i, Rune_NearestSeed(r, p->s.origin),
		                true, (p->s.renderfx & RF_GLOW) != 0);
	}
}

/* ------------------------------------------------------------------- the ear
 *
 * Called from the sound wrappers in sg_net.c for every gi.sound and
 * gi.positioned_sound the mod issues, AFTER the engine has been handed the
 * call through unchanged. Nothing here touches the wire; this is a tap on a
 * real event, which is the whole difference from what it replaces.
 *
 * WHAT A BOT IS ALLOWED TO LEARN. Two engine facts and nothing else: gi.inPHS,
 * which is precisely "could a sound made there be heard here", and the
 * attenuation the caller passed, which is what decides how far the sound
 * actually carries for a human client. No server-private player state is read.
 *
 * THE RADII. Quake II's client mixes a sound at
 * scale = 1 - (dist - SOUND_FULLVOLUME) * attenuation * 0.0005
 * (snd_dma.c, S_SpatializeOrigin) with SOUND_FULLVOLUME 80, so a sound is
 * inaudible past 80 + 2000/attenuation units:
 *
 *     ATTN_NONE   (0)  map-wide -- the engine sends it to every client
 *     ATTN_NORM   (1)  2080u    -- weapons, pain, most of what matters
 *     ATTN_IDLE   (2)  1080u    -- quieter incidentals
 *     ATTN_STATIC (3)   746u    -- very short
 *
 * scaled by the volume the caller asked for, since a half-volume sound hits
 * the same floor at half the distance. Past that radius the bot is not told.
 * ATTN_NONE skips the PHS test as well as the radius, because the engine
 * itself ignores both for a full-volume-everywhere sound.
 *
 * WHAT IT PLACES. A region, not a fix. The error grows with distance and
 * shrinks with volume -- both through the same frac, the share of the audible
 * radius the sound had to cross -- and the belief is snapped to the nearest
 * rune seed of a point offset from the truth by up to frac * 300 units. A shot
 * at the edge of hearing lands the enemy up to three hundred units from where
 * they really are; a shot in the next room lands close. It is filed
 * heard_only, which every consumer already reads as "warn a post, never aim"
 * (sg_combat.c, sg_arach.c, sg_chat.c).
 */

#define SG_EAR_FULLVOL	80.0f       /* SOUND_FULLVOLUME */
#define SG_EAR_SPAN	2000.0f     /* 1 / 0.0005, the client's distance slope */
#define SG_EAR_SPREAD	300.0f      /* worst-case placement error, units */
#define SG_EAR_MAPWIDE	2080.0f     /* ATTN_NONE has no falloff to measure
                                     * against, so the ATTN_NORM radius is the
                                     * yardstick for how vague it is */

void SG_NoteSound(edict_t *emitter, vec3_t origin_or_null, int channel,
                  int soundindex, float volume, float attenuation)
{
	rune_t *r = SG_Rune();
	vec3_t sorg;
	int eteam, ecl, i;

	if (!r)
		return;                     /* no rune loaded: nowhere to place onto */
	if (!emitter || !emitter->inuse || !emitter->client)
		return;                     /* world noise names nobody */

	eteam = emitter->client->ctf.teamnum;
	if (eteam <= CTF_TEAM_UNDEFINED)
		return;
	if (volume <= 0.0f)
		return;
	if (volume > 1.0f)
		volume = 1.0f;

	if (origin_or_null)
		VectorCopy(origin_or_null, sorg);
	else
		VectorCopy(emitter->s.origin, sorg);

	ecl = (int)(emitter - g_edicts) - 1;
	if (ecl < 0 || ecl >= game.maxclients)
		return;

	for (i = 0; i < game.maxclients; i++)
	{
		edict_t *b = g_edicts + 1 + i;
		vec3_t d, guess;
		float dist, radius, frac;
		int seed, team;

		if (!b->inuse || !b->client || b->deadflag)
			continue;
		if (b == emitter)
			continue;
		if (!SG_OwnsBot(b))
			continue;               /* only SLIPGATE bots listen through here */

		team = b->client->ctf.teamnum;
		if (team <= CTF_TEAM_UNDEFINED || team == eteam)
			continue;               /* teammates are not tracked as enemies */

		VectorSubtract(sorg, b->s.origin, d);
		dist = VectorLength(d);

		if (attenuation > 0.0f)
		{
			radius = (SG_EAR_FULLVOL + SG_EAR_SPAN / attenuation) * volume;
			if (dist > radius)
				continue;           /* out of earshot */
			if (!gi.inPHS(b->s.origin, sorg))
				continue;           /* no path for the sound to travel */
		}
		else
		{
			radius = SG_EAR_MAPWIDE;
		}

		frac = (radius > 0.0f) ? dist / radius : 1.0f;
		if (frac > 1.0f)
			frac = 1.0f;

		guess[0] = sorg[0] + (float)crandom() * frac * SG_EAR_SPREAD;
		guess[1] = sorg[1] + (float)crandom() * frac * SG_EAR_SPREAD;
		guess[2] = sorg[2] + (float)crandom() * frac * SG_EAR_SPREAD;

		seed = Rune_NearestSeed(r, guess);
		if (seed < 0)
			continue;

		Caco_EnemyPlace(team - 1, ecl, seed, false, false);

		if (gi.cvar("sg_debug", "0", 0)->value)
			gi.dprintf("EAR %s heard %s snd=%i chan=%i d=%.0f r=%.0f "
			           "err<=%.0f seed=%i\n",
			           b->client->pers.netname,
			           emitter->client->pers.netname,
			           soundindex, channel, dist, radius,
			           frac * SG_EAR_SPREAD, seed);
	}
}

/* ------------------------------------------------------------ the hit sense */

sg_damage_hit_t sg_caco_damage[SG_DMG_CLIENTS][SG_DMG_RING];

/*
 * Did this land on a line that existed for no time at all? Pellets and
 * slugs did; everything else -- blaster bolts included, they fly -- had to
 * travel to get here.
 */
static qboolean Caco_Hitscan(int mod)
{
	switch (mod)
	{
	case MOD_SHOTGUN:
	case MOD_SSHOTGUN:
	case MOD_MACHINEGUN:
	case MOD_CHAINGUN:
	case MOD_RAILGUN:
		return true;
	default:
		return false;
	}
}

/* how far back down the incoming line a projectile's firing point is
 * believed to be, world permitting -- a region, deliberately coarse */
#define SG_DMG_BACKTRACK	512.0f

/*
 * The third sense, called from T_Damage at the one site where damage
 * actually lands (g_combat.c, beside SG_CombatHit). Every test that decides
 * whether a hit is interesting lives here: the game file is not the place
 * to know what a bot believes.
 *
 * What a hit tells you depends entirely on what hit you, and the split is
 * hitscan against flight:
 *
 *   A slug or a burst of machinegun arrives down a straight line that
 *   existed for zero time. The shooter WAS at the far end of it at the
 *   instant it landed, so his real origin is honest evidence and is what
 *   gets believed. A hit genuinely reveals the man.
 *
 *   A rocket or a grenade was fired seconds ago from somewhere the shooter
 *   has since left, and has been travelling ever since. Its arrival says
 *   only "from that way", so the belief is placed back along the line as
 *   far as the world allows and no further -- a region, not a point.
 *
 *   Splash is looser still. For MOD_*_SPLASH the direction T_Damage hands
 *   over runs from the DETONATION, not from the shooter, so what gets
 *   believed is roughly where the thing went off. Worth knowing, and not
 *   the same fact; this is written down so nobody later reads it as one.
 *
 * Neither branch clears heard_only. The bot did not see the man. A belief
 * placed by pain is good enough to warn a post and to swing a scan cone
 * around, and never good enough to aim at, which is what that flag has
 * meant downstream since the ear first set it.
 */
void SG_NoteDamage(edict_t *victim, edict_t *attacker, int damage, int mod,
                   vec3_t dir)
{
	sg_damage_hit_t	*ring, *slot;
	vec3_t			eye, from;
	qboolean		seen, hitscan;
	int				ci, ac, team, i;

	if (!victim || !victim->inuse || !victim->client)
		return;
	if (!(victim->flags & FL_BOT))
		return;
	if (!attacker || !attacker->inuse || !attacker->client ||
	    attacker == victim)
		return;

	mod &= ~MOD_FRIENDLY_FIRE;
	team = victim->client->ctf.teamnum;
	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return;
	if (attacker->client->ctf.teamnum == team)
		return;		/* a teammate's rocket is regret, not contact */

	ci = (int)(victim->client - game.clients);
	ac = (int)(attacker->client - game.clients);
	if (ci < 0 || ci >= SG_DMG_CLIENTS || ac < 0)
		return;

	VectorCopy(victim->s.origin, eye);
	eye[2] += victim->viewheight;

	/*
	 * Where it came from is the reverse of the way it was going. T_Damage
	 * normalises dir before any of this (g_combat.c), but a few damage
	 * paths hand over a zero vector, and for those the bearing to the man
	 * himself is the only direction there is.
	 */
	VectorNegate(dir, from);
	if (VectorNormalize(from) < 0.1f)
	{
		VectorSubtract(attacker->s.origin, eye, from);
		if (VectorNormalize(from) < 1.0f)
			return;
	}

	seen = Caco_Visible(victim, attacker);
	hitscan = Caco_Hitscan(mod);

	/* oldest entry loses: four of them, and no head index to keep in sync */
	ring = sg_caco_damage[ci];
	slot = ring;
	for (i = 1; i < SG_DMG_RING; i++)
		if (ring[i].time < slot->time)
			slot = &ring[i];

	slot->attacker = ac;
	slot->mod = mod;
	slot->damage = damage;
	slot->time = level.time;
	slot->unseen = !seen;
	VectorCopy(from, slot->from);

	if (seen)
		return;		/* the eye already has him; belief needs nothing */

	{
		rune_t	*r = SG_Rune();
		vec3_t	pos;
		int		seed;

		if (!r)
			return;

		if (hitscan)
			VectorCopy(attacker->s.origin, pos);
		else
		{
			trace_t	tr;
			vec3_t	back;

			VectorMA(eye, SG_DMG_BACKTRACK, from, back);
			tr = gi.trace(eye, NULL, NULL, back, victim, MASK_OPAQUE);
			VectorCopy(tr.endpos, pos);
			/* off whatever surface it stopped against, so the seed lookup
			 * cannot snap to a node on the far side of that wall */
			VectorMA(pos, -16.0f, from, pos);
		}

		seed = Rune_NearestSeed(r, pos);
		if (seed >= 0)
		{
			sg_belief_enemy_t	*tab = sg_caco_enemies[team - 1];
			int					s = Caco_EnemySlot(tab, ac);

			/* a fresh eye entry outranks a hit, the same way it outranks
			 * an ear -- pain places a man coarsely at best */
			if (!(tab[s].client == ac && !tab[s].heard_only &&
			      level.time - tab[s].seen_time < 2.0f))
			{
				if (tab[s].client != ac)
					tab[s].runed = false;	/* a hit says nothing about glow */
				tab[s].client = ac;
				tab[s].seed = seed;
				tab[s].seen_time = level.time;
				tab[s].heard_only = true;
			}
		}

		if (gi.cvar("sg_debug", "0", 0)->value)
			gi.dprintf("HITFROM %s<%s dmg=%d mod=%d %s seed=%d dir=%.2f,%.2f,%.2f\n",
			           victim->client->pers.netname,
			           attacker->client->pers.netname, damage, mod,
			           hitscan ? "hitscan" : "flight", seed,
			           from[0], from[1], from[2]);
	}
}

/*
 * The newest hit inside the window whose shooter this bot could not see.
 * Hits with the man in plain sight are skipped on purpose: the scan already
 * has him, and steering attention toward a target it is currently holding
 * would be a bias that does nothing.
 */
qboolean SG_RecentUnseenHit(edict_t *self, float window, vec3_t out_from)
{
	sg_damage_hit_t	*ring, *best = NULL;
	int				ci, i;

	if (!self || !self->client || window <= 0.0f)
		return false;
	ci = (int)(self->client - game.clients);
	if (ci < 0 || ci >= SG_DMG_CLIENTS)
		return false;

	ring = sg_caco_damage[ci];
	for (i = 0; i < SG_DMG_RING; i++)
	{
		if (ring[i].attacker < 0 || !ring[i].unseen)
			continue;
		if (level.time - ring[i].time > window)
			continue;
		if (!best || ring[i].time > best->time)
			best = &ring[i];
	}
	if (!best)
		return false;
	if (out_from)
		VectorCopy(best->from, out_from);
	return true;
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
	SG_ChatSee(viewer);                 /* body/power armour: not belief classes */
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
	SG_ChatFrame();
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
	memset(sg_caco_damage, 0, sizeof(sg_caco_damage));
	{
		int c, k;

		/* a zeroed entry names client 0, who is a real player; -1 is the
		 * only thing that means "nothing landed here" */
		for (c = 0; c < SG_DMG_CLIENTS; c++)
			for (k = 0; k < SG_DMG_RING; k++)
				sg_caco_damage[c][k].attacker = -1;
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

	/* after the item scan: sg_chat.c seeds its told-state from it */
	SG_ChatReset();

	caco_next_scan = 0.0f;
	caco_next_human = 0.0f;
	caco_next_advect = 0.0f;
}
