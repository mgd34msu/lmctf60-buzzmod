/*
 * sg_net.c -- the layer the whole mod's prints and network writes go through.
 *
 * Quake II hands the game library a table of engine function pointers at load
 * time and the mod parks it in the global `gi`. SG_NetInstall runs once, on
 * the statement right after that store, takes a private verbatim copy of the
 * table, and then overwrites nineteen of the slots in `gi` with the
 * functions below. Every one of them ends up calling through the private
 * copy. From that point on the entire mod -- not just bot code -- issues its
 * console prints, its network writes, its sounds and its console-argument
 * reads through here.
 *
 * THE PREMISE. SLIPGATE bots are created inside the game library
 * (sg_arach.c, SG_AddBotTeam). The engine is never told. svs.clients[] has
 * no entry for a bot's slot and the bot has no netchan, so any engine call
 * that appends bytes to "that client's reliable message" is appending to a
 * zero-sized buffer. Stock Quake II answers with Com_Error and takes the
 * server down with it. The predicate below is therefore not "is this a bot";
 * it is "does this entity have an engine-side client to address at all".
 *
 * WHY THE NETWORK WRITERS STAGE INTO A PRIVATE BUFFER. The engine already
 * accumulates game-side Write* calls into one message and only ships it on
 * multicast/unicast, so the staging here buys nothing in ordering terms and
 * costs a full copy of every message. It earns its keep on exactly one path:
 * a unicast aimed at a bot has to be RETRACTED. By the time the wrapper
 * learns who the recipient is, an unstaged message would already be sitting
 * inside the engine's buffer, and game_import_t has no call that takes bytes
 * back out of it. The choices would be to send the bot's layout to somebody
 * else or to leave it in the buffer where it prepends itself to the next
 * real recipient's message and garbles it. Staging is what makes "throw this
 * one away" expressible. Read this buffer as a cancellation point, not as a
 * re-implementation of the engine's writers.
 *
 * THE TWO SOUND SLOTS ARE WRAPPED, BUT NOT FILTERED. sound and
 * positioned_sound forward to the engine first and verbatim, and only then
 * tell SG_NoteSound (sg_caco.c) that a noise happened. They are NOT
 * per-recipient calls -- both resolve through the engine's own client list,
 * which contains no bots -- so there is nothing here to suppress, and
 * filtering them would only break audio for real players. The wrapper exists
 * to give bots an ear on real sounds instead of on server-private player
 * state; see the commentary over SG_NoteSound.
 *
 * WHAT IS DELIBERATELY NOT WRAPPED. dprintf, configstring, error, trace,
 * pointcontents, inPVS, inPHS, SetAreaPortalState, AreasConnected,
 * linkentity, unlinkentity, BoxEdicts, Pmove, TagMalloc, TagFree, FreeTags,
 * cvar, cvar_set, cvar_forceset, AddCommandString, DebugGraph, modelindex,
 * soundindex, imageindex and setmodel all stay the engine's own.
 *
 * The four index/setmodel slots used to be wrapped so a deleted third-party
 * bot library could keep a name for every registered asset. Those arrays had
 * no reader left, they were allocated under TAG_LEVEL and never cleared when
 * the tag was freed, and the one surviving external call read the dangling
 * copies on every map load. The whole apparatus is gone; the slots are the
 * engine's again.
 */

#include "g_local.h"
#include "slipgate/sg_net.h"

/* ClientCommand is defined in g_cmds.c and declared in no header. */
void ClientCommand(edict_t *ent);

/* ---------------------------------------------------------- module state */

static game_import_t	sg_engine;      /* byte-for-byte as the engine gave it */
static qboolean		sg_installed;

/*
 * The one question every suppression asks. NULL is legal and means "the
 * server console" for the print slots, which is an engine destination and
 * must go through -- so a NULL entity is never engine-less.
 */
static qboolean SG_IsEngineless(edict_t *ent)
{
	if (!ent)
		return false;
	return (ent->flags & FL_BOT) ? true : false;
}

