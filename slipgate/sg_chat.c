/*
 * sg_chat.c -- SLIPGATE's voice.
 *
 * Three jobs, one channel authority.
 *
 *   1. TEAM CALLOUTS. What a bot saw, said to its own team, named by the
 *      landmark a player would name it by. Every line here is emitted by the
 *      bot that SAW the transition, about what that bot's team believes --
 *      the rule sg_caco.c's head comment sets out and SLIPGATE.md makes
 *      absolute. Two claims in this file were not looked at by anybody:
 *        - a bot naming an item it picked up itself (it knows: it is holding
 *          the thing);
 *        - a respawn countdown, which is arithmetic on ent->item->quantity
 *          (g_items.c:198) -- map knowledge every player carries.
 *      Nothing else is said that no teammate has seen.
 *
 *   2. PERSONALITY. Sixteen bots, four voices, keyed by the netname prefix
 *      sg_arach.c gives them ("Arach[SG]"). Public chat only: greeting,
 *      map open, taunt, grumble, celebration, match end, idle banter -- plus
 *      the five reactions the capability census found missing, which a human
 *      has and the bots did not: surviving a big hit, watching an enemy kill
 *      himself, being spoken to by name, conceding a capture, and getting the
 *      flag back. Short, lowercase, rate-limited, and probabilistic so it is
 *      not a script.
 *
 *      Three things keep sixteen mouths from reading as one. Every line is
 *      rolled, not scheduled, so a category firing is never certain. Every
 *      line a bot speaks is off the table for EVERY bot for the next
 *      SG_CHAT_REUSE_GAP seconds (Chat_Recent), because the tell is not one
 *      bot repeating itself, it is two bots saying the same four words a
 *      breath apart. And each bot's odds are scaled by a fixed per-slot
 *      chattiness (Chat_Chatty), so the loud ones and the quiet ones are the
 *      same loud ones and quiet ones all night.
 *
 *   3. HUMAN ORDERS. A human teammate's chat is parsed for
 *      "[addressee] <verb>" and stored as a role override with a 90-second
 *      life. This module never applies a role; SG_Role reads it back through
 *      SG_ChatOrderedRole / SG_ChatEscortTarget (see sg_chat.h).
 *
 * ONE emitter owns say_team. SG_ChatSayTeam holds the per-bot budget and the
 * per-topic team cooldown, and sg_caco.c's Caco_Speak routes through it
 * rather than calling SG_BotClientCommand itself. Two emitters with separate
 * limits cannot keep a channel readable no matter how tight each one is.
 *
 * The chat route is the game's own: SG_BotClientCommand(client, "say_team",
 * line, NULL) fills the redirected sg_host.argv and runs ClientCommand ->
 * Cmd_Say_f for that client (slipgate/sg_net.c), so human teammates read it in
 * their own chat window. Same route bl_know.c and sg_caco.c use.
 */

#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_net.h"
#include "slipgate/sg_persona.h"                    /* SG_BotClientCommand -- the chat route */
#include "p_stats.h"                    /* stats_get -- the scoreboard's own count */
#include "slipgate/sg_local.h"
#include "slipgate/sg_chat.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_util.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_chat_random.h"
#include "slipgate/sg_callout_policy.h"

/* ------------------------------------------------------------- constants */

#define SG_CHAT_BOT_GAP		4.0f    /* one say_team per bot per this */
#define SG_CHAT_SAY_GAP		8.0f    /* one public line per bot per this */
#define SG_CHAT_DELAY_MIN	0.4f    /* reaction time before a queued line */
#define SG_CHAT_DELAY_MAX	0.9f
#define SG_CHAT_LINE		96      /* nothing said here is longer */

/*
 * The greeting waits out the spam lockout before it opens its mouth. See
 * Chat_Greetings for why a line sent any earlier is silently eaten.
 * SG_CHAT_GREET_STAGGER keeps sixteen bots from greeting in one frame.
 */
#define SG_CHAT_GREET_SETTLE	1.0f    /* after the bot is alive on a team */
#define SG_CHAT_GREET_STAGGER	0.30f   /* per client slot */
#define SG_CHAT_GREET_RETRY	(CTF_SPAM_LOCKOUT_TIME + 1.0f)
#define SG_CHAT_GREET_TRIES	4       /* then give up and stay quiet */

#define SG_CHAT_ACK_GAP		1.5f    /* floor under order acknowledgements */
#define SG_CHAT_TAUNT_GAP	30.0f
#define SG_CHAT_TAUNT_ODDS	0.30f
#define SG_CHAT_GRUMBLE_GAP	45.0f
#define SG_CHAT_GRUMBLE_ODDS	0.15f

/*
 * HURT AND STILL HERE. A rocket lands, the bot walks away on twenty health,
 * and three seconds later nothing is shooting at it any more. That is when a
 * player says "lucky shot" -- not during, when his hands are on the keys that
 * matter, and not a minute later, when it is somebody else's fight.
 *
 * The reader is the damage ring sg_caco.c already keeps (four entries per
 * client, every landed enemy hit with the number on it), so "something big
 * landed and I am still standing" needs no sense of its own. The read idiom is
 * Beat_HurtSince's (sg_arach.c:2380): walk the four slots, take anything newer
 * than a stamp. Note what the ring does NOT hold -- SG_NoteDamage refuses a
 * teammate's splash and refuses the world -- so a fall down a lift shaft
 * cannot produce "lucky shot", which is exactly right.
 */
#define SG_CHAT_HURT_DMG	30      /* one hit at least this big */
#define SG_CHAT_HURT_WAIT	3.0f    /* said this long after it, never during */
#define SG_CHAT_HURT_CALM	3.0f    /* and only if it has stayed quiet since */
#define SG_CHAT_HURT_ODDS	0.10f
#define SG_CHAT_HURT_GAP	45.0f   /* one per bot per this: grumble's rate */

/*
 * SOMEBODY ELSE'S MISTAKE. An enemy walks into his own rocket, or the lava, or
 * off the ledge, and whoever watched it says something. The watching is the
 * whole gate -- this is a bystander line, so the speaker must have had the
 * corpse in sight, or at least in earshot through the PHS, because a scream
 * carries through a wall and a player laughs at what he heard as readily as at
 * what he saw. One mouth per death, and it spends the taunt cooldown, because
 * to the channel that is what it is.
 */
#define SG_CHAT_SUICIDE_ODDS	0.15f

/*
 * ADDRESSED REPLY. A human types a bot's name and nothing the order grammar
 * recognises -- "arach where are you", "nice one arach", "arach?" -- and the
 * bot answers. Sixteen names that answer to being called is the difference
 * between a roster and a scoreboard; the limits below are what keep it from
 * becoming a way to make the server talk to itself.
 *
 * THE DELAY IS A TYPIST, not a timer. The census's note is the whole model: a
 * human reads the line at SG_CHAT_REPLY_READ characters a second, takes
 * SG_CHAT_REPLY_THINK to decide there is anything worth saying, and then types
 * his own answer at SG_CHAT_REPLY_TYPE -- a fast player's mid-game rate, which
 * is nothing like his desk rate, because one hand is still on the mouse. The
 * sum is clamped into MIN..MAX: under two seconds reads as a macro, over five
 * and he is answering a conversation that has moved on.
 */
#define SG_CHAT_REPLY_GAP	30.0f   /* one reply per bot per this */
#define SG_CHAT_REPLY_MIN	2.0f
#define SG_CHAT_REPLY_MAX	5.0f
#define SG_CHAT_REPLY_READ	25.0f   /* characters a second, reading */
#define SG_CHAT_REPLY_TYPE	5.0f    /* characters a second, typing mid-game */
#define SG_CHAT_REPLY_THINK	0.6f    /* the beat before the hands move */
#define SG_CHAT_REPLY_CALM	3.0f    /* nothing has hit it for this long */

/*
 * THE FLAG EVENTS NOBODY HAD A WORD FOR. Our own capture has had a voice since
 * the personality pools went in (SG_LINE_CAP, off Chat_Captures) and so has the
 * steal (SG_LINE_STEAL, off the carrier belief). The two that were silent are
 * the two a pub server is loudest about: the enemy scoring ON us, and our own
 * flag coming back to its stand.
 *
 * Those two events are the same event half the time, which is why the window
 * below exists. A capture puts the CONCEDING side's flag back on its stand --
 * red carries blue's flag over red's line, blue's flag goes home -- so a flag
 * that came home inside SG_CHAT_RETURN_CAPWIN of the other side scoring came
 * home because they scored, and has already been groaned about. Only a return
 * outside that window is a return somebody earned.
 */
#define SG_CHAT_CONCEDE_ODDS	0.50f
#define SG_CHAT_RETURN_ODDS		0.40f
#define SG_CHAT_RETURN_CAPWIN	1.0f

/*
 * The roster size, used only to space the level-open and level-end staggers
 * across the bots that are actually on the server. sg_arach.c's SG_MAXBOTS is
 * private to that file, and a slot number is all this needs, so the number is
 * repeated here rather than exported. Too small a value only bunches the
 * stagger; nothing here indexes by it.
 */
#define SG_CHAT_ROSTER		16

/*
 * MAP OPEN. One line per bot per level, well short of certain, spread so the
 * server does not open with a wall of text. The window starts after the bot's
 * own greeting has cleared the public say budget -- scheduled any earlier the
 * line is simply eaten by that budget (Chat_SayEx refuses), which would silence
 * the low slots every single map.
 */
#define SG_CHAT_OPEN_ODDS	0.35f
#define SG_CHAT_OPEN_SPAN	20.0f   /* spread across this many seconds */
#define SG_CHAT_OPEN_JITTER	0.50f   /* so the spacing is not a metronome */

/*
 * MATCH END. Fired from BeginIntermission (p_hud.c) and delivered over the
 * following few seconds. The whole stagger fits inside four seconds because
 * intermission can be exited after five (p_client.c:2823) -- a line booked
 * later than that is a line nobody reads.
 */
#define SG_CHAT_END_ODDS	0.50f
#define SG_CHAT_END_STAGGER	0.25f   /* per roster slot: 16 * 0.25 = 4.0s */
#define SG_CHAT_END_JITTER	0.20f
#define SG_CHAT_CLOSE_MARGIN	5   /* summed team score within this = close */

/*
 * IDLE BANTER. The rarest mouth in the file: an attempt is booked no sooner
 * than SG_CHAT_IDLE_GAP and the attempt itself is a coin flip, so the mean gap
 * between two idle lines from one bot is comfortably past two minutes even for
 * the chattiest slot. It only fires in a genuine lull -- nothing shooting at
 * this bot for SG_CHAT_IDLE_CALM, and no team callout anywhere on its side for
 * SG_CHAT_IDLE_QUIET. Banter over a live callout is how a channel stops being
 * read.
 */
#define SG_CHAT_IDLE_GAP	90.0f
#define SG_CHAT_IDLE_JITTER	60.0f
#define SG_CHAT_IDLE_ODDS	0.50f
#define SG_CHAT_IDLE_CALM	10.0f   /* out of contact at least this long */
#define SG_CHAT_IDLE_QUIET	10.0f   /* team channel silent at least this long */

/* Suppress recently emitted pool entries across all bots. */
#define SG_CHAT_REUSE_GAP	20.0f
/*
 * Deep enough that the ring cannot wrap inside its own window and quietly
 * forgive a line early: the public say budget lets one bot speak every
 * SG_CHAT_SAY_GAP, so sixteen of them can put at most 16 * (20 / 8) = 40
 * lines into a twenty-second window, and the queued steal callout is the only
 * other thing recorded.
 */
#define SG_CHAT_REUSE_RING	48

#define SG_CHAT_LM_RANGE	448.0f  /* a landmark names a spot within this */
#define SG_CHAT_TAKER_AGE	2.0f    /* a sighting this fresh names a taker */
#define SG_CHAT_TAKER_RANGE	512.0f  /* and only this close to the pad */
#define SG_CHAT_SOON		10.0f    /* "up in ~Ns" fires this far ahead */

/* Timer calls use their own team cooldown. */
#define SG_CHAT_TIMER_GAP	20.0f   /* one timer call per team per this */

/*
 * Per-topic team cooldowns. CACO's flag topics keep their own 8-second gap
 * inside sg_caco.c, so this table asks nothing further of them.
 */
static const float chat_topic_gap[SG_CHAT_TOPICS] = {
	0.0f,       /* SG_CHAT_TOPIC_CACO    -- caco_teamsaid already gates it */
	5.0f,       /* SG_CHAT_TOPIC_CARRIER */
	3.0f,       /* SG_CHAT_TOPIC_ITEM_UP */
	3.0f,       /* SG_CHAT_TOPIC_ITEM_GONE */
	6.0f,       /* SG_CHAT_TOPIC_ITEM_SOON */
	0.0f,       /* SG_CHAT_TOPIC_ORDER   -- capped at one ack per order */
	8.0f,       /* SG_CHAT_TOPIC_STEAL */
	SG_CHAT_TIMER_GAP,  /* SG_CHAT_TOPIC_TIMER */
	2.0f,       /* SG_CHAT_TOPIC_MAJOR -- short: majors are rare and urgent */
	/*
	 * SG_CHAT_TOPIC_REPLY. Short, because the per-bot 30-second reply gap is
	 * already the hard limit and this only has to stop two bots named in one
	 * breath from answering in the same frame.
	 */
	2.0f
};

/* ----------------------------------------------------------- personality */

enum { SG_TONE_TERSE = 0, SG_TONE_COCKY, SG_TONE_DRY, SG_TONE_MECH, SG_TONES };

enum {
	SG_LINE_JOIN = 0, SG_LINE_KILL, SG_LINE_DEATH,
	SG_LINE_CAP, SG_LINE_STEAL,
	SG_LINE_OPEN,                   /* the map just came up */
	SG_LINE_WIN, SG_LINE_LOSE, SG_LINE_CLOSE,   /* how the match ended */
	SG_LINE_IDLE,                   /* nothing happening, filling the air */
	/*
	 * The reactions the census named as missing. APPENDED, never inserted:
	 * the row order below is this enum's order and nothing but a comment
	 * keeps them in step, so a category slotted into the middle would shift
	 * every row after it into the wrong category in all four voices at once.
	 */
	SG_LINE_HURT,                   /* took a big one and lived */
	SG_LINE_SUICIDE,                /* an enemy killed himself, we watched */
	SG_LINE_REPLY,                  /* a human said this bot's name */
	SG_LINE_CONCEDE,                /* they capped on us */
	SG_LINE_RETURN,                 /* our flag is back on its stand */
	SG_LINE_CATS
};

enum {
	SG_ACK_ATTACK = 0, SG_ACK_DEFEND, SG_ACK_ESCORT,
	SG_ACK_RECOVER, SG_ACK_FREE, SG_ACK_KINDS
};

#define SG_CHAT_MAXLINES	12

/*
 * Four voices across sixteen bots, not sixteen voices: a server full of
 * individually written characters reads as a script the second two of them
 * speak in the same minute. Era-appropriate deathmatch banter, lowercase,
 * nothing over ~50 characters -- a long line is the tell that a bot wrote it.
 *
 * MOST OF THESE LINES ARE NOT WRITTEN. They were mined out of the human demo
 * corpus by tools/chatmine.py -- 268 client .dm2 recordings from 2020-2023,
 * 4459 chat lines said, filtered down to what a stranger could say again --
 * and every mined line here is verbatim, as typed, down to the missing
 * apostrophe in "thats game" and the caps-lock in "DIE !!". The buckets that
 * fed each row:
 *
 *   GREETING   -> JOIN, OPEN        TAUNT      -> KILL, STEAL (cocky)
 *   GRUMBLE    -> DEATH             GG_ENDGAME -> WIN, LOSE, CLOSE
 *   REACTION   -> CAP, DEATH, IDLE  CALL       -> STEAL, IDLE
 *
 * tools/chat-corpus.json is the mined list, and re-running the miner reports
 * any line in it that has stopped appearing in the corpus. Written lines that
 * survived are the ones no human in the corpus had occasion to type -- the
 * dry voice's asides and the whole mech register, which is a character rather
 * than an imitation of pub chat. The lines that read like an assistant doing
 * an impression of 1998 ("nice game", "gg easy", "outclassed") are gone,
 * replaced by what people actually said.
 *
 * The pools are deeper than the four-per-category the first eight bots ran
 * on. Four bots now share each voice rather than two, so a category with
 * four lines in it would have the same phrase come back around inside a
 * single firefight -- the echo the deeper pool exists to break up. Rows are
 * NULL-terminated where they are short of SG_CHAT_MAXLINES; Chat_Pick counts
 * to the first NULL, so a row may be any length up to the maximum.
 *
 * Row order is the SG_LINE_* enum's order, and nothing but this comment keeps
 * the two in step: a category added there needs a row added to all four
 * voices here, or a voice reads its neighbour's lines.
 *
 * A row tagged MINE is a STRUCTURE with placeholder text in it. The category
 * is wired, gated and rate-limited exactly like every other row -- what it has
 * not had yet is a pass over the real chat corpus, so the lines read as
 * somebody's guess at how a player phrases it rather than as how a player
 * phrased it. Replacing the strings is the whole job; nothing else about the
 * category changes when they are.
 */
