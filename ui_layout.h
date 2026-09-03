// ui_layout.h -- the declarative screen layer (see docs/LAYOUT.md for
// the wire budget this compiles against and docs/layout-isa.md for
// the token vocabulary it emits).
//
// Ten hand-assembled layout producers in this tree build svc_layout
// strings by Com_sprintf-ing fixed token lines into a scratch buffer
// and strcat-ing the result in if it fits under an ad hoc 1024 (one
// board: 1000) cap, checked and enforced separately by every single
// producer. docs/LAYOUT.md's survey found five different guard styles
// grown up around that one requirement, and two producers (menu
// system, MOTD) had no guard at all until one was bolted on. A screen
// declared as data -- an array of elements, one of them possibly a
// table with a row callback -- and compiled through one function
// removes the need for every producer to reimplement its own guard,
// its own "did this row fit" check, and its own truncation policy.
//
// ui_layout_compile() emits through ui_buf_t (ui_text.h), which
// already guarantees the output buffer is never overrun and every
// individual append either lands whole or not at all. What this
// module adds on top: a screen is a tree of elements (some of them,
// tables, containing many rows), and the ISA's "if a token stream is
// cut off mid-element it renders garbage" risk (docs/layout-isa.md
// Sec 6: an orphaned if with no endif, or a table row missing one of
// its columns) means the compiler must roll an element or row back to
// nothing rather than let ui_appendf's per-call atomicity be the only
// thing standing between "one field didn't fit" and "half a row of
// digits with no picture behind them." See ui_layout.c's checkpoint /
// rollback around each compound element for how that's done.
//
// Needs qboolean (q_shared.h) and ui_buf_t (ui_text.h) but does not
// #include either -- same convention as ui_text.h and ui_stats.h:
// q_shared.h has no include guard, and this codebase's rule is every
// module includes it exactly once (see g_local.h). Include this
// header after both.

#ifndef UI_LAYOUT_H
#define UI_LAYOUT_H

// Interim wire ceiling, not the protocol's hard limit. docs/LAYOUT.md:
// MAX_MSGLEN (the real per-datagram ceiling) is 1400 bytes and shared
// with everything else the engine queues into the same frame
// (entity deltas, playerstate, sounds, prints) -- a layout write that
// fills 1400 bytes on its own still risks the crash-on-multicast /
// silent-drop-on-unicast failure docs/LAYOUT.md describes, so every
// current producer's manual guard already stops short of it (1380,
// one board at 1000). This is that same interim margin, in one place.
#define UI_LAYOUT_BUDGET 1000

// Per-cell scratch size a table row callback formats into. Generous
// against every field width any current board packs into one row
// (longest today: a 15-char name plus four small integers).
#define UI_CELL_LEN 64

// A table's column count is fixed at compile time (no VLAs -- this
// tree's strictest CI compiler builds as legacy C, see the portability
// note in ui_stats.h). Raise this if a future table genuinely needs
// more columns than this.
#define UI_TABLE_MAX_COLUMNS 8

typedef enum
{
	UI_TEXT,
	UI_PIC,
	UI_STATNUM,
	UI_STATSTRING,
	UI_IFSTAT,
	UI_TABLE,
	UI_RAW
} ui_elem_kind_t;

// UI_TEXT -- "string"/"string2": a literal line of text at (x,y).
// highlight false draws the normal charset ("string"); true draws the
// alt/inverted charset ("string2") -- what every current board calls
// its "highlighted" or team-colored row.
typedef struct
{
	int			x, y;
	const char	*text;
	qboolean	highlight;
} ui_text_t;

// UI_PIC -- "picn"/"pic": an image at (x,y). stat_driven false bakes
// a literal filename into the layout string (picn, no server-side
// precache required, docs/layout-isa.md Sec 5); true reads the pic's
// *name* through a stat-indexed CS_IMAGES configstring at draw time
// (pic <statIndex>) -- the caller is responsible for having already
// pointed that stat at a populated CS_IMAGES slot.
//
// image.name is the union's first member on purpose: this tree's
// strictest CI compiler builds as legacy C, where a plain (non-
// designated) aggregate initializer for a union can only ever target
// its first member (designated initializers are C99 and off the
// table -- see the portability note in ui_stats.h). Putting the more
// common picn case first lets a static ui_pic_t literal "just work";
// the stat-driven case has to be assigned field-by-field regardless.
typedef struct
{
	int			x, y;
	qboolean	stat_driven;
	union
	{
		const char	*name;	// stat_driven == false
		int			stat;	// stat_driven == true: stats[] slot holding the CS_IMAGES offset
	} image;
} ui_pic_t;

