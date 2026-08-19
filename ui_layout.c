// ui_layout.c -- the declarative screen layer (see ui_layout.h for
// what problem this solves and docs/layout-isa.md for the token
// vocabulary emitted below).

#include "g_local.h"
#include "ui_text.h"
#include "ui_layout.h"
#include <string.h>

static int UI_CompileOne (const ui_elem_t *e, ui_buf_t *out, qboolean allow_ifstat);

// -- atomic elements ---------------------------------------------------
//
// Each of these is exactly one ui_appendf() call. ui_appendf's own
// contract (ui_text.c: the fit check happens before any byte is
// copied) already makes a single call atomic -- it either lands
// whole or not at all -- so none of these need the checkpoint/
// rollback dance the compound elements (UI_IFSTAT, UI_TABLE) do.

static qboolean UI_EmitText (ui_buf_t *out, const ui_text_t *t)
{
	return ui_appendf (out, "xv %i yv %i %s \"%s\" ",
		t->x, t->y, t->highlight ? "string2" : "string", t->text);
}

static qboolean UI_EmitPic (ui_buf_t *out, const ui_pic_t *p)
{
	if (p->stat_driven)
		return ui_appendf (out, "xv %i yv %i pic %i ", p->x, p->y, p->image.stat);

	return ui_appendf (out, "xv %i yv %i picn %s ", p->x, p->y, p->image.name);
}

static qboolean UI_EmitStatnum (ui_buf_t *out, const ui_statnum_t *n)
{
	return ui_appendf (out, "xv %i yv %i num %i %i ", n->x, n->y, n->width, n->stat);
}

static qboolean UI_EmitStatstring (ui_buf_t *out, const ui_statstring_t *s)
{
	return ui_appendf (out, "xv %i yv %i stat_string %i ", s->x, s->y, s->stat);
}

// UI_RAW -- see ui_layout.h: no wrapper, the caller already formatted
// the complete token. Still one ui_appendf() call, so it is atomic
// the same way the other atomic elements are (ui_text.c: the fit
// check happens before any byte is copied).
static qboolean UI_EmitRaw (ui_buf_t *out, const ui_raw_t *r)
{
	return ui_appendf (out, "%s", r->text);
}

// -- UI_IFSTAT -----------------------------------------------------------
//
// "if <stat> " + every child + "endif ". Children compile with
// allow_ifstat == false, so a UI_IFSTAT nested anywhere inside this
// range (not just directly) is refused rather than emitted -- see
// ui_layout.h for why nesting breaks the client's skip-scan.
//
// Checkpointed as one unit: if any piece (the if token, a child, or
// the endif token) fails to fit, the whole thing is rolled back to
// nothing rather than leave an if with no endif sitting in the
// buffer.

static int UI_CompileIfstat (const ui_elem_t *e, ui_buf_t *out)
{
	const ui_ifstat_t	*ifs = &e->u.ifstat;
	int					saved_used;
	qboolean			saved_truncated;
	int					i;

	saved_used = out->used;
	saved_truncated = out->truncated;

	if (!ui_appendf (out, "if %i ", ifs->stat))
		goto rollback;

	for (i = 0; i < ifs->count; i++)
	{
		if (UI_CompileOne (&ifs->elems[i], out, false) != 0)
			goto rollback;
	}

	if (!ui_appendf (out, "endif "))
		goto rollback;

	return 0;

rollback:
	out->used = saved_used;
	out->buf[saved_used] = 0;
	out->truncated = saved_truncated;
	return 1;
}

// -- UI_TABLE -------------------------------------------------------------
//
// Row by row: fill_row() hands back already-formatted cell text, the
// compiler positions each column at (table.x + column.x_offset,
// table.y + row*row_dy) and emits it. A row is checkpointed as one
// unit -- every column lands or the row is rolled back whole, matching
// docs/LAYOUT.md's "never emit half an element" rule at row
// granularity. The moment a row doesn't fit, every row still queued
// after it is dropped too without being attempted: rows arrive pre-
// sorted best-first by every board converted so far, so "ran out of
// room" and "drop the lowest-priority (i.e. remaining) rows" are the
// same thing -- exactly the break-on-overflow loop the hand-written
// boards used.
//
// column.priority (ui_layout.h) is declared for a future table that
// needs to drop individual low-priority columns from an otherwise-too-
// wide row instead of dropping the whole row; no table converted so
// far has more than one column, so that path isn't implemented here --
// only whole-row atomicity is.

