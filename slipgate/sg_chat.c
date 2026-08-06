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
 *      map open, taunt, grumble, celebration, match end, idle banter. Short,
 *      lowercase, rate-limited, and probabilistic so it is not a script.
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
 * line, NULL) fills the redirected gi.argv and runs ClientCommand ->
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

/*
 * THE REUSE GUARD. A line that has gone out in the last SG_CHAT_REUSE_GAP
 * seconds is not offered to anybody -- the same bot or any other. The ring
 * holds pointers into the pool table below, never a caller's buffer, so a
 * recorded entry stays valid for the life of the process.
 */
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

/*
 * THE TIMER CALL (sg_timercall, enhancement 8). A team that knows the clock
 * says so in four characters, and it says it once. "quad up in ~9s" is a
 * sentence; what actually went over say_team in 1998 was "quad 9" -- and one
 * voice said it, not five, because the fifth is what makes a team channel
 * unreadable. The pieces are already here: the respawn clock this file keeps
 * off ent->item->quantity, Chat_Speaker's single owner, and the per-topic
 * team cooldown. This gives the short form its own topic so the gap can be a
 * generous twenty seconds without slowing the ordinary item chatter down,
 * and leaves everything at sg_timercall 0 exactly as it was.
 */
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
	2.0f        /* SG_CHAT_TOPIC_MAJOR -- short: majors are rare and urgent */
};

/* ----------------------------------------------------------- personality */

enum { SG_TONE_TERSE = 0, SG_TONE_COCKY, SG_TONE_DRY, SG_TONE_MECH, SG_TONES };

enum {
	SG_LINE_JOIN = 0, SG_LINE_KILL, SG_LINE_DEATH,
	SG_LINE_CAP, SG_LINE_STEAL,
	SG_LINE_OPEN,                   /* the map just came up */
	SG_LINE_WIN, SG_LINE_LOSE, SG_LINE_CLOSE,   /* how the match ended */
	SG_LINE_IDLE,                   /* nothing happening, filling the air */
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
		  "hm", NULL }
	},
	/* SG_TONE_COCKY  -- caco, slip, fiend, spawn.
	 * Where the TAUNT bucket landed, caps-lock and all. */
	{
		{ "who wants it", "we were born ready", "dont u worry", "yoyo",
		  "whats up", "im here", "line up", "hope you practiced", NULL },
		{ "DIE !!", "zoom", "no u", "im fast", "bam", "lol", "sit down",
		  "too easy", "all day", "thats mine", NULL },
		{ "you gotta be kidding me", "ns", "sure", "lol wat", "no!",
		  "wow", "lucky", "cheap", "whatever", NULL },
		{ "there you go", "lol yay", "bam", "nice!", "told you",
		  "run it back", "count it", "thats how you do it", NULL },
		{ "im fast", "zoom", "dont u worry", "flags mine", "watch this",
		  "coming through", "ill take that", NULL },
		{ "here we go", "we were born ready", "lets run it", "my map",
		  "easy map", "i live in that flag room", "this ones mine", NULL },
		{ "gg", "that's game", "word", "really ggs!", "told you",
		  "we owned that", "any time", NULL },
		{ "good to take the L", "gfg", "tough one", "nah.", "gg", "lag",
		  "rematch", "you got lucky", NULL },
		{ "lol close call", "insane game", "phew!", "crazy!", "too close",
		  "we let that get close", "next one wont be close", NULL },
		{ "zoom", "brb", "somebody do something", "im getting bored",
		  "wake up out there", "who wants a go", NULL }
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
		  "lovely weather in here", NULL }
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
		  "power conserved", "scan clear", "holding position", NULL }
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

	/* the standing order, if any */
	int			order_role;     /* SG_CHAT_ROLE_NONE when none */
	float		order_expire;
	int			order_from;     /* client number of the human who gave it */
	int			order_team;     /* the team both were on at the time */

	/* what this bot is holding: the one thing it may speak of unseen */
	qboolean	had_quad;
	qboolean	had_invul;
	int			had_rune;       /* edict index, 0 = empty handed */
} sg_chat_bot_t;

static sg_chat_bot_t chat_bot[MAX_CLIENTS];

