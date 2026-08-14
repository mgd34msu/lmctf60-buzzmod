/*
 * sg_hooks.c -- the LMCTF filling of the host table (sg_hooks.h).
 *
 * The engine import block speaks older signatures (bare char pointers,
 * tagged allocation); the thin wrappers below adapt them to the table's
 * contract so the bot code sees one stable boundary. Formatting happens
 * here for the print channels because varargs cannot be forwarded raw.
 */
#include <stdarg.h>
#include <stdio.h>

#include "g_local.h"
#include "slipgate/sg_hooks.h"

sg_host_t sg_host;

static void Host_Dprint(const char *fmt, ...)
{
	char	buf[1024];
	va_list	ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	gi.dprintf("%s", buf);
}

static void Host_Cprint(edict_t *ent, int level, const char *fmt, ...)
{
	char	buf[1024];
	va_list	ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	gi.cprintf(ent, level, "%s", buf);
}

static void Host_Bprint(int level, const char *fmt, ...)
{
	char	buf[1024];
	va_list	ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	gi.bprintf(level, "%s", buf);
}

static trace_t Host_Trace(const vec3_t start, const vec3_t mins,
                          const vec3_t maxs, const vec3_t end,
                          edict_t *passent, int contentmask)
{
	return gi.trace((float *)start, (float *)mins, (float *)maxs,
	                (float *)end, passent, contentmask);
}

static int Host_PointContents(const vec3_t point)
{
	return gi.pointcontents((float *)point);
}

static int Host_BoxEdicts(const vec3_t mins, const vec3_t maxs,
	edict_t **list, int maxcount, int areatype)
{
	return gi.BoxEdicts((float *)mins, (float *)maxs, list, maxcount,
	                    areatype);
}

static qboolean Host_InPVS(const vec3_t p1, const vec3_t p2)
{
	return gi.inPVS((float *)p1, (float *)p2);
}

static qboolean Host_InPHS(const vec3_t p1, const vec3_t p2)
{
	return gi.inPHS((float *)p1, (float *)p2);
}

static void Host_Pmove(pmove_t *pm)
{
	gi.Pmove(pm);
}

static void *Host_LevelAlloc(int size)
{
	return gi.TagMalloc(size, TAG_LEVEL);
}

static void Host_LevelFree(void *block)
{
	gi.TagFree(block);
}

static cvar_t *Host_Cvar(const char *name, const char *value, int flags)
{
	return gi.cvar((char *)name, (char *)value, flags);
}

static char *Host_Argv(int n)
{
	return gi.argv(n);
}

static void Host_Sound(edict_t *ent, int channel, int soundindex,
                       float volume, float attenuation, float timeofs)
{
	gi.sound(ent, channel, soundindex, volume, attenuation, timeofs);
}

static int Host_SoundIndex(const char *name)
{
	return gi.soundindex((char *)name);
}

static void Host_PositionedSound(const vec3_t origin, edict_t *ent,
                                 int channel, int soundindex, float volume,
                                 float attenuation, float timeofs)
{
	gi.positioned_sound((float *)origin, ent, channel, soundindex,
	                    volume, attenuation, timeofs);
}

static void *Host_GameAlloc(int size)
{
	return gi.TagMalloc(size, TAG_GAME);
}

static void Host_LinkEntity(edict_t *ent)
{
	gi.linkentity(ent);
}

static void Host_SetModel(edict_t *ent, const char *name)
{
	gi.setmodel(ent, (char *)name);
}

static void Host_CenterPrint(edict_t *ent, const char *fmt, ...)
{
	char	buf[1024];
	va_list	ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	gi.centerprintf(ent, "%s", buf);
}

static int Host_Argc(void)
{
	return gi.argc();
}

static char *Host_Args(void)
{
	return gi.args();
}

static void Host_WriteChar(int c)      { gi.WriteChar(c); }
static void Host_WriteByte(int c)      { gi.WriteByte(c); }
static void Host_WriteShort(int c)     { gi.WriteShort(c); }
static void Host_WriteLong(int c)      { gi.WriteLong(c); }
static void Host_WriteFloat(float f)   { gi.WriteFloat(f); }
static void Host_WriteString(const char *s) { gi.WriteString((char *)s); }
static void Host_WritePosition(const vec3_t pos) { gi.WritePosition((float *)pos); }
static void Host_WriteDir(const vec3_t dir) { gi.WriteDir((float *)dir); }
static void Host_WriteAngle(float f)   { gi.WriteAngle(f); }

static void Host_Unicast(edict_t *ent, qboolean reliable)
{
	gi.unicast(ent, reliable);
}

static void Host_Multicast(const vec3_t origin, multicast_t to)
{
	gi.multicast((float *)origin, to);
}

void SG_HooksInit(void)
{
	if (sg_host.dprint)
		return;

	sg_host.dprint = Host_Dprint;
	sg_host.cprint = Host_Cprint;
	sg_host.bprint = Host_Bprint;
	sg_host.trace = Host_Trace;
	sg_host.pointcontents = Host_PointContents;
	sg_host.box_edicts = Host_BoxEdicts;
	sg_host.in_pvs = Host_InPVS;
	sg_host.in_phs = Host_InPHS;
	sg_host.pmove = Host_Pmove;
	sg_host.level_alloc = Host_LevelAlloc;
	sg_host.level_free = Host_LevelFree;
	sg_host.cvar = Host_Cvar;
	sg_host.argv = Host_Argv;
	sg_host.sound = Host_Sound;
	sg_host.positioned_sound = Host_PositionedSound;
	sg_host.soundindex = Host_SoundIndex;
	sg_host.game_alloc = Host_GameAlloc;
	sg_host.linkentity = Host_LinkEntity;
	sg_host.setmodel = Host_SetModel;
	sg_host.centerprint = Host_CenterPrint;
	sg_host.argc = Host_Argc;
	sg_host.args = Host_Args;
	sg_host.write_char = Host_WriteChar;
	sg_host.write_byte = Host_WriteByte;
	sg_host.write_short = Host_WriteShort;
	sg_host.write_long = Host_WriteLong;
	sg_host.write_float = Host_WriteFloat;
	sg_host.write_string = Host_WriteString;
	sg_host.write_position = Host_WritePosition;
	sg_host.write_dir = Host_WriteDir;
	sg_host.write_angle = Host_WriteAngle;
	sg_host.unicast = Host_Unicast;
	sg_host.multicast = Host_Multicast;
}