static const char *chat_line[SG_TONES][SG_LINE_CATS][SG_CHAT_MAXLINES] = {
	/* SG_TONE_TERSE  -- arach, trace, ogre, knight.
	 * The corpus's two-letter classics live here: gg, ns, n1, rdy, hf. */
	{
		{ "hi", "hey", "rdy", "gl hf", "hf", "im here", "we good",
		  "here", "in", "up", NULL },
		{ "got him", "down", "next", "too slow", "stay down", "yep",
		  "ok", "word", NULL },
		{ "damn", "ouch", "ns", "n1", "oh", "no", "hm", "my bad",
		  "again", "ok", NULL },
		{ "cap", "thats one", "yes", "good", "n1", "there you go",
		  "on the board", "point", NULL },
		{ "flag is out", "got it", "moving", "have it", "going home",
		  "run", "back", NULL },
		{ "here we go", "lets run it", "gl hf", "i like it", "ok",
		  "new map", "know this one", NULL },
		{ "gg", "ggs", "gg wp", "that's game", "well played",
		  "won that", "we take it", NULL },
		{ "gg", "ggs", "gg's", "well played", "good game", "beat us",
		  "our fault", "next map", NULL },
		{ "tough one", "gg wp", "ggs", "well played", "lol close call",
		  "close one", "that was close", NULL },
		{ "brb", "back", "break", "quiet", "still here", "waiting",
		  "hm", NULL },
		{ "ouch", "phew", "still up", "that hurt", "close", NULL },
		{ "lol", "rofl", "nice one", "hah", "saved me the trouble", NULL },
		{ "?", "what", "ok", "yeah", "busy", "here", NULL },
		{ "damn", "no!", "they scored", "on us", "good lord", NULL },
		{ "returned", "got it back", "flags home", "back", "clear", NULL }
	},
	/* SG_TONE_COCKY  -- caco, slip, fiend, spawn */
	{
		{ "who wants it", "easy day", "im here now", "lets have it",
		  "line up", "hope you practiced", "im back", NULL },
		{ "sit down", "too easy", "all day", "thats mine", "get better",
		  "outclassed", "not even close", NULL },
		{ "lucky", "cheap", "whatever", "sure", "nice shot i guess",
		  "wont happen twice", NULL },
		{ "thats how you do it", "run it back", "count it", "told you",
		  "put it up", "thats a point", NULL },
		{ "flags mine", "watch this", "coming through", "ill take that",
		  "say goodbye to it", NULL },
		{ "my map", "easy map", "i live in that flag room",
		  "lets see who shows up", "hope you know the route",
		  "this ones mine", NULL },
		{ "told you", "not even close", "gg easy", "we owned that",
		  "learn the map", "any time", NULL },
		{ "lag", "rematch", "we werent trying", "you got lucky",
		  "next map is mine", "wont happen next map", NULL },
		{ "too close", "we let that get close", "good game i guess",
		  "you got lucky at the end", "next one wont be close", NULL },
		{ "somebody do something", "im getting bored", "wake up out there",
		  "who wants a go", "this is too easy", NULL },
		{ "barely felt it", "is that it", "still standing", "nice try",
		  "youll have to do better", NULL },
		{ "lol", "did it for me", "saved me a rocket", "quality",
		  "thats embarrassing", NULL },
		{ "what", "yeah", "im busy", "talk later", "go on then", NULL },
		{ "who was watching that", "sloppy", "cover the base", "come on",
		  "that ones on us", NULL },
		{ "got it back", "youre welcome", "flags home", "i had it",
		  "nice try though", NULL }
	},
	/* SG_TONE_DRY    -- rune, phase, wizard, scrag.
	 * Mined understatement plus the written asides nobody in the corpus
	 * had occasion to type. */
	{
		{ "been a while", "good to see you", "hello?", "good luck",
		  "evening", "right then", "shall we", "lovely", NULL },
		{ "i suppose", "i bet", "well...", "predictable", "as expected",
		  "noted", "that was quick", "quite", NULL },
		{ "good lord", "ns", "well...", "def weird", "of course",
		  "wonderful", "typical", "ah", NULL },
		{ "there you go", "nicely done", "nice one", "one for us",
		  "there it is", "acceptable", "as planned", NULL },
		{ "we have theirs", "flag is away", "borrowed it", "taking this",
		  "do excuse me", "back", NULL },
		{ "i like it", "lets run it", "gl hf", "ah, this map",
		  "not this one again", "quaint",
		  "that flag room is a deathtrap", NULL },
		{ "well played", "nicely done", "good game", "ggs everyone",
		  "loved this game", "that went nicely", "thank you all", NULL },
		{ "well played", "good game", "tough one", "gg's", "nite",
		  "they earned it", "next time perhaps", "so it goes", NULL },
		{ "that was tough!", "great game", "nicely done", "gg wp",
		  "closer than i would like", "a proper game at last",
		  "very nearly", NULL },
		{ "need a mo", "be back", "i suppose", "quiet, isnt it",
		  "i shall put the kettle on", "any moment now",
		  "lovely weather in here", NULL },
		{ "ow", "that stung", "how rude", "nearly had me",
		  "closer than i would like", NULL },
		{ "oh dear", "how embarrassing", "well done him", "quite the exit",
		  "marvellous", NULL },
		{ "yes", "hm", "im listening", "one moment", "do go on", NULL },
		{ "how disappointing", "we let that in", "must we", "hm, careless",
		  "somebody was asleep", NULL },
		{ "returned", "flag is home", "recovered", "back where it belongs",
		  "there we are", NULL }
	},
	/* SG_TONE_MECH   -- gate, field, vore, shal.
	 * A character rather than an imitation, so it keeps its written
	 * register; the mined lines here are the flat, procedural ones. */
	{
		{ "ready", "good to go", "online", "unit ready", "standing by",
		  "systems nominal", "link established", "active", NULL },
		{ "target down", "confirmed kill", "one less", "clean",
		  "threat eliminated", "target neutralized", "ok", NULL },
		{ "ns", "reset", "respawning", "damage critical", "recycling",
		  "systems failing", "rebuilding", NULL },
		{ "objective complete", "point scored", "capture logged",
		  "score updated", "mission success", "yes", NULL },
		{ "flag acquired", "carrying", "objective in hand",
		  "asset secured", "extracting", "back", NULL },
		{ "map loaded", "terrain acquired", "layout known",
		  "route table ready", "scanning layout", "position confirmed",
		  "gl hf", NULL },
		{ "gg", "match complete", "objective secured", "victory logged",
		  "we win", "mission accomplished", "score final", NULL },
		{ "gg", "ggs", "match lost", "objective failed", "defeat logged",
		  "outperformed", "recalibrating", "analysis pending", NULL },
		{ "gg wp", "margin minimal", "close result", "within tolerance",
		  "narrow finish", "closely contested", NULL },
		{ "BASE IS CLEAR", "idle", "no contacts", "awaiting contact",
		  "power conserved", "scan clear", "holding position", NULL },
		{ "damage taken", "integrity holding", "impact absorbed",
		  "still operational", "armor compromised", NULL },
		{ "self terminated", "no action required", "target removed itself",
		  "logged", "efficient", NULL },
		{ "acknowledged", "listening", "standing by", "occupied",
		  "go ahead", NULL },
		{ "objective lost", "enemy scored", "defense failed",
		  "score against", "recalibrating", NULL },
		{ "flag recovered", "objective home", "asset returned",
		  "flag secured", "position restored", NULL }
	}
};

static const char *chat_ack[SG_TONES][SG_ACK_KINDS] = {
	/* terse */    { "going",         "on d",         "with you",
	                 "on the flag",   "clear" },
	/* cocky */    { "on my way",     "ill hold it",  "stay close",
	                 "getting it",    "free again" },
	/* dry */      { "attacking then","defending",    "behind you",
	                 "recovering",    "as you were" },
	/* mech */     { "attacking",     "defending",    "escorting",
	                 "recovering flag", "orders cleared" }
};

/*
 * The roster is sg_arach.c's sg_names table; the voice map is ours. Sixteen
 * names, four to a voice, so the two halves of the roster are spread evenly
 * rather than the newer eight all landing in one tone. A name absent from
 * this table still speaks -- Chat_Tone falls back to terse -- so the table
 * going stale against sg_names costs flavour, not function.
 */
static const struct { const char *name; int tone; } chat_voice[] = {
	{ "arach",  SG_TONE_TERSE }, { "trace",  SG_TONE_TERSE },
	{ "ogre",   SG_TONE_TERSE }, { "knight", SG_TONE_TERSE },
	{ "caco",   SG_TONE_COCKY }, { "slip",   SG_TONE_COCKY },
	{ "fiend",  SG_TONE_COCKY }, { "spawn",  SG_TONE_COCKY },
	{ "rune",   SG_TONE_DRY   }, { "phase",  SG_TONE_DRY   },
	{ "wizard", SG_TONE_DRY   }, { "scrag",  SG_TONE_DRY   },
	{ "gate",   SG_TONE_MECH  }, { "field",  SG_TONE_MECH  },
	{ "vore",   SG_TONE_MECH  }, { "shal",   SG_TONE_MECH  },
	{ NULL, 0 }
};

/* ---------------------------------------------------------------- state */

typedef struct
{
	qboolean	greeted;
	float		greet_at;       /* 0 = not scheduled yet */
	int			greet_tries;
	float		next_team;      /* say_team budget */
	float		next_say;       /* public say budget */
	float		next_taunt;
	float		next_grumble;
	float		next_ack;       /* floor under a human spamming orders */

	/* the map-open line: booked once, spoken at most once */
	qboolean	opened;
	float		open_at;        /* 0 = not scheduled yet */

	/* the match-end line, booked by SG_ChatLevelEnd */
	int			end_cat;        /* -1 = nothing booked */
	float		end_at;

	/* idle banter */
	float		next_idle;      /* next ATTEMPT, which is then rolled */
	float		combat_at;      /* last moment this bot was in a fight */

	/*
	 * The big hit that landed and did not kill. hurt_seen is the newest ring
	 * stamp already accounted for, so one hit is noticed once however many
	 * frames it stays in the four-slot ring; hurt_at is the one being waited
	 * out, and zero means nothing is pending.
	 */
	float		hurt_at;
	float		hurt_seen;
	float		next_hurt;

	/*
	 * The reply owed to a human who used this bot's name. reply_line points
	 * into chat_line[][][] and nowhere else -- picked at hearing time so its
	 * length can be typed at a human rate -- and NULL means nothing is owed.
	 */
	const char	*reply_line;
	float		reply_at;
	qboolean	reply_team;     /* answer on the channel he used */
	float		next_reply;

	/* the standing order, if any */
	int			order_role;     /* SG_CHAT_ROLE_NONE when none */
	float		order_expire;
	int			order_from;     /* client number of the human who gave it */
	int			order_team;     /* the team both were on at the time */

	/* what this bot is holding: the one thing it may speak of unseen */
	qboolean	had_quad;
	qboolean	had_invul;
	int			had_rune;       /* edict index, 0 = empty handed */

	/* Chat texture must not consume the engine-global RNG that owns weapon
	 * spread and other physical outcomes. This stream belongs to the current
	 * client generation and is retired with the rest of this structure. */
	uint32_t	random_state;
} sg_chat_bot_t;

static sg_chat_bot_t chat_bot[MAX_CLIENTS];

/* A team learns an item clock only when its queued callout is emitted. */
enum { SG_ARM_NONE = 0, SG_ARM_ITEM, SG_ARM_WATCH, SG_ARM_QUIET,
       SG_ARM_MEGATAKE };

/*
 * Who each team believes is wearing the mega (client index, -1 nobody).
 * Written only when a mega take-call actually goes out -- the record is
 * knowledge and knowledge rides the spoken line (Rule 19). Read by
 * SG_ChatMegaDeath: the obituary is public, so "the mega guy just died"
 * plus this record is the one honest way to start the mega's clock.
 */
static int chat_mega_taker[2] = { -1, -1 };

typedef struct
{
	qboolean	pending;
	int			speaker;        /* client number */
	unsigned long	speaker_ctfid; /* exact bot generation that queued it */
	float		due;
	char		line[SG_CHAT_LINE];

	/* the respawn clock this line earns for its team IF it is spoken */
	int			arm_kind;       /* SG_ARM_* -- NONE for every ordinary line */
	int			arm_slot;       /* index into sg_caco_items rows, or chat_watch */
	int			arm_ent;        /* the item entity, for the sg_debug line */
	int			arm_src;        /* SG_ITEMCALL_* -- who is making the call */
	float		arm_back_at;    /* absolute: take time + the item's own delay */
	int			arm_who;        /* taker client index for the mega record, -1 */
} sg_chatq_t;

static sg_chatq_t	chat_q[2][SG_CHAT_TOPICS];      /* [team-1][topic] */
static float		chat_teamsaid[2][SG_CHAT_TOPICS];

/*
 * When each side last had a callout land, per team-1. chat_teamsaid cannot
 * answer that question -- it is a per-topic "not before" stamp, so a quiet
 * topic and a topic that just fired look alike once their gaps differ. Idle
 * banter is the only reader: it stays out of the way of live information.
 */
static float		chat_team_last[2];

/*
 * The reuse ring. Entries are pointers into chat_line[][][] and nowhere else;
 * Chat_Note is called only where a pool pointer is in hand, never with a
 * caller's buffer, because an entry outlives the call that made it.
 *
 * Pointer identity is the comparison on purpose. Two rows holding the same
 * text are the same line to a reader, and the compiler is free to fold them to
 * one address, which makes the guard catch that case for free.
 */
static struct { const char *line; float at; } chat_recent[SG_CHAT_REUSE_RING];
static int			chat_recent_head;

/*
 * Belief bookkeeping for the items CACO already tracks (quad, invuln, the
 * five runes). Parallel to sg_caco_items by index.
 *
 * `up` is what a team has been TOLD, which is not the same as
 * sg_caco_items[].believed_up on two counts. The respawn clock moves CACO's
 * belief with nobody looking, and a bot saying "quad is up" off a clock
 * would be claiming a look nobody took. And sg_caco_items is one table for
 * both sides, so a red sighting would otherwise silence blue's callout about
 * the same pad. Everything here is per team-1, and moves only on that team's
 * own sighting or on one of that team's bots picking the thing up.
 */
typedef struct
{
	qboolean	up[2];
	float		back_at[2];     /* clock says it returns then; 0 = no clock */
	qboolean	soon_said[2];
} sg_chat_item_t;

static sg_chat_item_t chat_item[SG_MAX_BELIEF_ITEMS];

/*
 * Body armour and power armour are not CACO belief classes, so their state
 * is kept here and fed by this module's own sighting scan (SG_ChatSee),
 * running the same PVS-plus-trace test CACO runs. Respawn delays are read,
 * not assumed: Pickup_Armor calls SetRespawn(ent, 20) (g_items.c:759) and
 * Pickup_PowerArmor calls SetRespawn(ent, ent->item->quantity)
 * (g_items.c:912).
 */
#define SG_CHAT_MAX_WATCH	12

typedef struct
{
	int			ent;            /* edict index */
	const char	*name;
	float		respawn;
	qboolean	up[2];          /* per team-1, for the reason above */
	float		back_at[2];
	qboolean	soon_said[2];
} sg_chat_watch_t;

static sg_chat_watch_t	chat_watch[SG_CHAT_MAX_WATCH];
static int				chat_num_watch;

/*
 * Landmarks: the things a player names a place by. Walked once at level
 * start, the way Caco_ScanItemSpawns walks the item entities once -- these
 * are map knowledge, constant, and free. Runes are deliberately absent: one
 * relocates itself every RUNETHINKTIME = 30s (g_runes.c:10, Rune_Think at
 * g_runes.c:334-352), so "at the haste rune" names nowhere.
 */
#define SG_CHAT_MAX_LM	32

typedef struct
{
	vec3_t		org;
	const char	*name;
} sg_chat_lm_t;

static sg_chat_lm_t	chat_lm[SG_CHAT_MAX_LM];
static int			chat_num_lm;

static vec3_t	chat_flagpos[2];        /* [0] red stand, [1] blue stand */
static qboolean	chat_flagpos_ok[2];

/* team events watched off common knowledge (the scoreboard, the HUD icon) */
static int	chat_lastscore[2];      /* team capture totals, per team-1 */
static int	chat_lastcarrier[2];

/*
 * Our own flag on its own stand, per team-1, and when the other side last
 * scored on us. Both are HUD-level facts -- the score line shows a taken flag
 * and the scoreboard shows the capture -- so believing either needs no
 * sighting from anybody. The second exists only to tell the two ways a flag
 * comes home apart; see SG_CHAT_RETURN_CAPWIN.
 */
static qboolean	chat_flaghome[2];
static float	chat_conceded_at[2];

/* ------------------------------------------------------------- utilities */

static int Chat_ClientNum(edict_t *e)
{
	return (int)(e - g_edicts - 1);
}

/* the tree has no Q_strncpyz; this is the same contract, always terminated */
static void Chat_Copy(char *dst, const char *src, int size)
{
	int i;

	if (size <= 0 || !src)
		return;
	for (i = 0; i < size - 1 && src[i]; i++)
		dst[i] = src[i];
	dst[i] = '\0';
}

static qboolean Chat_Playing(edict_t *e)
{
	if (!e || !e->inuse || !e->client)
		return false;
	if (e->client->ctf.teamnum != CTF_TEAM_RED &&
	    e->client->ctf.teamnum != CTF_TEAM_BLUE)
		return false;
	if (e->deadflag == DEAD_DEAD || e->health <= 0)
		return false;
	return true;
}

/* one of ours, in the game, alive enough to talk */
static qboolean Chat_OurBot(edict_t *e)
{
	if (!e || !e->inuse || !e->client)
		return false;
	if (!(e->flags & FL_BOT))
		return false;
	return SG_OwnsBot(e);
}

/*
 * The voice, from the name sg_arach.c gave the bot: "Arach[SG]" -> terse.
 * The same prefix match answers "is this word addressing that bot", so both
 * the personality and the order parser go through here.
 */
static qboolean Chat_NameIs(edict_t *e, const char *word)
{
	const char	*n;
	int			i;

	if (!e || !e->client || !word || !word[0])
		return false;
	n = e->client->pers.netname;
	if (n[0] == '[')
	{
		const char *close = strchr(n, ']');
		if (close)
			n = close + 1;          /* "[SG]Arach" -> "Arach" */
	}
	for (i = 0; word[i] && n[i]; i++)
		if (tolower((unsigned char)word[i]) != tolower((unsigned char)n[i]))
			return false;
	if (word[i])
		return false;                   /* the word ran past the name */
	return (n[i] == '\0' || n[i] == '[') ? true : false;
}

static int Chat_Tone(edict_t *e)
{
	int i;

	if (!e || !e->client)
		return SG_TONE_TERSE;
	for (i = 0; chat_voice[i].name; i++)
		if (Chat_NameIs(e, chat_voice[i].name))
			return chat_voice[i].tone;
	return SG_TONE_TERSE;               /* a renamed bot still gets a voice */
}

/*
 * How chatty this particular bot is: 0.5 to 1.5, fixed for the life of the
 * slot, multiplying the odds of every rolled personality line.
 *
 * (slot * 7) % 11 is the whole derivation. 7 and 11 are coprime, so the eleven
 * rates are hit in a stride that puts NEIGHBOURING slots far apart -- slots 0,
 * 1, 2, 3 come out 0.5, 1.2, 0.8, 1.5 -- and bots are added into consecutive
 * slots, so a run of adjacent rates would have made the first half of a
 * botfilled team uniformly quiet and the second half uniformly loud. It is
 * deterministic rather than rolled because a bot that is talkative tonight and
 * withdrawn tomorrow is not a personality, it is noise. Eleven rates over
 * sixteen slots means a few pairs share one; that is fine, they still differ
 * in voice.
 */
static float Chat_Chatty(int cl)
{
	float pf;

	if (cl < 0)
		return 1.0f;
	/* the persona table owns per-bot character when it is on; the coprime
	 * spread below is the fallback voice of a persona-less build */
	pf = SG_PersonaBanterFreqSlot(cl);
	if (pf > 0.0f)
		return pf;
	return 0.5f + (float)((cl * 7) % 11) * 0.1f;
}

static qboolean Chat_RandomDraw(edict_t *speaker, uint32_t *draw)
{
	int cl;

	if (!speaker || !speaker->client || !draw)
		return false;
	cl = Chat_ClientNum(speaker);
	if (cl < 0 || cl >= game.maxclients || cl >= MAX_CLIENTS)
		return false;
	if (chat_bot[cl].random_state == 0)
		chat_bot[cl].random_state = SG_ChatRandomInitial(
		    (uint64_t)speaker->client->ctf.ctfid, (unsigned)cl);
	chat_bot[cl].random_state = SG_ChatRandomNext(chat_bot[cl].random_state);
	*draw = chat_bot[cl].random_state;
	return true;
}

static float Chat_RandomUnit(edict_t *speaker)
{
	uint32_t draw;

	/* Invalid speakers fail personality probability gates instead of getting a
	 * guaranteed zero roll. Valid queue callers are checked before this point. */
	return Chat_RandomDraw(speaker, &draw) ? SG_ChatRandomUnit(draw) : 1.0f;
}

static int Chat_RandomBounded(edict_t *speaker, int count)
{
	uint32_t draw;

	if (count <= 0 || !Chat_RandomDraw(speaker, &draw))
		return -1;
	return (int)(draw % (uint32_t)count);
}

/* has this exact line been said by anybody inside the reuse window */
static qboolean Chat_Recent(const char *line)
{
	int i;

	if (!line)
		return false;
	for (i = 0; i < SG_CHAT_REUSE_RING; i++)
		if (chat_recent[i].line == line &&
		    level.time - chat_recent[i].at < SG_CHAT_REUSE_GAP)
			return true;
	return false;
}

/*
 * Record a line as said. Called where the line actually went out, not where it
 * was picked: a line the say budget refused was never heard, and burning it
 * for twenty seconds would thin the pools for nothing.
 */
static void Chat_Note(const char *line)
{
	if (!line)
		return;
	chat_recent[chat_recent_head].line = line;
	chat_recent[chat_recent_head].at = level.time;
	chat_recent_head = (chat_recent_head + 1) % SG_CHAT_REUSE_RING;
}

/*
 * A line from this voice's row, preferring one nobody has used lately.
 *
 * The fallback matters: when every line in a short row is inside the reuse
 * window the pick is made from the whole row anyway. Going silent instead
 * would turn the guard into a mute button on exactly the categories with the
 * fewest lines, which is the opposite of what it is for.
 */
static const char *Chat_Pick(edict_t *speaker, int tone, int cat)
{
	const char	*fresh[SG_CHAT_MAXLINES];
	int			n = 0, f = 0, i, pick;

	if (!speaker || tone < 0 || tone >= SG_TONES ||
	    cat < 0 || cat >= SG_LINE_CATS)
		return NULL;
	while (n < SG_CHAT_MAXLINES && chat_line[tone][cat][n])
		n++;
	if (n == 0)
		return NULL;

	for (i = 0; i < n; i++)
		if (!Chat_Recent(chat_line[tone][cat][i]))
			fresh[f++] = chat_line[tone][cat][i];

	if (f > 0)
	{
		pick = Chat_RandomBounded(speaker, f);
		return pick >= 0 ? fresh[pick] : NULL;
	}
	pick = Chat_RandomBounded(speaker, n);
	return pick >= 0 ? chat_line[tone][cat][pick] : NULL;
}

