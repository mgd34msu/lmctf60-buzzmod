#include "../g_local.h"

#include "sg_local.h"
#include "sg_bot.h"
#include "sg_bot_combat.h"
#include "sg_bot_host.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The engine's own functions, kept for the pass-through. */
void ClientCommand(edict_t *ent);

static game_import_t sg_engine;
static qboolean sg_installed;

static qboolean Bot(const edict_t *ent)
{
	return ent && (ent->flags & FL_BOT) != 0;
}

/* ---- staged messages ---------------------------------------------------------
 *
 * The game writes a message piece by piece, then sends it with unicast or
 * multicast.  The pieces are staged here and replayed to the engine at
 * the send, unless the send is a unicast to a bot: then they are dropped
 * and the engine never hears of the message. */

#define STAGE_MAX 4096

typedef enum piece_kind_e
{
	PIECE_CHAR, PIECE_BYTE, PIECE_SHORT, PIECE_LONG, PIECE_FLOAT, PIECE_STRING,
	PIECE_POSITION, PIECE_DIR, PIECE_ANGLE
} piece_kind_t;

typedef struct piece_s
{
	piece_kind_t kind;
	int number;
	float value;
	vec3_t vector;
	const char *string;       /* into sg_stage_text */
} piece_t;

static piece_t sg_stage[STAGE_MAX];
static int sg_stage_count;
static char sg_stage_text[STAGE_MAX * 4];
static int sg_stage_text_used;
static qboolean sg_stage_overflow;

static void StageReset(void)
{
	sg_stage_count = 0;
	sg_stage_text_used = 0;
	sg_stage_overflow = false;
}

static piece_t *Stage(piece_kind_t kind)
{
	piece_t *piece;

	if (sg_stage_count >= STAGE_MAX)
	{
		sg_stage_overflow = true;
		return NULL;
	}
	piece = &sg_stage[sg_stage_count++];
	memset(piece, 0, sizeof(*piece));
	piece->kind = kind;
	return piece;
}

static void Host_WriteChar(int c)
{
	piece_t *piece = Stage(PIECE_CHAR);

	if (piece)
		piece->number = c;
}

static void Host_WriteByte(int c)
{
	piece_t *piece = Stage(PIECE_BYTE);

	if (piece)
		piece->number = c;
}

static void Host_WriteShort(int c)
{
	piece_t *piece = Stage(PIECE_SHORT);

	if (piece)
		piece->number = c;
}

static void Host_WriteLong(int c)
{
	piece_t *piece = Stage(PIECE_LONG);

	if (piece)
		piece->number = c;
}

static void Host_WriteFloat(float f)
{
	piece_t *piece = Stage(PIECE_FLOAT);

	if (piece)
		piece->value = f;
}

static void Host_WriteString(char *s)
{
	piece_t *piece = Stage(PIECE_STRING);
	size_t length = s ? strlen(s) : 0U;

	if (!piece)
		return;
	if (sg_stage_text_used + (int)length + 1 > (int)sizeof(sg_stage_text))
	{
		sg_stage_overflow = true;
		piece->string = "";
		return;
	}
	memcpy(sg_stage_text + sg_stage_text_used, s ? s : "", length + 1U);
	piece->string = sg_stage_text + sg_stage_text_used;
	sg_stage_text_used += (int)length + 1;
}

static void Host_WritePosition(vec3_t pos)
{
	piece_t *piece = Stage(PIECE_POSITION);

	if (piece)
		VectorCopy(pos, piece->vector);
}

static void Host_WriteDir(vec3_t dir)
{
	piece_t *piece = Stage(PIECE_DIR);

	if (piece)
		VectorCopy(dir, piece->vector);
}

static void Host_WriteAngle(float f)
{
	piece_t *piece = Stage(PIECE_ANGLE);

	if (piece)
		piece->value = f;
}

static void Replay(void)
{
	int i;

	for (i = 0; i < sg_stage_count; i++)
	{
		piece_t *piece = &sg_stage[i];

		switch (piece->kind)
		{
		case PIECE_CHAR: sg_engine.WriteChar(piece->number); break;
		case PIECE_BYTE: sg_engine.WriteByte(piece->number); break;
		case PIECE_SHORT: sg_engine.WriteShort(piece->number); break;
		case PIECE_LONG: sg_engine.WriteLong(piece->number); break;
		case PIECE_FLOAT: sg_engine.WriteFloat(piece->value); break;
		case PIECE_STRING: sg_engine.WriteString((char *)piece->string); break;
		case PIECE_POSITION: sg_engine.WritePosition(piece->vector); break;
		case PIECE_DIR: sg_engine.WriteDir(piece->vector); break;
		case PIECE_ANGLE: sg_engine.WriteAngle(piece->value); break;
		}
	}
}

static void Host_multicast(vec3_t origin, multicast_t to)
{
	if (!sg_stage_overflow)
	{
		Replay();
		sg_engine.multicast(origin, to);
	}
	StageReset();
}

static void Host_unicast(edict_t *ent, qboolean reliable)
{
	if (!Bot(ent) && !sg_stage_overflow)
	{
		Replay();
		sg_engine.unicast(ent, reliable);
	}
	StageReset();
}

/* ---- prints -------------------------------------------------------------------- */

#define PRINT_MAX 2048

static void Host_bprintf(int printlevel, char *fmt, ...)
{
	char text[PRINT_MAX];
	va_list args;

	va_start(args, fmt);
	vsnprintf(text, sizeof(text), fmt, args);
	va_end(args);
	sg_engine.bprintf(printlevel, "%s", text);
}

