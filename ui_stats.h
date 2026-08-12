// ui_stats.h -- the stat-slot registry
//
// player_state_t.stats[] is 32 slots of 16-bit int, delta-compressed
// by the engine every frame (docs/LAYOUT.md: 5 free slots out of 32
// before this table existed). Ten producers across this tree read or
// write individual slots by bare number -- g_menu.c, p_hud.c,
// p_view.c, p_client.c -- with no single place recording who owns
// which slot or what is still free. A new feature grabbing an
// already-used slot number compiles cleanly and corrupts a different
// HUD element at runtime; nothing catches it. This table is that
// single place: every slot 0-31, its owner, and its meaning, so
// claiming a slot is a one-line addition here instead of a grep
// through three files to see what's unused.
//
// The numbers below are NOT the authority. q_shared.h's STAT_*
// #defines still are, and are left untouched -- migrating the real
// definitions to originate here is a later step. This header's job
// today is to prove, at compile time, that its copy of each slot
// number agrees with q_shared.h's: if the two ever drift apart, the
// build stops instead of shipping a HUD element silently reading the
// wrong stat.
//
// Include order: this header reads the STAT_* names from q_shared.h,
// so it must be included after q_shared.h (see g_local.h). It does
// not #include q_shared.h itself -- q_shared.h has no include guard
// and this codebase's convention is that every module includes it
// exactly once (see g_local.h); a second #include here would
// redeclare every enum in it and fail the build.

#ifndef UI_STATS_H
#define UI_STATS_H

// Who is allowed to assign this slot / what put the value there.
#define UI_STAT_OWNER_ENGINE 0	// stock id Software HUD, read by the engine's built-in interpreter
#define UI_STAT_OWNER_LMCTF  1	// LMCTF team/CTF HUD (LM_JORM / Bat additions)
#define UI_STAT_OWNER_FREE   2	// unclaimed -- next feature needing a slot starts here

// UI_STAT(slot, symbol, owner, meaning)
//   slot    -- the stats[] index, 0-31
//   symbol  -- for engine/lmctf slots, the real STAT_* name from
//              q_shared.h (checked against it below); for free slots,
//              a placeholder name unique to this table
//   owner   -- one of the UI_STAT_OWNER_* values above
//   meaning -- one-line description for humans reading the table
#define UI_STAT_TABLE \
	UI_STAT(0,  STAT_HEALTH_ICON,   UI_STAT_OWNER_ENGINE, "health icon") \
	UI_STAT(1,  STAT_HEALTH,        UI_STAT_OWNER_ENGINE, "health value") \
	UI_STAT(2,  STAT_AMMO_ICON,     UI_STAT_OWNER_ENGINE, "ammo icon") \
	UI_STAT(3,  STAT_AMMO,          UI_STAT_OWNER_ENGINE, "ammo value") \
	UI_STAT(4,  STAT_ARMOR_ICON,    UI_STAT_OWNER_ENGINE, "armor icon") \
	UI_STAT(5,  STAT_ARMOR,         UI_STAT_OWNER_ENGINE, "armor value") \
	UI_STAT(6,  STAT_SELECTED_ICON, UI_STAT_OWNER_ENGINE, "selected weapon icon") \
	UI_STAT(7,  STAT_PICKUP_ICON,   UI_STAT_OWNER_ENGINE, "pickup icon") \
	UI_STAT(8,  STAT_PICKUP_STRING, UI_STAT_OWNER_ENGINE, "pickup name, indexes a configstring") \
	UI_STAT(9,  STAT_TIMER_ICON,    UI_STAT_OWNER_ENGINE, "timer icon") \
	UI_STAT(10, STAT_TIMER,         UI_STAT_OWNER_ENGINE, "timer value") \
	UI_STAT(11, STAT_HELPICON,      UI_STAT_OWNER_ENGINE, "help/chat pending icon") \
	UI_STAT(12, STAT_SELECTED_ITEM, UI_STAT_OWNER_ENGINE, "selected inventory item") \
	UI_STAT(13, STAT_LAYOUTS,       UI_STAT_OWNER_ENGINE, "layout visibility bitflags") \
	UI_STAT(14, STAT_FRAGS,         UI_STAT_OWNER_ENGINE, "frag count") \
	UI_STAT(15, STAT_FLASHES,       UI_STAT_OWNER_ENGINE, "screen flash flags, cleared every frame") \
	UI_STAT(16, STAT_CHASE,         UI_STAT_OWNER_ENGINE, "chase-cam target name, indexes a configstring") \
	UI_STAT(17, STAT_SPECTATOR,     UI_STAT_OWNER_ENGINE, "spectator flag") \
	UI_STAT(18, STAT_TEAM_ICON,     UI_STAT_OWNER_LMCTF,  "own team icon") \
	UI_STAT(19, STAT_RED_FRAGS,     UI_STAT_OWNER_LMCTF,  "red team frag count") \
	UI_STAT(20, STAT_BLUE_FRAGS,    UI_STAT_OWNER_LMCTF,  "blue team frag count") \
	UI_STAT(21, STAT_RED_ICON,      UI_STAT_OWNER_LMCTF,  "red team icon") \
	UI_STAT(22, STAT_BLUE_ICON,     UI_STAT_OWNER_LMCTF,  "blue team icon") \
	UI_STAT(23, STAT_RUNE_ICON,     UI_STAT_OWNER_LMCTF,  "carried rune icon") \
	UI_STAT(24, STAT_RED_CAPS,      UI_STAT_OWNER_LMCTF,  "red team capture count") \
	UI_STAT(25, STAT_BLUE_CAPS,     UI_STAT_OWNER_LMCTF,  "blue team capture count") \
	UI_STAT(26, STAT_MATCH_TIME,    UI_STAT_OWNER_LMCTF,  "match clock") \
	UI_STAT(27, UI_STAT_FREE_27,    UI_STAT_OWNER_FREE,   "unclaimed") \
	UI_STAT(28, UI_STAT_FREE_28,    UI_STAT_OWNER_FREE,   "unclaimed") \
	UI_STAT(29, UI_STAT_FREE_29,    UI_STAT_OWNER_FREE,   "unclaimed") \
	UI_STAT(30, UI_STAT_FREE_30,    UI_STAT_OWNER_FREE,   "unclaimed") \
	UI_STAT(31, UI_STAT_FREE_31,    UI_STAT_OWNER_FREE,   "unclaimed")

// Free slots have no q_shared.h counterpart to check against; give
// them a self-referential symbol (its own literal slot number) so the
// same X-macro expansion works for every row uniformly below.
#define UI_STAT_FREE_27 27
#define UI_STAT_FREE_28 28
#define UI_STAT_FREE_29 29
#define UI_STAT_FREE_30 30
#define UI_STAT_FREE_31 31

// Compile-time proof that this table agrees with q_shared.h: if
// anyone edits a STAT_* value there without updating the matching row
// here (or vice versa), the build fails right here instead of
// shipping a HUD element pointed at the wrong slot.
#define UI_STAT(slot, symbol, owner, meaning) \
	_Static_assert((slot) == (symbol), #symbol " slot number in ui_stats.h no longer matches its q_shared.h #define");
UI_STAT_TABLE
#undef UI_STAT

_Static_assert(MAX_STATS == 32, "ui_stats.h's registry covers slots 0-31; MAX_STATS changed out from under it");

#endif // UI_STATS_H