/* ---------------------------------------------------- the event recorder
 *
 * Everything the mod says to a client and every message it puts on the wire
 * already passes through this file, so the cheapest honest record of "what
 * did this server actually send, to whom, when" is a tap at the five points
 * that finish those operations: the two flush points (SG_multicast,
 * SG_unicast) and the three print paths.
 *
 * OFF BY DEFAULT and free when off. sg_eventlog gates it, and the gate is
 * tested before any formatting happens -- in the suppressed print paths, which
 * are the hot ones with bots on the server, a disabled recorder costs one
 * pointer test and one float compare and the wrapper returns exactly as it did
 * before. Nothing here can change what reaches a client: every call site
 * records and then takes the same branch it always took, and the suppression
 * and retraction semantics are untouched. A bot-bound unicast is still thrown
 * away, and is recorded as DROPPED precisely because it was.
 *
 * File is per match: <sg_eventlog_dir or gamedir>/events-<map>-<frame>.log,
 * opened on the first recorded event and closed at level change.
 */

#define SG_PRINT_MAX	2048    /* formatting scratch, bounded */

static cvar_t	*sg_eventlog;       /* 0 = off, 1 = on */
static cvar_t	*sg_eventlog_dir;   /* empty = the game directory */
static FILE	*sg_ev_file;
static qboolean	sg_ev_tried;        /* opened or failed already this level */
static int	sg_ev_base;         /* frames elapsed before this level began */

static qboolean SG_EvOn(void)
{
	if (!sg_eventlog)
		sg_eventlog = sg_engine.cvar("sg_eventlog", "0", 0);
	return sg_eventlog->value != 0.0f;
}

static void SG_EvOpen(void)
{
	char	path[MAX_QPATH * 3];
	char	*dir;
	char	*map;

	sg_ev_tried = true;

	if (!sg_eventlog_dir)
		sg_eventlog_dir = sg_engine.cvar("sg_eventlog_dir", "", 0);

	dir = sg_eventlog_dir->string;
	if (!dir[0])
	{
		cvar_t *gamedir = sg_engine.cvar("gamedir", "", 0);

		dir = gamedir->string[0] ? gamedir->string : ".";
	}

	map = level.mapname[0] ? level.mapname : "nomap";

	snprintf(path, sizeof(path), "%s/events-%s-%i.log", dir, map, sg_ev_base);

	sg_ev_file = fopen(path, "a");
	if (!sg_ev_file)
		sg_engine.dprintf("SG_Net: could not open event log %s\n", path);
	else
		sg_engine.dprintf("SG_Net: recording events to %s\n", path);
}

/*
 * One record. Flushed per line on purpose: these servers are run under a
 * kill-after timeout, so anything sitting in a stdio buffer at the end of a
 * match is anything the match never gets to explain.
 */
static void SG_EvLine(char *recipient, char *kind, char *detail)
{
	if (!sg_ev_tried)
		SG_EvOpen();
	if (!sg_ev_file)
		return;
	fprintf(sg_ev_file, "%.2f %s %s %s\n", level.time, recipient, kind,
	        detail);
	fflush(sg_ev_file);
}

/* "<edict number>:<netname>" for one client, or a fixed word. */
static char *SG_EvWho(edict_t *ent)
{
	static char buf[80];

	if (!ent)
		return "console";
	if (!ent->client)
		return "nonclient";

	snprintf(buf, sizeof(buf), "%i:%s", (int)(ent - g_edicts),
	         ent->client->pers.netname[0] ? ent->client->pers.netname : "?");
	return buf;
}

/*
 * Who a multicast actually reaches, resolved the way the engine resolves it:
 * the connected clients, tested against the message's position with the same
 * visibility set the multicast type names. Engine-less clients are left out
 * because the engine's own multicast walks svs.clients, which has no entry for
 * them -- a bot is not a recipient of anything, and a record that pretended
 * otherwise would be the same fiction this layer exists to avoid.
 */