/*
 * THE CLOCK RIDES ON THE LINE (owner's ruling, 2026-08-05).
 *
 *   "bots should know when an item is coming back IF AND ONLY IF another bot
 *    on the team has called it out in team chat when the item is taken."
 *
 * Which makes WHEN the clock is armed the whole question, and the answer is
 * not "when the bot decided to speak". A queued line still has two gates to
 * clear -- the reaction delay below, then SG_ChatSayTeam's per-bot budget and
 * per-topic team cooldown -- and a line the cooldown eats was never heard by
 * anybody. Arming at queue time would give the team a countdown off a sentence
 * that never reached the channel, which is the omniscience this whole file
 * exists to take away.
 *
 * So the arm travels WITH the line, in the fields below, and is applied by
 * Chat_ArmClock at the moment SG_ChatSayTeam reports the line actually went
 * out. Suppressed line, no clock, and sg_debug says so.
 */
enum { SG_ARM_NONE = 0, SG_ARM_ITEM, SG_ARM_WATCH, SG_ARM_QUIET };

typedef struct
{
	qboolean	pending;
	int			speaker;        /* client number */
	float		due;
	char		line[SG_CHAT_LINE];

	/* the respawn clock this line earns for its team IF it is spoken */
	int			arm_kind;       /* SG_ARM_* -- NONE for every ordinary line */
	int			arm_slot;       /* index into sg_caco_items rows, or chat_watch */
	int			arm_ent;        /* the item entity, for the sg_debug line */
	int			arm_src;        /* SG_ITEMCALL_* -- who is making the call */
	float		arm_back_at;    /* absolute: take time + the item's own delay */
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
static const char *Chat_Pick(int tone, int cat)
{
	const char	*fresh[SG_CHAT_MAXLINES];
	int			n = 0, f = 0, i;

	if (tone < 0 || tone >= SG_TONES || cat < 0 || cat >= SG_LINE_CATS)
		return NULL;
	while (n < SG_CHAT_MAXLINES && chat_line[tone][cat][n])
		n++;
	if (n == 0)
		return NULL;

	for (i = 0; i < n; i++)
		if (!Chat_Recent(chat_line[tone][cat][i]))
			fresh[f++] = chat_line[tone][cat][i];

	if (f > 0)
		return fresh[(int)(random() * f) % f];
	return chat_line[tone][cat][(int)(random() * n) % n];
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
	 */
	if (topic != SG_CHAT_TOPIC_ORDER)
	{
		if (level.time < chat_bot[cl].next_team)
			return false;
		if (level.time < chat_teamsaid[team - 1][topic])
			return false;
	}

	Chat_Copy(buf, line, sizeof(buf));
	SG_BotClientCommand(cl, "say_team", buf, NULL);

	/* stamped for every topic, acknowledgements included: idle banter reads
	 * this to stay off a channel that is carrying something */
	chat_team_last[team - 1] = level.time;

	if (topic != SG_CHAT_TOPIC_ORDER)
	{
		chat_bot[cl].next_team = level.time + SG_CHAT_BOT_GAP;
		chat_teamsaid[team - 1][topic] =
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
                              int arm_ent, int arm_src, float arm_back_at)
{
	sg_chatq_t *q;

	if (!speaker || !speaker->client || !(speaker->flags & FL_BOT))
		return false;
	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return false;
	if (topic < 0 || topic >= SG_CHAT_TOPICS)
		return false;

	q = &chat_q[team - 1][topic];
	if (q->pending)
		return false;
	if (level.time < chat_teamsaid[team - 1][topic])
		return false;

	Chat_Copy(q->line, line, sizeof(q->line));
	q->speaker = Chat_ClientNum(speaker);
	q->due = level.time + SG_CHAT_DELAY_MIN +
	         random() * (SG_CHAT_DELAY_MAX - SG_CHAT_DELAY_MIN);
	q->arm_kind = arm_kind;
	q->arm_slot = arm_slot;
	q->arm_ent = arm_ent;
	q->arm_src = arm_src;
	q->arm_back_at = arm_back_at;
	q->pending = true;
	return true;
}

static void Chat_Queue(edict_t *speaker, int team, int topic, const char *line)
{
	Chat_QueueArm(speaker, team, topic, line, SG_ARM_NONE, 0, 0, 0, 0.0f);
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
				/* not "he switched sides mid-thought" -- then the line is his
				 * old team's news said by somebody who is no longer on it */
				if (!sp->client || sp->client->ctf.teamnum == t + 1)
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

	e = G_Find(NULL, FOFS(classname), "info_flag_red");
	if (e)
	{
		VectorCopy(e->s.origin, chat_flagpos[0]);
		chat_flagpos_ok[0] = true;
	}
	e = G_Find(NULL, FOFS(classname), "info_flag_blue");
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
		sg_belief_enemy_t	*en = &sg_caco_enemies[team - 1][i];
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
	ti = team - 1;

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

	/*
	 * SIGHTING, NOT STOPWATCH (owner's ruling, 2026-08-05). An empty pad is
	 * evidence that the thing is gone and evidence of nothing else -- this bot
	 * has no idea whether it emptied a second ago or fifty, so it cannot count
	 * down to the return. Under sg_itemcomm the countdown comes from the bot
	 * who was THERE saying so; here it stays at whatever the team was told.
	 */
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

	if (!gi.inPVS(eye, mid))
		return false;
	tr = gi.trace(eye, NULL, NULL, mid, viewer, MASK_OPAQUE);
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
	ti = team - 1;

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

			/* the same rule as the powerup pads: seeing the armour gone is
			 * knowledge, knowing when it is back is a callout (2026-08-05) */
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
 *
 * THE OWNER'S RULING, 2026-08-05: "callout in conjunction, that's just good
 * teamplay."
 *
 * LMCTF has had a real radio since it shipped and the bots have never touched
 * it. PlayTeamSound (g_cmds.c:121) stuffs `play radio/<sound>` at every
 * same-team client running radio sound, prints the radiotext line at the ones
 * running text mode, and refuses a sender whose own radio bits are clear --
 * which is why sg_arach.c now sets those bits on a joining bot. A team callout
 * and the radio call that goes with it are ONE act to a player, so this rides
 * the itemcomm line rather than growing a second opinion about who speaks: the
 * same emission moment, the same single speaker, the same team. sg_radio 0
 * (the default) makes every function here return before it does anything.
 *
 * THREE THINGS IT IS NOT.
 *
 *   It is NOT frame-perfect. A human hears the pickup, moves a hand to the
 *   key and presses it, and the hand is a second or three behind the eye.
 *   Firing the radio inside the pickup frame is the tell that gave every
 *   previous generation of bot away, so the call is queued with
 *   SG_RADIO_LAG_MIN..MAX of hand time on it and flushed from the chat frame.
 *
 *   It is NOT a second channel of information. Everything said here has just
 *   gone out as text to the same team from the same mouth. sg_radio adds a
 *   noise a human teammate can act on without reading, and adds no fact.
 *
 *   It is NOT exempt from spam control. ctf_SpamCheck (g_ctffunc.c:1314)
 *   gates humans on the radio and gates bots identically. The one thing done
 *   differently is that a refused call is dropped SILENTLY: the three refusal
 *   tests are READ here rather than tripped, because tripping ctf_SpamCheck
 *   prints "blocked by spam control" at the bot AND re-arms its lockout, so a
 *   call nobody asked for would go on making the bot quieter.
 *
 * The sounds are the ones the paks actually carry, and every one of them is
 * `_`-prefixed -- PlayTeamSound plays those verbatim instead of prepending a
 * gender (g_cmds.c:150-153), so one name works for any skin.
 */

#define SG_RADIO_LAG_MIN	1.0f
#define SG_RADIO_LAG_MAX	3.0f
#define SG_RADIO_SOUND		24

static qboolean SG_Radio(void)
{
	return (gi.cvar("sg_radio", "0", 0)->value > 0.0f) ? true : false;
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

	q = &radio_q[team - 1];
	if (q->pending)
		return;                     /* one hand on the key at a time */

	q->speaker = cl;
	q->due = level.time + SG_RADIO_LAG_MIN +
	         random() * (SG_RADIO_LAG_MAX - SG_RADIO_LAG_MIN);
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
	if (sp->client->ctf.teamnum != t + 1)
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
			edict_t *sp = Chat_Speaker(t + 1);

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
				Chat_QueueArm(sp, t + 1, SG_CHAT_TOPIC_MAJOR, line,
				              SG_ARM_QUIET, -1, 0, SG_ITEMCALL_MATE,
				              heard + 33.0f +
				              ((float)(rand() % 10) / 10.0f - 0.5f));
				Chat_RadioQueue(sp, t + 1, "_quad30");
			}
		}

		Chat_RadioSay(t);
	}
}

