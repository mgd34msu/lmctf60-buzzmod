

#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_net.h"                    /* SG_BotClientCommand -- the chat route */
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_chat.h"           /* the one owner of the say_team channel */
#include "slipgate/sg_lead.h"
#include "slipgate/sg_goal.h"
#include "slipgate/sg_item_policy.h"
#include "slipgate/sg_item_route.h"
#include "slipgate/sg_sound_policy.h"
#include "slipgate/sg_death_belief.h"
#include "slipgate/sg_role_policy.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_util.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_callout_random.h"
#include "slipgate/sg_callout_policy.h"
#include "slipgate/sg_ear_random.h"

sg_team_belief_t sg_caco_team_belief;   /* [0]=red beliefs about red flag etc */

/* Last enemy death position known by each observing team.  The obituary
 * supplies identity/time; the seed must pre-exist in that team's sensor table. */
static int sg_caco_enemy_death_seed[2] = { -1, -1 };
static float sg_caco_enemy_death_time[2] = { -1000.0f, -1000.0f };

void SG_NoteDeath(edict_t *victim)
{
	SG_ChatMegaDeath(victim);   /* the mega clock's one honest trigger */
	int client, observer, seed, t;

	if (!victim->client)
		return;
	t = victim->client->ctf.teamnum;
	if (t != CTF_TEAM_RED && t != CTF_TEAM_BLUE)
		return;
	client = (int)(victim - g_edicts) - 1;
	observer = SG_TeamIdx(SG_EnemyTeam(t));
	seed = SG_DeathBeliefSeed(sg_caco_enemies[observer],
	    SG_MAX_ENEMY_TRACK, client, level.time, SG_BELIEF_STALE,
	    SG_Rune() ? SG_Rune()->hdr.num_seeds : 0);
	sg_caco_enemy_death_seed[observer] = seed;
	sg_caco_enemy_death_time[observer] =
	    seed >= 0 ? level.time : -1000.0f;
	/* The obituary is public, so no team may keep pricing this body as a live
	 * sighting through the respawn window.  Capture the already-earned seed
	 * above, then retire every client-indexed fact from the old life. */
	Caco_ResetClient(victim);
}

qboolean SG_EnemyRoomDeathKnown(int team, const vec3_t stand_origin,
	float max_age, float max_distance)
{
	vec3_t delta;
	int observer, seed;

	if ((team != CTF_TEAM_RED && team != CTF_TEAM_BLUE) || !stand_origin ||
	    !SG_Rune() || !isfinite(max_age) || max_age < 0.0f ||
	    !isfinite(max_distance) || max_distance < 0.0f)
		return false;
	observer = SG_TeamIdx(team);
	seed = sg_caco_enemy_death_seed[observer];
	if (seed < 0 || seed >= SG_Rune()->hdr.num_seeds ||
	    !SG_AgeUnder(sg_caco_enemy_death_time[observer], max_age))
		return false;
	VectorSubtract(SG_Rune()->seeds[seed].origin, stand_origin, delta);
	return VectorLength(delta) < max_distance;
}

static float caco_next_scan;
static float caco_next_human;
static float caco_next_advect;

static void Caco_InvalidateOurCarrierField(rune_t *r, int team)
{
	int seed;

	sg_fields.our_carrier_valid[team] = false;
	if (!r || !sg_fields.our_carrier[team])
		return;
	for (seed = 0; seed < r->hdr.num_seeds; seed++)
		sg_fields.our_carrier[team][seed] = SG_FIELD_INF;
}

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
	unsigned long	speaker_ctfid;     /* exact client life that queued it */
	float		due;
	char		line[160];
} sg_callout_t;