static char *SG_EvMulticastWho(vec3_t origin, multicast_t to)
{
	static char	buf[512];
	int		i, len = 0, n = 0;
	qboolean	usepvs, usephs;

	if (to == MULTICAST_ALL || to == MULTICAST_ALL_R)
		return "all";

	usepvs = (to == MULTICAST_PVS || to == MULTICAST_PVS_R);
	usephs = (to == MULTICAST_PHS || to == MULTICAST_PHS_R);

	buf[0] = '\0';
	for (i = 0; i < game.maxclients; i++)
	{
		edict_t	*e = g_edicts + 1 + i;
		int	room, w;

		if (!e->inuse || !e->client || !e->client->pers.connected)
			continue;
		if (SG_IsEngineless(e))
			continue;
		if (origin)
		{
			if (usepvs && !sg_engine.inPVS(origin, e->s.origin))
				continue;
			if (usephs && !sg_engine.inPHS(origin, e->s.origin))
				continue;
		}

		/*
		 * snprintf reports the length it WANTED, not the length it wrote, so
		 * the return value is only safe to add to len after it has been shown
		 * to have fit. On a short buffer, back out the partial entry and stop
		 * with a whole list rather than a torn one.
		 */
		room = (int)sizeof(buf) - len;
		w = snprintf(buf + len, (size_t)room, "%s%i:%s",
		             n ? "," : "", (int)(e - g_edicts),
		             e->client->pers.netname[0]
		                 ? e->client->pers.netname : "?");
		if (w < 0 || w >= room)
		{
			buf[len] = '\0';
			break;
		}
		len += w;
		n++;
	}
	if (!n)
		return "none";
	return buf;
}

/*
 * A print, made safe to put on one whitespace-delimited line. The text is the
 * last field, so interior spaces are fine; newlines and tabs are not.
 */
static char *SG_EvText(char *s)
{
	static char	buf[SG_PRINT_MAX];
	int		i, n = 0;

	for (i = 0; s[i] && n < (int)sizeof(buf) - 1; i++)
		buf[n++] = (s[i] == '\n' || s[i] == '\r' || s[i] == '\t')
		           ? ' ' : s[i];
	while (n > 0 && buf[n - 1] == ' ')
		n--;
	buf[n] = '\0';
	return buf;
}

/* ------------------------------------------------------- console prints */

/*
 * All three print wrappers hand the engine the FINISHED text under a "%s",
 * never the caller's format string. Player names and chat lines flow through
 * here; a name containing a percent sequence must not be handed to the
 * engine's own vsprintf for a second expansion.
 */

static void SG_bprintf(int printlevel, char *fmt, ...)
{
	char	buf[SG_PRINT_MAX];
	va_list	argptr;

	/*
	 * No filtering, ever. Broadcasts are resolved by the engine walking its
	 * own client list, which does not contain bots, so they are inherently
	 * safe with bots on the server. This wrapper exists only to do the
	 * formatting itself.
	 */
	va_start(argptr, fmt);
	vsnprintf(buf, sizeof(buf), fmt, argptr);
	va_end(argptr);

	if (SG_EvOn())
		SG_EvLine("all", "bprint", SG_EvText(buf));

	sg_engine.bprintf(printlevel, "%s", buf);
}

/*
 * The two per-recipient print paths share a shape: when the recipient has no
 * engine side, the wrapper returns without formatting -- unless the recorder
 * is on, in which case the text has to be produced to be recorded, and the
 * event is filed as DROPPED. Either way the engine is not called, which is
 * the suppression this layer exists for.
 */
static void SG_cprintf(edict_t *ent, int printlevel, char *fmt, ...)
{
	char		buf[SG_PRINT_MAX];
	va_list		argptr;
	qboolean	drop = SG_IsEngineless(ent);

	if (drop && !SG_EvOn())
		return;

	va_start(argptr, fmt);
	vsnprintf(buf, sizeof(buf), fmt, argptr);
	va_end(argptr);

	if (SG_EvOn())
		SG_EvLine(SG_EvWho(ent), drop ? "DROPPED" : "cprint",
		          SG_EvText(buf));
	if (drop)
		return;

	sg_engine.cprintf(ent, printlevel, "%s", buf);
}

static void SG_centerprintf(edict_t *ent, char *fmt, ...)
{
	char		buf[SG_PRINT_MAX];
	va_list		argptr;
	qboolean	drop = SG_IsEngineless(ent);

	if (drop && !SG_EvOn())
		return;

	va_start(argptr, fmt);
	vsnprintf(buf, sizeof(buf), fmt, argptr);
	va_end(argptr);

	if (SG_EvOn())
		SG_EvLine(SG_EvWho(ent), drop ? "DROPPED" : "centerprint",
		          SG_EvText(buf));
	if (drop)
		return;

	sg_engine.centerprintf(ent, "%s", buf);
}

/* ------------------------------------------------- the staging buffer */