// UI_STATNUM -- "num <width> <statIndex>": a live numeric field.
// SCR_DrawFieldScaled clamps width to 5 digits and no-ops under 1
// (docs/layout-isa.md Sec 1) -- the client does that clamping, this
// compiler does not repeat it.
typedef struct
{
	int	x, y, width, stat;
} ui_statnum_t;

// UI_STATSTRING -- "stat_string <statIndex>": double indirection,
// stats[statIndex] is itself read as a configstring index whose text
// is drawn. Both bounds checks fail silently on the client
// (docs/layout-isa.md Sec 1) -- an out-of-range stat here is a silent
// blank, not a compile-time or run-time error.
typedef struct
{
	int	x, y, stat;
} ui_statstring_t;

// UI_RAW -- a wire token emitted exactly as given, with none of
// UI_TEXT's "xv %i yv %i string \"...\" " wrapper added. The one
// deliberate addition this codebase's DeathmatchScoreboardMessage
// conversion made to the vocabulary: docs/layout-isa.md Sec 1's
// `client` and `ctf` tokens carry their OWN x/y as their first two
// positional arguments and are not quoted text to draw at a point --
// wrapping either in UI_TEXT's template would make the client try to
// draw the literal characters "client 0 32 ..." on screen instead of
// executing the token. No existing element kind fits a raw multi-arg
// positional token, so this one was added rather than forced through
// UI_TEXT. text must already be the complete, correctly spaced token
// (name, args, trailing space) -- the compiler treats it as opaque
// and appends it verbatim.
typedef struct
{
	const char	*text;
} ui_raw_t;

// UI_IFSTAT -- "if <statIndex> ... endif" wrapping a sub-range of
// elements. The ISA's skip-scan for a false condition is not nesting-
// aware (docs/layout-isa.md Sec 1/6): it hunts for the literal token
// "endif" with no depth counter, so an if nested inside a skipped
// if's body lets that inner endif close the *outer* skip early,
// leaking whatever comes after it as if it were live, unconditional
// content. ui_layout_compile() refuses to emit a UI_IFSTAT found
// anywhere inside another UI_IFSTAT's elems (dropping it whole, with
// a gi.dprintf so the mistake is visible to whoever wrote the screen)
// rather than ship that failure mode.
typedef struct ui_elem_s ui_elem_t;	// forward decl, this struct owns a range of them below

typedef struct
{
	int					stat;
	const ui_elem_t		*elems;
	int					count;
} ui_ifstat_t;

// One column of a UI_TABLE. x_offset is added to the table's origin
// x for every row. priority exists so a future multi-column table can
// ask the compiler to drop its least-important column(s) first when a
// row is otherwise too wide to fit -- no producer converted so far
// has more than one column, so that half of the contract is declared
// here but not yet exercised; see ui_layout.c's UI_CompileTable for
// what is implemented today (whole-row drop, never a half row).
typedef struct
{
	int	x_offset;
	int	priority;
} ui_table_col_t;

// Fills cells[0..num_columns-1] for the given zero-based row. Each
// cell is a NUL-terminated string already formatted the way it should
// appear (padding, sign, etc. included) -- the compiler treats a cell
// as opaque text, it does not print into it.
typedef void (*ui_table_row_fn)(void *userdata, int row, int num_columns,
	char cells[UI_TABLE_MAX_COLUMNS][UI_CELL_LEN]);

// UI_TABLE -- an origin, a row height, up to UI_TABLE_MAX_COLUMNS
// columns, and a callback that fills one row's cells at a time. Row 0
// draws at (x,y); row N draws at (x, y + N*row_dy). Rows are compiled
// in order and are expected to already be sorted the way the board
// wants them to degrade -- when the wire budget runs out mid-table,
// every remaining row is dropped (docs/LAYOUT.md: "mirror what the
// existing boards do by hand", which is a straight break-on-overflow
// loop, i.e. the tail of the row order is what's sacrificed first).
//
// footer is optional (NULL: no footer). When set, the compiler always
// attempts it after the row loop -- including after a row-budget
// truncation -- so a board that wants a "N more not shown" line or
// similar can rely on it still appearing even when rows were dropped.
// Railboard (the one board converted so far) has no footer of its
// own; this field exists for the boards that do.
typedef struct
{
	int					x, y;
	int					row_dy;
	const ui_table_col_t	*columns;
	int					num_columns;
	int					num_rows;
	ui_table_row_fn		fill_row;
	void				*userdata;
	qboolean			highlight;

	const char			*footer;
	int					footer_x, footer_y;
	qboolean			footer_highlight;
} ui_table_t;