/* a live bot of ours on that team, preferring one whose budget is clear */
static edict_t *Chat_Speaker(int team)
{
	edict_t	*fallback = NULL;
	int		i;

	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return NULL;

	for (i = 0; i < game.maxclients; i++)
	{
		edict_t *e = g_edicts + 1 + i;

		if (!Chat_OurBot(e) || !Chat_Playing(e))
			continue;
		if (e->client->ctf.teamnum != team)
			continue;
		if (level.time >= chat_bot[i].next_team)
			return e;
		if (!fallback)
			fallback = e;
	}
	return fallback;
}

/* ------------------------------------------------------------- emission */

qboolean SG_ChatSayTeam(edict_t *speaker, const char *line, int topic)
{
	char	buf[SG_CHAT_LINE];
	int		cl, team;

	if (!speaker || !line || !line[0])
		return false;
	if (topic < 0 || topic >= SG_CHAT_TOPICS)
		return false;
	if (!speaker->inuse || !speaker->client || !(speaker->flags & FL_BOT))
		return false;
	if (!Chat_Playing(speaker))
		return false;                   /* the dead do not call it out */

	cl = Chat_ClientNum(speaker);
	if (cl < 0 || cl >= game.maxclients)
		return false;
	team = speaker->client->ctf.teamnum;

	/*
	 * An order acknowledgement is exempt from the budget: four seconds late
	 * it is worse than silence, and the parser only ever asks for one.
	 *
	 * CACO's flag line also admits through an older, lower-priority use of this
	 * bot's voice. The sighting has already paid CACO's team/topic gap and its
	 * human reaction delay; silently throwing it away because the same bot
	 * mentioned an item a moment earlier loses earned objective information.
	 * Unlike ORDER, CACO still stamps the budget below, so the urgent line
	 * quiets ordinary chatter that follows it.
	 */
	if (SG_ChatTopicBlocksOnBotGap(topic))
	{
		if (level.time < chat_bot[cl].next_team)
			return false;
	}
	if (SG_ChatTopicStampsBotGap(topic))
	{
		if (level.time < chat_teamsaid[SG_TeamIdx(team)][topic])
			return false;
	}

	Chat_Copy(buf, line, sizeof(buf));
	SG_BotClientCommand(cl, "say_team", buf, NULL);

	/* stamped for every topic, acknowledgements included: idle banter reads
	 * this to stay off a channel that is carrying something */
	chat_team_last[SG_TeamIdx(team)] = level.time;

	if (SG_ChatTopicStampsBotGap(topic))
	{
		chat_bot[cl].next_team = level.time + SG_CHAT_BOT_GAP;
		chat_teamsaid[SG_TeamIdx(team)][topic] =
			level.time + chat_topic_gap[topic];
	}
	return true;
}

/*
 * The public channel: personality only, and on its own slower budget.
 *
 * Two things speak from outside the ordinary alive-and-well case, so they are
 * flags here rather than a second copy of the emitter:
 *
 *   SG_SAYF_DEAD    the speaker is a corpse or is frozen at the intermission
 *                   point. A grumble comes from a bot that just died, and a
 *                   match-end line comes from one the level has already
 *                   stopped; Chat_Playing refuses both, correctly, for
 *                   callouts -- but neither of these is a callout.
 *   SG_SAYF_NOGAP   skip the public say budget, still stamping it. Used only
 *                   by the match-end line, which is one line per bot at a
 *                   moment that will not come again this level; letting a
 *                   taunt from four seconds earlier eat it would silence
 *                   whoever was busiest at the whistle.
 *
 * The speaker must still be one of ours and on a team either way.
 */
#define SG_SAYF_DEAD	1
#define SG_SAYF_NOGAP	2

static qboolean Chat_SayEx(edict_t *speaker, const char *line, int flags)
{
	char	buf[SG_CHAT_LINE];
	int		cl;

	if (!speaker || !line || !line[0])
		return false;
	if (!Chat_OurBot(speaker))
		return false;
	if (flags & SG_SAYF_DEAD)
	{
		if (speaker->client->ctf.teamnum != CTF_TEAM_RED &&
		    speaker->client->ctf.teamnum != CTF_TEAM_BLUE)
			return false;
	}
	else if (!Chat_Playing(speaker))
		return false;

	cl = Chat_ClientNum(speaker);
	if (cl < 0 || cl >= game.maxclients)
		return false;
	if (!(flags & SG_SAYF_NOGAP) && level.time < chat_bot[cl].next_say)
		return false;

	Chat_Copy(buf, line, sizeof(buf));
	SG_BotClientCommand(cl, "say", buf, NULL);
	chat_bot[cl].next_say = level.time + SG_CHAT_SAY_GAP;
	return true;
}

/* say a line picked out of the pools, and burn it for everybody if it lands */
static qboolean Chat_SayPooled(edict_t *speaker, const char *line, int flags)
{
	if (!Chat_SayEx(speaker, line, flags))
		return false;
	Chat_Note(line);
	return true;
}

/*
 * Queue a team line behind a reaction delay. One slot per team per topic:
 * a second sighting of the same thing while the first is still on its way
 * is the same news, and six bots blurting it in one frame reads as one.
 *
 * Returns whether the line took the slot. Every caller but the item-taken
 * one ignores it, as they always have; that one needs to know, because a
 * refused line is a respawn clock the team does not get.
 */
static qboolean Chat_QueueArm(edict_t *speaker, int team, int topic,
                              const char *line, int arm_kind, int arm_slot,
                              int arm_ent, int arm_src, float arm_back_at,
                              int arm_who)
{
	sg_chatq_t *q;

	if (!speaker || !speaker->client || !(speaker->flags & FL_BOT))
		return false;
	if (speaker->client->ctf.ctfid == 0)
		return false;
	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return false;
	if (topic < 0 || topic >= SG_CHAT_TOPICS)
		return false;

	q = &chat_q[SG_TeamIdx(team)][topic];
	if (q->pending)
		return false;
	if (level.time < chat_teamsaid[SG_TeamIdx(team)][topic])
		return false;

	Chat_Copy(q->line, line, sizeof(q->line));
	q->speaker = Chat_ClientNum(speaker);
	q->speaker_ctfid = speaker->client->ctf.ctfid;
	q->due = level.time + SG_CHAT_DELAY_MIN +
	         Chat_RandomUnit(speaker) *
	         (SG_CHAT_DELAY_MAX - SG_CHAT_DELAY_MIN);
	q->arm_kind = arm_kind;
	q->arm_slot = arm_slot;
	q->arm_ent = arm_ent;
	q->arm_src = arm_src;
	q->arm_back_at = arm_back_at;
	q->arm_who = arm_who;
	q->pending = true;
	return true;
}

static void Chat_Queue(edict_t *speaker, int team, int topic, const char *line)
{
	Chat_QueueArm(speaker, team, topic, line, SG_ARM_NONE, 0, 0, 0, 0.0f, -1);
}

/* defined below, beside the majors table it is about */
static void Chat_ArmClock(int ti, const sg_chatq_t *q, qboolean said);

static void Chat_Flush(void)
{
	int t, k;

	for (t = 0; t < 2; t++)
		for (k = 0; k < SG_CHAT_TOPICS; k++)
		{
			sg_chatq_t	*q = &chat_q[t][k];
			sg_chatq_t	held;
			edict_t		*sp;
			qboolean	said = false;

			if (!q->pending || level.time < q->due)
				continue;

			/*
			 * Taken off the slot before anything can fail: a muted line must
			 * not jam it, and the copy is what carries the arm through the
			 * emit so a re-entrant queue on the same slot cannot claim it.
			 */
			held = *q;
			q->pending = false;
			q->arm_kind = SG_ARM_NONE;

			if (held.speaker >= 0 && held.speaker < game.maxclients)
			{
				sp = g_edicts + 1 + held.speaker;
				/* The queue belongs to one exact bot generation. A recycled slot
				 * or a side switch cannot inherit and publish the old body's news. */
				if (Chat_OurBot(sp) && Chat_Playing(sp) &&
				    SG_CalloutSpeakerCurrent(held.speaker_ctfid,
				        sp->client->ctf.ctfid) &&
				    sp->client->ctf.teamnum == SG_TeamFromIdx(t))
					said = SG_ChatSayTeam(sp, held.line, k);
			}

			Chat_ArmClock(t, &held, said);
		}
}

/* --------------------------------------------------------- place naming */

/*
 * The landmark table. Powerups, both armours, mega, and the five weapons a
 * player calls a position by. Dropped copies are skipped for the reason
 * Caco_Where skips them: "by the shotgun" is no use when the shotgun
 * belonged to a corpse and is about to vanish.
 */
static const struct { const char *cls; const char *name; } chat_lm_class[] = {
	{ "item_quad",              "the quad" },
	{ "item_invulnerability",   "the invuln" },
	{ "item_armor_body",        "red armor" },
	{ "item_armor_combat",      "yellow armor" },
	{ "item_health_mega",       "the mega" },
	{ "item_power_shield",      "the power shield" },
	{ "item_power_screen",      "the power screen" },
	{ "weapon_rocketlauncher",  "the rl" },
	{ "weapon_railgun",         "the rg" },
	{ "weapon_chaingun",        "the cg" },
	{ "weapon_hyperblaster",    "the hb" },
	{ "weapon_bfg",             "the bfg" },
	{ NULL, NULL }
};

static void Chat_ScanLandmarks(void)
{
	edict_t	*e;
	int		i;

	chat_num_lm = 0;
	chat_flagpos_ok[0] = chat_flagpos_ok[1] = false;

	for (i = 0; i < globals.num_edicts && chat_num_lm < SG_CHAT_MAX_LM; i++)
	{
		edict_t *ent = &g_edicts[i];
		int		k;

		if (!ent->inuse || !ent->classname)
			continue;
		if (ent->spawnflags & DROPPED_ITEM)
			continue;
		for (k = 0; chat_lm_class[k].cls; k++)
			if (strcmp(ent->classname, chat_lm_class[k].cls) == 0)
			{
				VectorCopy(ent->s.origin, chat_lm[chat_num_lm].org);
				chat_lm[chat_num_lm].name = chat_lm_class[k].name;
				chat_num_lm++;
				break;
			}
	}

	e = SG_FlagStand(CTF_TEAM_RED, true);
	if (e)
	{
		VectorCopy(e->s.origin, chat_flagpos[0]);
		chat_flagpos_ok[0] = true;
	}
	e = SG_FlagStand(CTF_TEAM_BLUE, true);
	if (e)
	{
		VectorCopy(e->s.origin, chat_flagpos[1]);
		chat_flagpos_ok[1] = true;
	}
}

/*
 * Name a position the way a player would. Nearest landmark inside
 * SG_CHAT_LM_RANGE wins; failing that the two flag stands split the map into
 * three, which is how a callout with no landmark to hand is phrased. Team
 * neutral -- see Chat_LocNameFor for the "our/their" form the callouts use.
 *
 * `skip` is a landmark position to leave out of the running, or NULL for none,
 * and it exists for one case: naming the spot where an item was just taken.
 * Half the majors ARE landmarks, so without it a bot says
 * "took quad at the quad", which tells a teammate nothing he did not already
 * have from the first two words. Skipping the item's own landmark makes the
 * call fall through to the next thing in sight, or to the base thirds.
 *
 * False means it had nothing to name the place by -- no landmark in range and
 * no flag stands to divide the map with -- and `out` is untouched. The caller
 * then says the item without a place, which is the owner's own instruction:
 * "calling the item specifically will help and it should count even without
 * the location" (2026-08-05).
 */
static qboolean Chat_LocNameSkip(vec3_t pos, const float *skip,
                                 char *out, int len)
{
	float	best = SG_CHAT_LM_RANGE, dist, dred, dblue;
	int		bi = -1, i;
	vec3_t	d;

	for (i = 0; i < chat_num_lm; i++)
	{
		if (skip)
		{
			VectorSubtract(chat_lm[i].org, skip, d);
			if (VectorLength(d) < 1.0f)
				continue;               /* that landmark IS the thing taken */
		}
		VectorSubtract(chat_lm[i].org, pos, d);
		dist = VectorLength(d);
		if (dist < best)
		{
			best = dist;
			bi = i;
		}
	}
	if (bi >= 0)
	{
		Chat_Copy(out, chat_lm[bi].name, len);
		return true;
	}

	if (!chat_flagpos_ok[0] || !chat_flagpos_ok[1])
		return false;

	VectorSubtract(chat_flagpos[0], pos, d);
	dred = VectorLength(d);
	VectorSubtract(chat_flagpos[1], pos, d);
	dblue = VectorLength(d);

	if (dred < dblue * 0.6f)
		Chat_Copy(out, "red base", len);
	else if (dblue < dred * 0.6f)
		Chat_Copy(out, "blue base", len);
	else
		Chat_Copy(out, "midfield", len);
	return true;
}

static void Chat_LocName(vec3_t pos, char *out, int len)
{
	if (!Chat_LocNameSkip(pos, NULL, out, len))
		Chat_Copy(out, "out there", len);
}

void SG_ChatLocName(vec3_t pos, char *out, int len)
{
	Chat_LocName(pos, out, len);
}

/* the same name, said from one team's point of view */
static qboolean Chat_LocNameForSkip(vec3_t pos, const float *skip, int team,
                                    char *out, int len)
{
	if (!Chat_LocNameSkip(pos, skip, out, len))
		return false;

	if (strcmp(out, "red base") == 0)
		Chat_Copy(out, (team == CTF_TEAM_RED) ? "our base" : "their base",
		           len);
	else if (strcmp(out, "blue base") == 0)
		Chat_Copy(out, (team == CTF_TEAM_BLUE) ? "our base" : "their base",
		           len);
	return true;
}

static void Chat_LocNameFor(vec3_t pos, int team, char *out, int len)
{
	if (!Chat_LocNameForSkip(pos, NULL, team, out, len))
		Chat_Copy(out, "out there", len);
}

/* ------------------------------------------------------------ item names */

static const struct { const char *cls; const char *name; } chat_item_name[] = {
	{ "item_quad",              "quad" },
	{ "item_invulnerability",   "invuln" },
	{ "damage_rune",            "damage rune" },
	{ "haste_rune",             "haste rune" },
	{ "resist_rune",            "resist rune" },
	{ "regen_rune",             "regen rune" },
	{ "vampire_rune",           "vamp rune" },
	{ "item_armor_body",        "red armor" },
	{ "item_power_shield",      "power shield" },
	{ "item_power_screen",      "power screen" },
	{ NULL, NULL }
};

static const char *Chat_ItemName(edict_t *e)
{
	int i;

	if (!e || !e->classname)
		return NULL;
	for (i = 0; chat_item_name[i].cls; i++)
		if (strcmp(e->classname, chat_item_name[i].cls) == 0)
			return chat_item_name[i].name;
	return NULL;
}

/*
 * Did this team see an enemy standing by that pad a moment ago? That, plus
 * an empty pad, is a player's whole basis for "enemy took quad" -- and it is
 * the only basis this module will accept for naming a taker. A sighting
 * placed by ear (heard_only, sg_local.h:87) is not good enough: noise in the
 * PHS says somebody is around, never that somebody took it.
 */
static qboolean Chat_EnemySeenNear(int team, vec3_t org)
{
	rune_t	*r = SG_Rune();
	int		i;

	if (!r || team < CTF_TEAM_RED || team > CTF_TEAM_BLUE)
		return false;

	for (i = 0; i < SG_MAX_ENEMY_TRACK; i++)
	{
		sg_belief_enemy_t	*en = &sg_caco_enemies[SG_TeamIdx(team)][i];
		vec3_t				d;

		if (en->client < 0 || en->heard_only)
			continue;
		if (level.time - en->seen_time > SG_CHAT_TAKER_AGE)
			continue;
		if (en->seed < 0 || en->seed >= r->hdr.num_seeds)
			continue;
		VectorSubtract(r->seeds[en->seed].origin, org, d);
		if (VectorLength(d) < SG_CHAT_TAKER_RANGE)
			return true;
	}
	return false;
}

/* ------------------------------------------------------ the damage ring
 *
 * sg_caco.c books every landed hit on one of our bots in a four-slot ring per
 * client (sg_local.h:143), with the attacker, the number and the moment. Two
 * questions are asked of it here and both are Beat_HurtSince's question
 * (sg_arach.c:2380) with a different filter on it: "has anything landed since
 * X" and "has anything BIG landed since X".
 *
 * The idiom is copied rather than shared for the reason the ring exists at
 * all: it is the module boundary. Exporting a four-line loop out of sg_arach.c
 * so this file could call it would put a private beat helper in a public header
 * and buy nothing.
 *
 * What the ring does not hold is as load-bearing as what it does. SG_NoteDamage
 * refuses a hit with no client attacker, refuses a teammate's splash, and
 * refuses the world -- so a fall, the lava and a crusher are all invisible
 * here. That is correct for both readers: "lucky shot" is a thing said about a
 * shooter, and being in contact means somebody is shooting at you.
 */

static qboolean Chat_HurtSince(edict_t *e, float since)
{
	int ci, k;

	if (!e || !e->client)
		return false;
	ci = (int)(e->client - game.clients);
	if (ci < 0 || ci >= SG_DMG_CLIENTS)
		return false;
	for (k = 0; k < SG_DMG_RING; k++)
		if (sg_caco_damage[ci][k].attacker >= 0 &&
		    sg_caco_damage[ci][k].time > since)
			return true;
	return false;
}

/* when the newest hit of at least `dmg` landed, 0.0 for none since `since` */
static float Chat_BigHitSince(edict_t *e, int dmg, float since)
{
	float	best = 0.0f;
	int		ci, k;

	if (!e || !e->client)
		return 0.0f;
	ci = (int)(e->client - game.clients);
	if (ci < 0 || ci >= SG_DMG_CLIENTS)
		return 0.0f;
	for (k = 0; k < SG_DMG_RING; k++)
	{
		sg_damage_hit_t *h = &sg_caco_damage[ci][k];

		if (h->attacker < 0 || h->damage < dmg)
			continue;
		if (h->time <= since || h->time <= best)
			continue;
		best = h->time;
	}
	return best;
}

/*
 * Busy hands. A bot carrying a flag is running, a bot that has been hit inside
 * the window is fighting, and a bot with a fresh enemy sighting on its own
 * side's books is about to be. None of the three stops to type.
 *
 * The carry test is EF_FLAG1/EF_FLAG2 on the player entity, which is how
 * sg_caco.c finds carriers (Caco_ScanCarriers) and how the HUD draws them.
 */
static qboolean Chat_Busy(edict_t *e)
{
	if (!e || !e->client)
		return true;
	if (e->s.effects & (EF_FLAG1 | EF_FLAG2))
		return true;
	if (e->pain_debounce_time > level.time)
		return true;
	if (Chat_HurtSince(e, level.time - SG_CHAT_REPLY_CALM))
		return true;
	return Chat_EnemySeenNear(e->client->ctf.teamnum, e->s.origin);
}

/* ------------------------------------------------------- item callouts */

/*
 * A believed transition on one of CACO's items, from the bot that saw it.
 * `index` indexes sg_caco_items; sg_caco.c calls this from Caco_ScanItems
 * at the two points where believed_up changes under a sighting.
 */
void SG_ChatItemSeen(edict_t *viewer, int index, qboolean up)
{
	sg_belief_item_t	*b;
	sg_chat_item_t		*c;
	edict_t				*e;
	const char			*name;
	char				line[SG_CHAT_LINE], place[48];
	int					team, ti;

	if (!Chat_OurBot(viewer) || !Chat_Playing(viewer))
		return;
	if (index < 0 || index >= sg_caco_num_items)
		return;

	team = viewer->client->ctf.teamnum;
	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return;
	ti = SG_TeamIdx(team);

	b = &sg_caco_items[ti][index];      /* this viewer's team's row */
	c = &chat_item[index];
	e = g_edicts + b->ent;
	name = Chat_ItemName(e);
	if (!name)
		return;

	if (up)
	{
		if (c->up[ti])
			return;                     /* the team already believed it */
		c->up[ti] = true;
		c->back_at[ti] = 0.0f;
		c->soon_said[ti] = false;

		/*
		 * A powerup's pad is map knowledge, so "quad is up" is the whole
		 * message. A rune's is not -- it moves itself every 30 seconds
		 * (g_runes.c:10) -- so where it is IS the news.
		 */
		if (b->cls == SG_BI_RUNE)
		{
			Chat_LocNameFor(b->org, team, place, sizeof(place));
			Com_sprintf(line, sizeof(line), "%s at %s", name, place);
		}
		else
			Com_sprintf(line, sizeof(line), "%s is up", name);

		Chat_Queue(viewer, team, SG_CHAT_TOPIC_ITEM_UP, line);
		return;
	}

	if (!c->up[ti])
		return;                         /* the team already believed it gone */
	c->up[ti] = false;
	c->soon_said[ti] = false;

		/* An empty pad proves absence, not when the item was taken. */
	if (!SG_ItemComm())
		c->back_at[ti] = (b->respawn_delay > 0.0f)
		               ? level.time + b->respawn_delay
		               : 0.0f;

	if (Chat_EnemySeenNear(team, b->org))
		Com_sprintf(line, sizeof(line), "enemy took %s", name);
	else
		Com_sprintf(line, sizeof(line), "%s is gone", name);

	Chat_Queue(viewer, team, SG_CHAT_TOPIC_ITEM_GONE, line);
}