/* bound at the engine's own transmit limit (MAX_MSGLEN): a guard sized
 * larger than the buffer it protects would pass 1401-2048 byte messages
 * straight into the engine's error path */
#define SG_NETMSG_MAX	1400

static byte	sg_netmsg[SG_NETMSG_MAX];
static int	sg_netmsg_len;
static qboolean	sg_netmsg_over;

static void SG_NetReset(void)
{
	sg_netmsg_len = 0;
	sg_netmsg_over = false;
}

/*
 * The engine's own message buffer detects overflow and discards the message
 * rather than writing past the end; this one used to do neither. Nothing in
 * the mod comes close today -- the scoreboard layout is the largest producer
 * at roughly 1.5 KB -- but "nothing today" is not a bound, and the writers
 * are reached from every temp entity in the game. Match the engine: refuse
 * the append, remember that the message is ruined, and drop the whole thing
 * at flush time so a half-message never reaches a client.
 */
static qboolean SG_NetRoom(int need, char *where)
{
	if (sg_netmsg_over)
		return false;
	if (sg_netmsg_len + need > SG_NETMSG_MAX)
	{
		sg_netmsg_over = true;
		sg_engine.dprintf("SG_Net: %s overflowed the %i-byte staging buffer "
		                  "(%i used, %i more asked); message discarded.\n",
		                  where, SG_NETMSG_MAX, sg_netmsg_len, need);
		return false;
	}
	return true;
}

static void SG_NetPut(int b)
{
	sg_netmsg[sg_netmsg_len++] = (byte)b;
}

static void SG_NetFlush(void)
{
	int	i;

	for (i = 0; i < sg_netmsg_len; i++)
		sg_engine.WriteByte(sg_netmsg[i]);
}

/* A staged binary message: the opcode it opens with, and how long it is. */
static void SG_EvBinary(char *recipient, char *kind)
{
	char	op[16], size[16];

	if (sg_netmsg_len <= 0)
		return;
	if (!kind)
	{
		snprintf(op, sizeof(op), "op%i", sg_netmsg[0]);
		kind = op;
	}
	snprintf(size, sizeof(size), "%i", sg_netmsg_len);
	SG_EvLine(recipient, kind, size);
}

/* ------------------------------------------------------------ the writers
 *
 * Where the stock engine's writers call Com_Error on a range violation,
 * these substitute a value and keep going. That tolerance is load-bearing:
 * the laser-target thinker writes an entity skin number as a byte and that
 * field is a full-width integer, so stock Quake II would kill the server
 * over a cosmetic value. It is kept.
 *
 * What is NOT kept is the silence. Two of the three substitutions used to
 * happen with no output at all, which meant a laser quietly rendering with
 * skin byte zero and no way to find out why. They now say so in the server
 * log, capped, because an uncapped per-write print in this file is how the
 * console got flooded before (see the unicast drop below).
 */

#define SG_RANGE_REPORTS	8

static int	sg_char_said, sg_byte_said, sg_short_said;

static void SG_RangeNote(int *said, char *what, int value)
{
	if (*said >= SG_RANGE_REPORTS)
		return;
	(*said)++;
	sg_engine.dprintf("SG_Net: %s value %i out of range, substituting 0.\n",
	                  what, value);
	if (*said == SG_RANGE_REPORTS)
		sg_engine.dprintf("SG_Net: further %s range reports suppressed.\n",
		                  what);
}

static void SG_WriteChar(int c)
{
	if (c < -128 || c > 127)
	{
		/*
		 * Unlike the byte and short writers this one does not substitute:
		 * it warns and writes the low eight bits, which for an in-range
		 * negative is byte-identical to what the engine's char writer
		 * produces anyway. (The legacy line lacked a newline "for the
		 * scrapers" -- but the identifier in it changed anyway, so no
		 * scraper survives; the newline is back.)
		 */
		if (sg_char_said < SG_RANGE_REPORTS)
		{
			sg_char_said++;
			sg_engine.dprintf("WARNING: SG_WriteChar: range error\n");
			if (sg_char_said == SG_RANGE_REPORTS)
				sg_engine.dprintf("SG_Net: further WriteChar range reports "
				                  "suppressed.\n");
		}
	}
	if (!SG_NetRoom(1, "WriteChar"))
		return;
	SG_NetPut(c & 0xff);
}