static int UI_CompileTable (const ui_elem_t *e, ui_buf_t *out)
{
	const ui_table_t	*t = &e->u.table;
	char				cells[UI_TABLE_MAX_COLUMNS][UI_CELL_LEN];
	int					row, col;
	int					dropped;
	int					saved_used;
	qboolean			saved_truncated;
	qboolean			row_ok;
	int					y;
	int					x;

	if (t->num_columns > UI_TABLE_MAX_COLUMNS)
	{
		gi.dprintf ("ui_layout: table asked for %d columns, max is %d -- dropping the whole table\n",
			t->num_columns, UI_TABLE_MAX_COLUMNS);
		return t->num_rows;
	}

	dropped = 0;

	for (row = 0; row < t->num_rows; row++)
	{
		saved_used = out->used;
		saved_truncated = out->truncated;
		y = t->y + row * t->row_dy;

		memset (cells, 0, sizeof(cells));
		t->fill_row (t->userdata, row, t->num_columns, cells);

		row_ok = true;
		for (col = 0; col < t->num_columns; col++)
		{
			x = t->x + t->columns[col].x_offset;

			if (!ui_appendf (out, "xv %i yv %i %s \"%s\" ",
					x, y, t->highlight ? "string2" : "string", cells[col]))
			{
				row_ok = false;
				break;
			}
		}

		if (!row_ok)
		{
			out->used = saved_used;
			out->buf[saved_used] = 0;
			out->truncated = saved_truncated;

			dropped += (t->num_rows - row);
			break;
		}
	}

	if (t->footer)
	{
		saved_used = out->used;
		saved_truncated = out->truncated;

		if (!ui_appendf (out, "xv %i yv %i %s \"%s\" ",
				t->footer_x, t->footer_y,
				t->footer_highlight ? "string2" : "string", t->footer))
		{
			out->used = saved_used;
			out->buf[saved_used] = 0;
			out->truncated = saved_truncated;
		}
	}

	return dropped;
}

// -- dispatch --------------------------------------------------------------

static int UI_CompileOne (const ui_elem_t *e, ui_buf_t *out, qboolean allow_ifstat)
{
	switch (e->kind)
	{
	case UI_TEXT:
		return UI_EmitText (out, &e->u.text) ? 0 : 1;

	case UI_PIC:
		return UI_EmitPic (out, &e->u.pic) ? 0 : 1;

	case UI_STATNUM:
		return UI_EmitStatnum (out, &e->u.statnum) ? 0 : 1;

	case UI_STATSTRING:
		return UI_EmitStatstring (out, &e->u.statstring) ? 0 : 1;

	case UI_IFSTAT:
		if (!allow_ifstat)
		{
			gi.dprintf ("ui_layout: rejected a UI_IFSTAT nested inside another UI_IFSTAT -- "
				"the client's skip-scan for a false 'if' is not nesting-aware "
				"(docs/layout-isa.md), so the block was dropped whole instead of emitted\n");
			return 1;
		}
		return UI_CompileIfstat (e, out);

	case UI_TABLE:
		return UI_CompileTable (e, out);

	case UI_RAW:
		return UI_EmitRaw (out, &e->u.raw) ? 0 : 1;

	default:
		gi.dprintf ("ui_layout: element with unknown kind %d dropped\n", (int)e->kind);
		return 1;
	}
}

int ui_layout_compile (const ui_screen_t *screen, ui_buf_t *out)
{
	int	i;
	int	dropped;

	if (!screen || !out)
		return 0;

	dropped = 0;

	for (i = 0; i < screen->count; i++)
		dropped += UI_CompileOne (&screen->elems[i], out, true);

	return dropped;
}

// -- density variant ladder --------------------------------------------
//
// See ui_layout.h for the contract. out is re-bound to its own
// storage/size before every attempt (ui_buf_init), so a rejected
// attempt's partial bytes never leak into the next layout's compile.

int ui_layout_compile_ladder (void *userdata, ui_board_build_fn build,
	ui_buf_t *out, ui_board_variant_t *variant_used)
{
	ui_board_variant_t	variant;
	ui_screen_t			screen;
	int					pre_filtered;
	int					dropped;
	char				*storage;
	int					capacity;

	storage = out->buf;
	capacity = out->size;

	variant = UI_BOARD_FULL;
	for (;;)
	{
		ui_buf_init (out, storage, capacity);

		screen.elems = NULL;
		screen.count = 0;
		pre_filtered = build (userdata, variant, &screen);

		dropped = pre_filtered + ui_layout_compile (&screen, out);

		if (dropped == 0 || variant == UI_BOARD_MINIMAL)
			break;

		variant++;
	}

	if (variant_used)
		*variant_used = variant;

	return dropped;
}