/*
 * The items CACO does not track. Same visibility test it uses: line from the
 * viewer's eyes to the target's centre, PVS first (sg_caco.c Caco_Visible).
 */
static qboolean Chat_Visible(edict_t *viewer, edict_t *target)
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

static void Chat_ScanWatched(void)
{
	static const struct { const char *cls; float respawn; } want[] = {
		/* g_items.c:759 -- Pickup_Armor calls SetRespawn(ent, 20) */
		{ "item_armor_body",    20.0f },
		/* g_items.c:912 -- Pickup_PowerArmor uses ent->item->quantity */
		{ "item_power_shield",  -1.0f },
		{ "item_power_screen",  -1.0f },
			/* Mega has no take-time countdown; death or a later sighting starts it. */
		{ "item_health_mega",    0.0f },
		{ NULL, 0.0f }
	};
	int i;

	chat_num_watch = 0;

	for (i = 0; i < globals.num_edicts && chat_num_watch < SG_CHAT_MAX_WATCH;
	     i++)
	{
		edict_t	*e = &g_edicts[i];
		int		k;

		if (!e->inuse || !e->classname)
			continue;
		if (e->spawnflags & DROPPED_ITEM)
			continue;

		for (k = 0; want[k].cls; k++)
		{
			sg_chat_watch_t *w;

			if (strcmp(e->classname, want[k].cls) != 0)
				continue;
			if (!SG_ItemComm() &&
			    strcmp(want[k].cls, "item_health_mega") == 0)
				continue;

			w = &chat_watch[chat_num_watch++];
			w->ent = i;
			w->name = Chat_ItemName(e);
			w->respawn = want[k].respawn;
			if (w->respawn < 0.0f)
				w->respawn = (e->item && e->item->quantity > 0)
				           ? (float)e->item->quantity : 60.0f;
			/* what a player assumes walking in, same as the quad pad */
			w->up[0] = w->up[1] = true;
			w->back_at[0] = w->back_at[1] = 0.0f;
			w->soon_said[0] = w->soon_said[1] = false;
			break;
		}
	}
}

void SG_ChatSee(edict_t *viewer)
{
	char	line[SG_CHAT_LINE];
	int		i, team, ti;

	if (!Chat_OurBot(viewer) || !Chat_Playing(viewer))
		return;
	team = viewer->client->ctf.teamnum;
	ti = SG_TeamIdx(team);

	for (i = 0; i < chat_num_watch; i++)
	{
		sg_chat_watch_t	*w = &chat_watch[i];
		edict_t			*e = g_edicts + w->ent;

		if (!e->inuse || !w->name)
			continue;
		if (!Chat_Visible(viewer, e))
			continue;

		if (e->solid != SOLID_NOT)
		{
			if (w->up[ti])
				continue;
			w->up[ti] = true;
			w->back_at[ti] = 0.0f;
			w->soon_said[ti] = false;
			Com_sprintf(line, sizeof(line), "%s is up", w->name);
			Chat_Queue(viewer, team, SG_CHAT_TOPIC_ITEM_UP, line);
		}
		else
		{
			if (!w->up[ti])
				continue;
			w->up[ti] = false;
			w->soon_said[ti] = false;

			/* Seeing armor gone does not reveal its take time. */
			if (!SG_ItemComm())
				w->back_at[ti] = level.time + w->respawn;

			if (Chat_EnemySeenNear(team, e->s.origin))
				Com_sprintf(line, sizeof(line), "enemy took %s", w->name);
			else
				Com_sprintf(line, sizeof(line), "%s is gone", w->name);
			Chat_Queue(viewer, team, SG_CHAT_TOPIC_ITEM_GONE, line);
		}
	}
}

/* ------------------------------------------------------------- the radio
 * Radio mirrors an emitted team call after a human-scale delay. It adds no
 * knowledge and uses the normal spam limits. */

#define SG_RADIO_LAG_MIN	1.0f
#define SG_RADIO_LAG_MAX	3.0f
#define SG_RADIO_SOUND		24

static qboolean SG_Radio(void)
{
	return (sg_cv.radio->value > 0.0f) ? true : false;
}

/*
 * One call in the air per team. A radio is a channel and not a mailbox: a
 * second call queued while the first is still under somebody's finger is the
 * same news arriving twice, and the item topic's own cooldown has already
 * decided that the team hears this once.
 */
typedef struct
{
	qboolean	pending;
	int			speaker;                /* client number */
	unsigned long	speaker_ctfid;         /* exact bot generation */
	float		due;
	char		sound[SG_RADIO_SOUND];
} sg_radioq_t;

static sg_radioq_t	radio_q[2];         /* per team-1 */

/*
 * THE QUAD-30 CALL.
 *
 * "when the enemy's quad has just worn off and respawn is ~30s out" is a call
 * a human makes off nothing but a clock he started himself, and it is the
 * second half of timing quad: the first half tells the team when to leave, the
 * second tells them the danger is over and the pad is worth walking to.
 *
 * The thirty seconds is READ, not assumed. Use_Quad (g_items.c:367-389) sets
 * quad_framenum to level.framenum + 300 and FRAMETIME is 0.1 (g_local.h:141),
 * so a quad taken off its pad burns for thirty seconds while the pad's own
 * respawn runs sixty (LM_QUAD_DEFAULT_TIME, the item's quantity). Wear-off is
 * therefore the armed clock minus thirty, which is also exactly halfway -- and
 * saying so in arithmetic rather than writing 30.0f twice is what keeps the
 * two halves from drifting apart if the mod ever retunes one of them.
 *
 * The one modelling assumption is the one a player makes out loud: that the
 * quad went live at the pickup. Without DF_INSTANT_ITEMS a taker can sit on it
 * (Pickup_Powerup, g_items.c:195-205), and a human calling "quad's dead" at
 * thirty is wrong in exactly the same way and for exactly the same reason.
 */
#define SG_QUAD_LIVE	(300.0f * FRAMETIME)

/*
 * EITHER 60 OR 30, never both (owner's correction, 2026-08-05, his words:
 * "quad 30 when they hear an enemy quad sound dying off and there was no
 * other quad callout, or quad 60 when they take the quad themselves or see
 * the quad taken"). A called take at 60 SILENCES the 30 for that cycle;
 * the 30 exists for the quad nobody called -- its trigger is the EAR
 * (sg_caco_quadheard) noticing the enemy quad's voice has died away.
 */
static struct
{
	float		called_until;           /* a 60-call covered this cycle */
	float		quiet_fired_at;         /* last ear-driven 30-call, anti-repeat */
} radio_q30[2];

/*
 * Which takes are worth a radio call, and what the paks call the sound. The
 * table is short because the radio is short: LMCTF ships a fixed set of
 * recordings and there is no "_ra20" to play for red armour however much a
 * team would like one.
 *
 * _equad is in the paks and is deliberately unused. It names the threat
 * ("enemy has quad") and drops the number, and the number is what the team
 * cannot work out for itself -- the witness's text line already says who took
 * it. One call per take is the whole budget; it is spent on the clock.
 */
static const struct { const char *cls; const char *sound; } radio_take[] = {
	/* the sixty in the name is the pad's own respawn, not a round number:
	 * Pickup_Powerup sets it from the item's quantity and quad's quantity is
	 * LM_QUAD_DEFAULT_TIME = 60 (g_items.c:2253) */
	{ "item_quad",              "_quad60" },
	/* likewise Pickup_PowerArmor off quantity, which the power shield gives
	 * as 60 (g_items.c:1823). The watch list already keeps its clock, so the
	 * take arms one and the call has something true to say */
	{ "item_power_shield",      "_ps60" },
	{ NULL, NULL }
};

/*
 * ctf_SpamCheck's three refusal tests, read rather than tripped. The referee
 * exemption is carried across too: the check grants it and this must not be
 * stricter than the thing it is predicting.
 */
static qboolean Chat_RadioSpamClear(edict_t *e)
{
	if (e->client->ctf.extra_flags & CTF_EXTRAFLAGS_REFEREE)
		return true;
	if (e->client->spam_band_count <= 0)
		return false;
	if (e->client->spam_freq_count > CTF_SPAM_FREQ_MAX_ALLOWED)
		return false;
	if (level.time - e->client->spam_lock_time < CTF_SPAM_LOCKOUT_TIME)
		return false;
	return true;
}

static void Chat_RadioQueue(edict_t *speaker, int team, const char *sound)
{
	sg_radioq_t *q;
	int			cl;

	if (!SG_Radio() || !sound || !sound[0])
		return;
	if (!Chat_OurBot(speaker) || !Chat_Playing(speaker))
		return;
	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return;
	cl = Chat_ClientNum(speaker);
	if (cl < 0 || cl >= game.maxclients)
		return;

	q = &radio_q[SG_TeamIdx(team)];
	if (q->pending)
		return;                     /* one hand on the key at a time */

	q->speaker = cl;
	q->speaker_ctfid = speaker->client->ctf.ctfid;
	q->due = level.time + SG_RADIO_LAG_MIN +
	         Chat_RandomUnit(speaker) *
	         (SG_RADIO_LAG_MAX - SG_RADIO_LAG_MIN);
	Chat_Copy(q->sound, sound, sizeof(q->sound));
	q->pending = true;
}

static void Chat_RadioSay(int t)
{
	sg_radioq_t	*q = &radio_q[t];
	edict_t		*sp;
	char		sound[SG_RADIO_SOUND];

	if (!q->pending || level.time < q->due)
		return;

	/* off the slot before anything can refuse it: a call the spam gate eats
	 * must not sit in the queue jamming the next one */
	q->pending = false;

	if (q->speaker < 0 || q->speaker >= game.maxclients)
		return;
	sp = g_edicts + 1 + q->speaker;
	if (!Chat_OurBot(sp) || !Chat_Playing(sp))
		return;                     /* died or left while the hand was moving */
	if (!SG_CalloutSpeakerCurrent(q->speaker_ctfid,
	    sp->client->ctf.ctfid))
		return;
	if (sp->client->ctf.teamnum != SG_TeamFromIdx(t))
		return;                     /* switched sides: not his team's news */
	if (!Chat_RadioSpamClear(sp))
		return;                     /* humans get spam-limited too */

	/* PlayTeamSound takes a plain char *, and it is the mod's own signature */
	Chat_Copy(sound, q->sound, sizeof(sound));
	PlayTeamSound(sp, sound);
}

/*
 * The take call and, for the quad, the wear-off call it schedules. Called from
 * Chat_ArmClock's SAID path and nowhere else -- a callout the channel swallowed
 * taught the team nothing and has nothing to be in conjunction with.
 */
static void Chat_RadioTaken(int ti, const sg_chatq_t *q)
{
	edict_t	*item, *sp;
	int		i;

	if (!SG_Radio())
		return;
	if (q->arm_ent <= 0 || q->arm_ent >= globals.num_edicts)
		return;
	if (q->speaker < 0 || q->speaker >= game.maxclients)
		return;

	item = g_edicts + q->arm_ent;
	sp = g_edicts + 1 + q->speaker;
	if (!item->inuse || !item->classname)
		return;

	for (i = 0; radio_take[i].cls; i++)
	{
		if (strcmp(item->classname, radio_take[i].cls) != 0)
			continue;
		Chat_RadioQueue(sp, ti + 1, radio_take[i].sound);
		break;
	}

	/*
	 * Arm the wear-off call on the same clock the line just earned. Re-arming
	 * is how "no fresher quad callout" is enforced: a later take overwrites
	 * this record with its own, and the fire test below only speaks for the
	 * clock the team is actually holding.
	 */
	if (q->arm_kind == SG_ARM_ITEM &&
	    strcmp(item->classname, "item_quad") == 0 &&
	    q->arm_back_at > level.time)
		radio_q30[ti].called_until = q->arm_back_at;
}

static void Chat_RadioFrame(void)
{
	int t;

	if (!SG_Radio())
	{
		/* the cvar going off mid-match must not leave a call in the air or a
		 * wear-off armed against a clock nobody is watching any more */
		memset(radio_q, 0, sizeof(radio_q));
		memset(radio_q30, 0, sizeof(radio_q30));
		return;
	}

	for (t = 0; t < 2; t++)
	{
		/*
		 * The uncalled quad. The ear stamped the FADE WARNING -- the
		 * sound the quad plays in its own last three seconds (owner's
		 * correction: "the sound it makes when it is turning off...
		 * that's when you know you can call quad 30"). Hear it, and the
		 * arithmetic is exact: 3s of quad left, 30 to respawn behind it.
		 * If nobody called the take -- no 60 covering this cycle -- one
		 * owner calls the 30 within a human beat of the warning.
		 */
		float heard = sg_caco_quadheard[t];

		if (SG_ItemComm() && heard > 0.0f &&
		    level.time - heard > 0.5f && level.time - heard < 4.0f &&
		    level.time > radio_q30[t].quiet_fired_at + 20.0f &&
		    level.time > radio_q30[t].called_until)
		{
			edict_t *sp = Chat_Speaker(SG_TeamFromIdx(t));

			radio_q30[t].quiet_fired_at = level.time;
			if (sp)
			{
				char line[32];

				Com_sprintf(line, sizeof(line), "quad 30");
				/* the 30-call IS the callout for this cycle: it arms the
				 * team clock through the same spoken-line law as a take
				 * call, at wear-off + the pad's respawn, +/- the slop a
				 * human ear carries */
				/* the fade fires at 3.0s remaining, so the pad is back
				 * at heard + 3 + 30; the half-second of jitter is the
				 * hand, not the ear */
				Chat_QueueArm(sp, SG_TeamFromIdx(t), SG_CHAT_TOPIC_MAJOR, line,
				              SG_ARM_QUIET, -1, 0, SG_ITEMCALL_MATE,
				              heard + 33.0f +
				              ((float)Chat_RandomBounded(sp, 10) /
				               10.0f - 0.5f), -1);
				Chat_RadioQueue(sp, SG_TeamFromIdx(t), "_quad30");
			}
		}

		Chat_RadioSay(t);
	}
}

/* ------------------------------------------------------ the taken callout
 * A taker may report its held item. Other pickup reports require a witness.
 * The emitted line teaches the speaker's team the item's real respawn time. */

static const struct { const char *cls; float respawn; } chat_major[] = {
	/* g_items.c:198 -- Pickup_Powerup: SetRespawn(ent, ent->item->quantity).
	 * Quad's quantity is LM_QUAD_DEFAULT_TIME = 60 and invuln's is 300, so
	 * the -1 here means "ask the item", not "assume a minute". */
	{ "item_quad",              -1.0f },
	{ "item_invulnerability",   -1.0f },
	/* g_items.c:759 -- Pickup_Armor's 20 is written into the game code rather
	 * than into the item, so it is transcribed rather than read */
	{ "item_armor_body",        20.0f },
	/* g_items.c:912 -- Pickup_PowerArmor: SetRespawn(ent, ent->item->quantity) */
	{ "item_power_shield",      -1.0f },
	{ "item_power_screen",      -1.0f },
	/*
	 * The runes are majors worth CALLING and are never worth TIMING -- a zero
	 * here, permanently. Pickup_Rune schedules no respawn at all (g_runes.c:
	 * 450-460): the rune stays in the carrier's hands until he dies or drops
	 * it, and the loose rune relocates itself to a random health spot every 30
	 * seconds (Rune_Think, g_runes.c:334-352). "runes ... spawn randomly", per
	 * the ruling; a rune countdown would be a number invented on the spot.
	 */
	{ "damage_rune",             0.0f },
	{ "haste_rune",              0.0f },
	{ "resist_rune",             0.0f },
	{ "regen_rune",              0.0f },
	{ "vampire_rune",            0.0f },
		/* Mega respawn begins after its bonus decays, so take time cannot time it. */
	{ "item_health_mega",        0.0f },
	{ NULL, 0.0f }
};

/* seconds until it is back, 0 when no clock exists for this item at all */
static float Chat_MajorRespawn(edict_t *e)
{
	int i;

	if (!e || !e->classname)
		return 0.0f;
	for (i = 0; chat_major[i].cls; i++)
	{
		if (strcmp(e->classname, chat_major[i].cls) != 0)
			continue;
		if (chat_major[i].respawn >= 0.0f)
			return chat_major[i].respawn;
		return (e->item && e->item->quantity > 0)
		     ? (float)e->item->quantity : 0.0f;
	}
	return 0.0f;
}

/* is this one of the things a bot bothers to open its mouth about */
qboolean SG_ChatItemMajor(edict_t *e)
{
	int i;

	if (!e || !e->classname || !Chat_ItemName(e))
		return false;
	for (i = 0; chat_major[i].cls; i++)
		if (strcmp(e->classname, chat_major[i].cls) == 0)
			return true;
	return false;
}

/*
 * The speaker discipline, exported for sg_caco.c's witness pick: the same
 * question Chat_Speaker asks of a candidate, so a witnessing bot is chosen the
 * way every other single-owner callout chooses its voice.
 */
qboolean SG_ChatBudgetClear(edict_t *bot)
{
	int cl;

	if (!Chat_OurBot(bot) || !Chat_Playing(bot))
		return false;
	cl = Chat_ClientNum(bot);
	if (cl < 0 || cl >= game.maxclients)
		return false;
	return (level.time >= chat_bot[cl].next_team) ? true : false;
}

/* which sg_caco_items slot / chat_watch slot this entity is, -1 for neither */
static int Chat_BeliefSlot(edict_t *e)
{
	int i, num = (int)(e - g_edicts);

	for (i = 0; i < sg_caco_num_items; i++)
		if (sg_caco_items[0][i].ent == num)
			return i;
	return -1;
}

static int Chat_WatchSlot(edict_t *e)
{
	int i, num = (int)(e - g_edicts);

	for (i = 0; i < chat_num_watch; i++)
		if (chat_watch[i].ent == num)
			return i;
	return -1;
}

static const char *chat_arm_src[] = {
	"taker-call", "mate-call", "witness-call"
};

/*
 * The moment the ruling turns on: a queued call has just been handed to
 * SG_ChatSayTeam and either went out or did not. Only "did" arms a clock.
 */