static void SG_WriteByte(int c)
{
	if (c < 0 || c > 255)
	{
		SG_RangeNote(&sg_byte_said, "WriteByte", c);
		c = 0;      /* zero, not (c & 255) -- see the header comment */
	}
	if (!SG_NetRoom(1, "WriteByte"))
		return;
	SG_NetPut(c);
}

static void SG_WriteShort(int c)
{
	if (c < -32768 || c > 32767)
	{
		SG_RangeNote(&sg_short_said, "WriteShort", c);
		c = 0;
	}
	if (!SG_NetRoom(2, "WriteShort"))
		return;
	SG_NetPut(c & 0xff);
	SG_NetPut((c >> 8) & 0xff);
}

static void SG_WriteLong(int c)
{
	if (!SG_NetRoom(4, "WriteLong"))
		return;
	SG_NetPut(c & 0xff);
	SG_NetPut((c >> 8) & 0xff);
	SG_NetPut((c >> 16) & 0xff);
	SG_NetPut((c >> 24) & 0xff);
}

static void SG_WriteFloat(float f)
{
	byte	b[4];

	/*
	 * A copy, not a type-punned pointer: the mod's byte-swap helpers are
	 * compiled out (the whole swap block in q_shared.c sits inside #if 0)
	 * and every target here is little-endian, so the four bytes go out as
	 * they sit in memory.
	 */
	memcpy(b, &f, sizeof(b));
	if (!SG_NetRoom(4, "WriteFloat"))
		return;
	SG_NetPut(b[0]);
	SG_NetPut(b[1]);
	SG_NetPut(b[2]);
	SG_NetPut(b[3]);
}

static void SG_WriteString(char *s)
{
	int	len;

	if (!s)
	{
		if (!SG_NetRoom(1, "WriteString"))
			return;
		SG_NetPut(0);
		return;
	}
	len = (int)strlen(s);
	if (!SG_NetRoom(len + 1, "WriteString"))
		return;
	memcpy(sg_netmsg + sg_netmsg_len, s, (size_t)len);
	sg_netmsg_len += len;
	SG_NetPut(0);
}

static void SG_WritePosition(vec3_t pos)
{
	/*
	 * Eighth-unit fixed point, truncated toward zero, through the short
	 * writer -- so a coordinate whose eighth-units leave short range
	 * collapses to origin on that axis instead of erroring.
	 */
	SG_WriteShort((int)(pos[0] * 8));
	SG_WriteShort((int)(pos[1] * 8));
	SG_WriteShort((int)(pos[2] * 8));
}

#define SG_VERTEXNORMALS	162

static vec3_t sg_bytedirs[SG_VERTEXNORMALS] =
{
#include "anorms.h"
};

static void SG_WriteDir(vec3_t dir)
{
	int	i, best;
	float	d, bestd;
	vec3_t	n;

	if (!dir)
	{
		if (!SG_NetRoom(1, "WriteDir"))
			return;
		SG_NetPut(0);
		return;
	}

	VectorCopy(dir, n);
	VectorNormalize(n);

	/*
	 * Nearest of the 162 standard normals. The comparison is strictly
	 * greater from index 0 upward, so ties go to the lowest index, and the
	 * running best starts below any real dot product so a zero vector lands
	 * on index 0 rather than on whatever happens to be first past -1.
	 */
	best = 0;
	bestd = -999999.0f;
	for (i = 0; i < SG_VERTEXNORMALS; i++)
	{
		d = DotProduct(n, sg_bytedirs[i]);
		if (d > bestd)
		{
			bestd = d;
			best = i;
		}
	}
	SG_WriteByte(best);
}

static void SG_WriteAngle(float f)
{
	/*
	 * INTEGER FIRST, and that is not a typo.
	 *
	 * The engine computes (int)(f * 256 / 360) & 255 -- float multiply and
	 * divide, then truncate. This computes ((int)f * 256) / 360 & 255 --
	 * truncate, then integer multiply and divide. The two disagree whenever
	 * f has a fractional part large enough to have carried the float form
	 * across an integer boundary: at f = 1.5 the engine writes 1 and this
	 * writes 0. One byte step is 360/256 = 1.40625 degrees.
	 *
	 * Every angle the mod puts on the wire carries that difference, and it
	 * has for as long as this layer has existed. It is preserved here on
	 * purpose so that "demo streams are byte-identical" stays available as
	 * the acceptance test for this rewrite -- nothing reads these angles
	 * back, they are display-only, so switching to the engine's form is a
	 * safe change but it belongs in its own commit where the only thing that
	 * moved is the angle bytes.
	 */
	SG_WriteByte((((int)f * 256) / 360) & 255);
}