static sg_callout_t	caco_callout[2][SG_CALL_TOPICS];    /* [team-1][topic] */
static float		caco_teamsaid[2][SG_CALL_TOPICS];
static uint32_t		caco_callout_random[2][SG_CALL_TOPICS];

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

	if (!sg_host.in_pvs(eye, mid))
		return false;

	/* Optional cone and range limits reduce visual belief acquisition. */
	{
		float cone = sg_cv.beliefcone->value;
		float range = sg_cv.beliefrange->value;
		vec3_t to;

		VectorSubtract(mid, eye, to);
		if (range > 0.0f && VectorLength(to) > range)
			return false;
		if (cone > 0.0f && viewer->client)
		{
			vec3_t fwd;
			float d;

			AngleVectors(viewer->client->v_angle, fwd, NULL, NULL);
			VectorNormalize(to);
			d = DotProduct(to, fwd);
			if (d < cos(cone * 0.5f * M_PI / 180.0f))
				return false;
		}
	}

	tr = sg_host.trace(eye, NULL, NULL, mid, viewer, MASK_OPAQUE);
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
	if (speaker->client->ctf.ctfid == 0)
		return;
	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return;
	if (topic < 0 || topic >= SG_CALL_TOPICS)
		return;

	{
		int team_index = SG_TeamIdx(team);

		c = &caco_callout[team_index][topic];
		if (c->pending)
			return;                     /* someone is already about to say it */
		if (level.time < caco_teamsaid[team_index][topic])
			return;                     /* the team heard this a moment ago */
		caco_callout_random[team_index][topic] = SG_CalloutRandomNext(
		    caco_callout_random[team_index][topic]);
	}

	Caco_Where(origin, place, sizeof(place));
	Com_sprintf(c->line, sizeof(c->line), "%s %s", what, place);
	c->speaker = (int)(speaker - g_edicts - 1);
	c->speaker_ctfid = speaker->client->ctf.ctfid;
	c->due = level.time + SG_CalloutRandomDelay(
	    caco_callout_random[SG_TeamIdx(team)][topic],
	    SG_CALL_DELAY_MIN, SG_CALL_DELAY_MAX);
	c->pending = true;
}


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
			if (!SG_CalloutSpeakerCurrent(c->speaker_ctfid,
			    sp->client->ctf.ctfid))
				continue;
			if (sp->client->ctf.teamnum != SG_TeamFromIdx(t))
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
		int fi = SG_TeamIdx(e->flagteam);
		int bt;
		sg_belief_flag_t *b = NULL;
		edict_t *look;
		qboolean was_unknown, carried;

		/* LMCTF hides a carried flag but leaves its entity at the take
		 * position, which is normally the stand.  Geometry alone therefore
		 * reports HOME throughout an ordinary carry; the owner/inventory pair
		 * is the authoritative HUD-level fact. */
		carried = e->owner && e->owner->inuse && e->owner->client &&
		          ClientHasFlag(e->owner) == e;

		/* common knowledge: home or not (HUD) */
		if (!carried && ctf_flagathome(e))
		{
			for (bt = 0; bt < 2; bt++)
			{
				sg_caco_team_belief.flag[bt][fi].state = SG_FLAG_HOME;
				sg_caco_team_belief.flag[bt][fi].where_seed = -1;
			}
			continue;
		}

		/* not home. carried or dropped is also HUD-level in LMCTF
		 * (the score line shows taken flags), but WHERE requires sight. */
		for (bt = 0; bt < 2; bt++)
		{
			sg_belief_flag_t *row = &sg_caco_team_belief.flag[bt][fi];

			if (row->state == SG_FLAG_HOME)
			{
				row->state = SG_FLAG_ASTRAY;
				row->where_seed = -1;     /* until this team sees it */
				row->seen_time = 0.0f;
			}
		}

		/*
		 * A flag in somebody's hands does not move its own entity: LMCTF
		 * hides the model, hands it to the player (g_ctffunc.c
		 * ctf_flagtouch) and leaves the flag edict sitting where it was
		 * taken from. So the thing to look at, and the position worth
		 * believing, is the carrier -- not the invisible flag edict, which
		 * would hand us a spot nobody is standing in.
		 */
		look = carried ? e->owner : e;
		if (viewer && (viewer_team == CTF_TEAM_RED ||
		               viewer_team == CTF_TEAM_BLUE))
			b = &sg_caco_team_belief.flag[SG_TeamIdx(viewer_team)][fi];

		was_unknown = (!b || b->where_seed < 0) ? true : false;

		if (viewer && b && Caco_Visible(viewer, look))
		{
			b->where_seed = Rune_NearestSeed(r, look->s.origin);
			b->seen_time = level.time;

			/*
			 * Our own flag, astray and until now unlocated, is the one the
			 * team most needs to hear about.
			 */
			if (was_unknown && b->where_seed >= 0 && fi == SG_TeamIdx(viewer_team))
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
	int ti = SG_TeamIdx(viewer_team);
	qboolean have_ours = false;

	for (i = 0; i < game.maxclients; i++)
	{
		edict_t *p = g_edicts + 1 + i;
		edict_t *held;
		int carried, enemy_of;

		if (!p->inuse || !p->client)
			continue;
		held = ClientHasFlag(p);
		if (!held)
			continue;

		/* Inventory is carrier truth. A dropped flag deliberately retains its
		 * former owner for one second to prevent immediate self-repickup, and
		 * G_SetClientEffects mirrors that owner into EF_FLAGx. Neither means the
		 * player still carries it. */
		if (held == redflag)
			carried = 0;
		else if (held == blueflag)
			carried = 1;
		else
			continue;
		/* the team whose flag is carried wants to know the carrier */
		enemy_of = carried;

		if (p->client->ctf.teamnum == viewer_team)
		{
			/* our own carrier: identity is team knowledge, position from
			 * sighting (including the carrier seeing themself) */
			sg_belief_carrier_t *c =
				&sg_caco_team_belief.carrier[ti];

			have_ours = true;

			/* A position belongs to one carrier, not to the team slot.  Until
			 * the replacement is actually seen, its location is unknown. */
			if (c->client != i)
			{
				c->client = i;
				c->seed = -1;
				c->seen_time = 0.0f;
				Caco_InvalidateOurCarrierField(r, ti);
			}
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

	/* Losing the HUD carrier identity is knowledge too.  Do not let the
	 * departed carrier's last position survive until the aging cadence. */
	if (!have_ours)
	{
		sg_belief_carrier_t *c = &sg_caco_team_belief.carrier[ti];

		if (c->client >= 0 || c->seed >= 0 || sg_fields.our_carrier_valid[ti])
		{
			c->client = -1;
			c->seed = -1;
			c->seen_time = 0.0f;
			Caco_InvalidateOurCarrierField(r, ti);
		}
	}
}



/* g_runes.c:10, RUNETHINKTIME -- how long before a rune moves on its own */
#define SG_RUNE_WANDER	30.0f

sg_belief_item_t	sg_caco_items[2][SG_MAX_BELIEF_ITEMS];
int					sg_caco_num_items;


qboolean SG_ItemComm(void)
{
	return (sg_cv.itemcomm->value > 0.0f) ? true : false;
}

/* row index for a team number, and the "not on a team" case folded to red so
 * a stray caller reads a valid row rather than off the end of the table */
static int Caco_TeamRow(int team)
{
	return (team == CTF_TEAM_BLUE) ? 1 : 0;
}

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

/* the index of an entity's belief slot, valid in BOTH rows, -1 when the
 * entity is not one of the belief classes */
static int Caco_ItemIndex(edict_t *e)
{
	int i, num = (int)(e - g_edicts);

	for (i = 0; i < sg_caco_num_items; i++)
		if (sg_caco_items[0][i].ent == num)
			return i;
	return -1;
}

static sg_belief_item_t *Caco_ItemBelief(int team, edict_t *e)
{
	int i = Caco_ItemIndex(e);

	return (i < 0) ? NULL : &sg_caco_items[Caco_TeamRow(team)][i];
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
		int					cls, t, slot;

		if (!e->inuse)
			continue;
		cls = Caco_ItemClassOf(e);
		if (cls < 0)
			continue;

		/* somebody's dropped quad is not a place on the map */
		if (cls == SG_BI_POWERUP && (e->spawnflags & DROPPED_ITEM))
			continue;

		slot = sg_caco_num_items++;

		/*
		 * Both rows, identically. A pad's position and its respawn delay are
		 * map knowledge -- read once, off the item definition, and handed to
		 * both sides because both sides have played the map. What the split
		 * buys is the DYNAMIC half below, which each team then moves on its
		 * own sightings and its own team chat.
		 */
		for (t = 0; t < 2; t++)
		{
			b = &sg_caco_items[t][slot];
			b->ent = i;
			b->cls = cls;
			b->seed = -1;
			b->seen_up_time = -1.0f;            /* nobody has looked yet */
			b->believed_respawn_time = 0.0f;
			/* the errand lease starts free, and free is -1 rather than the
			 * zero memset leaves: client 0 is a real player */
			b->claimed_until = 0.0f;
			b->claimed_by = -1;

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
				b->respawn_delay = 0.0f;    /* no clock exists to infer from */
				b->believed_up = false;     /* and no location either, yet */
			}
		}
	}
}

