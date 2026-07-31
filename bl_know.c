/*
 * bl_know.c -- what a bot is allowed to know, and how it tells its team.
 *
 * See bl_know.h for why this exists.
 */
#include "g_local.h"
#include "g_ctffunc.h"
#include "bl_redirgi.h"	/* BotClientCommand */
#include "bl_know.h"

/*
 * is_infront() lives in g_runes.c next to is_visible(), but only is_visible()
 * was ever given a home in g_local.h. Declared here rather than adding to
 * g_local.h so this module stays self-contained.
 */
qboolean is_infront(edict_t *self, edict_t *other);

/* runes a bot can hold in mind at once; maps carry far fewer than this */
#define KNOW_MAX_RUNES		8

/* topics, and with them the channel each one goes out on */
#define KNOW_TOPIC_NONE		0
#define KNOW_TOPIC_OURFLAG	1	/* our flag is on the floor over there */
#define KNOW_TOPIC_CARRIER	2	/* the enemy holding our flag is over there */
#define KNOW_TOPIC_RUNE		3	/* a rune is sitting over there */
#define KNOW_TOPIC_THEIRFLAG	4	/* their flag is on the floor over there */
#define KNOW_TOPIC_COUNT	5

/*
 * A bot blurting out every sighting the instant it happens reads as a script,
 * not a player. The delay is reaction time, the per-bot gap stops one bot
 * narrating, and the per-team gap stops five bots reporting the same thing.
 */
#define KNOW_SPEAK_DELAY	0.7f
#define KNOW_BOT_GAP		8.0f
#define KNOW_TEAM_GAP		15.0f

/* how long a sighting of something that moves is still worth acting on */
#define KNOW_CARRIER_LIFE	6.0f

/* a rune seen this long ago has probably been taken by now */
#define KNOW_RUNE_LIFE		45.0f

/*
 * Sight sweeps trace, so they are not free. Four a second is well inside
 * human reaction time, and staggering them by client number keeps every bot
 * on the server from tracing on the same frame.
 */
#define KNOW_LOOK_GAP		0.25f

typedef struct
{
	vec3_t		origin;
	float		time;		/* level.time the bot learned it */
	int		ref;		/* which episode, or which rune, this is about */
	qboolean	valid;
} knowfact_t;

typedef struct
{
	knowfact_t	flag[CTF_TEAM_LIMIT];		/* that flag lying on the floor */
	knowfact_t	carrier[CTF_TEAM_LIMIT];	/* whoever is holding that flag */
	knowfact_t	rune[KNOW_MAX_RUNES];
	float		nextlook;
	float		nextspeak;
	int		pending;			/* KNOW_TOPIC_*, queued but unsaid */
	int		pendingsubject;			/* team, or rune slot, it is about */
	float		pendingtime;
} botknow_t;

typedef struct
{
	int		dropepoch;	/* bumped each time the flag starts lying down */
	int		carryepoch;	/* bumped each time a new player picks it up */
	int		carrier;	/* client number, or -1 when nobody has it */
	qboolean	onground;
} flagtrack_t;

static botknow_t	know_bot[MAX_CLIENTS];
static flagtrack_t	know_flag[CTF_TEAM_LIMIT];
static float		know_teamsaid[CTF_TEAM_LIMIT][KNOW_TOPIC_COUNT];

/*
 * How far a bot can pick something out. Flags and runes are large, lit and
 * animated, so this is deliberately generous -- the limit that matters in
 * practice is the line of sight, not the distance.
 */
static float Know_SightRange(void)
{
	cvar_t *c = gi.cvar("bot_know_range", "1200", 0);
	return (c && c->value > 0) ? c->value : 1200.0f;
}

static qboolean Know_TeamTopic(int topic)
{
	/*
	 * Where our flag fell, who is running with it, and where the runes are
	 * all cost us something if the other side hears them. Everything else
	 * goes out in the open.
	 */
	return (topic == KNOW_TOPIC_OURFLAG ||
		topic == KNOW_TOPIC_CARRIER ||
		topic == KNOW_TOPIC_RUNE) ? true : false;
}