/* ------------------------------------------------------------ the flushes */

static void SG_multicast(vec3_t origin, multicast_t to)
{
	if (sg_netmsg_over)
	{
		if (SG_EvOn())
			SG_EvLine("-", "OVERFLOW", "multicast");
		SG_NetReset();
		return;
	}
	if (SG_EvOn())
		SG_EvBinary(SG_EvMulticastWho(origin, to), NULL);
	SG_NetFlush();
	sg_engine.multicast(origin, to);
	SG_NetReset();
}

static void SG_unicast(edict_t *ent, qboolean reliable)
{
	if (SG_IsEngineless(ent))
	{
		/*
		 * Throw the staged message away and say nothing about it. The reset
		 * is the whole point and is not optional: leave the bytes staged and
		 * they are prepended to the next real client's message and corrupt
		 * it. And it must be SILENT -- LMCTF unicasts a layout to every
		 * showing-scores client every 32nd frame and again on every death
		 * and intermission, so a print on this path is a print several times
		 * a second per bot.
		 *
		 * The recorder is the one place that retraction becomes visible, and
		 * it is a file rather than the console for exactly the reason above.
		 */
		if (SG_EvOn())
			SG_EvBinary(SG_EvWho(ent), "DROPPED");
		SG_NetReset();
		return;
	}
	if (sg_netmsg_over)
	{
		if (SG_EvOn())
			SG_EvLine(SG_EvWho(ent), "OVERFLOW", "unicast");
		SG_NetReset();
		return;
	}
	if (SG_EvOn())
		SG_EvBinary(SG_EvWho(ent), NULL);
	SG_NetFlush();
	sg_engine.unicast(ent, reliable);
	SG_NetReset();
}

/* ------------------------------------------------- the argument shim
 *
 * This is what lets bot code put a command line into the same path a human's
 * typed command takes. While an injected command is in flight the three
 * argument readers answer from the pending vector instead of the engine's
 * last-parsed console line; outside that window they are pure passthrough.
 */

#define SG_MAX_CMDARGS	20
#define SG_CMDTAIL_MAX	150

static char	*sg_cmdargv[SG_MAX_CMDARGS];
static char	sg_cmdtail[SG_CMDTAIL_MAX];

void SG_ClearBotArgs(void)
{
	memset(sg_cmdargv, 0, sizeof(sg_cmdargv));
}

static int SG_argc(void)
{
	int	n;

	for (n = 0; n < SG_MAX_CMDARGS; n++)
		if (!sg_cmdargv[n])
			break;
	if (n)
		return n;
	return sg_engine.argc();
}

static char *SG_argv(int n)
{
	/*
	 * The bound comes first. The index is the caller's and used to reach
	 * straight into a twenty-slot array; nothing in the tree asks past 2
	 * today (g_cmds.c's give and fov handlers), but "no caller does" is not
	 * a bound either.
	 */
	if (n >= 0 && n < SG_MAX_CMDARGS && sg_cmdargv[n])
		return sg_cmdargv[n];
	return sg_engine.argv(n);
}

static char *SG_args(void)
{
	/*
	 * Gated on slot 1, not slot 0, and that asymmetry with argc is the
	 * existing behavior: a command injected with argv(0) and no tail reports
	 * argc 1 from the shim but takes args() from the engine. No caller
	 * injects a tail-less command, so it is unobservable; it is kept rather
	 * than silently changed.
	 */
	if (sg_cmdargv[1])
		return sg_cmdtail;
	return sg_engine.args();
}

/*
 * Arguments 1..n joined with exactly one space, no leading or trailing one.
 * Bounded, unlike the concatenation this replaces: today's longest possible
 * tail is a 95-character chat line (SG_CHAT_LINE in sg_chat.c) and 150 bytes
 * has room for it, but the writer had no idea how big the buffer was.
 */