/* ------------------------------------------------------ the taken callout
 *
 * THE OWNER'S RULING, 2026-08-05, in his words:
 *
 *   "items can be timed, but runes (the in-game kind) spawn randomly. bots
 *    should know when an item is coming back IF AND ONLY IF another bot on the
 *    team has called it out in team chat when the item is taken ... So if quad
 *    is taken and a bot says 'i just took quad at __location__' then we know in
 *    exactly [live-time plus respawn] it will be back ... A bot on the other
 *    team could see a bot on a different team take the red armor or quad, and
 *    they could call that out ... calling the item specifically will help and
 *    it should count even without the location."
 *
 * Three ways a clock is earned, and there is no fourth:
 *
 *   (a) TAKER'S CALL. One of ours picks the thing up and says so. No sighting
 *       needed by anybody -- it is holding the item, which is the one unseen
 *       claim SLIPGATE.md has always allowed.
 *   (b) WITNESS CALL. Somebody else took it and one of ours WATCHED, from the
 *       other team. That bot may say "enemy took quad at the rl", and its own
 *       team gets the clock. Note which team: the witness's, never the taker's.
 *   (c) TEAMMATE'S TAKE, WITNESSED. A human (or a legacy bot) on our side takes
 *       it and says nothing, because humans do not narrate. One of ours who saw
 *       it happen calls it, and our team gets the clock off that.
 *
 * The respawn interval is read, never guessed, per item -- the ruling says
 * "live-time plus respawn" and the mod's own numbers are the respawn.
 */

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
	/*
	 * The mega is deliberately absent. It has no clock anywhere in this build
	 * -- it is not a CACO belief class and not on the watch list -- and its
	 * real denial is 100s of decay on top of 20s of respawn (g_items.c:586-595),
	 * which is not the arithmetic the rest of this table does. Adding it means
	 * adding it to the watch list first, and this line is here so the next
	 * reader knows that rather than guessing it was forgotten.
	 */
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
	qboolean	dbg = (gi.cvar("sg_debug", "0", 0)->value > 0.0f)
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
			gi.dprintf("SG itemcomm: %s for %s, team %d SUPPRESSED "
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
		gi.dprintf("SG itemcomm: %s armed %s for team %d -- back at %.1f "
		           "(in %.0fs)\n", src, what, ti + 1, q->arm_back_at,
		           q->arm_back_at - level.time);
}