/* Cache fixed mega-health spawn entities for direct worth checks. */
static int	sg_mega_ents[SG_MAX_MEGA];
static int	sg_mega_ent_count;

static void Caco_ScanMegaSpawns(void)
{
	int i;

	sg_mega_ent_count = 0;
	for (i = 0; i < globals.num_edicts && sg_mega_ent_count < SG_MAX_MEGA; i++)
	{
		edict_t *e = &g_edicts[i];

		if (!e->inuse || !e->classname)
			continue;
		if (strcmp(e->classname, "item_health_mega") != 0)
			continue;
		sg_mega_ents[sg_mega_ent_count++] = i;
	}
}

/* Return cached mega-health entity numbers in ascending scan order. */
int SG_MegaEntities(const int **out_ents)
{
	*out_ents = sg_mega_ents;
	return sg_mega_ent_count;
}

/*
 * One bot's eyes on the item entities. Looking at the pad is what settles it,
 * either way: the model standing there says up, an empty pad says taken. A
 * taken powerup starts its clock; a taken rune starts nothing, because there
 * is nothing to start.
 */
static void Caco_ScanItems(rune_t *r, edict_t *viewer)
{
	qboolean	comm;
	int			i, mine, t;

	if (!r || !viewer || !viewer->client)
		return;

	comm = SG_ItemComm();
	mine = Caco_TeamRow(viewer->client->ctf.teamnum);

	for (i = 0; i < sg_caco_num_items; i++)
	{
		edict_t	*e = g_edicts + sg_caco_items[0][i].ent;
		qboolean up;

		if (!e->inuse)
			continue;
		if (!Caco_Visible(viewer, e))
			continue;

		up = (e->solid != SOLID_NOT) ? true : false;

		/*
		 * SEEING IS KNOWING, AND ONLY THAT.
		 * A bot walking past the pad learns whether the thing is standing
		 * there -- that half of the old scan is honest and stays. What the
		 * scan may no longer do is start a countdown: a player who glances
		 * at an empty pad does not know whether it emptied a second ago or
		 * fifty, so the clock is not his to start. It is started by a
		 * teammate SAYING when it went, and by nothing else.
		 */
		for (t = 0; t < 2; t++)
		{
			sg_belief_item_t *b = &sg_caco_items[t][i];

			/* with the ruling off, one bot's eyes still teach both sides,
			 * which is the behaviour this build shipped with */
			if (comm && t != mine)
				continue;

			if (up)
			{
				b->believed_up = true;
				b->seen_up_time = level.time;
				b->believed_respawn_time = 0.0f;    /* a look beats a clock */
				VectorCopy(e->s.origin, b->org);
				b->seed = Rune_NearestSeed(r, b->org);
			}
			else
			{
				b->believed_up = false;
				if (!comm)
					b->believed_respawn_time = (b->respawn_delay > 0.0f)
						? level.time + b->respawn_delay
						: 0.0f;
				/* comm on: whatever clock this team was TOLD stands; an
				 * empty pad neither confirms nor starts one */
				if (b->cls == SG_BI_RUNE)
					b->seed = -1;   /* it is in somebody's hands now */
			}
		}

		/*
		 * The team's word for what this bot just looked at. sg_chat.c keeps
		 * its own record of what the team has been TOLD, which is not this
		 * belief: the respawn clock below moves the belief with nobody
		 * looking, and a bot saying "quad is up" off a clock would be
		 * claiming a look nobody took.
		 */
		SG_ChatItemSeen(viewer, i, up);
	}
}