static int Know_ClientNum(edict_t *ent)
{
	return (int)(ent - g_edicts - 1);
}

static botknow_t *Know_ForBot(edict_t *ent)
{
	int num;

	if (!ent || !ent->client)
		return NULL;
	num = Know_ClientNum(ent);
	if (num < 0 || num >= MAX_CLIENTS)
		return NULL;
	return &know_bot[num];
}

/* a bot that is dead, spectating or teamless is not looking at anything */
static qboolean Know_Playing(edict_t *ent)
{
	if (!ent || !ent->inuse || !ent->client)
		return false;
	if (!(ent->flags & FL_BOT))
		return false;
	if (ent->client->ctf.teamnum != CTF_TEAM_RED &&
		ent->client->ctf.teamnum != CTF_TEAM_BLUE)
		return false;
	if (ent->deadflag == DEAD_DEAD || ent->health <= 0)
		return false;
	return true;
}

/*
 * Line of sight, in the bot's field of view, and near enough to make out.
 * This is the whole of what a bot is allowed to notice on its own.
 */
static qboolean Know_CanSee(edict_t *bot, edict_t *target, float range)
{
	vec3_t	delta;

	if (!target || !target->inuse)
		return false;
	VectorSubtract(target->s.origin, bot->s.origin, delta);
	if (VectorLength(delta) > range)
		return false;
	if (!is_infront(bot, target))
		return false;
	return is_visible(bot, target);
}

static void Know_Record(knowfact_t *fact, vec3_t origin, int ref)
{
	VectorCopy(origin, fact->origin);
	fact->time = level.time;
	fact->ref = ref;
	fact->valid = true;
}

/*
 * True when the bot has nothing usable. 'life' of zero is for things that do
 * not move: once seen, the position stays good until the episode number says
 * that episode is over.
 */
static qboolean Know_Stale(knowfact_t *fact, int ref, float life)
{
	if (!fact->valid)
		return true;
	if (fact->ref != ref)
		return true;
	if (life > 0 && level.time - fact->time > life)
		return true;
	return false;
}

static void Know_Queue(botknow_t *bk, int topic, int subject)
{
	/* one thing at a time, and the older thought wins */
	if (bk->pending != KNOW_TOPIC_NONE)
		return;
	bk->pending = topic;
	bk->pendingsubject = subject;
	bk->pendingtime = level.time + KNOW_SPEAK_DELAY;
}

/*
 * Follow the flags on the entities themselves. Everyone is entitled to this
 * much: the HUD draws home / dropped / taken for both flags, so the state is
 * public and only the position behind it is private.
 */
static void Know_TrackFlags(void)
{
	int		t, carrier;
	edict_t		*flag;
	flagtrack_t	*tr;
	qboolean	onground;

	for (t = CTF_TEAM_RED; t < CTF_TEAM_LIMIT; t++)
	{
		tr = &know_flag[t];
		flag = ctf_getteamflag(t, CTF_TEAM_MATCHING);
		if (!flag)
		{
			tr->carrier = -1;
			tr->onground = false;
			continue;
		}

		carrier = -1;
		if (flag->owner && flag->owner->client && flag->owner->inuse)
			carrier = Know_ClientNum(flag->owner);
		onground = (carrier < 0 && !ctf_flagathome(flag)) ? true : false;

		/*
		 * Numbering the episodes is what lets a remembered position expire
		 * on its own. A bot keeps the spot it saw the flag fall across its
		 * own death and respawn, but the moment that flag is picked up or
		 * returned the number moves on and the old spot stops answering --
		 * without this module having to reach into every bot and clear it.
		 */
		if (onground && !tr->onground)
			tr->dropepoch++;
		if (carrier >= 0 && carrier != tr->carrier)
			tr->carryepoch++;

		tr->onground = onground;
		tr->carrier = carrier;
	}
}