static void Chat_ArmClock(int ti, const sg_chatq_t *q, qboolean said)
{
	qboolean	dbg = (sg_cv.debug->value > 0.0f)
	                ? true : false;
	const char	*what, *src;

	if (q->arm_kind == SG_ARM_NONE)
		return;

	what = Chat_ItemName(g_edicts + q->arm_ent);
	if (!what)
		what = "item";
	src = (q->arm_src >= 0 && q->arm_src < 3) ? chat_arm_src[q->arm_src]
	                                          : "call";

	if (!said)
	{
		/* the line lost to the per-bot budget or the topic cooldown, so the
		 * team was never told and does not get to count */
		if (dbg)
			sg_host.dprint("SG itemcomm: %s for %s, team %d SUPPRESSED "
			           "(line eaten) -- no clock armed\n",
			           src, what, ti + 1);
		return;
	}

	/*
	 * ONE VOICE PER QUAD CYCLE PER TEAM (owner, 2026-08-05: "we don't
	 * like when multiple people on the team call the same quad timer...
	 * it creates confusion"). ANY armed quad clock -- take-call, fade
	 * call, a parsed human line -- marks the cycle covered, and every
	 * other would-be caller (the fade watcher, the countdown reminder)
	 * checks this mark and stays quiet. Set here, not in the radio path,
	 * so a text-only server gets the same discipline.
	 */
	{
		const char *acls = NULL;

		if (q->arm_kind == SG_ARM_ITEM &&
		    sg_caco_items[ti][q->arm_slot].ent > 0)
			acls = g_edicts[sg_caco_items[ti][q->arm_slot].ent].classname;
		if (q->arm_kind == SG_ARM_QUIET || (acls &&
		    strcmp(acls, "item_quad") == 0))
			radio_q30[ti].called_until = q->arm_back_at;
	}

	if (q->arm_kind == SG_ARM_MEGATAKE)
	{
		/* no clock -- the record IS the arm; see chat_mega_taker */
		chat_mega_taker[ti] = q->arm_who;
		if (sg_cv.debug->value)
			sg_host.dprint("SG itemcomm: mega taker recorded for team %d "
			           "(client %d) -- clock waits on the obituary\n",
			           ti + 1, q->arm_who);
		Chat_RadioTaken(ti, q);
		return;
	}

	if (q->arm_kind == SG_ARM_QUIET)
	{
		/* the ear-driven "quad 30": no slot rode the queue, so find the
		 * quad row now. One pad per map in practice; the first match is
		 * the match. */
		int i;

		for (i = 0; i < SG_MAX_BELIEF_ITEMS; i++)
			if (sg_caco_items[ti][i].ent > 0 &&
			    g_edicts[sg_caco_items[ti][i].ent].classname &&
			    strcmp(g_edicts[sg_caco_items[ti][i].ent].classname,
			           "item_quad") == 0)
			{
				chat_item[i].back_at[ti] = q->arm_back_at;
				chat_item[i].soon_said[ti] = false;
				sg_caco_items[ti][i].believed_respawn_time = q->arm_back_at;
				break;
			}
	}
	else if (q->arm_kind == SG_ARM_ITEM)
	{
		chat_item[q->arm_slot].back_at[ti] = q->arm_back_at;
		chat_item[q->arm_slot].soon_said[ti] = false;
		sg_caco_items[ti][q->arm_slot].believed_respawn_time = q->arm_back_at;
	}
	else
	{
		chat_watch[q->arm_slot].back_at[ti] = q->arm_back_at;
		chat_watch[q->arm_slot].soon_said[ti] = false;
	}

	/*
	 * IN CONJUNCTION (owner, 2026-08-05). The radio call belongs to the same
	 * instant as the clock: the line was spoken, the team believes the number,
	 * and the noise that goes with it is queued behind a human's hand time.
	 * Inert with sg_radio 0.
	 */
	Chat_RadioTaken(ti, q);

	if (dbg)
		sg_host.dprint("SG itemcomm: %s armed %s for team %d -- back at %.1f "
		           "(in %.0fs)\n", src, what, ti + 1, q->arm_back_at,
		           q->arm_back_at - level.time);
}

/*
 * One team's reaction to a major changing hands. `speaker` is the single bot
 * that gets to say it -- sg_caco.c picks the taker itself for (a) and one
 * witness for (b) and (c) -- and `team` is the team being told, which for a
 * witness call is the WITNESS's team and never the taker's.
 */
void SG_ChatItemTaken(edict_t *speaker, int team, edict_t *item, int src,
                      edict_t *taker)
{
	char		line[SG_CHAT_LINE], place[48];
	const char	*name;
	float		respawn, back_at;
	int			ti, bslot, wslot, kind, slot;
	qboolean	located;

	if (!Chat_OurBot(speaker) || !Chat_Playing(speaker))
		return;
	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return;
	if (!item || !item->inuse)
		return;
	name = Chat_ItemName(item);
	if (!name)
		return;
	ti = SG_TeamIdx(team);

	bslot = Chat_BeliefSlot(item);
	wslot = Chat_WatchSlot(item);

	/*
	 * What this team knows WITHOUT being told, because its own bot either has
	 * the thing in its hands or watched it go: the pad is empty. That is a
	 * sighting and it lands whether or not the line survives the channel --
	 * "seeing is knowing" is the half of the ruling that did not change.
	 */
	if (bslot >= 0)
	{
		chat_item[bslot].up[ti] = false;
		chat_item[bslot].soon_said[ti] = false;
		sg_caco_items[ti][bslot].believed_up = false;
		if (sg_caco_items[ti][bslot].cls == SG_BI_RUNE)
			sg_caco_items[ti][bslot].seed = -1;     /* in somebody's hands */
	}
	if (wslot >= 0)
	{
		chat_watch[wslot].up[ti] = false;
		chat_watch[wslot].soon_said[ti] = false;
	}

	/* the clock the line will earn if it is spoken, and nothing for a rune */
	respawn = Chat_MajorRespawn(item);
	back_at = (respawn > 0.0f) ? level.time + respawn : 0.0f;

	kind = SG_ARM_NONE;
	slot = 0;
	if (back_at > 0.0f)
	{
		if (bslot >= 0)
		{
			kind = SG_ARM_ITEM;
			slot = bslot;
		}
		else if (wslot >= 0)
		{
			kind = SG_ARM_WATCH;
			slot = wslot;
		}
	}

	/*
	 * The mega's numberless call still carries a payload: WHO has it.
	 * Applied at emission like every clock, because a team that never
	 * heard the call must not know whose obituary matters.
	 */
	if (kind == SG_ARM_NONE && wslot >= 0 && taker && taker->client &&
	    item->classname &&
	    strcmp(item->classname, "item_health_mega") == 0)
	{
		kind = SG_ARM_MEGATAKE;
		slot = wslot;
	}

	/* where it went, named the way a player names it -- and the item's own
	 * landmark left out, so "took quad at the quad" cannot happen */
	located = Chat_LocNameForSkip(item->s.origin, item->s.origin, team,
	                             place, sizeof(place));

	switch (src)
	{
	case SG_ITEMCALL_TAKER:
		if (located)
			Com_sprintf(line, sizeof(line), "took %s at %s", name, place);
		else
			Com_sprintf(line, sizeof(line), "took %s", name);
		break;
	case SG_ITEMCALL_MATE:
		if (located)
			Com_sprintf(line, sizeof(line), "%s taken at %s", name, place);
		else
			Com_sprintf(line, sizeof(line), "%s taken", name);
		break;
	default:
		if (located)
			Com_sprintf(line, sizeof(line), "enemy took %s at %s", name, place);
		else
			Com_sprintf(line, sizeof(line), "enemy took %s", name);
		break;
	}

	if (!Chat_QueueArm(speaker, team, SG_CHAT_TOPIC_MAJOR, line,
	                   kind, slot, (int)(item - g_edicts), src, back_at,
	                   (kind == SG_ARM_MEGATAKE && taker && taker->client)
	                       ? (int)(taker->client - game.clients) : -1) &&
	    kind != SG_ARM_NONE &&
	    sg_cv.debug->value > 0.0f)
		sg_host.dprint("SG itemcomm: %s for %s, team %d SUPPRESSED "
		           "(topic busy) -- no clock armed\n",
		           chat_arm_src[(src >= 0 && src < 3) ? src : 0], name, team);
}

/* A known mega taker's death starts the approximate 21-second return clock. */
void SG_ChatMegaDeath(edict_t *victim)
{
	int ci, ti, i;

	if (!SG_ItemComm() || !victim || !victim->client)
		return;
	ci = (int)(victim->client - game.clients);

	for (ti = 0; ti < 2; ti++)
	{
		if (chat_mega_taker[ti] != ci)
			continue;
		chat_mega_taker[ti] = -1;

		for (i = 0; i < chat_num_watch; i++)
		{
			edict_t *we = &g_edicts[chat_watch[i].ent];

			if (!we->inuse || !we->classname ||
			    strcmp(we->classname, "item_health_mega") != 0)
				continue;
			chat_watch[i].back_at[ti] = level.time + 21.0f +
			    (float)Chat_RandomBounded(victim, 20) / 10.0f;
			chat_watch[i].soon_said[ti] = false;
			if (sg_cv.debug->value)
				sg_host.dprint("SG itemcomm: mega taker died -- team %d "
				           "clock armed, back at %.1f\n",
				           ti + 1, chat_watch[i].back_at[ti]);
			break;
		}
	}
}

/*
 * Which pads get the short form. Deliberately not "everything with a clock":
 * a timer call is a call on the things a match turns on, and "sg 12" is
 * noise. The classes here are the ones the belief tables already keep a
 * respawn clock for -- quad and invuln come off sg_caco_items, red armour off
 * this file's own watch list -- plus the mega, which carries no clock in this
 * build and so simply never matches. It is written down anyway: the day mega
 * joins the watch list the callout is already spelled, and a reader looking
 * for "why does nobody call mega" finds the answer in one line instead of in
 * the absence of one.
 */
static const struct { const char *cls; const char *shortname; }
chat_timer_major[] = {
	{ "item_quad",              "quad" },
	{ "item_invulnerability",   "invul" },
	{ "item_armor_body",        "ra" },
	{ "item_health_mega",       "mega" },
	{ NULL, NULL }
};

static const char *Chat_TimerShort(edict_t *e)
{
	int i;

	if (!e || !e->classname)
		return NULL;
	for (i = 0; chat_timer_major[i].cls; i++)
		if (strcmp(e->classname, chat_timer_major[i].cls) == 0)
			return chat_timer_major[i].shortname;
	return NULL;
}

/*
 * The same call in each voice's register. Item first in every one of them,
 * because a teammate reading fast reads the first word: the tone lives in
 * what comes after it, not in front of it. "%s" is the pad, "%d" the
 * seconds -- both arguments in that order in all four rows.
 *
 * char * const rather than const char *: Com_sprintf takes a plain char *
 * format, so a const row would need a cast at the one place it is read.
 */
static char * const chat_timer_fmt[SG_TONES] = {
	"%s %d",                /* terse */
	"%s in %d, mine",       /* cocky */
	"%s up soon, ~%d",      /* dry */
	"%s t-%d"               /* mech */
};

/*
 * Emit one countdown for a pad, in whichever form is switched on. The
 * speaker is Chat_Speaker's single owner either way; the topic is what
 * differs, and with it the team cooldown the line has to clear.
 */
static void Chat_SoonSay(int ti, edict_t *item, const char *name,
                         float back_at)
{
	char		line[SG_CHAT_LINE];
	edict_t		*sp = Chat_Speaker(ti + 1);
	const char	*shortname = NULL;
	int			secs;

	if (!sp)
		return;
	secs = (int)(back_at - level.time + 0.5f);

	if (sg_cv.timercall->value > 0.0f)
		shortname = Chat_TimerShort(item);
	if (shortname)
	{
		Com_sprintf(line, sizeof(line), chat_timer_fmt[Chat_Tone(sp)],
		            shortname, secs);
		Chat_Queue(sp, ti + 1, SG_CHAT_TOPIC_TIMER, line);
		return;
	}

	if (!name)
		return;
	Com_sprintf(line, sizeof(line), "%s up in ~%ds", name, secs);
	Chat_Queue(sp, ti + 1, SG_CHAT_TOPIC_ITEM_SOON, line);
}

/*
 * The one thing said off a clock rather than off eyes, and the one thing a
 * player says off a clock too: a respawn is due. SetRespawn's delay comes
 * from ent->item->quantity (g_items.c:198), which is map knowledge; the
 * moment it was taken came from a sighting or from one of ours taking it.
 * Runes carry no clock (Pickup_Rune schedules no respawn, g_runes.c:450-460)
 * so respawn_delay is 0 for them and nothing here fires.
 */
static void Chat_Countdown(void)
{
	int		i, ti;

	for (ti = 0; ti < 2; ti++)
	{
		for (i = 0; i < sg_caco_num_items; i++)
		{
			sg_chat_item_t	*c = &chat_item[i];
			edict_t			*ie = g_edicts + sg_caco_items[0][i].ent;

			if (c->up[ti] || c->soon_said[ti] || c->back_at[ti] <= 0.0f)
				continue;
			if (c->back_at[ti] - level.time > SG_CHAT_SOON)
				continue;

			/* burned before the emit, as it always was: a line the topic
			 * cooldown eats is a line this team does not get told twice
			 * on the same respawn either */
			c->soon_said[ti] = true;
			/* a covered quad cycle already had its one voice; the reminder
			 * would be the second caller the owner banned */
			if (ie->classname && strcmp(ie->classname, "item_quad") == 0 &&
			    radio_q30[ti].called_until >= c->back_at[ti])
				continue;
			Chat_SoonSay(ti, ie, Chat_ItemName(ie), c->back_at[ti]);
		}

		for (i = 0; i < chat_num_watch; i++)
		{
			sg_chat_watch_t	*w = &chat_watch[i];

			if (w->up[ti] || w->soon_said[ti] || w->back_at[ti] <= 0.0f ||
			    !w->name)
				continue;
			if (w->back_at[ti] - level.time > SG_CHAT_SOON)
				continue;

			w->soon_said[ti] = true;
			Chat_SoonSay(ti, g_edicts + w->ent, w->name, w->back_at[ti]);
		}
	}
}

/*
 * A bot that picks something up knows it did -- the exception SLIPGATE.md's
 * belief rule allows, because the thing is in its hands. quad_framenum and
 * invincible_framenum are the powerup timers (g_local.h:1275-1276) and
 * client->rune is the held rune (g_local.h:1304).
 */
static void Chat_SelfPickups(void)
{
	char	line[SG_CHAT_LINE];
	int		i;

	/* The pickup event owns item communication when enabled. */
	if (SG_ItemComm())
		return;

	for (i = 0; i < game.maxclients; i++)
	{
		edict_t			*e = g_edicts + 1 + i;
		sg_chat_bot_t	*cb = &chat_bot[i];
		qboolean		quad, invul;
		int				runeent;
		int				team, k;

		if (!Chat_OurBot(e))
		{
			cb->had_quad = cb->had_invul = false;
			cb->had_rune = 0;
			continue;
		}

		quad = (e->client->quad_framenum > level.framenum) ? true : false;
		invul = (e->client->invincible_framenum > level.framenum)
		      ? true : false;
		runeent = (e->client->rune && e->client->rune->inuse)
		        ? (int)(e->client->rune - g_edicts) : 0;
		team = e->client->ctf.teamnum;

		if (quad && !cb->had_quad && Chat_Playing(e))
		{
			Com_sprintf(line, sizeof(line), "got quad");
			Chat_Queue(e, team, SG_CHAT_TOPIC_ITEM_GONE, line);
		}
		if (invul && !cb->had_invul && Chat_Playing(e))
		{
			Com_sprintf(line, sizeof(line), "got invuln");
			Chat_Queue(e, team, SG_CHAT_TOPIC_ITEM_GONE, line);
		}
		if (runeent && runeent != cb->had_rune && Chat_Playing(e))
		{
			const char *name = Chat_ItemName(g_edicts + runeent);

			if (name)
			{
				Com_sprintf(line, sizeof(line), "got the %s", name);
				Chat_Queue(e, team, SG_CHAT_TOPIC_ITEM_GONE, line);
			}
		}

		/*
		 * Whatever it was, this team now believes the pad is empty and, for
		 * a powerup, knows when it is due back. The belief moved without
		 * anybody looking, which is legitimate exactly here.
		 */
		if ((quad && !cb->had_quad) || (invul && !cb->had_invul) ||
		    (runeent && runeent != cb->had_rune))
		{
			for (k = 0; k < sg_caco_num_items; k++)
			{
				sg_belief_item_t	*b = &sg_caco_items[SG_TeamIdx(team)][k];
				edict_t				*ie = g_edicts + b->ent;
				qboolean			mine = false;

				if (!ie->classname)
					continue;
				if (quad && !cb->had_quad &&
				    strcmp(ie->classname, "item_quad") == 0)
					mine = true;
				if (invul && !cb->had_invul &&
				    strcmp(ie->classname, "item_invulnerability") == 0)
					mine = true;
				if (runeent && runeent != cb->had_rune && b->ent == runeent)
					mine = true;
				if (!mine)
					continue;

				chat_item[k].up[SG_TeamIdx(team)] = false;
				chat_item[k].soon_said[SG_TeamIdx(team)] = false;
				chat_item[k].back_at[SG_TeamIdx(team)] = (b->respawn_delay > 0.0f)
				                               ? level.time + b->respawn_delay
				                               : 0.0f;
			}
		}

		cb->had_quad = quad;
		cb->had_invul = invul;
		cb->had_rune = runeent;
	}
}

/* --------------------------------------------------------- flag carrier */

/*
 * The enemy carrier, seen. sg_caco.c calls this from Caco_ScanCarriers at
 * the point it has decided the sighting is material -- a carrier the team
 * had no fix on, a different player carrying, or the same one a long way
 * from where the team believed he was. The speaker is the bot that saw him.
 */
void SG_ChatCarrierSeen(edict_t *viewer, int team, edict_t *carrier)
{
	char place[48], line[SG_CHAT_LINE];

	if (!Chat_OurBot(viewer) || !Chat_Playing(viewer))
		return;
	if (!carrier || !carrier->inuse || !carrier->client)
		return;
	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return;

	Chat_LocNameFor(carrier->s.origin, team, place, sizeof(place));
	if (Chat_RandomUnit(viewer) < 0.5f)
		Com_sprintf(line, sizeof(line), "enemy fc at %s", place);
	else
		Com_sprintf(line, sizeof(line), "fc spotted at %s", place);

	Chat_Queue(viewer, team, SG_CHAT_TOPIC_CARRIER, line);
}

/* ------------------------------------------------------------- personality */

/*
 * Why the greeting used to be a line nobody ever read.
 *
 * The old version picked a line the first frame the bot was alive on a team
 * and set greeted = true right there, "once, whether or not it lands". Both
 * halves of that were wrong at exactly the same moment.
 *
 * Every chat line in this mod goes through Cmd_Say_f, which asks
 * ctf_SpamCheck first (g_cmds.c:2201). ctf_SpamCheck refuses when
 *
 *     level.time - ent->client->spam_lock_time < CTF_SPAM_LOCKOUT_TIME
 *
 * (g_ctffunc.c:1313). Nothing ever initialises spam_lock_time -- ClientConnect
 * seeds spam_band_count and spam_freq_count and stops there (p_client.c:2523)
 * -- so it is the zero the client struct was memset to. For the first
 * CTF_SPAM_LOCKOUT_TIME (5) seconds of a level, level.time - 0 < 5 is true for
 * every client on the server, and every say and say_team is dropped as "already
 * in penalty box". Bots are added at map load, spawn inside that window, and
 * greet inside it. The say was swallowed, and the failed check then set
 * spam_lock_time = level.time, re-arming the lockout on the way out.
 *
 * The once-flag is what made it permanent. greeted was set from the return-
 * less call, so the one attempt each bot got was spent on a frame where the
 * line could not possibly go out, and it was never retried. Item callouts
 * survived the same bug because they fire repeatedly and well past the five
 * second mark, so they simply land on a later attempt -- which is why the
 * smoke test saw callouts and no greetings.
 *
 * The fix that matters is the schedule: book a time instead of speaking on
 * sight, and put the first attempt past the lockout window, where the say can
 * actually go out. Everything else is belt and braces.
 *
 * Note what Chat_SayEx can and cannot tell us. It returns false for its own
 * refusals -- the public say budget, a bot that died before its turn -- and
 * those are worth retrying. It cannot see spam control's verdict at all:
 * SG_BotClientCommand returns void, so once the line is handed to Cmd_Say_f,
 * it reports true whether ctf_SpamCheck passed it or ate it. So the
 * retry below is not a delivery check, and the greeting's correctness rests on
 * being scheduled late enough rather than on noticing a rejection. The retry
 * gap is still longer than the lockout, because a refused attempt re-arms
 * spam_lock_time -- retrying faster than the lockout would let a bot hold
 * itself in the penalty box with its own retries.
 */
static void Chat_Greetings(void)
{
	int i;

	for (i = 0; i < game.maxclients; i++)
	{
		edict_t			*e = g_edicts + 1 + i;
		sg_chat_bot_t	*cb = &chat_bot[i];
		const char		*line;

		if (!Chat_OurBot(e))
		{
			cb->greeted = false;
			cb->greet_at = 0.0f;
			cb->greet_tries = 0;
			continue;
		}
		if (cb->greeted || !Chat_Playing(e))
			continue;

		/*
		 * First frame this bot is alive and on a team: book a time, do not
		 * speak. The floor is absolute level time, not a delay from now,
		 * because the lockout window is measured from level.time against a
		 * spam_lock_time of zero. A bot added mid-match is already past it
		 * and only waits out the settle.
		 */
		if (cb->greet_at <= 0.0f)
		{
			float settle = level.time + SG_CHAT_GREET_SETTLE
			             + (float)i * SG_CHAT_GREET_STAGGER;
			float unlock = CTF_SPAM_LOCKOUT_TIME + SG_CHAT_GREET_SETTLE
			             + (float)i * SG_CHAT_GREET_STAGGER;

			cb->greet_at = (settle > unlock) ? settle : unlock;
			continue;
		}
		if (level.time < cb->greet_at)
			continue;

		line = Chat_Pick(e, Chat_Tone(e), SG_LINE_JOIN);
		if (!line)
		{
			cb->greeted = true;         /* nothing to say: do not come back */
			continue;
		}

		if (Chat_SayPooled(e, line, 0))
		{
			cb->greeted = true;
			continue;
		}

		/* refused -- by our own say budget or by spam control. Try later. */
		if (++cb->greet_tries >= SG_CHAT_GREET_TRIES)
			cb->greeted = true;
		else
			cb->greet_at = level.time + SG_CHAT_GREET_RETRY;
	}
}