static void Caco_AgeItemsRow(rune_t *r, int t)
{
	int i;

	for (i = 0; i < sg_caco_num_items; i++)
	{
		sg_belief_item_t *b = &sg_caco_items[t][i];

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

/* both teams' rows: each side runs its own clocks, off its own callouts */
static void Caco_AgeItems(rune_t *r)
{
	Caco_AgeItemsRow(r, 0);
	Caco_AgeItemsRow(r, 1);
}

/*
 * What one team believes about one item. An item outside the belief classes
 * (weapons, armour, ammo, health) is answered exactly as the field code
 * answers it today, so wiring these in changes only the two classes that are
 * meant to change.
 */
qboolean Caco_ItemBelievedUpFor(int team, edict_t *e)
{
	sg_belief_item_t *b;

	if (!e || !e->inuse || !e->classname)
		return false;
	b = Caco_ItemBelief(team, e);
	if (!b)
		return (e->solid != SOLID_NOT) ? true : false;
	return b->believed_up;
}

qboolean Caco_ItemBelievedRouteableFor(int team, edict_t *e)
{
	sg_belief_item_t *b;
	qboolean respawn_within_lead = false;
	int route_class;

	if (!e || !e->inuse || !e->classname)
		return false;
	b = Caco_ItemBelief(team, e);
	if (!b)
		return e->solid != SOLID_NOT;
	route_class = b->cls == SG_BI_POWERUP ? SG_FC_POWERUP : SG_FC_RUNE;
	if (b->cls == SG_BI_POWERUP && !b->believed_up &&
	    b->believed_respawn_time > 0.0f &&
	    b->believed_respawn_time - level.time <=
	        SG_ITEM_ROUTE_POWERUP_LEAD_SECONDS)
		respawn_within_lead = true;
	return SG_IdentityItemBeliefAdmission(route_class, b->believed_up,
	    respawn_within_lead);
}

qboolean Caco_ItemBelievedRouteable(edict_t *e)
{
	return (Caco_ItemBelievedRouteableFor(CTF_TEAM_RED, e) ||
	        Caco_ItemBelievedRouteableFor(CTF_TEAM_BLUE, e)) ? true : false;
}


qboolean Caco_ItemBelievedUp(edict_t *e)
{
	return (Caco_ItemBelievedUpFor(CTF_TEAM_RED, e) ||
	        Caco_ItemBelievedUpFor(CTF_TEAM_BLUE, e)) ? true : false;
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
	int			i, t;

	/* both rows: the field is shared, so it has to rebuild when EITHER side's
	 * belief moves -- and with sg_itemcomm 0 the rows are identical, so this
	 * changes value at exactly the moments it always did */
	for (t = 0; t < 2; t++)
		for (i = 0; i < sg_caco_num_items; i++)
		{
			sg_belief_item_t *b = &sg_caco_items[t][i];
			qboolean lead = false;

			if (b->cls == SG_BI_POWERUP && !b->believed_up &&
			    b->believed_respawn_time > 0.0f &&
			    b->believed_respawn_time - level.time <=
			        SG_ITEM_ROUTE_POWERUP_LEAD_SECONDS)
				lead = true;
			sig = (sig ^ (unsigned)b->believed_up) * 16777619u;
			sig = (sig ^ (unsigned)lead) * 16777619u;
			sig = (sig ^ (unsigned)b->seed) * 16777619u;
		}
	return sig;
}

int Caco_ItemBeliefSeed(rune_t *r, edict_t *e)
{
	int i, t;

	if (!e || !e->inuse)
		return -1;
	i = Caco_ItemIndex(e);
	if (i < 0)
		return Rune_NearestSeed(r, e->s.origin);

	/* the shared field again: seeded from whichever side still believes in
	 * the thing, for the reason written over Caco_ItemBelievedUp */
	for (t = 0; t < 2; t++)
		if (Caco_ItemBelievedRouteableFor(t ? CTF_TEAM_BLUE : CTF_TEAM_RED, e))
			return sg_caco_items[t][i].seed;
	return -1;
}



/* one of ours, on that team, alive, with a clear sight line to the item at the
 * moment it went. Prefers a bot whose say_team budget is free, exactly as
 * Chat_Speaker does, so the witness call comes out of a mouth that can use it. */
static edict_t *Caco_ItemWitness(edict_t *item, int team, edict_t *taker)
{
	edict_t	*fallback = NULL;
	int		i;

	for (i = 0; i < game.maxclients; i++)
	{
		edict_t *e = g_edicts + 1 + i;

		if (e == taker)
			continue;
		if (!e->inuse || !e->client || !(e->flags & FL_BOT) || !SG_OwnsBot(e))
			continue;
		if (e->client->ctf.teamnum != team)
			continue;
		if (e->deadflag == DEAD_DEAD || e->health <= 0)
			continue;
		if (!Caco_Visible(e, item))
			continue;                   /* it did not see it happen */

		if (SG_ChatBudgetClear(e))
			return e;
		if (!fallback)
			fallback = e;
	}
	return fallback;
}

void SG_NoteItemTaken(edict_t *taker, edict_t *item)
{
	int takerteam, t;
	sg_item_pickup_disposition_t disposition;

	if (!taker || !taker->client || !item || !item->inuse)
		return;
	/*
	 * A dropped copy is not a pad. It has no respawn (g_items.c:198 and :759
	 * both skip SetRespawn for DROPPED_ITEM), it is not in the belief table
	 * for the same reason Caco_ScanItemSpawns skips it, and "quad is back in
	 * 60" said about a corpse's quad would be a lie with a number on it.
	 */
	SG_DefenseSupplyNoteItemTouch(taker, item);
	SG_StrikeWeaponNoteItemTouch(taker, item);
	disposition = SG_ItemPickupDisposition(true,
	    (item->spawnflags & (DROPPED_ITEM | DROPPED_PLAYER_ITEM)) != 0,
	    SG_ChatItemMajor(item), SG_ItemComm());
	if (disposition == SG_ITEM_PICKUP_IGNORE)
		return;

	/* The physical pickup closes any matching early-return commitment before
	 * belief/chat bookkeeping can move the clock.  Touch_Item calls us only
	 * after the engine accepted the pickup, so this covers both inventory and
	 * instant-use powerups without guessing from client state. */
	Lead_NoteItemTaken(taker, item);
	if (disposition != SG_ITEM_PICKUP_COMMIT_AND_COMMUNICATE)
		return;                     /* no shared belief or callout */

	takerteam = taker->client->ctf.teamnum;

	for (t = 0; t < 2; t++)
	{
		int		team = SG_TeamFromIdx(t);
		int		src;
		edict_t	*speaker;

		if (team == takerteam && SG_OwnsBot(taker) && (taker->flags & FL_BOT))
		{
			/* (a) it is holding the thing; no eyes are needed and no other
			 * bot on the team is asked to speak for it */
			speaker = taker;
			src = SG_ITEMCALL_TAKER;
		}
		else
		{
			/* (b) and (c): somebody else took it and one of ours watched */
			speaker = Caco_ItemWitness(item, team, taker);
			if (!speaker)
				continue;           /* nobody on this side saw a thing */
			src = (team == takerteam) ? SG_ITEMCALL_MATE : SG_ITEMCALL_ENEMY;
		}

		SG_ChatItemTaken(speaker, team, item, src, taker);
	}
}

void SG_NoteItemRejected(edict_t *taker, edict_t *item)
{
	/* A refusal is private physical state of the touching bot. It neither
	 * changes team belief nor earns a callout; it only closes the exact local
	 * errand that has now proved unable to collect its target. */
	SG_DefenseSupplyNoteItemTouch(taker, item);
	SG_StrikeWeaponNoteItemTouch(taker, item);
	Lead_NoteItemRejected(taker, item);
}

static void Caco_Age(rune_t *r)
{
	int i, bt;

	Caco_AgeItems(r);

	for (i = 0; i < 2; i++)
	{
		for (bt = 0; bt < 2; bt++)
			if (sg_caco_team_belief.flag[bt][i].where_seed >= 0 &&
			    level.time - sg_caco_team_belief.flag[bt][i].seen_time >
			        SG_BELIEF_STALE)
				sg_caco_team_belief.flag[bt][i].where_seed = -1;

		if (sg_caco_team_belief.carrier[i].seed >= 0 &&
		    level.time - sg_caco_team_belief.carrier[i].seen_time > SG_BELIEF_STALE)
		{
			sg_caco_team_belief.carrier[i].seed = -1;
			Caco_InvalidateOurCarrierField(r, i);
		}

		if (sg_caco_team_belief.enemy_carrier[i].seed >= 0 &&
		    level.time - sg_caco_team_belief.enemy_carrier[i].seen_time > SG_BELIEF_STALE)
			sg_caco_team_belief.enemy_carrier[i].seed = -1;

		/* a carrier who died stops being one */
		if (sg_caco_team_belief.carrier[i].client >= 0)
		{
			edict_t *p = g_edicts + 1 + sg_caco_team_belief.carrier[i].client;
			edict_t *expected = i == 0 ? blueflag : redflag;

			if (!p->inuse || !p->client ||
			    !SG_CarrierBeliefIdentityCurrent(SG_TeamFromIdx(i),
			        p->client->ctf.teamnum, 1,
			        expected && ClientHasFlag(p) == expected))
			{
				sg_caco_team_belief.carrier[i].client = -1;
				sg_caco_team_belief.carrier[i].seed = -1;
				sg_caco_team_belief.carrier[i].seen_time = 0.0f;
				Caco_InvalidateOurCarrierField(r, i);
			}
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
			edict_t *expected = i == 0 ? redflag : blueflag;

			if (!p->inuse || !p->client ||
			    !SG_CarrierBeliefIdentityCurrent(SG_TeamFromIdx(i),
			        p->client->ctf.teamnum, 0,
			        expected && ClientHasFlag(p) == expected))
			{
				sg_caco_team_belief.enemy_carrier[i].client = -1;
				sg_caco_team_belief.enemy_carrier[i].seed = -1;
				sg_caco_team_belief.enemy_carrier[i].seen_time = 0.0f;
			}
		}
	}
}



sg_proj_t sg_caco_proj[2];

/* scratch for one step: every member's successors before dedupe and cull */
static int caco_proj_cand[SG_PROJ_MAX * SG_PROJ_BRANCH];

/*
 * The best SG_PROJ_BRANCH descending neighbours of one seed, written into
 * out[] in ascending field order (out[0] is the step the old code took).
 * Neighbours that do not descend are not steps toward his stand and are not
 * offered: a carrier who wanders backwards is not the case we must cover.
 */
#ifdef SG_CACO_TEST
#define SG_CACO_PRIVATE
#else
#define SG_CACO_PRIVATE static
#endif

SG_CACO_PRIVATE int Caco_BestSteps(rune_t *r, int seed,
	const int *field, int *out)
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

		if (!Fields_ActionAdmitted(r->links[li].action))
			continue;
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

#undef SG_CACO_PRIVATE

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
					Caco_Relay(SG_RELAY_OURS, SG_TeamIdx(team), j,
					           Rune_NearestSeed(r, p->s.origin));
			}
			else if (carried == SG_TeamIdx(team))
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
static void Caco_RelayFlush(rune_t *r)
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
			if (!p->inuse || !p->client)
				continue;
			/*
			 * Validate the fact that was relayed, not the cosmetic effect bit.
			 * A voluntary toss clears the flag owner immediately but EF_FLAGx is
			 * rebuilt only in ClientEndServerFrame; accepting that stale bit can
			 * briefly resurrect a carrier field.  Owner identity also prevents a
			 * relay about one flag/team from being satisfied by carrying the other.
			 */
			if (k == SG_RELAY_OURS)
			{
				edict_t *flag = (i == 0) ? blueflag : redflag;

					if (p->client->ctf.teamnum != SG_TeamFromIdx(i) ||
					    !flag || !flag->inuse || ClientHasFlag(p) != flag)
					continue;
			}
			else
			{
				edict_t *flag = (i == 0) ? redflag : blueflag;

				if (p->client->ctf.teamnum !=
				        SG_EnemyTeam(SG_TeamFromIdx(i)) ||
					    !flag || !flag->inuse || ClientHasFlag(p) != flag)
					continue;
			}

			c = (k == SG_RELAY_THEIRS)
				? &sg_caco_team_belief.enemy_carrier[i]
				: &sg_caco_team_belief.carrier[i];

			if (rl->at <= c->seen_time)
				continue;           /* the team already knows something newer */

			if (k == SG_RELAY_OURS && c->client != rl->client)
				Caco_InvalidateOurCarrierField(r, i);
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

/* Last enemy quad fade warning heard by each team. */
float sg_caco_quadheard[2];
static int sg_quadsound_idx;
static int sg_hastesound_idx;

static unsigned sg_ear_said;    /* EAR print sampler */
static uint32_t sg_ear_random[2]; /* one tactical sequence per listening team */

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

#ifdef SG_CACO_TEST
#define SG_CACO_ENEMY_PRIVATE
#else
#define SG_CACO_ENEMY_PRIVATE static
#endif

/* A generic enemy row is route belief, so an observed client without a
 * proved local RUNE seed is not a partial row: it has no position the
 * controller may consume.  Rune_NearestSeed deliberately returns -1 outside
 * its bounded local topology.  Reject that at the shared writer rather than
 * requiring every movement/weapon consumer to survive seeds[-1]. */
SG_CACO_ENEMY_PRIVATE qboolean Caco_EnemyObservationValid(const rune_t *r,
	int team_index, int client, int maxclients, int seed)
{
	return r && r->seeds && (team_index == 0 || team_index == 1) &&
	       maxclients > 0 && client >= 0 && client < maxclients &&
	       seed >= 0 && seed < r->hdr.num_seeds;
}

#undef SG_CACO_ENEMY_PRIVATE

/*
 * The shared writer over that slot rule, used by the eye
 * (Caco_ScanEnemies, below) and the ear (SG_NoteSound, further down).
 * `seen` is what separates the two callers: an eye entry is exact and
 * carries the rune tell, an ear entry is a region and carries neither.
 */
#ifdef SG_CACO_TEST
#define SG_CACO_PLACE_PRIVATE
#else
#define SG_CACO_PLACE_PRIVATE static
#endif

SG_CACO_PLACE_PRIVATE void Caco_EnemyPlace(rune_t *r, int team1, int client,
                                           int seed, qboolean seen,
                                           qboolean runed)
{
	sg_belief_enemy_t *tab;
	int slot;

	/* A current eye can prove that a previously localized enemy has left the
	 * graph without supplying a new graph position.  Retire that exact row;
	 * otherwise its old seed remains actionable for the normal freshness
	 * window even though the newer observation disproved it. */
	if (r && r->seeds && (team1 == 0 || team1 == 1) &&
	    client >= 0 && client < game.maxclients &&
	    (seed < 0 || seed >= r->hdr.num_seeds))
	{
		tab = sg_caco_enemies[team1];
		for (slot = 0; slot < SG_MAX_ENEMY_TRACK; slot++)
			if (tab[slot].client == client)
			{
				memset(&tab[slot], 0, sizeof(tab[slot]));
				tab[slot].client = -1;
				tab[slot].seed = -1;
				return;
			}
		return;
	}
	if (!Caco_EnemyObservationValid(r, team1, client, game.maxclients, seed))
		return;
	tab = sg_caco_enemies[team1];
	slot = Caco_EnemySlot(tab, client);

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

#undef SG_CACO_PLACE_PRIVATE


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

		Caco_EnemyPlace(r, SG_TeamIdx(viewer_team), i,
		                Rune_NearestSeed(r, p->s.origin), true,
		                (p->s.renderfx & RF_GLOW) != 0);
	}
}



