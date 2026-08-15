// ui_text.h -- the bounded appender
//
// Ten producers in this tree build wire-limited text -- layout
// strings and centerprints that ride the 1400-byte MAX_MSGLEN no-
// fragmentation ceiling documented in docs/LAYOUT.md. An oversized
// multicast layout is a fatal server crash; an oversized unicast one
// just silently drops the whole frame, so every producer needs a
// guard. Five different hand-rolled guard styles grew up around that
// one requirement -- some checking strlen() before every strcat(),
// some copying into an oversized scratch buffer and hoping, and two
// producers (the menu system, the MOTD screen) had no guard at all
// until a length check was bolted on ad hoc at a fixed 1380. None of
// them record whether they actually dropped anything; the truncation
// is silent either way.
//
// ui_buf_t is the one guard, done once: ui_appendf() refuses to write
// past the buffer's declared size and marks .truncated instead of
// overflowing or guessing, so a caller can log or react to the
// dropped tail instead of it vanishing unnoticed.

#ifndef UI_TEXT_H
#define UI_TEXT_H

// Needs qboolean and q_printf_fmt from q_shared.h, but does not
// #include it: q_shared.h has no include guard and this codebase's
// convention is every module includes it exactly once (see
// g_local.h) -- a second #include here would redeclare every enum in
// it and fail the build. Include this header after q_shared.h.

typedef struct
{
	char		*buf;		// caller-owned storage, NUL-terminated after every call
	int			size;		// total capacity of buf, including the terminating NUL
	int			used;		// bytes written so far, not counting the terminating NUL
	qboolean	truncated;	// set once an append didn't fit; sticky for the buffer's life
} ui_buf_t;

// Binds b to caller-owned storage. Does not take ownership -- storage
// must outlive b. Leaves the buffer empty ("") and untruncated.
void ui_buf_init (ui_buf_t *b, char *storage, int size);

// Formats fmt/... and appends the result to b, exactly like strcat
// would, except: if the formatted text doesn't fit in the remaining
// space, nothing is written, b->truncated is set, and false is
// returned. Once truncated, every further call is a no-op that
// returns false -- callers don't need to check before every append,
// only decide once whether they care that some was dropped.
qboolean ui_appendf (ui_buf_t *b, const char *fmt, ...) q_printf_fmt(2, 3);

// Bytes still available for another append (capacity minus what's
// used minus the reserved terminating NUL). Never negative.
int ui_remaining (const ui_buf_t *b);

#endif // UI_TEXT_H