struct ui_elem_s
{
	ui_elem_kind_t	kind;
	union
	{
		ui_text_t		text;
		ui_pic_t		pic;
		ui_statnum_t	statnum;
		ui_statstring_t	statstring;
		ui_ifstat_t		ifstat;
		ui_table_t		table;
		ui_raw_t		raw;
	} u;
};

typedef struct
{
	const ui_elem_t	*elems;
	int					count;
} ui_screen_t;

// Compiles screen into out (ui_buf_t, ui_text.h) as a layout token
// stream. Never exceeds out's declared capacity: out's own bounded
// appender guarantees that already, and this compiler additionally
// guarantees the emitted stream never stops in the *middle* of an
// element or table row -- a refused append rolls that whole element
// or row back out of out rather than leave a dangling token behind
// (docs/layout-isa.md: a truncated token stream can orphan an if with
// no matching endif, or a table row missing a column, either of which
// renders wrong rather than just short).
//
// Returns how much of the screen did not make it in: every dropped
// non-table element counts 1; a dropped/truncated UI_TABLE counts the
// number of its rows that did not get emitted (0 if every row fit).
// Callers that care can log the count (gi.dprintf, the sanctioned
// diagnostic channel -- SLIPGATE STYLE.md rule 12) or ignore it, same
// as every hand-written producer did today.
int ui_layout_compile(const ui_screen_t *screen, ui_buf_t *out);

// -- density variant ladder ------------------------------------------
//
// docs/LAYOUT.md point 2: boards never paginate -- when a screen does
// not fit, the whole screen is rebuilt one rung shorter rather than
// truncating the roster it already started drawing. UI_BOARD_FULL is
// today's format; UI_BOARD_CONDENSED shortens names and drops
// secondary columns; UI_BOARD_MINIMAL keeps only a name and the
// board's single headline number. UI_BOARD_MINIMAL is the floor --
// whatever it produces ships as-is, ui_layout_compile's own budget
// enforcement (UI_LAYOUT_BUDGET) is what keeps that wire-safe even
// when MINIMAL still can't show every row.
typedef enum
{
	UI_BOARD_FULL,
	UI_BOARD_CONDENSED,
	UI_BOARD_MINIMAL
} ui_board_variant_t;

// Fills *screen for the given variant and returns how many rows/lines
// this board already knows it left out BEFORE handing anything to
// ui_layout_compile -- StatboardMessage, TeamStatboardMessage,
// CTFSquadboardMessage and DeathmatchScoreboardMessage each still run
// their own pre-existing legacy byte-cap admission pass (Board_LineLen
// in p_hud.c) ahead of the compiler so UI_BOARD_FULL stays byte-
// identical to the pre-ladder output; whatever that pass excludes
// never reaches ui_layout_compile and so would otherwise be invisible
// to the fit check below. Return 0 when the builder has no such pre-
// filter (Railboard: every row it builds goes to the compiler, so the
// compiler's own dropped count is already the whole story).
//
// The elements *screen ends up pointing at (and any text they
// reference) must live in storage the caller of
// ui_layout_compile_ladder owns (typically fields on userdata) --
// they need to stay valid until ui_layout_compile_ladder compiles
// this attempt, which happens immediately after build() returns,
// before build() is ever called again for the next variant.
typedef int (*ui_board_build_fn)(void *userdata, ui_board_variant_t variant, ui_screen_t *screen);

// Fit-verified variant selection: calls build() for UI_BOARD_FULL,
// compiles it into out, and stops there if nothing was dropped --
// neither by build()'s own pre-filter nor by the compiler. Otherwise
// rebuilds the WHOLE screen at the next rung down (never a partial
// rebuild of just the part that didn't fit) and compiles again,
// continuing until either a rung fits or UI_BOARD_MINIMAL has been
// tried -- MINIMAL always ships, dropped or not. out must already be
// bound (ui_buf_init) to caller-owned storage; each attempt re-binds
// it to the same storage before compiling so a rejected attempt never
// leaves partial bytes behind for the next one. Returns build()'s
// pre-filter count plus the final ui_layout_compile dropped count; if
// variant_used is non-NULL, the rung actually served is written there
// for the caller's own logging.
int ui_layout_compile_ladder(void *userdata, ui_board_build_fn build,
	ui_buf_t *out, ui_board_variant_t *variant_used);

#endif // UI_LAYOUT_H