/*
 * The team capture total, summed the way the scoreboard sums it: per player
 * STATS_CAPTURES over the team (p_hud.c:307, p_hud.c:377). bluescore /
 * redscore are NOT this number -- they are summed player score and move on
 * every frag (p_hud.c:306) -- so a celebration hung off them would fire on
 * kills.
 */
static int Chat_Captures(int team)
{
	int	total = 0, i;

	for (i = 0; i < game.maxclients; i++)
	{
		edict_t *e = g_edicts + 1 + i;

		if (!e->inuse || !e->client)
			continue;
		if (e->client->ctf.teamnum != team)
			continue;
		total += (int)stats_get(e, STATS_CAPTURES);
	}
	return total;
}

/*
 * THEY SCORED ON US. The mirror of the capture cheer, and the half of the
 * moment nobody had written: sixteen bots that celebrate their own captures
 * and say nothing at all when the other side scores are sixteen bots that are
 * only watching their own half of the game.
 *
 * It spends the grumble cooldown rather than one of its own, because that is
 * what it is -- the same mouth, the same register, and the same reason for
 * wanting three quarters of a minute between two of them.
 */
static void Chat_Conceded(int team)
{
	edict_t		*sp = Chat_Speaker(team);
	const char	*line;
	int			cl;

	if (!sp)
		return;
	cl = Chat_ClientNum(sp);
	if (cl < 0 || cl >= game.maxclients)
		return;
	if (level.time < chat_bot[cl].next_grumble)
		return;
	if (Chat_RandomUnit(sp) >= SG_CHAT_CONCEDE_ODDS * Chat_Chatty(cl))
		return;

	line = Chat_Pick(sp, Chat_Tone(sp), SG_LINE_CONCEDE);
	if (!line)
		return;                         /* empty pool: nothing to say */
	if (Chat_SayPooled(sp, line, 0))
		chat_bot[cl].next_grumble = level.time + SG_CHAT_GRUMBLE_GAP;
}

/*
 * OUR FLAG IS BACK. Said to the team rather than to the server: a return is
 * information -- it is the difference between "we are defending" and "we are
 * playing" -- where a capture is a scoreboard event everybody already watched.
 *
 * It rides SG_CHAT_TOPIC_CACO, which is the flag lane. sg_caco.c keeps its own
 * queue slots and only shares the topic's team stamp, whose gap is zero, so
 * nothing here can eat one of its flag callouts or be eaten by one; the
 * per-bot say_team budget is the limit that actually applies.
 */
static void Chat_Returned(int team)
{
	edict_t		*sp = Chat_Speaker(team);
	const char	*line;
	int			cl;

	if (!sp)
		return;
	cl = Chat_ClientNum(sp);
	if (cl < 0 || cl >= game.maxclients)
		return;
	if (Chat_RandomUnit(sp) >= SG_CHAT_RETURN_ODDS * Chat_Chatty(cl))
		return;

	line = Chat_Pick(sp, Chat_Tone(sp), SG_LINE_RETURN);
	if (!line)
		return;
	/* burned at queue time for the reason the steal callout is: Chat_Queue
	 * copies the text into its slot and the pool pointer is gone by the time
	 * Chat_Flush speaks it */
	Chat_Queue(sp, team, SG_CHAT_TOPIC_CACO, line);
	Chat_Note(line);
}

/*
 * Captures and who holds a flag are on everyone's screen -- the scoreboard
 * and the HUD carrier icon -- so celebrating either leaks nothing. A steal
 * goes to the team as information; a capture goes public, because everybody
 * watched it happen anyway.
 *
 * The stored total is overwritten every frame, not only when it rises: a
 * capturer who disconnects takes his captures off the board with him, and a
 * counter that only ever climbed would fire again on the next one.
 *
 * TWO PASSES, and the split is load-bearing. A capture puts the conceding
 * side's flag back on its stand in the same frame it moves the score, and the
 * return pass has to be able to see that the concession happened -- to BOTH
 * sides -- before it decides whether a flag coming home is worth cheering.
 * One pass would let blue's capture stamp red's concession after red's flag
 * had already been examined, and red would cheer a return it did not earn.
 */
static void Chat_TeamEvents(void)
{
	int	score[2];
	int	t;

	score[0] = Chat_Captures(CTF_TEAM_RED);
	score[1] = Chat_Captures(CTF_TEAM_BLUE);

	for (t = 0; t < 2; t++)
	{
		edict_t		*sp;
		const char	*line;
		int			carrier;

		if (score[t] > chat_lastscore[t])
		{
			sp = Chat_Speaker(SG_TeamFromIdx(t));
			line = sp ? Chat_Pick(sp, Chat_Tone(sp), SG_LINE_CAP) : NULL;
			if (sp && line)
				Chat_SayPooled(sp, line, 0);

			/* and the other side has a word for it too */
			chat_conceded_at[1 - t] = level.time;
			Chat_Conceded(2 - t);
		}
		chat_lastscore[t] = score[t];

		carrier = sg_caco_team_belief.carrier[t].client;
		if (carrier >= 0 && chat_lastcarrier[t] < 0)
		{
			sp = Chat_Speaker(SG_TeamFromIdx(t));
			line = sp ? Chat_Pick(sp, Chat_Tone(sp), SG_LINE_STEAL) : NULL;
			if (sp && line)
			{
				/*
				 * Burned at queue time, not on delivery: Chat_Queue copies
				 * the text into its slot and the pool pointer is gone by the
				 * time Chat_Flush speaks it. A queued line is committed
				 * anyway -- the slot is taken and no second steal callout
				 * will be queued behind it.
				 */
				Chat_Queue(sp, SG_TeamFromIdx(t), SG_CHAT_TOPIC_STEAL, line);
				Chat_Note(line);
			}
		}
		chat_lastcarrier[t] = carrier;
	}

	/*
	 * The return pass. flag[0][t] is team t+1's OWN flag; home state is
	 * mirrored across both belief rows and is read straight off
	 * ctf_flagathome by Caco_ScanFlags -- HUD knowledge, not a sighting.
	 */
	for (t = 0; t < 2; t++)
	{
		qboolean home = (sg_caco_team_belief.flag[0][t].state == SG_FLAG_HOME)
		              ? true : false;

		if (home && !chat_flaghome[t] &&
		    level.time - chat_conceded_at[t] > SG_CHAT_RETURN_CAPWIN)
			Chat_Returned(SG_TeamFromIdx(t));
		chat_flaghome[t] = home;
	}
}

/*
 * A death nobody else can take credit for. Two shapes, and the mod is only
 * half the answer:
 *
 *   The map killed him. MOD_LAVA, MOD_SLIME, MOD_WATER, MOD_FALLING,
 *   MOD_CRUSH, MOD_TRIGGER_HURT and the console MOD_SUICIDE are the deaths
 *   the game's own obituary phrases without an attacker.
 *
 *   He killed himself with something of his own. The mod then names a weapon
 *   -- his rocket, his grenade, the barrel he shot -- and what identifies it
 *   is the attacker field pointing back at him, or at the world.
 *
 * MOD_TELEFRAG is deliberately absent from the list: somebody else stood on
 * the pad. meansOfDeath arrives with the friendly-fire bit still set on it,
 * so it is masked here the way SG_NoteDamage masks it.
 */
static qboolean Chat_SelfInflicted(edict_t *victim, edict_t *attacker, int mod)
{
	switch (mod & ~MOD_FRIENDLY_FIRE)
	{
	case MOD_SUICIDE:
	case MOD_FALLING:
	case MOD_LAVA:
	case MOD_SLIME:
	case MOD_WATER:
	case MOD_CRUSH:
	case MOD_TRIGGER_HURT:
		return true;
	default:
		break;
	}
	return (!attacker || attacker == victim || attacker == world)
	     ? true : false;
}

/*
 * BYSTANDER. One of theirs has just killed himself and one of OURS watched it
 * happen. Everything about the line is off that watching: the speaker must
 * have had the body in sight, or at least inside its PHS -- a scream carries
 * through a wall, and a player laughs at what he heard as readily as at what
 * he saw -- and exactly one bot on that side gets to say it, because two bots
 * laughing at one death is the tell this whole file is built around avoiding.
 *
 * The first candidate whose taunt cooldown is clear wins, which is the same
 * "prefer a mouth that is free" rule Chat_Speaker applies to callouts. The
 * roll comes after the pick rather than before it, so a server where the only
 * witness happens to be on cooldown stays quiet instead of hunting for a
 * second one.
 */
static void Chat_Bystander(edict_t *victim)
{
	edict_t		*seer = NULL;
	const char	*line;
	int			team, i, cl;

	if (!victim || !victim->client)
		return;
	team = victim->client->ctf.teamnum;
	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return;

	for (i = 0; i < game.maxclients && !seer; i++)
	{
		edict_t	*e = g_edicts + 1 + i;
		vec3_t	eye;

		if (!Chat_OurBot(e) || !Chat_Playing(e))
			continue;
		if (e->client->ctf.teamnum == team)
			continue;               /* his own side does not find it funny */
		if (level.time < chat_bot[i].next_taunt)
			continue;

		VectorCopy(e->s.origin, eye);
		eye[2] += e->viewheight;
		if (!Chat_Visible(e, victim) && !sg_host.in_phs(eye, victim->s.origin))
			continue;               /* neither saw it nor heard it */
		seer = e;
	}
	if (!seer)
		return;

	cl = Chat_ClientNum(seer);
	if (cl < 0 || cl >= game.maxclients)
		return;
	if (Chat_RandomUnit(seer) >= SG_CHAT_SUICIDE_ODDS * Chat_Chatty(cl))
		return;

	line = Chat_Pick(seer, Chat_Tone(seer), SG_LINE_SUICIDE);
	if (!line)
		return;                     /* empty pool: nothing to say */
	if (Chat_SayPooled(seer, line, 0))
		chat_bot[cl].next_taunt = level.time + SG_CHAT_TAUNT_GAP;
}

void SG_ChatDeath(edict_t *victim, edict_t *attacker, int mod)
{
	const char	*line;
	int			cl;

	if (attacker && attacker != victim && Chat_OurBot(attacker) &&
	    Chat_Playing(attacker))
	{
		cl = Chat_ClientNum(attacker);
		if (cl >= 0 && cl < game.maxclients &&
		    level.time >= chat_bot[cl].next_taunt &&
		    Chat_RandomUnit(attacker) < SG_CHAT_TAUNT_ODDS)
		{
			line = Chat_Pick(attacker, Chat_Tone(attacker), SG_LINE_KILL);
			chat_bot[cl].next_taunt = level.time + SG_CHAT_TAUNT_GAP;
			if (line)
				Chat_SayPooled(attacker, line, 0);
		}
		/* a kill is a fight: idle banter stays away from one */
		if (cl >= 0 && cl < game.maxclients)
			chat_bot[cl].combat_at = level.time;
	}

	if (victim && Chat_OurBot(victim))
	{
		cl = Chat_ClientNum(victim);
		if (cl >= 0 && cl < game.maxclients &&
		    level.time >= chat_bot[cl].next_grumble &&
		    Chat_RandomUnit(victim) < SG_CHAT_GRUMBLE_ODDS)
		{
			line = Chat_Pick(victim, Chat_Tone(victim), SG_LINE_DEATH);
			chat_bot[cl].next_grumble = level.time + SG_CHAT_GRUMBLE_GAP;
			/*
			 * By here the victim is a corpse, which Chat_Playing refuses --
			 * hence SG_SAYF_DEAD. The public say budget still applies: a bot
			 * that just taunted does not also get to grumble.
			 */
			if (line)
				Chat_SayPooled(victim, line, SG_SAYF_DEAD);
		}
		if (cl >= 0 && cl < game.maxclients)
			chat_bot[cl].combat_at = level.time;
	}

	/*
	 * And somebody else's mistake, watched. The taunt above is about what a
	 * bot DID; this one is about what it saw, which is why it is off the
	 * victim and the mod rather than off the attacker.
	 */
	if (victim && victim->inuse && victim->client &&
	    Chat_SelfInflicted(victim, attacker, mod))
		Chat_Bystander(victim);
}

/*
 * THE MAP-OPEN LINE. One per bot per level, at roughly a third of them, so a
 * full server opens with five or six voices rather than sixteen or none.
 *
 * Two things about the schedule are load-bearing.
 *
 * It sits behind the bot's OWN greeting plus the public say budget. Both lines
 * go out on the same budgeted channel, and the greeting is booked first
 * (Chat_Greetings runs ahead of this in SG_ChatFrame), so a map-open line
 * booked any earlier is refused by SG_CHAT_SAY_GAP and lost. Scheduling it
 * off greet_at rather than off level.time is what keeps the low slots -- the
 * ones a botfilled team fills first -- from being silenced every map.
 *
 * And the roll is made ONCE, when the line is booked, not when it comes due.
 * Rolling at the due moment would mean rerolling every frame from then on,
 * which is not a 35% chance of speaking, it is a certainty with a delay.
 */
static void Chat_LevelOpen(void)
{
	int i;

	if (level.intermissiontime)
		return;

	for (i = 0; i < game.maxclients; i++)
	{
		edict_t			*e = g_edicts + 1 + i;
		sg_chat_bot_t	*cb = &chat_bot[i];
		const char		*line;

		if (!Chat_OurBot(e))
		{
			cb->opened = false;
			cb->open_at = 0.0f;
			continue;
		}
		if (cb->opened || !Chat_Playing(e))
			continue;

		if (cb->open_at <= 0.0f)
		{
			if (cb->greet_at <= 0.0f)
				continue;               /* not greeted yet: nothing to trail */

			if (Chat_RandomUnit(e) >= SG_CHAT_OPEN_ODDS * Chat_Chatty(i))
			{
				cb->opened = true;      /* this one has nothing to say */
				continue;
			}

			/*
			 * Slot-spaced first, jittered second: the spacing guarantees no
			 * two bots land in the same frame, the jitter keeps the result
			 * from sounding like a roll call.
			 */
			cb->open_at = cb->greet_at + SG_CHAT_SAY_GAP
			            + (float)(i % SG_CHAT_ROSTER)
			              * (SG_CHAT_OPEN_SPAN / (float)SG_CHAT_ROSTER)
			            + Chat_RandomUnit(e) * SG_CHAT_OPEN_JITTER;
			continue;
		}
		if (level.time < cb->open_at)
			continue;

		cb->opened = true;                  /* one attempt, spoken or not */
		line = Chat_Pick(e, Chat_Tone(e), SG_LINE_OPEN);
		if (line)
			Chat_SayPooled(e, line, 0);
	}
}

/*
 * The team's score as the game itself announces it: summed per-player
 * STATS_SCORE, exactly the sum Victory() prints as "Blue: N beats red: M"
 * (g_tourney.c:117-127). Deliberately NOT Chat_Captures -- a bot gloating
 * about a win the scoreboard does not show is worse than a bot that says
 * nothing, and capture totals and score totals disagree often.
 */
static int Chat_TeamScore(int team)
{
	int	total = 0, i;

	for (i = 0; i < game.maxclients; i++)
	{
		edict_t *e = g_edicts + 1 + i;

		if (!e->inuse || !e->client)
			continue;
		if (e->client->ctf.teamnum != team)
			continue;
		total += (int)stats_get(e, STATS_SCORE);
	}
	return total;
}

/*
 * THE MATCH-END LINE, booked from BeginIntermission (p_hud.c) and delivered
 * over the next four seconds by Chat_LevelEndFlush.
 *
 * Booked rather than spoken on the spot for the ordinary reason -- sixteen
 * lines in one frame is a wall, not a conversation -- and the whole stagger is
 * kept inside four seconds because a client may exit intermission five seconds
 * in (p_client.c:2823) and take the rest of the lines with it.
 *
 * A margin inside SG_CHAT_CLOSE_MARGIN either way, ties included, is a close
 * game and both sides say so. Gloating over two points reads as a bot that
 * only knows how to compare two numbers.
 */
void SG_ChatLevelEnd(void)
{
	int	score[2], i;

	score[0] = Chat_TeamScore(CTF_TEAM_RED);
	score[1] = Chat_TeamScore(CTF_TEAM_BLUE);

	for (i = 0; i < game.maxclients; i++)
	{
		edict_t			*e = g_edicts + 1 + i;
		sg_chat_bot_t	*cb = &chat_bot[i];
		int				team, diff;

		if (!Chat_OurBot(e))
			continue;
		team = e->client->ctf.teamnum;
		if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
			continue;
		if (cb->end_cat >= 0)
			continue;               /* already booked, and once is enough */
		if (Chat_RandomUnit(e) >= SG_CHAT_END_ODDS * Chat_Chatty(i))
			continue;

		diff = score[SG_TeamIdx(team)] - score[SG_TeamIdx(SG_EnemyTeam(team))];
		if (diff > SG_CHAT_CLOSE_MARGIN)
			cb->end_cat = SG_LINE_WIN;
		else if (diff < -SG_CHAT_CLOSE_MARGIN)
			cb->end_cat = SG_LINE_LOSE;
		else
			cb->end_cat = SG_LINE_CLOSE;

		cb->end_at = level.time
		           + (float)(i % SG_CHAT_ROSTER) * SG_CHAT_END_STAGGER
		           + Chat_RandomUnit(e) * SG_CHAT_END_JITTER;
	}
}

static void Chat_LevelEndFlush(void)
{
	int i;

	for (i = 0; i < game.maxclients; i++)
	{
		edict_t			*e = g_edicts + 1 + i;
		sg_chat_bot_t	*cb = &chat_bot[i];
		const char		*line;
		int				cat;

		/*
		 * end_at > 0 is belt and braces, not bookkeeping: it makes an
		 * all-zero chat_bot -- the state before SG_ChatReset has ever run --
		 * read as "nothing booked" rather than as category zero, which is
		 * SG_LINE_JOIN, due at time zero, for every slot at once.
		 */
		if (cb->end_cat < 0 || cb->end_at <= 0.0f || level.time < cb->end_at)
			continue;

		cat = cb->end_cat;
		cb->end_cat = -1;               /* consumed either way */
		cb->end_at = 0.0f;

		if (!Chat_OurBot(e))
			continue;

		line = Chat_Pick(e, Chat_Tone(e), cat);
		if (line)
		{
			/*
			 * Frozen at the intermission point, possibly a corpse, and past
			 * caring about the say budget: this is the last thing this bot
			 * says on this level.
			 */
			Chat_SayPooled(e, line, SG_SAYF_DEAD | SG_SAYF_NOGAP);
		}
	}
}

/*
 * IDLE BANTER. The lull filler, and the easiest line in the file to get wrong:
 * it carries no information, so every time it lands on top of something that
 * does, it has cost more than it gave.
 *
 * Hence three gates before the roll. The bot must be out of contact for
 * SG_CHAT_IDLE_CALM -- nothing has hurt it and its side has no fresh eyes on an
 * enemy standing near it. Its team's channel must have been silent for
 * SG_CHAT_IDLE_QUIET. And the attempt itself is booked no sooner than
 * SG_CHAT_IDLE_GAP, then rolled, so the mean gap between two idle lines from
 * one mouth runs past two minutes even at the chattiest slot rate.
 *
 * "In contact" is read off pain_debounce_time, which T_Damage pushes to
 * level.time + 2 whenever it hurts a player (g_combat.c:528-531), plus CACO's
 * enemy sightings through Chat_EnemySeenNear. Neither is a perfect combat
 * flag; together they cover the two cases that matter, being shot at and
 * standing next to somebody who will.
 */