/* a rune still lying in the world, rather than one in somebody's pocket */
static qboolean Know_RuneInWorld(edict_t *ent)
{
	if (!ent->inuse || !ent->runetype)
		return false;
	if (ent->solid == SOLID_NOT)
		return false;
	if (ent->svflags & SVF_NOCLIENT)
		return false;
	return true;
}

/* the slot already holding this rune, else the least useful one to overwrite */
static knowfact_t *Know_RuneSlot(botknow_t *bk, int ref)
{
	int		i, oldest = 0;
	knowfact_t	*fact;

	for (i = 0; i < KNOW_MAX_RUNES; i++)
	{
		fact = &bk->rune[i];
		if (fact->valid && fact->ref == ref)
			return fact;
	}
	for (i = 0; i < KNOW_MAX_RUNES; i++)
	{
		if (!bk->rune[i].valid)
			return &bk->rune[i];
		if (bk->rune[i].time < bk->rune[oldest].time)
			oldest = i;
	}
	return &bk->rune[oldest];
}

static void Know_Look(edict_t *bot, botknow_t *bk)
{
	int		t, myteam;
	float		range;
	edict_t		*flag, *carrier, *rune;
	flagtrack_t	*tr;
	knowfact_t	*fact;

	myteam = bot->client->ctf.teamnum;
	range = Know_SightRange();

	for (t = CTF_TEAM_RED; t < CTF_TEAM_LIMIT; t++)
	{
		tr = &know_flag[t];
		flag = ctf_getteamflag(t, CTF_TEAM_MATCHING);
		if (!flag)
			continue;

		if (tr->onground)
		{
			if (Know_CanSee(bot, flag, range))
			{
				if (Know_Stale(&bk->flag[t], tr->dropepoch, 0))
					Know_Queue(bk, (t == myteam) ? KNOW_TOPIC_OURFLAG
								    : KNOW_TOPIC_THEIRFLAG, t);
				Know_Record(&bk->flag[t], flag->s.origin, tr->dropepoch);
			}
		}
		else if (tr->carrier >= 0)
		{
			carrier = g_edicts + 1 + tr->carrier;
			if (carrier != bot && Know_CanSee(bot, carrier, range))
			{
				/*
				 * Only the enemy running off with our own flag is worth
				 * the airtime; a teammate carrying theirs is already on
				 * everyone's screen as "taken".
				 */
				if (t == myteam && Know_Stale(&bk->carrier[t], tr->carryepoch,
								KNOW_CARRIER_LIFE))
					Know_Queue(bk, KNOW_TOPIC_CARRIER, t);
				Know_Record(&bk->carrier[t], carrier->s.origin, tr->carryepoch);
			}
		}
	}

	for (rune = g_edicts; rune < &g_edicts[globals.num_edicts]; rune++)
	{
		if (!Know_RuneInWorld(rune))
			continue;
		if (!Know_CanSee(bot, rune, range))
			continue;
		fact = Know_RuneSlot(bk, (int)(rune - g_edicts));
		if (Know_Stale(fact, (int)(rune - g_edicts), KNOW_RUNE_LIFE))
			Know_Queue(bk, KNOW_TOPIC_RUNE, (int)(fact - bk->rune));
		Know_Record(fact, rune->s.origin, (int)(rune - g_edicts));
	}
}

/*
 * Players call places by the thing that sits there, so a call-out names the
 * nearest fixed item. Dropped items are skipped because "by the shotgun"
 * is useless if the shotgun is a corpse's and will be gone in a moment.
 */