static void Host_cprintf(edict_t *ent, int printlevel, char *fmt, ...)
{
	char text[PRINT_MAX];
	va_list args;

	if (Bot(ent))
		return;
	va_start(args, fmt);
	vsnprintf(text, sizeof(text), fmt, args);
	va_end(args);
	sg_engine.cprintf(ent, printlevel, "%s", text);
}

static void Host_centerprintf(edict_t *ent, char *fmt, ...)
{
	char text[PRINT_MAX];
	va_list args;

	if (Bot(ent))
		return;
	va_start(args, fmt);
	vsnprintf(text, sizeof(text), fmt, args);
	va_end(args);
	sg_engine.centerprintf(ent, "%s", text);
}

/* ---- sounds -------------------------------------------------------------------- */

static void Host_sound(edict_t *ent, int channel, int soundindex, float volume,
	float attenuation, float timeofs)
{
	if (ent)
		SG_NoteSound(ent, ent->s.origin, channel, soundindex, volume, attenuation);
	sg_engine.sound(ent, channel, soundindex, volume, attenuation, timeofs);
}

static void Host_positioned_sound(vec3_t origin, edict_t *ent, int channel,
	int soundindex, float volume, float attenuation, float timeofs)
{
	SG_NoteSound(ent, origin, channel, soundindex, volume, attenuation);
	sg_engine.positioned_sound(origin, ent, channel, soundindex, volume,
		attenuation, timeofs);
}

/* ---- a bot's command ------------------------------------------------------------ */

#define ARGS_MAX 20
#define TAIL_MAX 256

static char *sg_args[ARGS_MAX];
static char sg_tail[TAIL_MAX];

void SG_BotHostClearArgs(void)
{
	memset(sg_args, 0, sizeof(sg_args));
	sg_tail[0] = 0;
}

static int Host_argc(void)
{
	int n;

	for (n = 0; n < ARGS_MAX && sg_args[n]; n++)
		;
	return n ? n : sg_engine.argc();
}

static char *Host_argv(int n)
{
	if (n >= 0 && n < ARGS_MAX && sg_args[n])
		return sg_args[n];
	return sg_args[0] ? "" : sg_engine.argv(n);
}

static char *Host_args(void)
{
	return sg_args[0] ? sg_tail : sg_engine.args();
}

void SG_BotHostCommand(int client_index, char *arg0, ...)
{
	va_list args;
	int n, used = 0;
	edict_t *ent;

	if (client_index < 0 || client_index >= game.maxclients || !arg0)
		return;
	SG_BotHostClearArgs();
	sg_args[0] = arg0;
	va_start(args, arg0);
	for (n = 1; n < ARGS_MAX; n++)
	{
		char *piece = va_arg(args, char *);

		if (!piece)
			break;
		sg_args[n] = piece;
	}
	va_end(args);
	/* The tail: arguments after the first, one space between. */
	for (n = 1; n < ARGS_MAX && sg_args[n]; n++)
	{
		int length = (int)strlen(sg_args[n]);

		if (n > 1 && used < TAIL_MAX - 1)
			sg_tail[used++] = ' ';
		if (length > TAIL_MAX - 1 - used)
			length = TAIL_MAX - 1 - used;
		if (length > 0)
		{
			memcpy(sg_tail + used, sg_args[n], (size_t)length);
			used += length;
		}
		sg_tail[used] = 0;
	}
	ent = g_edicts + 1 + client_index;
	if (ent->inuse && ent->client)
		ClientCommand(ent);
	SG_BotHostClearArgs();
}

/* ---- client slots --------------------------------------------------------------- */

edict_t *SG_BotHostSpawnClient(void)
{
	int i;

	/* The highest free slot: humans connect from the low end. */
	for (i = game.maxclients - 1; i >= 0; i--)
	{
		edict_t *e = g_edicts + 1 + i;

		if (e->inuse)
			continue;
		memset(e, 0, sizeof(*e));
		G_InitEdict(e);
		e->client = game.clients + i;
		return e;
	}
	return NULL;
}

void SG_BotHostFreeClient(edict_t *ent)
{
	if (!ent || !ent->client)
		return;
	ent->s.modelindex = 0;
	ent->solid = SOLID_NOT;
	ent->inuse = false;
	ent->classname = "disconnected";
	ent->client->pers.connected = false;
	/* The client record outlives the level: leave no team for whoever
	 * takes the slot next. */
	memset(&ent->client->ctf, 0, sizeof(ent->client->ctf));
	ent->flags &= ~FL_BOT;
}

/* ---- install ------------------------------------------------------------------------ */

void SG_BotHostNewLevel(void)
{
	StageReset();
	SG_BotHostClearArgs();
}

void SG_BotHostInstall(void)
{
	if (sg_installed && gi.unicast == Host_unicast)
		return;
	sg_engine = gi;
	sg_installed = true;
	gi.bprintf = Host_bprintf;
	gi.cprintf = Host_cprintf;
	gi.centerprintf = Host_centerprintf;
	gi.sound = Host_sound;
	gi.positioned_sound = Host_positioned_sound;
	gi.multicast = Host_multicast;
	gi.unicast = Host_unicast;
	gi.WriteChar = Host_WriteChar;
	gi.WriteByte = Host_WriteByte;
	gi.WriteShort = Host_WriteShort;
	gi.WriteLong = Host_WriteLong;
	gi.WriteFloat = Host_WriteFloat;
	gi.WriteString = Host_WriteString;
	gi.WritePosition = Host_WritePosition;
	gi.WriteDir = Host_WriteDir;
	gi.WriteAngle = Host_WriteAngle;
	gi.argc = Host_argc;
	gi.argv = Host_argv;
	gi.args = Host_args;
	StageReset();
	SG_BotHostClearArgs();
}