/*
 * One team's reaction to a major changing hands. `speaker` is the single bot
 * that gets to say it -- sg_caco.c picks the taker itself for (a) and one
 * witness for (b) and (c) -- and `team` is the team being told, which for a
 * witness call is the WITNESS's team and never the taker's.
 */
void SG_ChatItemTaken(edict_t *speaker, int team, edict_t *item, int src)
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
	ti = team - 1;

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
	                   kind, slot, (int)(item - g_edicts), src, back_at) &&
	    kind != SG_ARM_NONE &&
	    gi.cvar("sg_debug", "0", 0)->value > 0.0f)
		gi.dprintf("SG itemcomm: %s for %s, team %d SUPPRESSED "
		           "(topic busy) -- no clock armed\n",
		           chat_arm_src[(src >= 0 && src < 3) ? src : 0], name, team);
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

	if (gi.cvar("sg_timercall", "0", 0)->value > 0.0f)
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

	/*
	 * Superseded wholesale by the taken callout above when sg_itemcomm is on
	 * (owner's ruling, 2026-08-05). This scan is a once-a-frame poll of what
	 * bots are HOLDING, which is a second-hand way of noticing a pickup: it
	 * sees quad, invuln and runes and is blind to red armour, it cannot name
	 * where the thing was, and it arms a clock at queue time rather than at
	 * emission. SG_NoteItemTaken sees the pickup itself, covers every major,
	 * names the spot, and arms only what was actually said. Two mouths on one
	 * event would also mean the team hears it twice.
	 */
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
				sg_belief_item_t	*b = &sg_caco_items[team - 1][k];
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

				chat_item[k].up[team - 1] = false;
				chat_item[k].soon_said[team - 1] = false;
				chat_item[k].back_at[team - 1] = (b->respawn_delay > 0.0f)
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
	if (random() < 0.5f)
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

		line = Chat_Pick(Chat_Tone(e), SG_LINE_JOIN);
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
 * Captures and who holds a flag are on everyone's screen -- the scoreboard
 * and the HUD carrier icon -- so celebrating either leaks nothing. A steal
 * goes to the team as information; a capture goes public, because everybody
 * watched it happen anyway.
 *
 * The stored total is overwritten every frame, not only when it rises: a
 * capturer who disconnects takes his captures off the board with him, and a
 * counter that only ever climbed would fire again on the next one.
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
			sp = Chat_Speaker(t + 1);
			line = sp ? Chat_Pick(Chat_Tone(sp), SG_LINE_CAP) : NULL;
			if (sp && line)
				Chat_SayPooled(sp, line, 0);
		}
		chat_lastscore[t] = score[t];

		carrier = sg_caco_team_belief.carrier[t].client;
		if (carrier >= 0 && chat_lastcarrier[t] < 0)
		{
			sp = Chat_Speaker(t + 1);
			line = sp ? Chat_Pick(Chat_Tone(sp), SG_LINE_STEAL) : NULL;
			if (sp && line)
			{
				/*
				 * Burned at queue time, not on delivery: Chat_Queue copies
				 * the text into its slot and the pool pointer is gone by the
				 * time Chat_Flush speaks it. A queued line is committed
				 * anyway -- the slot is taken and no second steal callout
				 * will be queued behind it.
				 */
				Chat_Queue(sp, t + 1, SG_CHAT_TOPIC_STEAL, line);
				Chat_Note(line);
			}
		}
		chat_lastcarrier[t] = carrier;
	}
}