static void SG_BuildTail(void)
{
	int	n, len, piece;

	len = 0;
	sg_cmdtail[0] = '\0';
	for (n = 1; n < SG_MAX_CMDARGS && sg_cmdargv[n]; n++)
	{
		if (n > 1 && len < SG_CMDTAIL_MAX - 1)
			sg_cmdtail[len++] = ' ';

		piece = (int)strlen(sg_cmdargv[n]);
		if (piece > SG_CMDTAIL_MAX - 1 - len)
			piece = SG_CMDTAIL_MAX - 1 - len;
		if (piece > 0)
		{
			memcpy(sg_cmdtail + len, sg_cmdargv[n], (size_t)piece);
			len += piece;
		}
		sg_cmdtail[len] = '\0';
	}
}

void SG_BotClientCommand(int clientIndex, char *arg0, ...)
{
	va_list	argptr;
	char	*s;
	int	n;
	edict_t	*ent;

	SG_ClearBotArgs();
	sg_cmdargv[0] = arg0;

	va_start(argptr, arg0);
	for (n = 1; ; n++)
	{
		if (n >= SG_MAX_CMDARGS)
		{
			va_end(argptr);
			sg_engine.error("SG_BotClientCommand: too many arguments");
			return;     /* error never returns; this keeps gcc quiet */
		}
		s = va_arg(argptr, char *);
		if (!s)
			break;
		sg_cmdargv[n] = s;
	}
	va_end(argptr);

	SG_BuildTail();

	/*
	 * Client indices are 0-based and edict 0 is the world. Guarded because
	 * an index off the end would hand ClientCommand a pointer into whatever
	 * follows the client edicts, and every caller of this already knows its
	 * own client number -- if one has it wrong, dropping the line is the
	 * cheap failure.
	 */
	if (clientIndex < 0 || clientIndex >= game.maxclients)
	{
		SG_ClearBotArgs();
		return;
	}
	ent = g_edicts + 1 + clientIndex;

	ClientCommand(ent);

	SG_ClearBotArgs();
}

/* --------------------------------------------------------------- sounds
 *
 * These two are pure taps and must stay that way. The engine is handed the
 * call FIRST and verbatim -- same arguments, same order, same everything --
 * so nothing about what a human client hears changes by a byte. Only after
 * the sound is really on its way does the ear get told it happened.
 *
 * They are wrapped for one reason: a sound is the only honest evidence a bot
 * has that something is going on where it cannot see. Reading a player's
 * weaponstate to guess at it, which is what CACO used to do, is knowledge no
 * player has and it arrived with an exactness no sound carries.
 */

static void SG_sound(edict_t *ent, int channel, int soundindex, float volume,
                     float attenuation, float timeofs)
{
	sg_engine.sound(ent, channel, soundindex, volume, attenuation, timeofs);
	SG_NoteSound(ent, NULL, channel, soundindex, volume, attenuation);
}

static void SG_positioned_sound(vec3_t origin, edict_t *ent, int channel,
                                int soundindex, float volume,
                                float attenuation, float timeofs)
{
	sg_engine.positioned_sound(origin, ent, channel, soundindex, volume,
	                           attenuation, timeofs);
	SG_NoteSound(ent, origin, channel, soundindex, volume, attenuation);
}

/* --------------------------------------------------- client slot handling */

/*
 * Take a client edict, scanning the client range from the TOP down.
 *
 * The direction is load-bearing and cost a round of "a human joined and
 * became a bot" to learn. The engine seats real connecting players from the
 * low end of the client range and does not consult ent->inuse when it picks,
 * so a bot parked in a low slot can simply be overwritten by the next person
 * who connects. Filling downward keeps the game-side clients at the top of
 * the range and leaves the bottom, where the engine is going to write
 * anyway, for people.
 *
 * Note what this does NOT do: it does not clear the gclient_t. Persistent
 * and respawn state from the slot's last occupant survives untouched. The
 * caller's connect sequence is what initializes it, and it is written that
 * way on purpose -- see SG_AddBotTeam, which writes the team number into the
 * client while inuse is still false.
 */
edict_t *SG_SpawnClientEdict(void)
{
	int	i;
	edict_t	*e;

	for (i = game.maxclients - 1; i >= 0; i--)
	{
		e = g_edicts + 1 + i;
		if (e->inuse)
			continue;

		memset(e, 0, sizeof(*e));
		G_InitEdict(e);
		e->client = game.clients + ((e - g_edicts) - 1);
		return e;
	}
	return NULL;
}