#define SG_EAR_FULLVOL	80.0f       /* SOUND_FULLVOLUME */
#define SG_EAR_SPAN	2000.0f     /* 1 / 0.0005, the client's distance slope */
#define SG_EAR_SPREAD	300.0f      /* worst-case placement error, units */

void SG_NoteSound(edict_t *emitter, vec3_t origin_or_null, int channel,
                  int soundindex, float volume, float attenuation)
{
	rune_t *r = SG_Rune();
	vec3_t sorg;
	edict_t *best_listener[2] = { NULL, NULL };
	float best_fraction[2] = { 2.0f, 2.0f };
	float best_distance[2] = { 0.0f, 0.0f };
	float best_radius[2] = { 0.0f, 0.0f };
	int best_client[2] = { -1, -1 };
	int eteam, ecl, i, t;

	if (!r)
		return;                     /* no rune loaded: nowhere to place onto */
	if (!emitter || !emitter->inuse || !emitter->client)
		return;                     /* world noise names nobody */
	/* player_die emits gib/death audio after the public obituary purges this
	 * subject but before deadflag is assigned. Do not immediately resurrect the
	 * corpse as a fresh heard-only live enemy. */
	if (emitter->deadflag || emitter->health <= 0)
		return;

	eteam = emitter->client->ctf.teamnum;
	if (eteam != CTF_TEAM_RED && eteam != CTF_TEAM_BLUE)
		return;
	/* Global announcements are associated with an edict for protocol/channel
	 * ownership, not spatial attribution.  Human clients hear ATTN_NONE at full
	 * volume without a direction; do not turn that private emitter association
	 * into an enemy position.  This also fails closed on malformed non-finite
	 * mixer inputs before they reach radius/seed arithmetic. */
	if (!SG_SoundCarriesPosition(volume, attenuation))
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
		vec3_t d;
		float dist, radius, frac;
		int team, team_index;

		if (!b->inuse || !b->client || b->deadflag)
			continue;
		if (b == emitter)
			continue;
		if (!SG_OwnsBot(b))
			continue;               /* only SLIPGATE bots listen through here */

		team = b->client->ctf.teamnum;
		if ((team != CTF_TEAM_RED && team != CTF_TEAM_BLUE) ||
		    team == eteam)
			continue;               /* teammates are not tracked as enemies */

		VectorSubtract(sorg, b->s.origin, d);
		dist = VectorLength(d);

		radius = (SG_EAR_FULLVOL + SG_EAR_SPAN / attenuation) * volume;
		if (dist > radius)
			continue;           /* out of earshot */
		if (!sg_host.in_phs(b->s.origin, sorg))
			continue;           /* no path for the sound to travel */

		frac = (radius > 0.0f) ? dist / radius : 1.0f;
		if (frac > 1.0f)
			frac = 1.0f;

		team_index = SG_TeamIdx(team);
		if (!SG_EarCandidateBetter(frac, i,
		    best_listener[team_index] != NULL,
		    best_fraction[team_index], best_client[team_index]))
			continue;
		best_listener[team_index] = b;
		best_fraction[team_index] = frac;
		best_distance[team_index] = dist;
		best_radius[team_index] = radius;
		best_client[team_index] = i;
	}

	/* Team belief is one shared callout, so file one observation: the closest
	 * listener heard it most accurately.  Its noise comes from that team's
	 * private sequence, independent of client-slot order and cosmetic RNG. */
	for (t = 0; t < 2; t++)
	{
		edict_t *b = best_listener[t];
		vec3_t guess;
		float frac;
		int seed;

		if (!b)
			continue;
		frac = best_fraction[t];
		sg_ear_random[t] = SG_EarRandomNext(sg_ear_random[t]);
		guess[0] = sorg[0] + SG_EarRandomSigned(sg_ear_random[t]) *
		    frac * SG_EAR_SPREAD;
		sg_ear_random[t] = SG_EarRandomNext(sg_ear_random[t]);
		guess[1] = sorg[1] + SG_EarRandomSigned(sg_ear_random[t]) *
		    frac * SG_EAR_SPREAD;
		sg_ear_random[t] = SG_EarRandomNext(sg_ear_random[t]);
		guess[2] = sorg[2] + SG_EarRandomSigned(sg_ear_random[t]) *
		    frac * SG_EAR_SPREAD;

		seed = Rune_NearestSeed(r, guess);
		if (seed < 0)
			continue;

		Caco_EnemyPlace(r, t, ecl, seed, false, false);

		/* the quad announcing its own ending (damage2 = the fade warning,
		 * played once at 3s remaining). Index resolved lazily -- precache
		 * order is stable per map. */
		if (!sg_quadsound_idx)
			sg_quadsound_idx = sg_host.soundindex("items/damage2.wav");
		if (soundindex == sg_quadsound_idx)
			sg_caco_quadheard[t] = level.time;

		/* the haste rune's firing voice: this client is hasted NOW */
		if (!sg_hastesound_idx)
			sg_hastesound_idx = sg_host.soundindex("player/lava1.wav");
		if (soundindex == sg_hastesound_idx && ecl >= 0 &&
		    ecl < SG_DMG_CLIENTS)
			sg_caco_hastefire[t][ecl] = level.time;

		/* Sample debug output to avoid flooding the server log. */
		if (sg_cv.debug->value && !(sg_ear_said++ & 31))
			sg_host.dprint("EAR %s heard %s snd=%i chan=%i d=%.0f r=%.0f "
			           "err<=%.0f seed=%i\n",
			           b->client->pers.netname,
			           emitter->client->pers.netname,
			           soundindex, channel, best_distance[t], best_radius[t],
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


void SG_NoteDamage(edict_t *victim, edict_t *attacker, int damage, int mod,
                   vec3_t dir)
{
	sg_damage_hit_t	*ring, *slot;
	vec3_t			eye, from;
	qboolean		seen, hitscan;
	int				ci, ac, team, i;

	if (!victim || !victim->inuse || !victim->client)
		return;
	/* Team-shared CACO knowledge belongs only to clients this subsystem owns.
	 * Other bot implementations may also set FL_BOT, but their private damage
	 * is not an SG sighting or callout and must not leak across that boundary. */
	if (!SG_OwnsBot(victim))
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

	/* A rail hit is direct evidence of the attacker's firing cadence. */
	if (mod == MOD_RAILGUN && ac < SG_DMG_CLIENTS && SG_RailRhythm())
		sg_caco_railshot[SG_TeamIdx(team)][ac] = level.time;

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
			tr = sg_host.trace(eye, NULL, NULL, back, victim, MASK_OPAQUE);
			VectorCopy(tr.endpos, pos);
			/* off whatever surface it stopped against, so the seed lookup
			 * cannot snap to a node on the far side of that wall */
			VectorMA(pos, -16.0f, from, pos);
		}

		seed = Rune_NearestSeed(r, pos);
		if (seed >= 0)
		{
			sg_belief_enemy_t	*tab = sg_caco_enemies[SG_TeamIdx(team)];
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

		if (sg_cv.debug->value)
			sg_host.dprint("HITFROM %s<%s dmg=%d mod=%d %s seed=%d dir=%.2f,%.2f,%.2f\n",
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

/* Every client-indexed sensor fact describes one concrete body generation.
 * Client slots survive deaths, team changes, disconnects and fake-client
 * reuse; keeping these rows across any of those boundaries turns a corpse or
 * a new teammate into an enemy for several seconds. */
void Caco_ResetClient(edict_t *client)
{
	int ci, k, t, s;

	if (!client)
		return;
	ci = (int)(client - g_edicts) - 1;
	if (ci < 0 || ci >= SG_DMG_CLIENTS)
		return;
	memset(sg_caco_damage[ci], 0, sizeof(sg_caco_damage[ci]));
	for (k = 0; k < SG_DMG_RING; k++)
		sg_caco_damage[ci][k].attacker = -1;
	for (t = 0; t < 2; t++)
	{
		for (k = 0; k < SG_CALL_TOPICS; k++)
			if (caco_callout[t][k].pending &&
			    caco_callout[t][k].speaker == ci)
				memset(&caco_callout[t][k], 0,
				       sizeof(caco_callout[t][k]));
		for (s = 0; s < SG_MAX_ENEMY_TRACK; s++)
			if (sg_caco_enemies[t][s].client == ci)
			{
				memset(&sg_caco_enemies[t][s], 0,
				       sizeof(sg_caco_enemies[t][s]));
				sg_caco_enemies[t][s].client = -1;
				sg_caco_enemies[t][s].seed = -1;
			}
		sg_caco_railshot[t][ci] = 0.0f;
		sg_caco_hastefire[t][ci] = 0.0f;
		if (sg_caco_team_belief.carrier[t].client == ci)
		{
			sg_caco_team_belief.carrier[t].client = -1;
			sg_caco_team_belief.carrier[t].seed = -1;
			sg_caco_team_belief.carrier[t].seen_time = 0.0f;
			Caco_InvalidateOurCarrierField(SG_Rune(), t);
		}
		if (sg_caco_team_belief.enemy_carrier[t].client == ci)
		{
			sg_caco_team_belief.enemy_carrier[t].client = -1;
			sg_caco_team_belief.enemy_carrier[t].seed = -1;
			sg_caco_team_belief.enemy_carrier[t].seen_time = 0.0f;
			sg_caco_proj[t].client = -1;
			sg_caco_proj[t].n = 0;
			sg_caco_proj[t].from_time = -1.0f;
		}
	}
	for (k = 0; k < 2; k++)
		for (t = 0; t < 2; t++)
			if (caco_relay[k][t].client == ci)
				memset(&caco_relay[k][t], 0,
				       sizeof(caco_relay[k][t]));
}



float		sg_caco_railshot[2][SG_DMG_CLIENTS];

/* Last audible haste firing cue for each client and listening team. */
float		sg_caco_hastefire[2][SG_DMG_CLIENTS];
static unsigned	sg_rail_said;       /* RAILSHOT print sampler */

qboolean SG_RailRhythm(void)
{
	return (sg_cv.railrhythm->value > 0.0f) ? true : false;
}

void SG_NoteRailShot(edict_t *shooter)
{
	int	sc, steam, i;

	if (!SG_RailRhythm())
		return;                     /* default: this call costs one cvar read */
	if (!shooter || !shooter->inuse || !shooter->client)
		return;                     /* a monster's railgun names no client */

	steam = shooter->client->ctf.teamnum;
	if (steam <= CTF_TEAM_UNDEFINED)
		return;

	sc = (int)(shooter - g_edicts) - 1;
	if (sc < 0 || sc >= game.maxclients || sc >= SG_DMG_CLIENTS)
		return;

	for (i = 0; i < game.maxclients; i++)
	{
		edict_t	*b = g_edicts + 1 + i;
		int		team;

		if (!b->inuse || !b->client || b->deadflag)
			continue;
		if (b == shooter)
			continue;
		if (!SG_OwnsBot(b))
			continue;               /* only SLIPGATE bots listen through here */

		team = b->client->ctf.teamnum;
		if (team <= CTF_TEAM_UNDEFINED || team == steam)
			continue;               /* a teammate's rail is not a lane to time */

		if (!sg_host.in_phs(b->s.origin, shooter->s.origin))
			continue;               /* the trail message never reached here */

		sg_caco_railshot[SG_TeamIdx(team)][sc] = level.time;

		/* sampled 1-in-8: a two-railer server fires on the order of a
		 * shot a second and the log is for reading */
		if (sg_cv.debug->value && !(sg_rail_said++ & 7))
			sg_host.dprint("RAILSHOT %s heard %s reload=%.1f window=%.1f\n",
			           b->client->pers.netname,
			           shooter->client->pers.netname,
			           SG_RAIL_RELOAD, SG_RAIL_WINDOW);
	}
}

qboolean SG_RailThreat(int team, float fresh, int *out_client, int *out_seed)
{
	sg_belief_enemy_t	*tab;
	int					s, best = -1;
	float				bt = -1.0f;

	if (!SG_RailRhythm())
		return false;
	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return false;

	tab = sg_caco_enemies[SG_TeamIdx(team)];
	for (s = 0; s < SG_MAX_ENEMY_TRACK; s++)
	{
		sg_belief_enemy_t *en = &tab[s];

		if (en->client < 0 || en->client >= SG_DMG_CLIENTS || en->seed < 0)
			continue;
		if (level.time - en->seen_time > fresh)
			continue;
		/* the belief says where; the rail table says whether he is the
		 * kind of enemy this feature is about at all */
		if (sg_caco_railshot[SG_TeamIdx(team)][en->client] <= 0.0f ||
		    level.time - sg_caco_railshot[SG_TeamIdx(team)][en->client] >
		        SG_RAIL_MEMORY)
			continue;
		if (en->seen_time > bt)
		{
			bt = en->seen_time;
			best = s;
		}
	}
	if (best < 0)
		return false;

	if (out_client)
		*out_client = tab[best].client;
	if (out_seed)
		*out_seed = tab[best].seed;
	return true;
}

qboolean SG_RailCold(int team, int client)
{
	float	t;

	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return false;
	if (client < 0 || client >= SG_DMG_CLIENTS)
		return false;

	t = sg_caco_railshot[SG_TeamIdx(team)][client];
	if (t <= 0.0f)
		return false;               /* never heard him fire: assume loaded */
	/* a hasted railer cycles at half the reload (double weaponthink,
	 * g_runes.c:812-813) -- heard hasted fire lately, halve the window */
	if (sg_caco_hastefire[SG_TeamIdx(team)][client] > 0.0f &&
	    level.time - sg_caco_hastefire[SG_TeamIdx(team)][client] < 10.0f)
		return (level.time - t < SG_RAIL_WINDOW * 0.5f) ? true : false;
	return (level.time - t < SG_RAIL_WINDOW) ? true : false;
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
		/* THIS team's row: the whole point of the D4 chain is that it is
		 * built out of what this side has seen. The other team's look at the
		 * Damage pad is not ours to reason from. */
		sg_belief_item_t *it = &sg_caco_items[Caco_TeamRow(team)][i];
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
		sg_belief_enemy_t *en = &sg_caco_enemies[SG_TeamIdx(team)][i];

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
	Caco_RelayFlush(r);
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
	/* 0 is "never heard him fire", which is the value the table wants */
	memset(sg_caco_railshot, 0, sizeof(sg_caco_railshot));
	memset(sg_caco_hastefire, 0, sizeof(sg_caco_hastefire));
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
	{
		int t, k;

		for (t = 0; t < 2; t++)
		{
			sg_ear_random[t] = SG_EarRandomInitial((unsigned)t);
			for (k = 0; k < SG_CALL_TOPICS; k++)
				caco_callout_random[t][k] =
				    SG_CalloutRandomInitial((unsigned)t, (unsigned)k);
		}
	}
	memset(caco_relay, 0, sizeof(caco_relay));
	memset(sg_caco_proj, 0, sizeof(sg_caco_proj));
	for (i = 0; i < 2; i++)
	{
		sg_caco_enemy_death_seed[i] = -1;
		sg_caco_enemy_death_time[i] = -1000.0f;
		sg_caco_quadheard[i] = 0.0f;
		sg_caco_team_belief.flag[0][i].where_seed = -1;
		sg_caco_team_belief.flag[1][i].where_seed = -1;
		sg_caco_team_belief.carrier[i].client = -1;
		sg_caco_team_belief.carrier[i].seed = -1;
		sg_caco_team_belief.enemy_carrier[i].client = -1;
		sg_caco_team_belief.enemy_carrier[i].seed = -1;
		sg_caco_proj[i].client = -1;
		sg_caco_proj[i].n = 0;
		sg_caco_proj[i].from_time = -1.0f;
	}
	/* Sound indices are level-local configstrings, even though this module's
	 * cache has process lifetime.  Earn them again in the new level. */
	sg_quadsound_idx = 0;
	sg_hastesound_idx = 0;
	sg_ear_said = 0;

	/* spawn locations are constant, so they are read exactly once -- and
	 * runes, which are not constant, get nothing from this scan but their
	 * entity numbers (see the commentary above Caco_ScanItemSpawns) */
	Caco_ScanItemSpawns();
	Caco_ScanMegaSpawns();

	/* after the item scan: sg_chat.c seeds its told-state from it */
	SG_ChatReset();

	caco_next_scan = 0.0f;
	caco_next_human = 0.0f;
	caco_next_advect = 0.0f;
}
