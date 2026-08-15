// ui_text.c -- the bounded appender (see ui_text.h for what problem this solves)

#include "g_local.h"
#include "ui_text.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

void ui_buf_init (ui_buf_t *b, char *storage, int size)
{
	b->buf = storage;
	b->size = size;
	b->used = 0;
	b->truncated = false;

	if (b->size > 0)
		b->buf[0] = 0;
}

qboolean ui_appendf (ui_buf_t *b, const char *fmt, ...)
{
	char	formatted[4096];
	va_list	argptr;
	int		len;

	if (!b || !b->buf || b->truncated)
		return false;

	va_start (argptr, fmt);
	len = vsnprintf (formatted, sizeof(formatted), fmt, argptr);
	va_end (argptr);

	// vsnprintf failed outright, or this one call's formatted text
	// alone overran our scratch buffer -- either way we don't have
	// the whole string to offer the caller's buffer.
	if (len < 0 || len >= (int)sizeof(formatted))
	{
		b->truncated = true;
		return false;
	}

	// +1 reserves the terminating NUL: ui_buf_t promises b->buf stays
	// a valid C string after every call, appended or refused.
	if (b->used + len + 1 > b->size)
	{
		b->truncated = true;
		return false;
	}

	memcpy (b->buf + b->used, formatted, len + 1);
	b->used += len;
	return true;
}

int ui_remaining (const ui_buf_t *b)
{
	int room;

	if (!b)
		return 0;

	room = b->size - b->used - 1;
	return room > 0 ? room : 0;
}