static void Know_Where(vec3_t origin, char *buf, int size)
{
	edict_t	*item, *best = NULL;
	float	dist, bestdist = 400;
	vec3_t	delta;

	for (item = g_edicts; item < &g_edicts[globals.num_edicts]; item++)
	{
		if (!item->inuse || !item->item)
			continue;
		if (item->spawnflags & DROPPED_ITEM)
			continue;
		if (item->runetype)
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
 * Everyone who heard it learns it. Team chat reaches teammates only; open
 * chat reaches the other side too, which is exactly the cost of using it.
 */
static void Know_Share(edict_t *speaker, int topic, int subject, knowfact_t *fact)
{
	int		i, team;
	edict_t		*ent;
	botknow_t	*bk;
	knowfact_t	*dest;

	team = speaker->client->ctf.teamnum;

	for (i = 0; i < game.maxclients; i++)
	{
		ent = g_edicts + 1 + i;
		if (ent == speaker || !ent->inuse || !ent->client)
			continue;
		if (!(ent->flags & FL_BOT))
			continue;
		if (Know_TeamTopic(topic) && ent->client->ctf.teamnum != team)
			continue;

		bk = &know_bot[i];
		if (topic == KNOW_TOPIC_CARRIER)
			dest = &bk->carrier[subject];
		else if (topic == KNOW_TOPIC_RUNE)
			dest = Know_RuneSlot(bk, fact->ref);
		else
			dest = &bk->flag[subject];

		/* being told is knowing it now, but about a moment already gone */
		VectorCopy(fact->origin, dest->origin);
		dest->time = fact->time;
		dest->ref = fact->ref;
		dest->valid = true;
	}
}

static void Know_Speak(edict_t *bot, botknow_t *bk)
{
	int		topic, subject, team;
	char		place[96];
	char		line[160];
	knowfact_t	*fact;

	if (bk->pending == KNOW_TOPIC_NONE)
		return;
	if (level.time < bk->pendingtime)
		return;

	topic = bk->pending;
	subject = bk->pendingsubject;

	/* consumed either way, or a muted thought would jam the queue for good */
	bk->pending = KNOW_TOPIC_NONE;

	team = bot->client->ctf.teamnum;
	if (level.time < bk->nextspeak)
		return;
	if (level.time < know_teamsaid[team][topic])
		return;

	if (topic == KNOW_TOPIC_CARRIER)
		fact = &bk->carrier[subject];
	else if (topic == KNOW_TOPIC_RUNE)
		fact = &bk->rune[subject];
	else
		fact = &bk->flag[subject];

	if (!fact->valid)
		return;

	Know_Where(fact->origin, place, sizeof(place));

	switch (topic)
	{
	case KNOW_TOPIC_OURFLAG:
		Com_sprintf(line, sizeof(line), "our flag is down %s", place);
		break;
	case KNOW_TOPIC_THEIRFLAG:
		Com_sprintf(line, sizeof(line), "their flag is down %s", place);
		break;
	case KNOW_TOPIC_CARRIER:
		Com_sprintf(line, sizeof(line), "enemy has our flag, %s", place);
		break;
	case KNOW_TOPIC_RUNE:
		Com_sprintf(line, sizeof(line), "rune %s", place);
		break;
	default:
		return;
	}

	BotClientCommand(Know_ClientNum(bot),
		Know_TeamTopic(topic) ? "say_team" : "say", line, NULL);

	bk->nextspeak = level.time + KNOW_BOT_GAP;
	know_teamsaid[team][topic] = level.time + KNOW_TEAM_GAP;

	Know_Share(bot, topic, subject, fact);
}

void Know_LevelInit(void)
{
	int i, t;

	memset(know_bot, 0, sizeof(know_bot));
	memset(know_flag, 0, sizeof(know_flag));
	memset(know_teamsaid, 0, sizeof(know_teamsaid));

	for (t = 0; t < CTF_TEAM_LIMIT; t++)
		know_flag[t].carrier = -1;

	/* stagger the first sweep so the bots do not all trace on frame one */
	for (i = 0; i < MAX_CLIENTS; i++)
		know_bot[i].nextlook = level.time + (float)i * 0.01f;
}

void Know_ClientDisconnect(edict_t *ent)
{
	int		i, t, num;
	botknow_t	*bk;

	num = ent ? Know_ClientNum(ent) : -1;
	if (num < 0 || num >= MAX_CLIENTS)
		return;

	memset(&know_bot[num], 0, sizeof(know_bot[num]));

	/*
	 * Everything anyone knew about this player as a carrier goes with them.
	 * The tracker will re-number the carry episode when the flag comes back
	 * into play, so the sightings would lapse anyway; this just stops a bot
	 * chasing a ghost for the few seconds in between.
	 */
	for (i = 0; i < MAX_CLIENTS; i++)
	{
		bk = &know_bot[i];
		for (t = 0; t < CTF_TEAM_LIMIT; t++)
		{
			if (know_flag[t].carrier == num)
				bk->carrier[t].valid = false;
		}
	}

	for (t = 0; t < CTF_TEAM_LIMIT; t++)
	{
		if (know_flag[t].carrier == num)
			know_flag[t].carrier = -1;
	}
}

void Know_Frame(void)
{
	int		i;
	edict_t		*ent;
	botknow_t	*bk;

	Know_TrackFlags();

	if (level.intermissiontime)
		return;

	for (i = 0; i < game.maxclients; i++)
	{
		ent = g_edicts + 1 + i;
		if (!Know_Playing(ent))
			continue;

		bk = &know_bot[i];
		if (level.time >= bk->nextlook)
		{
			bk->nextlook = level.time + KNOW_LOOK_GAP;
			Know_Look(ent, bk);
		}
		Know_Speak(ent, bk);
	}
}

qboolean Know_FlagPosition(edict_t *bot, int teamnum, vec3_t out)
{
	edict_t		*flag;
	flagtrack_t	*tr;
	knowfact_t	*fact;
	botknow_t	*bk;

	if (!out || teamnum <= CTF_TEAM_UNDEFINED || teamnum >= CTF_TEAM_LIMIT)
		return false;

	bk = Know_ForBot(bot);
	if (!bk)
		return false;

	flag = ctf_getteamflag(teamnum, CTF_TEAM_MATCHING);
	if (!flag)
		return false;

	tr = &know_flag[teamnum];

	if (tr->carrier >= 0)
	{
		/* a bot carrying it plainly knows where it is */
		if (tr->carrier == Know_ClientNum(bot))
		{
			VectorCopy(bot->s.origin, out);
			return true;
		}
		fact = &bk->carrier[teamnum];
		if (Know_Stale(fact, tr->carryepoch, KNOW_CARRIER_LIFE))
			return false;
		VectorCopy(fact->origin, out);
		return true;
	}

	if (tr->onground)
	{
		fact = &bk->flag[teamnum];
		if (Know_Stale(fact, tr->dropepoch, 0))
			return false;
		VectorCopy(fact->origin, out);
		return true;
	}

	/* on its stand: the HUD says so, and the stand has never moved */
	VectorCopy(flag->homeposition, out);
	return true;
}

qboolean Know_EnemyCarrier(edict_t *bot, vec3_t out)
{
	int team;

	if (!bot || !bot->client)
		return false;
	team = bot->client->ctf.teamnum;
	if (team <= CTF_TEAM_UNDEFINED || team >= CTF_TEAM_LIMIT)
		return false;

	/*
	 * Asking where the carrier is only means anything while there is one.
	 * Without this the caller would get the flag's stand back and read it
	 * as an enemy standing on it.
	 */
	if (know_flag[team].carrier < 0)
		return false;

	return Know_FlagPosition(bot, team, out);
}

qboolean Know_RunePosition(edict_t *bot, vec3_t out)
{
	int		i;
	float		dist, bestdist = 0;
	vec3_t		delta;
	knowfact_t	*fact, *best = NULL;
	botknow_t	*bk;

	if (!out)
		return false;
	bk = Know_ForBot(bot);
	if (!bk)
		return false;

	for (i = 0; i < KNOW_MAX_RUNES; i++)
	{
		fact = &bk->rune[i];
		if (!fact->valid)
			continue;
		if (level.time - fact->time > KNOW_RUNE_LIFE)
			continue;
		VectorSubtract(fact->origin, bot->s.origin, delta);
		dist = VectorLength(delta);
		if (!best || dist < bestdist)
		{
			bestdist = dist;
			best = fact;
		}
	}

	if (!best)
		return false;
	VectorCopy(best->origin, out);
	return true;
}