static void Chat_Idle(void)
{
	int i;

	if (level.intermissiontime)
		return;

	for (i = 0; i < game.maxclients; i++)
	{
		edict_t			*e = g_edicts + 1 + i;
		sg_chat_bot_t	*cb = &chat_bot[i];
		const char		*line;
		int				team;

		if (!Chat_OurBot(e) || !Chat_Playing(e))
			continue;
		team = e->client->ctf.teamnum;

		if (e->pain_debounce_time > level.time ||
		    Chat_EnemySeenNear(team, e->s.origin))
			cb->combat_at = level.time;

		if (cb->next_idle <= 0.0f)
		{
			cb->next_idle = level.time + SG_CHAT_IDLE_GAP
			              + Chat_RandomUnit(e) * SG_CHAT_IDLE_JITTER;
			continue;
		}
		if (level.time < cb->next_idle)
			continue;

		/* the attempt is spent whatever comes of it, or a bot held quiet by
		 * a long firefight would speak the instant the firefight ended */
		cb->next_idle = level.time + SG_CHAT_IDLE_GAP
		              + Chat_RandomUnit(e) * SG_CHAT_IDLE_JITTER;

		if (level.time - cb->combat_at < SG_CHAT_IDLE_CALM)
			continue;
		if (level.time - chat_team_last[SG_TeamIdx(team)] < SG_CHAT_IDLE_QUIET)
			continue;
		if (Chat_RandomUnit(e) >= SG_CHAT_IDLE_ODDS * Chat_Chatty(i))
			continue;

		line = Chat_Pick(e, Chat_Tone(e), SG_LINE_IDLE);
		if (line)
			Chat_SayPooled(e, line, 0);
	}
}

/*
 * HURT AND STILL HERE. The other half of SG_ChatDeath's grumble: a bot says
 * something when it dies, and until now said nothing at all when it nearly
 * did. "ouch" three seconds after a rocket you walked away from is one of the
 * most ordinary noises on a pub server.
 *
 * The shape is Chat_Idle's, and for the same reason -- this line carries no
 * information, so it has to stay out of the way of everything that does.
 *
 *   NOTICING is off the damage ring. Chat_BigHitSince returns the newest hit
 *   of at least SG_CHAT_HURT_DMG that is newer than the stamp this bot has
 *   already accounted for, so one rocket is noticed once no matter how many
 *   frames it sits in the four-slot ring, and a second bigger hit while the
 *   first is still being waited out simply becomes the one it mentions.
 *
 *   WAITING is what makes it a reaction rather than a scream. Nothing is said
 *   for SG_CHAT_HURT_WAIT, and then only if the fight has actually ended --
 *   nothing landed in SG_CHAT_HURT_CALM, no pain still on the clock, no fresh
 *   enemy on the team's books nearby. A bot narrating its own health mid
 *   firefight is the tell.
 *
 *   AND THE ATTEMPT IS SPENT either way. A bot held quiet by a long fight
 *   must not open its mouth about a rocket from forty seconds ago the instant
 *   the fight ends; the grievance expires with the moment.
 */
static void Chat_Hurt(void)
{
	int i;

	if (level.intermissiontime)
		return;

	for (i = 0; i < game.maxclients; i++)
	{
		edict_t			*e = g_edicts + 1 + i;
		sg_chat_bot_t	*cb = &chat_bot[i];
		const char		*line;
		float			hit;

		if (!Chat_OurBot(e) || !Chat_Playing(e))
		{
			/*
			 * Dead, gone, or a corpse: "and I lived" is no longer true.
			 * The stamp is dragged up to now as well, because the ring is
			 * not cleared by dying -- without this, the rocket that KILLED
			 * the bot is still sitting in it at respawn, older than the
			 * wait, and the bot walks out of the gate saying "still up"
			 * about the hit that just killed it.
			 */
			cb->hurt_at = 0.0f;
			cb->hurt_seen = level.time;
			continue;
		}

		hit = Chat_BigHitSince(e, SG_CHAT_HURT_DMG, cb->hurt_seen);
		if (hit > 0.0f)
		{
			cb->hurt_seen = hit;
			cb->hurt_at = hit;
		}

		if (cb->hurt_at <= 0.0f)
			continue;
		if (level.time - cb->hurt_at < SG_CHAT_HURT_WAIT)
			continue;

		cb->hurt_at = 0.0f;             /* the moment is spent, win or lose */

		if (level.time < cb->next_hurt)
			continue;
		if (e->pain_debounce_time > level.time ||
		    Chat_HurtSince(e, level.time - SG_CHAT_HURT_CALM) ||
		    Chat_EnemySeenNear(e->client->ctf.teamnum, e->s.origin))
			continue;                   /* still in it */
		if (Chat_RandomUnit(e) >= SG_CHAT_HURT_ODDS * Chat_Chatty(i))
			continue;

		line = Chat_Pick(e, Chat_Tone(e), SG_LINE_HURT);
		if (!line)
			continue;                   /* empty pool: nothing to say */
		if (Chat_SayPooled(e, line, 0))
			cb->next_hurt = level.time + SG_CHAT_HURT_GAP;
	}
}

/* -------------------------------------------------------------- orders */

#define SG_CHAT_MAXTOK	8
#define SG_CHAT_TOKLEN	16

static int Chat_Tokens(const char *msg, char tok[SG_CHAT_MAXTOK][SG_CHAT_TOKLEN])
{
	int n = 0, k = 0;

	tok[0][0] = '\0';
	while (*msg && n < SG_CHAT_MAXTOK)
	{
		unsigned char c = (unsigned char)*msg++;

		if (isalnum(c))
		{
			if (k < SG_CHAT_TOKLEN - 1)
				tok[n][k++] = (char)tolower(c);
		}
		else if (k > 0)
		{
			tok[n][k] = '\0';
			n++;
			k = 0;
			if (n < SG_CHAT_MAXTOK)
				tok[n][0] = '\0';
		}
	}
	if (k > 0 && n < SG_CHAT_MAXTOK)
	{
		tok[n][k] = '\0';
		n++;
	}
	return n;
}

static qboolean Chat_Word(char tok[SG_CHAT_MAXTOK][SG_CHAT_TOKLEN], int n,
                          int i, const char *w)
{
	if (i < 0 || i >= n)
		return false;
	return (strcmp(tok[i], w) == 0) ? true : false;
}

/*
 * "[addressee] <verb>". The verb table is the owner's, spelled the way
 * people actually type it mid-fight. A bare verb ("d", "go") is only taken
 * off team chat: on the public channel it needs a name in front of it, or
 * every stray "d" in the server becomes an order.
 */
static int Chat_Verb(char tok[SG_CHAT_MAXTOK][SG_CHAT_TOKLEN], int n, int i,
                     qboolean *clear)
{
	int j;

	*clear = false;
	if (i >= n)
		return SG_CHAT_ROLE_NONE;

	if (Chat_Word(tok, n, i, "defend") || Chat_Word(tok, n, i, "def") ||
	    Chat_Word(tok, n, i, "d"))
		return SG_CHAT_ROLE_DEFEND;

	if (Chat_Word(tok, n, i, "attack") || Chat_Word(tok, n, i, "go") ||
	    Chat_Word(tok, n, i, "off") || Chat_Word(tok, n, i, "offense") ||
	    Chat_Word(tok, n, i, "push"))
		return SG_CHAT_ROLE_ATTACK;

	if (Chat_Word(tok, n, i, "escort") || Chat_Word(tok, n, i, "cover"))
		return SG_CHAT_ROLE_ESCORT;
	if (Chat_Word(tok, n, i, "with") && Chat_Word(tok, n, i + 1, "me"))
		return SG_CHAT_ROLE_ESCORT;

	if (Chat_Word(tok, n, i, "recover"))
		return SG_CHAT_ROLE_RECOVER;
	if (Chat_Word(tok, n, i, "our") && Chat_Word(tok, n, i + 1, "flag"))
		return SG_CHAT_ROLE_RECOVER;
	if (Chat_Word(tok, n, i, "get"))
		for (j = i + 1; j < n; j++)
			if (strcmp(tok[j], "flag") == 0)
				return SG_CHAT_ROLE_RECOVER;

	if (Chat_Word(tok, n, i, "free"))
	{
		*clear = true;
		return SG_CHAT_ROLE_NONE;
	}
	if (Chat_Word(tok, n, i, "carry") && Chat_Word(tok, n, i + 1, "on"))
	{
		*clear = true;
		return SG_CHAT_ROLE_NONE;
	}
	if (Chat_Word(tok, n, i, "as") && Chat_Word(tok, n, i + 1, "you") &&
	    Chat_Word(tok, n, i + 2, "were"))
	{
		*clear = true;
		return SG_CHAT_ROLE_NONE;
	}

	return SG_CHAT_ROLE_NONE;
}

static int Chat_AckKind(int role, qboolean clear)
{
	if (clear)
		return SG_ACK_FREE;
	switch (role)
	{
	case SG_CHAT_ROLE_ATTACK:	return SG_ACK_ATTACK;
	case SG_CHAT_ROLE_DEFEND:	return SG_ACK_DEFEND;
	case SG_CHAT_ROLE_ESCORT:	return SG_ACK_ESCORT;
	case SG_CHAT_ROLE_RECOVER:	return SG_ACK_RECOVER;
	default:					return -1;
	}
}

static void Chat_Order(edict_t *bot, edict_t *from, int role, qboolean clear)
{
	int cl = Chat_ClientNum(bot);

	if (cl < 0 || cl >= game.maxclients)
		return;

	if (clear)
	{
		chat_bot[cl].order_role = SG_CHAT_ROLE_NONE;
		chat_bot[cl].order_expire = 0.0f;
		chat_bot[cl].order_from = -1;
		chat_bot[cl].order_team = CTF_TEAM_UNDEFINED;
		return;
	}

	chat_bot[cl].order_role = role;
	chat_bot[cl].order_expire = level.time + SG_CHAT_ORDER_TTL;
	chat_bot[cl].order_from = Chat_ClientNum(from);
	chat_bot[cl].order_team = from->client->ctf.teamnum;
}

/* ------------------------------------------------- the item call, parsed
 * Human and bot team messages share this parser. Repeated calls within
 * SG_CHAT_PARSE_SAME describe the same clock event. */

/* how close two clocks have to be before the second one is the same event */
#define SG_CHAT_PARSE_SAME	3.0f

/* longest sane call, and invuln's own 300 is the reason for the number */
#define SG_CHAT_PARSE_MAXSEC	300

/*
 * The majors vocabulary, as spoken. Both columns matter: the left is what
 * the bots themselves emit through Chat_ItemName ("invuln", "red armor",
 * "power shield"), the right is what a human types instead, and neither is
 * privileged. Two-word rows sit ahead of any single-word row that would
 * shadow them, so "power shield" is read whole rather than as a stray
 * "shield" one token late.
 *
 * Runes are absent on purpose and permanently: they carry no clock anywhere
 * in this build (see chat_major), so "haste taken" has no number to arm.
 * Mega is present and will simply find no row -- it is neither a CACO belief
 * class nor on the watch list -- which is the same standing absence
 * chat_timer_major already writes down.
 */
static const struct { const char *w1; const char *w2; const char *cls; }
chat_call_word[] = {
	{ "quad",            NULL,       "item_quad" },
	{ "invuln",          NULL,       "item_invulnerability" },
	{ "invul",           NULL,       "item_invulnerability" },
	{ "invulnerability", NULL,       "item_invulnerability" },
	{ "pent",            NULL,       "item_invulnerability" },
	{ "red",             "armor",    "item_armor_body" },
	{ "red",             "armour",   "item_armor_body" },
	{ "ra",              NULL,       "item_armor_body" },
	{ "power",           "shield",   "item_power_shield" },
	{ "power",           "screen",   "item_power_screen" },
	{ "ps",              NULL,       "item_power_shield" },
	{ "shield",          NULL,       "item_power_shield" },
	{ "mega",            NULL,       "item_health_mega" },
	{ NULL, NULL, NULL }
};

/* the item phrase starting at token i, with how many tokens it ate */
static const char *Chat_CallItem(char tok[SG_CHAT_MAXTOK][SG_CHAT_TOKLEN],
                                 int n, int i, int *used)
{
	int k;

	if (i < 0 || i >= n)
		return NULL;
	for (k = 0; chat_call_word[k].w1; k++)
	{
		if (strcmp(tok[i], chat_call_word[k].w1) != 0)
			continue;
		if (chat_call_word[k].w2)
		{
			if (i + 1 >= n)
				continue;
			if (strcmp(tok[i + 1], chat_call_word[k].w2) != 0)
				continue;
			*used = 2;
		}
		else
			*used = 1;
		return chat_call_word[k].cls;
	}
	return NULL;
}

/* somebody has it in his hands. "get" is not here: "get quad" is an order */
static qboolean Chat_CallTakeVerb(const char *w)
{
	return (strcmp(w, "took") == 0 || strcmp(w, "take") == 0 ||
	        strcmp(w, "taking") == 0 || strcmp(w, "got") == 0 ||
	        strcmp(w, "grabbed") == 0 || strcmp(w, "picked") == 0 ||
	        strcmp(w, "have") == 0 || strcmp(w, "has") == 0)
	     ? true : false;
}

/* filler a verb is allowed to drag behind it: "picked up the quad" */
static qboolean Chat_CallSkip(const char *w)
{
	return (strcmp(w, "up") == 0 || strcmp(w, "the") == 0 ||
	        strcmp(w, "a") == 0 || strcmp(w, "an") == 0 ||
	        strcmp(w, "my") == 0)
	     ? true : false;
}

/*
 * Filler between the item and its number. The last four are not human
 * typing at all -- they are this file's own chat_timer_fmt rows coming back
 * in ("quad in 12, mine", "quad up soon, ~12", "quad t-12"), which the parser
 * has no business failing to read just because a bot wrote them.
 */
static qboolean Chat_CallTimerWord(const char *w)
{
	return (strcmp(w, "in") == 0 || strcmp(w, "back") == 0 ||
	        strcmp(w, "is") == 0 || strcmp(w, "up") == 0 ||
	        strcmp(w, "soon") == 0 || strcmp(w, "t") == 0)
	     ? true : false;
}

/* a token that is nothing but digits, and a small enough number to mean it */
static qboolean Chat_CallNumber(const char *s, int *out)
{
	int v = 0, i;

	if (!s || !s[0])
		return false;
	for (i = 0; s[i]; i++)
	{
		if (!isdigit((unsigned char)s[i]))
			return false;
		v = v * 10 + (s[i] - '0');
		if (v > SG_CHAT_PARSE_MAXSEC)
			return false;
	}
	*out = v;
	return true;
}

/* the belief row or watch row this class occupies, for this team */
static qboolean Chat_CallRow(int ti, const char *cls, int *kind, int *slot,
                             int *ent)
{
	int i, num;

	for (i = 0; i < sg_caco_num_items && i < SG_MAX_BELIEF_ITEMS; i++)
	{
		num = sg_caco_items[ti][i].ent;
		if (num > 0 && g_edicts[num].classname &&
		    strcmp(g_edicts[num].classname, cls) == 0)
		{
			*kind = SG_ARM_ITEM;
			*slot = i;
			*ent = num;
			return true;
		}
	}
	for (i = 0; i < chat_num_watch; i++)
	{
		num = chat_watch[i].ent;
		if (num > 0 && g_edicts[num].classname &&
		    strcmp(g_edicts[num].classname, cls) == 0)
		{
			*kind = SG_ARM_WATCH;
			*slot = i;
			*ent = num;
			return true;
		}
	}
	return false;
}

/*
 * One team-chat line, read for an item call. `team` is the SPEAKER's team and
 * the only team that can be armed off it, which is also the only team that
 * heard the line: Cmd_Say_f's broadcast loop skips every client failing
 * OnSameTeam(ent, other) when the say_team flag is set (g_cmds.c:2263-2268),
 * so an enemy never sees the text this function is reading. The routing does
 * the containment; nothing in here needs to re-check it, and nothing in here
 * touches the other team's row.
 */
static void Chat_HearItemCall(edict_t *speaker,
                              char tok[SG_CHAT_MAXTOK][SG_CHAT_TOKLEN],
                              int n, int team)
{
	const char	*cls = NULL, *what;
	int			i, used = 0, secs = 0, ti = SG_TeamIdx(team);
	int			kind = SG_ARM_NONE, slot = -1, ent = 0;
	qboolean	take = false, timed = false, dbg;
	float		respawn, back_at, held, gap;

	if (!SG_ItemComm())
		return;

	/*
	 * The "at <place>" tail is scenery. Cutting the line there keeps a
	 * landmark from being read as the item ("took ra at the ps") and keeps
	 * any number in a place name out of the timer form.
	 */
	for (i = 0; i < n; i++)
		if (strcmp(tok[i], "at") == 0)
		{
			n = i;
			break;
		}

	for (i = 0; i < n; i++)
	{
		int j;

		/*
		 * "<anything> took <item>". "enemy took quad" needs no case of its
		 * own -- it is this sentence with one more leading word, and the
		 * clock it earns is the HEARING team's either way, exactly as the
		 * witness call has always worked.
		 */
		if (Chat_CallTakeVerb(tok[i]))
		{
			j = i + 1;
			while (j < n && Chat_CallSkip(tok[j]))
				j++;
			cls = Chat_CallItem(tok, n, j, &used);
			if (cls)
			{
				take = true;
				break;
			}
			continue;
		}

		cls = Chat_CallItem(tok, n, i, &used);
		if (!cls)
			continue;
		j = i + used;

		/* "<item> taken" */
		if (j < n && (strcmp(tok[j], "taken") == 0 ||
		              strcmp(tok[j], "gone") == 0))
		{
			take = true;
			break;
		}

		/* "<item> [in|back|is|up|soon|t] <seconds>" -- "quad 30" */
		while (j < n && Chat_CallTimerWord(tok[j]))
			j++;
		if (j < n && Chat_CallNumber(tok[j], &secs) && secs > 0)
		{
			timed = true;
			break;
		}

		cls = NULL;             /* the item was named but nothing was said */
	}

	if (!cls || (!take && !timed))
		return;

	if (!Chat_CallRow(ti, cls, &kind, &slot, &ent))
		return;                 /* mega, and anything else with no clock */

	/*
	 * A take is timed from the parse moment and not a tenth earlier. The
	 * owner's own accounting: a human types with lag, and the lag IS the
	 * imprecision -- inventing a correction for it would be inventing
	 * knowledge, which is the thing this file exists not to do.
	 */
	respawn = timed ? (float)secs : Chat_MajorRespawn(g_edicts + ent);
	if (respawn <= 0.0f)
	{
		/*
		 * The mega's take-form carries no number but does carry the
		 * WHO: a first-person "took mega" from a teammate records the
		 * speaker as the wearer, so his obituary can start the clock
		 * (SG_ChatMegaDeath). "enemy took mega" names a taker we
		 * cannot identify -- presence moved, nothing recorded, the
		 * same honest blank a human keeps.
		 */
		if (!timed && speaker->client &&
		    g_edicts[ent].classname &&
		    strcmp(g_edicts[ent].classname, "item_health_mega") == 0)
		{
			int e2;
			qboolean enemyform = false;

			for (e2 = 0; e2 < n && !enemyform; e2++)
				if (strcmp(tok[e2], "enemy") == 0 ||
				    strcmp(tok[e2], "they") == 0)
					enemyform = true;
			if (!enemyform)
				chat_mega_taker[ti] =
				    (int)(speaker->client - game.clients);
		}
		return;
	}
	back_at = level.time + respawn;

	dbg = (sg_cv.debug->value > 0.0f) ? true : false;
	what = Chat_ItemName(g_edicts + ent);
	if (!what)
		what = "item";

	held = (kind == SG_ARM_ITEM) ? chat_item[slot].back_at[ti]
	                             : chat_watch[slot].back_at[ti];
	gap = held - back_at;
	if (gap < 0.0f)
		gap = -gap;

	/*
	 * DEDUPE. A clock this team is already holding, landing within three
	 * seconds of what this line would arm, is the same knowledge arriving
	 * down the other route -- the bot's own emitted line, or its "soon"
	 * callout naming a number off the very clock being compared. Re-arming
	 * would move the countdown by a frame's worth of nothing and print a
	 * second story about one event. Leave it alone.
	 */
	if (held > level.time && gap <= SG_CHAT_PARSE_SAME)
	{
		static int skips = 0;

		if (dbg && (skips++ % 8) == 0)
			sg_host.dprint("SG itemcomm: %s said %s, team %d -- clock already "
			           "within %.1fs (%.1f vs %.1f), parse skipped "
			           "(1-in-8)\n", speaker->client->pers.netname, what,
			           team, gap, held, back_at);
		return;
	}

	if (kind == SG_ARM_ITEM)
	{
		chat_item[slot].up[ti] = false;
		chat_item[slot].back_at[ti] = back_at;
		chat_item[slot].soon_said[ti] = false;
		sg_caco_items[ti][slot].believed_up = false;
		sg_caco_items[ti][slot].believed_respawn_time = back_at;
		/* a parsed quad call covers the cycle like any other: no bot
		 * fade-watch or reminder speaks behind a human who already did
		 * (owner: "if 3 teammates see the enemy grab the quad, we don't
		 * want 3 separate quad timers at once firing") */
		if (sg_caco_items[ti][slot].ent > 0 &&
		    g_edicts[sg_caco_items[ti][slot].ent].classname &&
		    strcmp(g_edicts[sg_caco_items[ti][slot].ent].classname,
		           "item_quad") == 0)
			radio_q30[ti].called_until = back_at;
	}
	else
	{
		chat_watch[slot].up[ti] = false;
		chat_watch[slot].back_at[ti] = back_at;
		chat_watch[slot].soon_said[ti] = false;
	}

	if (dbg)
		sg_host.dprint("SG itemcomm: %s said %s (%s) -- parsed, armed team %d "
		           "for %.0fs, back at %.1f\n",
		           speaker->client->pers.netname, what,
		           take ? "take" : "timer", team, respawn, back_at);
}