void SG_ChatDeath(edict_t *victim, edict_t *attacker, int mod)
{
	const char	*line;
	int			cl;

	(void)mod;                          /* the taunt does not read the weapon */

	if (attacker && attacker != victim && Chat_OurBot(attacker) &&
	    Chat_Playing(attacker))
	{
		cl = Chat_ClientNum(attacker);
		if (cl >= 0 && cl < game.maxclients &&
		    level.time >= chat_bot[cl].next_taunt &&
		    random() < SG_CHAT_TAUNT_ODDS)
		{
			line = Chat_Pick(Chat_Tone(attacker), SG_LINE_KILL);
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
		    random() < SG_CHAT_GRUMBLE_ODDS)
		{
			line = Chat_Pick(Chat_Tone(victim), SG_LINE_DEATH);
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

			if (random() >= SG_CHAT_OPEN_ODDS * Chat_Chatty(i))
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
			            + random() * SG_CHAT_OPEN_JITTER;
			continue;
		}
		if (level.time < cb->open_at)
			continue;

		cb->opened = true;                  /* one attempt, spoken or not */
		line = Chat_Pick(Chat_Tone(e), SG_LINE_OPEN);
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
		if (random() >= SG_CHAT_END_ODDS * Chat_Chatty(i))
			continue;

		diff = score[team - 1] - score[2 - team];
		if (diff > SG_CHAT_CLOSE_MARGIN)
			cb->end_cat = SG_LINE_WIN;
		else if (diff < -SG_CHAT_CLOSE_MARGIN)
			cb->end_cat = SG_LINE_LOSE;
		else
			cb->end_cat = SG_LINE_CLOSE;

		cb->end_at = level.time
		           + (float)(i % SG_CHAT_ROSTER) * SG_CHAT_END_STAGGER
		           + random() * SG_CHAT_END_JITTER;
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

		line = Chat_Pick(Chat_Tone(e), cat);
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
			              + random() * SG_CHAT_IDLE_JITTER;
			continue;
		}
		if (level.time < cb->next_idle)
			continue;

		/* the attempt is spent whatever comes of it, or a bot held quiet by
		 * a long firefight would speak the instant the firefight ended */
		cb->next_idle = level.time + SG_CHAT_IDLE_GAP
		              + random() * SG_CHAT_IDLE_JITTER;

		if (level.time - cb->combat_at < SG_CHAT_IDLE_CALM)
			continue;
		if (level.time - chat_team_last[team - 1] < SG_CHAT_IDLE_QUIET)
			continue;
		if (random() >= SG_CHAT_IDLE_ODDS * Chat_Chatty(i))
			continue;

		line = Chat_Pick(Chat_Tone(e), SG_LINE_IDLE);
		if (line)
			Chat_SayPooled(e, line, 0);
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

/* ------------------------------------------------- the item call, PARSED
 *
 * THE OWNER'S RULING, 2026-08-05, in his words:
 *
 *   "just be able to parse team messages from your own bot teammates, and
 *    there should be no difference in parsing a human, right??"
 *
 * Right. Everything below reads the say_team TEXT and nothing else. It does
 * not ask whether the speaker is a bot, and there is deliberately no branch
 * in here that could: FL_BOT is not tested once between this comment and the
 * end of Chat_HearItemCall. A human typing "quad 30" and a bot emitting
 * "quad 30" put the same eleven characters on the same channel, and a
 * teammate reading them learns the same thing, which is the entire point.
 *
 * That makes the itemcomm knowledge two-routed. The direct route is the one
 * that was already here -- SG_ChatItemTaken queues the line with its clock
 * riding along, Chat_ArmClock applies it when SG_ChatSayTeam reports the line
 * went out. The parse route is this one, and a bot's own line arrives down it
 * too, because SG_ChatSayTeam's say_team goes SG_BotClientCommand ->
 * ClientCommand -> Cmd_Say_f -> SG_ChatHear (g_cmds.c:2239) like anybody
 * else's. Hence the dedupe below. One knowledge event, two routes, one clock.
 *
 * Order of the two, for whoever debugs this later: the parse fires FIRST. It
 * happens inside SG_ChatSayTeam's say_team, and Chat_ArmClock runs after
 * SG_ChatSayTeam returns (Chat_QueueDrain). For an ordinary take both arm the
 * same number in the same frame, so the second write is a no-op; for the
 * ear-driven "quad 30", whose real arm is jittered off when the quad was
 * HEARD rather than off now, the direct route lands second and correctly
 * wins. The dedupe is therefore aimed at what it is actually for: a human
 * repeating himself, an echoed line, and the "soon" callouts ("quad in 12"),
 * which say a number the team is already holding.
 *
 * The suppression law is untouched and gets a little stronger here. A line
 * the per-bot budget or the topic cooldown ate is never handed to say_team at
 * all, so it never reaches this parser either. No line, no clock, both ways.
 */

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
	int			i, used = 0, secs = 0, ti = team - 1;
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
		return;
	back_at = level.time + respawn;

	dbg = (gi.cvar("sg_debug", "0", 0)->value > 0.0f) ? true : false;
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
			gi.dprintf("SG itemcomm: %s said %s, team %d -- clock already "
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
		gi.dprintf("SG itemcomm: %s said %s (%s) -- parsed, armed team %d "
		           "for %.0fs, back at %.1f\n",
		           speaker->client->pers.netname, what,
		           take ? "take" : "timer", team, respawn, back_at);
}

void SG_ChatHear(edict_t *speaker, const char *msg, qboolean teamchat)
{
	char		tok[SG_CHAT_MAXTOK][SG_CHAT_TOKLEN];
	edict_t		*named = NULL, *acker = NULL;
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

	/* an unaddressed order is only an order on the team channel */
	if (!addressed && !teamchat)
		return;

	role = Chat_Verb(tok, n, idx, &clear);
	if (role == SG_CHAT_ROLE_NONE && !clear)
		return;

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
	edict_t			*bot, *from;

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
}