/*
 * The idempotent tail of a release. The caller runs the mod's
 * ClientDisconnect first, which has already broadcast the logout flash,
 * unlinked the entity and zeroed the model index and solidity -- so this
 * overlaps it deliberately and adds the two things ClientDisconnect does not
 * do: park the class name and clear the connected flag.
 *
 * It has to survive being called on an edict that was spawned but never
 * finished connecting, because that is exactly the failure path in
 * SG_AddBotTeam when ClientConnect refuses. No unlink here (the entity was
 * never linked on that path).
 *
 * FL_BOT is CLEARED, deliberately (refutation review, 2026-08-05): the
 * flag is what SG_IsEngineless reads, and the engine assigns client slots
 * without consulting ent->inuse -- so a human who lands on an edict a
 * departed bot once wore would inherit the flag and silently lose every
 * cprintf, centerprint and unicast addressed to them. A freed slot must
 * carry nothing that outlives its owner.
 */
void SG_FreeClientEdict(edict_t *ent)
{
	ent->s.modelindex = 0;
	ent->solid = SOLID_NOT;
	ent->inuse = false;
	ent->classname = "disconnected";
	ent->client->pers.connected = false;
	/* Fake-client CTF identity is not a reconnectable human identity. game.clients
	 * survives map changes, so leave no team/radio state for the next occupant. */
	memset(&ent->client->ctf, 0, sizeof(ent->client->ctf));
	ent->flags &= ~FL_BOT;
}

/*
 * Called at every level spawn so the capped range reports start over --
 * a lifetime cap on a long-running server means a fault that develops on
 * map twelve is invisible (refutation review, 2026-08-05).
 */
void SG_NetNewLevel(void)
{
	sg_char_said = sg_byte_said = sg_short_said = 0;

	/*
	 * SpawnEntities calls this BEFORE it clears `level`, so level.framenum
	 * here is still the OUTGOING level's last frame. Accumulating it makes
	 * sg_ev_base "frames since the server started", which is what tags the
	 * next log file -- level.framenum itself restarts at zero every map and
	 * would collide on every revisit.
	 *
	 * The file is closed here and opened lazily for the same ordering reason:
	 * level.mapname does not name the new map until after this returns.
	 */
	sg_ev_base += level.framenum;
	if (sg_ev_file)
	{
		fclose(sg_ev_file);
		sg_ev_file = NULL;
	}
	sg_ev_tried = false;
}

/* --------------------------------------------------------- the install */

void SG_NetInstall(void)
{
	/*
	 * Idempotence, done by inspection rather than a static flag
	 * (refutation review, 2026-08-05): the caller re-copies the engine's
	 * table into gi immediately before calling us, so a repeated install
	 * is harmless -- UNLESS gi already holds our own functions, which is
	 * the only state that could feed the shim to itself. A static guard
	 * gets this wrong across library reloads that preserve statics
	 * (sanitizer builds use RTLD_NODELETE): it would silently leave the
	 * whole layer uninstalled.
	 */
	if (gi.unicast == SG_unicast)
		return;
	sg_installed = true;

	sg_engine = gi;

	gi.bprintf       = SG_bprintf;
	gi.cprintf       = SG_cprintf;
	gi.centerprintf  = SG_centerprintf;

	gi.sound            = SG_sound;
	gi.positioned_sound = SG_positioned_sound;

	gi.multicast     = SG_multicast;
	gi.unicast       = SG_unicast;
	gi.WriteChar     = SG_WriteChar;
	gi.WriteByte     = SG_WriteByte;
	gi.WriteShort    = SG_WriteShort;
	gi.WriteLong     = SG_WriteLong;
	gi.WriteFloat    = SG_WriteFloat;
	gi.WriteString   = SG_WriteString;
	gi.WritePosition = SG_WritePosition;
	gi.WriteDir      = SG_WriteDir;
	gi.WriteAngle    = SG_WriteAngle;

	gi.argc          = SG_argc;
	gi.argv          = SG_argv;
	gi.args          = SG_args;

	SG_ClearBotArgs();
	SG_NetReset();
}
