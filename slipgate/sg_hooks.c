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

void SG_HooksInit(void)
{
	if (sg_host.dprint)
		return;

	sg_host.dprint = Host_Dprint;
	sg_host.cprint = Host_Cprint;
	sg_host.bprint = Host_Bprint;
	sg_host.trace = Host_Trace;
	sg_host.pointcontents = Host_PointContents;
	sg_host.in_pvs = Host_InPVS;
	sg_host.in_phs = Host_InPHS;
	sg_host.pmove = Host_Pmove;
	sg_host.level_alloc = Host_LevelAlloc;
	sg_host.level_free = Host_LevelFree;
	sg_host.cvar = Host_Cvar;
	sg_host.argv = Host_Argv;
	sg_host.sound = Host_Sound;
	sg_host.soundindex = Host_SoundIndex;
}