/* ------------------------------------------------------ addressed replies
 *
 * BEING SPOKEN TO IS NOT BEING ORDERED. The order grammar reads the FIRST
 * token because "arach defend" is how an order gets typed mid-fight. Being
 * talked to is not typed that way: "arach?", "hey arach", "nice one arach"
 * are all somebody addressing that bot and only one of them puts the name in
 * front. So the reply looks for a name anywhere in the line.
 *
 * WHICH IS A PROBLEM, because half the roster's names are ordinary CTF
 * vocabulary. gate, field, rune, spawn, slip and trace all turn up in lines
 * that are about the map and not about a bot -- "quad at the gate", "rune is
 * up", "watch the spawn" -- and a bot answering "?" to those is the noise this
 * file spends three thousand lines avoiding.
 *
 * Two word lists sort it out, and they are deliberately about GRAMMAR rather
 * than about a blocklist of phrases:
 *
 *   A name after a locator ("at the gate", "by the rune") is a place. The
 *   speaker is naming where something is.
 *
 *   A name in front of a state word ("rune is up", "gate was clear") is a
 *   subject. The speaker is telling his team a fact about a thing.
 *
 * Anything else that names one of ours is taken as addressing it. The cost of
 * the rule is the occasional real question phrased as a statement going
 * unanswered, which is the right way round: a bot that misses one is quiet,
 * and a bot that answers the map is broken.
 */

static const char *chat_locator[] = {
	"at", "the", "by", "near", "on", "in", "to", "from", "our", "their",
	"a", "an", "of", "past", "behind", "under", "over", NULL
};

static const char *chat_stateword[] = {
	"is", "was", "are", "were", "up", "down", "gone", "taken", "back",
	"respawn", "respawns", "spawns", "has", "had", NULL
};

static qboolean Chat_InList(const char *w, const char **list)
{
	int i;

	for (i = 0; list[i]; i++)
		if (strcmp(w, list[i]) == 0)
			return true;
	return false;
}

/*
 * One of ours, named somewhere in this line by a human on its own team, in a
 * position that reads as address rather than as description. NULL for none.
 */
static edict_t *Chat_Mentioned(char tok[SG_CHAT_MAXTOK][SG_CHAT_TOKLEN], int n,
                               int team)
{
	int i, k;

	for (i = 0; i < n; i++)
	{
		if (i > 0 && Chat_InList(tok[i - 1], chat_locator))
			continue;               /* "at the gate": a place, not a bot */
		if (i + 1 < n && Chat_InList(tok[i + 1], chat_stateword))
			continue;               /* "rune is up": a thing, not a bot */

		for (k = 0; k < game.maxclients; k++)
		{
			edict_t *e = g_edicts + 1 + k;

			if (!Chat_OurBot(e) || e->client->ctf.teamnum != team)
				continue;
			if (Chat_NameIs(e, tok[i]))
				return e;
		}
	}
	return NULL;
}

/*
 * Book the reply. The line is PICKED here rather than at delivery so that its
 * own length can be typed at a human rate -- which means the reuse guard is
 * consulted a few seconds before the line lands, the same trade the steal
 * callout makes in Chat_TeamEvents and for the same reason: the alternative is
 * not knowing how long the thing is.
 *
 * The gap runs from the moment the bot was SPOKEN TO, not from the answer, so
 * a human who says the same name three times in ten seconds gets one reply
 * either way.
 */
static void Chat_ReplyBook(edict_t *bot, const char *heard, qboolean teamchat)
{
	const char	*line;
	float		delay;
	int			cl;

	if (!bot || !Chat_OurBot(bot) || !Chat_Playing(bot))
		return;
	cl = Chat_ClientNum(bot);
	if (cl < 0 || cl >= game.maxclients)
		return;
	if (chat_bot[cl].reply_line)
		return;                     /* already answering: once is once */
	if (level.time < chat_bot[cl].next_reply)
		return;
	if (Chat_Busy(bot))
		return;                     /* carrying, or being shot at */

	line = Chat_Pick(bot, Chat_Tone(bot), SG_LINE_REPLY);
	if (!line)
		return;                     /* empty pool: stay quiet */

	delay = SG_CHAT_REPLY_THINK
	      + (float)strlen(heard ? heard : "") / SG_CHAT_REPLY_READ
	      + (float)strlen(line) / SG_CHAT_REPLY_TYPE;
	if (delay < SG_CHAT_REPLY_MIN)
		delay = SG_CHAT_REPLY_MIN;
	else if (delay > SG_CHAT_REPLY_MAX)
		delay = SG_CHAT_REPLY_MAX;

	chat_bot[cl].reply_line = line;
	chat_bot[cl].reply_at = level.time + delay;
	chat_bot[cl].reply_team = teamchat;
	chat_bot[cl].next_reply = level.time + SG_CHAT_REPLY_GAP;
}

/*
 * Deliver it, on the channel it was spoken on. Both routes are the ordinary
 * ones and both can refuse: the public say budget can eat a reply and so can
 * the team budget, and a refused reply is simply not said. That is the honest
 * outcome -- a bot with a callout already in its mouth is a bot whose hands
 * were full -- and it is why the reply is a courtesy rather than a promise.
 */
static void Chat_ReplyFlush(void)
{
	int i;

	for (i = 0; i < game.maxclients; i++)
	{
		edict_t			*e = g_edicts + 1 + i;
		sg_chat_bot_t	*cb = &chat_bot[i];
		const char		*line = cb->reply_line;

		if (!line || level.time < cb->reply_at)
			continue;

		cb->reply_line = NULL;      /* consumed however it goes */
		cb->reply_at = 0.0f;

		if (!Chat_OurBot(e) || !Chat_Playing(e))
			continue;               /* died in the middle of typing it */
		if (Chat_Busy(e))
			continue;               /* a fight started: he stopped typing */

		if (cb->reply_team)
		{
			if (SG_ChatSayTeam(e, line, SG_CHAT_TOPIC_REPLY))
				Chat_Note(line);
		}
		else
			Chat_SayPooled(e, line, 0);
	}
}

void SG_ChatHear(edict_t *speaker, const char *msg, qboolean teamchat)
{
	char		tok[SG_CHAT_MAXTOK][SG_CHAT_TOKLEN];
	edict_t		*named = NULL, *acker = NULL, *mentioned = NULL;
	qboolean	broadcast = false, clear = false, addressed = false;
	int			n, i, idx = 0, role, team, ack;

	if (!speaker || !speaker->inuse || !speaker->client || !msg)
		return;

	team = speaker->client->ctf.teamnum;
	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return;

	n = Chat_Tokens(msg, tok);
	if (n < 1)
		return;

	/*
	 * The item-call ear, ahead of the order parser and ahead of the bot
	 * test, because a bot's call is a call (owner, 2026-08-05: "there should
	 * be no difference in parsing a human"). Team channel only: an item call
	 * is knowledge for the side that heard it, and the public channel is
	 * heard by both.
	 */
	if (teamchat)
		Chat_HearItemCall(speaker, tok, n, team);

	if (speaker->flags & FL_BOT)
		return;                         /* bots do not take orders from bots */

	/* addressee: one of ours by name, or the whole team */
	for (i = 0; i < game.maxclients && !named; i++)
	{
		edict_t *e = g_edicts + 1 + i;

		if (!Chat_OurBot(e) || e->client->ctf.teamnum != team)
			continue;
		if (Chat_NameIs(e, tok[0]))
			named = e;
	}
	if (named)
	{
		addressed = true;
		idx = Chat_Word(tok, n, 1, "sg") ? 2 : 1;   /* "arach[sg] defend" */
	}
	else if (Chat_Word(tok, n, 0, "all") || Chat_Word(tok, n, 0, "everyone") ||
	         Chat_Word(tok, n, 0, "team") || Chat_Word(tok, n, 0, "bots") ||
	         Chat_Word(tok, n, 0, "slipgate"))
	{
		addressed = true;
		broadcast = true;
		idx = 1;
	}
	else
	{
		broadcast = true;
		idx = 0;
	}

	/*
	 * Who, if anybody, this line is TALKING TO, as opposed to who it is
	 * ordering. The addressee above is the first token because that is how an
	 * order is typed; this looks at the whole line. Worked out before either
	 * of the not-an-order exits below, because both of them are the reply's
	 * cue and only one of them ever reaches the verb parser.
	 *
	 * Deliberately NOT short-circuited on `named`. The order parser's first
	 * token match has no grammar filter on it, so "gate is up" -- a human
	 * calling the power screen -- makes Gate the addressee of an order that
	 * turns out not to exist, and answering it would be the exact false
	 * positive Chat_Mentioned's two word lists are there to catch.
	 */
	mentioned = Chat_Mentioned(tok, n, team);

	/* an unaddressed order is only an order on the team channel */
	if (!addressed && !teamchat)
	{
		Chat_ReplyBook(mentioned, msg, teamchat);
		return;
	}

	role = Chat_Verb(tok, n, idx, &clear);
	if (role == SG_CHAT_ROLE_NONE && !clear)
	{
		/* named, and nothing the grammar knows: he is being spoken to */
		Chat_ReplyBook(mentioned, msg, teamchat);
		return;
	}

	if (named)
	{
		Chat_Order(named, speaker, role, clear);
		acker = named;
	}
	else if (broadcast)
	{
		for (i = 0; i < game.maxclients; i++)
		{
			edict_t *e = g_edicts + 1 + i;

			if (!Chat_OurBot(e) || e->client->ctf.teamnum != team)
				continue;
			Chat_Order(e, speaker, role, clear);
			if (!acker && Chat_Playing(e))
				acker = e;
		}
	}

	/*
	 * One acknowledgement per order, immediately, off the 4-second budget --
	 * an ack that arrives after the bot has already turned round is worse
	 * than none. SG_CHAT_ACK_GAP is only a floor against a human repeating
	 * the same order every frame.
	 */
	ack = Chat_AckKind(role, clear);
	if (acker && ack >= 0)
	{
		int acl = Chat_ClientNum(acker);

		if (acl >= 0 && acl < game.maxclients &&
		    level.time >= chat_bot[acl].next_ack &&
		    SG_ChatSayTeam(acker, chat_ack[Chat_Tone(acker)][ack],
		                   SG_CHAT_TOPIC_ORDER))
			chat_bot[acl].next_ack = level.time + SG_CHAT_ACK_GAP;
	}
}

/*
 * An order stops binding when it runs out of time, when the human who gave
 * it leaves, or when either of them changes team -- "defend" from a red
 * player means nothing to a bot that is now blue.
 */
static qboolean Chat_OrderLive(int cl)
{
	sg_chat_bot_t	*cb;
	edict_t			*bot, *from, *flag;
	qboolean		carried;

	if (cl < 0 || cl >= game.maxclients)
		return false;
	cb = &chat_bot[cl];
	if (cb->order_role == SG_CHAT_ROLE_NONE)
		return false;
	if (level.time > cb->order_expire)
		return false;
	if (cb->order_from < 0 || cb->order_from >= game.maxclients)
		return false;

	bot = g_edicts + 1 + cl;
	from = g_edicts + 1 + cb->order_from;
	if (!from->inuse || !from->client)
		return false;
	/* "Cover me" names a live moving teammate. A corpse has no escort
	 * position and no valid carrier field; keeping this one order alive can
	 * turn the bot's objective into all-INF until the human respawns. */
	if (cb->order_role == SG_CHAT_ROLE_ESCORT &&
	    (from->deadflag || from->health <= 0))
		return false;
	/* RECOVER names a transient objective, not a place to camp.  End the
	 * override as soon as the ordering team's flag is authoritative at home;
	 * otherwise the bot grinds its own stand (and can wedge-kill/repeat) for
	 * the remainder of the ninety-second chat-order lease. */
	if (cb->order_role == SG_CHAT_ROLE_RECOVER)
	{
		flag = (cb->order_team == CTF_TEAM_RED) ? redflag : blueflag;
		if (!flag || !flag->inuse)
			return false;
		carried = flag->owner && flag->owner->inuse && flag->owner->client &&
		          flag->owner->client->ctf.teamnum ==
		              SG_EnemyTeam(cb->order_team) &&
		          ClientHasFlag(flag->owner) == flag;
		/* A carried flag entity remains hidden at its take origin, which may
		 * still satisfy ctf_flagathome().  Inventory/owner is authoritative for
		 * that state; the position test is authoritative only when uncarried. */
		if (!carried && ctf_flagathome(flag))
			return false;
	}
	if (from->client->ctf.teamnum != cb->order_team)
		return false;
	if (!bot->inuse || !bot->client ||
	    bot->client->ctf.teamnum != cb->order_team)
		return false;
	return true;
}

static void Chat_ExpireOrders(void)
{
	int i;

	for (i = 0; i < game.maxclients; i++)
		if (chat_bot[i].order_role != SG_CHAT_ROLE_NONE &&
		    !Chat_OrderLive(i))
		{
			chat_bot[i].order_role = SG_CHAT_ROLE_NONE;
			chat_bot[i].order_from = -1;
		}
}

int SG_ChatOrderedRole(edict_t *bot)
{
	int cl;

	if (!bot || !bot->inuse || !bot->client)
		return SG_CHAT_ROLE_NONE;
	cl = Chat_ClientNum(bot);
	if (!Chat_OrderLive(cl))
		return SG_CHAT_ROLE_NONE;
	return chat_bot[cl].order_role;
}

edict_t *SG_ChatEscortTarget(edict_t *bot)
{
	edict_t	*from;
	int		cl;

	if (!bot || !bot->inuse || !bot->client)
		return NULL;
	cl = Chat_ClientNum(bot);
	if (!Chat_OrderLive(cl))
		return NULL;
	if (chat_bot[cl].order_role != SG_CHAT_ROLE_ESCORT)
		return NULL;

	from = g_edicts + 1 + chat_bot[cl].order_from;
	if (!from->inuse || !from->client)
		return NULL;
	return from;
}

/* Client slots are recycled without a level reset. Personal cooldowns,
 * pending replies and human orders belong to the occupant, not the index. */
void SG_ChatResetClient(edict_t *client)
{
	int cl, i, t, topic;

	if (!client)
		return;
	cl = Chat_ClientNum(client);
	if (cl < 0 || cl >= game.maxclients)
		return;
	memset(&chat_bot[cl], 0, sizeof(chat_bot[cl]));
	chat_bot[cl].order_role = SG_CHAT_ROLE_NONE;
	chat_bot[cl].order_from = -1;
	chat_bot[cl].end_cat = -1;
	/* A queued voice line or radio press cannot be inherited by the next bot
	 * that happens to occupy the same client number. */
	for (t = 0; t < 2; t++)
	{
		for (topic = 0; topic < SG_CHAT_TOPICS; topic++)
			if (chat_q[t][topic].pending &&
			    (chat_q[t][topic].speaker == cl ||
			     (chat_q[t][topic].arm_kind == SG_ARM_MEGATAKE &&
			      chat_q[t][topic].arm_who == cl)))
				chat_q[t][topic].pending = false;
		if (radio_q[t].pending && radio_q[t].speaker == cl)
			radio_q[t].pending = false;
		if (chat_mega_taker[t] == cl)
			chat_mega_taker[t] = -1;
	}
	/* Orders are owned by the giver's live client generation too. Waiting
	 * for the periodic liveness sweep leaves a same-frame recycled slot able
	 * to command bots using the departed human's order. */
	for (i = 0; i < game.maxclients; i++)
		if (chat_bot[i].order_from == cl)
		{
			chat_bot[i].order_role = SG_CHAT_ROLE_NONE;
			chat_bot[i].order_from = -1;
		}
}

/* --------------------------------------------------------- frame, reset */

void SG_ChatFrame(void)
{
	Chat_ExpireOrders();
	Chat_Greetings();
	Chat_LevelOpen();               /* after Chat_Greetings: it trails greet_at */
	Chat_SelfPickups();
	Chat_TeamEvents();
	Chat_Countdown();
	Chat_LevelEndFlush();
	/* ahead of the fillers: a human is waiting on this one, and a bot that
	 * spends its say budget on banter first has ignored him */
	Chat_ReplyFlush();
	Chat_Hurt();
	Chat_Idle();                    /* last: everything above may silence it */
	Chat_Flush();
	/* after Chat_Flush: a take call queued THIS frame arms its clock in there,
	 * and the radio that rides with it starts its hand time from that moment */
	Chat_RadioFrame();
}

void SG_ChatReset(void)
{
	int i;

	memset(chat_bot, 0, sizeof(chat_bot));
	memset(chat_q, 0, sizeof(chat_q));
	memset(chat_teamsaid, 0, sizeof(chat_teamsaid));
	memset(chat_item, 0, sizeof(chat_item));
	memset(chat_watch, 0, sizeof(chat_watch));
	memset(chat_recent, 0, sizeof(chat_recent));
	memset(chat_team_last, 0, sizeof(chat_team_last));
	chat_mega_taker[0] = chat_mega_taker[1] = -1;
	/* a level change is not a place to be still holding somebody's radio key */
	memset(radio_q, 0, sizeof(radio_q));
	memset(radio_q30, 0, sizeof(radio_q30));
	chat_recent_head = 0;
	chat_num_watch = 0;

	for (i = 0; i < MAX_CLIENTS; i++)
	{
		chat_bot[i].order_role = SG_CHAT_ROLE_NONE;
		chat_bot[i].order_from = -1;
		/* the one field whose "none" is not zero: zero is SG_LINE_JOIN, and
		 * a level that opened with sixteen bots shouting "hi" at intermission
		 * is what this line prevents */
		chat_bot[i].end_cat = -1;
	}

	/*
	 * The one-time scans. Landmarks and the extra watched pads are map
	 * knowledge, constant for the level, and this is the only place the
	 * entity list is read for either -- the same discipline, and the same
	 * moment, as Caco_ScanItemSpawns.
	 */
	Chat_ScanLandmarks();
	Chat_ScanWatched();

	/*
	 * Seed the told-state from CACO's opening belief: a powerup pad is
	 * assumed occupied until there is reason to think otherwise, a rune's
	 * whereabouts are assumed unknown because they are.
	 */
	for (i = 0; i < sg_caco_num_items; i++)
	{
		chat_item[i].up[0] = sg_caco_items[0][i].believed_up;
		chat_item[i].up[1] = sg_caco_items[1][i].believed_up;
		chat_item[i].back_at[0] = chat_item[i].back_at[1] = 0.0f;
		chat_item[i].soon_said[0] = chat_item[i].soon_said[1] = false;
	}

	chat_lastscore[0] = Chat_Captures(CTF_TEAM_RED);
	chat_lastscore[1] = Chat_Captures(CTF_TEAM_BLUE);
	chat_lastcarrier[0] = chat_lastcarrier[1] = -1;

	/*
	 * Both flags start on their stands, and saying so here rather than
	 * letting the first frame discover it is what stops a level opening with
	 * two teams cheering a return that never happened.
	 */
	chat_flaghome[0] = chat_flaghome[1] = true;
	chat_conceded_at[0] = chat_conceded_at[1] = 0.0f;
}
