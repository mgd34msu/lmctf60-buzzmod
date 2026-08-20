# yquake2 svc_layout / statusbar protocol — capability reference

All citations are `file:line` paths in the surveyed yquake2 source tree.

## 1. Layout-string interpreter token vocabulary

Interpreter: `SCR_ExecuteLayoutString(char *s)`,
`src/client/cl_screen.c:1066-1477`. It re-tokenizes `s` from scratch with
`COM_Parse` (`src/common/shared/shared.c:1051`) on every call — there is no
compiled/cached form.

Cursor state: `x`, `y` (ints), both start at 0 at the top of every
`SCR_ExecuteLayoutString` call (`cl_screen.c:1084-1085`) — position does NOT
persist between the CS_STATUSBAR layout and the `cl.layout` layout; each call
is independent. A global `scale = SCR_GetHUDScale()` (yquake2-only, see §6)
multiplies all position math and drawing.

Guard clause: if `cls.state != ca_active` or `!cl.refresh_prepped`, or the
string is empty, the function returns immediately without parsing anything
(`cl_screen.c:1074-1082`).

| Token | Args | Semantics | Citation |
|---|---|---|---|
| `xl` | `<n>` | `x = scale * n` (absolute, left-relative to screen origin 0) | `cl_screen.c:1093-1098` |
| `xr` | `<n>` | `x = viddef.width + scale * n` (right-relative; `n` normally negative) | `cl_screen.c:1100-1105` |
| `xv` | `<n>` | `x = viddef.width/2 - scale*160 + scale*n` (centered on a virtual 320-wide HUD canvas) | `cl_screen.c:1107-1112` |
| `yt` | `<n>` | `y = scale * n` (top-relative) | `cl_screen.c:1114-1119` |
| `yb` | `<n>` | `y = viddef.height + scale * n` (bottom-relative; `n` normally negative) | `cl_screen.c:1121-1126` |
| `yv` | `<n>` | `y = viddef.height/2 - scale*120 + scale*n` (centered on virtual 240-tall HUD canvas) | `cl_screen.c:1128-1133` |
| `pic` | `<statIndex>` | Draws image whose *name* is the configstring at `CS_IMAGES + stats[statIndex]`. Bounds-checked: `statIndex` must be `0..MAX_STATS-1` else silently skipped w/ `Com_DPrintf`; resolved stat `value` must be `< MAX_IMAGES` else skipped; if the configstring slot is empty (`'\0'`), nothing is drawn (no error). Draws via `Draw_PicScaled` at current `x,y`. | `cl_screen.c:1135-1172` |
| `picn` | `<name>` | Draws image by literal filename token (no stat/configstring indirection). Size looked up via `Draw_GetPicSize`, drawn via `Draw_PicScaled` at current `x,y`. | `cl_screen.c:1273-1284` |
| `client` | `<x> <y> <clientnum> <score> <ping> <time>` | Deathmatch scoreboard block. `x`,`y` recomputed as `xv`/`yv`-style centered coords (ignores prior cursor). Validates `clientnum` in `0..MAX_CLIENTS-1` (else skip). Draws (inverted/alt-charset) name, "Score: N", then (normal charset) "Ping:  N" / "Time:  N" at fixed sub-offsets, plus the client's icon pic. | `cl_screen.c:1174-1220` |
| `ctf` | `<x> <y> <clientnum> <score> <ping>` | CTF-style one-line scoreboard row: `"%3d %3d %-12.12s"` of score/ping/name (ping clamped to 999). Drawn with the alt (inverted) charset if `clientnum == cl.playernum` (highlights own row), else normal charset. | `cl_screen.c:1222-1271` |
| `num` | `<width> <statIndex>` | Generic numeric field: draws `stats[statIndex]` right-justified in `width` digits, color 0 (green digit set), via `SCR_DrawFieldScaled`. | `cl_screen.c:1286-1297` |
| `hnum` | *(none)* | Health number. Fixed `width=3`, value = `stats[STAT_HEALTH]`. Color: green if `>25`; if `1..25`, flashes red/white by `(serverframe>>2)&1`; if `<=0`, solid color 1 (red). Draws `field_3` background pic first iff `stats[STAT_FLASHES] & 1`. | `cl_screen.c:1299-1327` |
| `anum` | *(none)* | Ammo number. Fixed `width=3`, value = `stats[STAT_AMMO]`. Color: green if `>5`; flashes if `0..5`; **negative value → token entirely skipped (no draw at all)**, used by the game DLL to hide the ammo field. Draws `field_3` first iff `stats[STAT_FLASHES] & 4`. | `cl_screen.c:1329-1357` |
| `rnum` | *(none)* | Armor number. Fixed `width=3`, value = `stats[STAT_ARMOR]`. **If `value < 1`, skipped entirely** (no draw). Always color 0 (green) otherwise. Draws `field_3` first iff `stats[STAT_FLASHES] & 2`. | `cl_screen.c:1359-1381` |
| `stat_string` | `<statIndex>` | Double indirection: `statIndex` (bounds-checked `0..MAX_STATS-1`) selects a stat whose *value* is itself a configstring index (bounds-checked `0..MAX_CONFIGSTRINGS-1`); that configstring's text is drawn with `DrawStringScaled` (normal charset). Either bounds failure is a silent skip w/ `Com_DPrintf`. | `cl_screen.c:1383-1408` |
| `cstring` | `<text>` | Draws `text` via `DrawHUDStringScaled(token, x, y, 320, 0, scale)` — a "centered HUD string" that honors embedded `\n`, wraps/centers each line within a 320-unit-wide field (`centerwidth=320`), normal (non-inverted) charset (`xor=0`). | `cl_screen.c:1410-1415`, string wrapper `cl_screen.c:921-966` |
| `string` | `<text>` | Draws `text` left-to-right with `DrawStringScaled` — normal charset, no centering, no `\n` handling (single Draw_CharScaled loop). | `cl_screen.c:1417-1422`, `src/client/cl_console.c:38-47` |
| `cstring2` | `<text>` | Same as `cstring` but `xor=0x80` — every char XORed with 0x80 before lookup, i.e. drawn from the "alt" (typically red/gold-tinted) half of the conchars strip. | `cl_screen.c:1424-1429` |
| `string2` | `<text>` | Same as `string` but via `DrawAltStringScaled`, which XORs each char with `0x80` — alt/inverted color glyphs. | `cl_screen.c:1431-1436`, `cl_console.c:49-58` |
| `if` | `<statIndex>` | Reads `stats[statIndex]` (out-of-range index treated as value `0`, i.e. false, with a `Com_DPrintf` warning, not a hard error). If the value is falsy (`0`), the interpreter **linearly re-calls `COM_Parse` in a tight loop discarding every token** until it sees the literal token `"endif"` or runs out of string (`s == NULL`) — i.e. it does *not* parse nested `if`s specially; a nested `if..endif` pair inside a skipped block is not tracked, so the *first* `endif` token terminates the skip regardless of nesting depth. | `cl_screen.c:1438-1467` |
| `endif` | *(none)* | No-op when reached normally (only meaningful as the scan target of a preceding `if`). | `cl_screen.c:1469-1473` |
| *(empty token)* | — | An empty/blank token (`!token[0]`, e.g. from trailing whitespace) is silently skipped, same branch as `endif`. | `cl_screen.c:1469-1473` |
| *(anything else)* | — | Unknown token: printed via `Com_DPrintf("%s: Unknown token: %s\n", ...)` and otherwise ignored — **the interpreter does not abort or error on garbage tokens**, it just logs (debug-build/`developer 1`-visible only) and continues from the next `COM_Parse` call. | `cl_screen.c:1475` |

Char metrics used by the string/field drawers: `CHAR_SIZE = 8` px advance
per character for `string`/`string2`/`cstring`/`cstring2`/`stat_string`/`ctf`/`client`
text (`cl_screen.c:78`, `cl_console.c:39-58`), all pre-multiplied by `scale`.
Numeric fields (`num`/`hnum`/`anum`/`rnum`) use a wider per-digit pic cell:
`CHARACTER_WIDTH = 16` px (`cl_screen.c:881`), because each digit is drawn as
a distinct `sb_nums` pic (`num_0`..`num_9`,`num_minus` / `anum_0`..`anum_9`,`anum_minus`
for color 0/1) rather than a font glyph (`cl_screen.c:867-877`, `975-1024`).
`SCR_DrawFieldScaled` clamps requested field `width` to a max of 5 digits
(`cl_screen.c:987-990`) and no-ops if `width < 1` (`cl_screen.c:981-984`).

## 2. Byte budgets

| Limit | Value | Citation |
|---|---|---|
| `cl.layout` client-side buffer (holds the last `svc_layout` string) | `char layout[1024]` | `src/client/header/client.h:156` |
| `svc_layout` receive path | `MSG_ReadString` into a **static 2048-byte** scratch buffer, then `Q_strlcpy`'d (silently truncated, NUL-terminated) into `cl.layout[1024]` | recv: `src/client/cl_parse.c:1460-1463`; `MSG_ReadString` buffer: `src/common/movemsg.c:1100-1125` |
| `MSG_ReadString` / `MSG_ReadStringLine` scratch buffer | 2048 bytes, hard stop at `l < sizeof(string)-1` (2047 chars + NUL); no overflow, just truncates the read | `src/common/movemsg.c:1103,1120` |
| `MAX_STRING_CHARS` (generic "string passed to Cmd_TokenizeString" limit, not directly the layout path but the general net-string convention) | 2048 | `src/common/header/shared.h:141` |
| `MAX_TOKEN_CHARS` (per-token cap inside `COM_Parse`, e.g. one `string "..."` argument) | 1024; extra chars beyond this are silently dropped, not an error | `src/common/header/shared.h:143`; drop logic `src/common/shared/shared.c:1105-1133` |
| `CS_STATUSBAR` configstring nominal per-slot size | `MAX_QPATH = 64` bytes like any configstring, **but** the client special-cases indices `CS_STATUSBAR..CS_STATUSBAR_END-1` (5..28) to let one write span forward into the following slots' backing memory: allowed length for a write starting at index `i` is `CS_STATUSBAR_SPACE(i) = (29 - i) * 64` bytes. Writing at index 5 (the actual `CS_STATUSBAR` the client renders) allows up to `24*64 - 1 = 1535` bytes. | defines: `src/common/header/shared.h:1197-1199`; enforcement: `src/client/cl_parse.c:1144-1158` |
| Any other (non-statusbar) configstring | `MAX_QPATH = 64` bytes (63 usable + NUL); overflow write is rejected outright (`Com_Printf` + `return`, configstring left unchanged) | `src/client/cl_parse.c:1159-1172`; `MAX_QPATH` def `src/common/header/shared.h:145` |
| `MAX_MSGLEN` — one UDP datagram (all svc_ commands for one server frame to one client, entity deltas + playerstate + queued unicast/multicast, including any `svc_layout`/`svc_configstring` in that frame) | 1400 bytes | `src/common/header/common.h:525` |
| Per-client outgoing datagram build buffer (`SV_SendClientDatagram`) | `byte msg_buf[MAX_MSGLEN]`, `allowoverflow=true`. On overflow: `Com_Printf("WARNING: msg overflowed for %s\n", ...)` then `SZ_Clear(&msg)` — **the entire frame's message for that client is dropped**, not just the offending write. | `src/server/sv_send.c:447-490` |
| Per-client accumulated "datagram" (queued unicast prints/sounds/layout/configstring writes before the frame is built) | `byte datagram_buf[MAX_MSGLEN]` | `src/server/header/server.h:135-136` |
| Server multicast staging buffer (`gi.WriteByte/WriteString/...` from the game DLL land here before `SV_Multicast`) | `byte multicast_buf[MAX_MSGLEN]`, default `allowoverflow=false` → **`Com_Error(ERR_FATAL, ...)`, i.e. a hard server crash**, if a single multicast burst (e.g. one oversized `svc_layout` write) exceeds 1400 bytes | init `src/server/sv_init.c:289`; overflow behavior `src/common/szone.c:44-71` |
| Network protocol message fragmentation | **None found.** No fragmentation logic exists in `src/common/netchan.c` for regular per-frame reliable/unreliable messages — a message is one UDP packet, hard-capped at `MAX_MSGLEN` (1400). | searched `src/common/netchan.c` (only OOB/`Netchan_OutOfBand` uses a `send_buf[MAX_MSGLEN]`, no split-across-packets logic) |

Practical implication for a layout compiler: **1400 bytes is the real
per-update ceiling**, not 1024/1536/2048 — anything the game DLL tries to
push through `gi.WriteByte(svc_layout); gi.WriteString(...)` shares that
1400-byte packet with everything else queued for the client that frame
(entity deltas, playerstate, sounds, prints), and blowing it either crashes
the server (multicast path, `allowoverflow=false`) or silently drops the
whole frame's update for that client (unicast/per-client datagram path,
`allowoverflow=true`).

## 3. Stats pipeline

- `player_state_t.stats` is `short stats[MAX_STATS]` — **32 slots, 16-bit
  signed each.** `src/common/header/shared.h:1281`, `1151`.
- Delta compression: the server computes a **32-bit `statbits` mask**
  (`int statbits`, one bit per stat index — this is exactly `MAX_STATS`,
  i.e. the wire format has zero headroom to grow past 32 stats without a
  protocol break) by comparing the current playerstate's stats to the old
  (last-acked-baseline) playerstate's stats; only changed stats are written.
  Write side: `src/server/sv_entities.c:375-394`. Read side (client mirrors
  the same bit-for-bit loop): `src/client/cl_parse.c:670-679`.
  - `statbits = MSG_WriteLong(...)` (32-bit), then one `MSG_WriteShort`
    per set bit, in index order.
- Reserved/engine-interpreted stat indices (the rest, up to index 31, are
  free for the game DLL to use however it likes, including as the operand
  to `num`/`if`/`stat_string`/`pic`):

  | Index | Name | Engine-side consumer |
  |---|---|---|
  | 0 | `STAT_HEALTH_ICON` | game-DLL convention only (not read by the engine layout interpreter directly — `pic` reads whichever stat index the layout string names) |
  | 1 | `STAT_HEALTH` | `hnum` token reads this directly | `cl_screen.c:1305` |
  | 2 | `STAT_AMMO_ICON` | convention only |
  | 3 | `STAT_AMMO` | `anum` token reads this directly | `cl_screen.c:1335` |
  | 4 | `STAT_ARMOR_ICON` | convention only |
  | 5 | `STAT_ARMOR` | `rnum` token reads this directly | `cl_screen.c:1365` |
  | 6 | `STAT_SELECTED_ICON` | convention only |
  | 7 | `STAT_PICKUP_ICON` | convention only |
  | 8 | `STAT_PICKUP_STRING` | convention only |
  | 9 | `STAT_TIMER_ICON` | convention only |
  | 10 | `STAT_TIMER` | convention only |
  | 11 | `STAT_HELPICON` | convention only |
  | 12 | `STAT_SELECTED_ITEM` | convention only |
  | 13 | `STAT_LAYOUTS` | **engine-interpreted**: bit 0 gates whether `cl.layout` is executed at all this frame (`SCR_DrawLayout`); bit 1 gates whether `CL_DrawInventory` runs | `cl_screen.c:1489,1494`, `1766-1774` |
  | 14 | `STAT_FRAGS` | convention only |
  | 15 | `STAT_FLASHES` | **engine-interpreted** by `hnum`/`anum`/`rnum`: bit 1 = flash armor's `field_3` backdrop, bit 2 = flash ammo's, bit 0 = flash health's (comment says "cleared each frame, 1=health, 2=armor" but code also checks `&4` for ammo) | def `shared.h:1147`; use `cl_screen.c:1320,1350,1374` |
  | 16 | `STAT_CHASE` | convention only (chase-cam name string index, by convention) |
  | 17 | `STAT_SPECTATOR` | convention only |
  | 18–31 | *(unnamed)* | entirely free for game DLL use |

  Indices with no dedicated define (18-31) and even the "convention only"
  ones above are **not actually special-cased by the client engine** except
  where the layout interpreter is told to use them via `num <w> <idx>`,
  `if <idx>`, `pic <idx>`, or `stat_string <idx>` in the layout string
  itself — the engine only hard-wires `STAT_HEALTH`(1), `STAT_AMMO`(3),
  `STAT_ARMOR`(5), `STAT_LAYOUTS`(13), and `STAT_FLASHES`(15).
- `num`/`pic` consume a stat as a raw integer index/value directly.
  `hnum`/`anum`/`rnum` are hard-wired to fixed stat indices (1/3/5) rather
  than taking an operand. `stat_string` double-dereferences: stat value is
  used as a **configstring index**, not a display value.

## 4. Configstrings

- `cl.configstrings[MAX_CONFIGSTRINGS][MAX_QPATH]` — `src/client/header/client.h:172`.
- `MAX_QPATH = 64` bytes per slot (63 usable chars + NUL) — `shared.h:145`.
- Ranges (all from `src/common/header/shared.h:1192-1212`):

  | Range | Base | Count | Notes |
  |---|---|---|---|
  | `CS_NAME` | 0 | 1 | |
  | `CS_CDTRACK` | 1 | 1 | |
  | `CS_SKY` | 2 | 1 | |
  | `CS_SKYAXIS` | 3 | 1 | `"%f %f %f"` format |
  | `CS_SKYROTATE` | 4 | 1 | |
  | `CS_STATUSBAR` | 5 | 24 slots (5..28) | display-program string, see §2 for the special multi-slot span rule |
  | `CS_AIRACCEL` | 29 | 1 | aliases `CS_STATUSBAR_END` |
  | `CS_MAXCLIENTS` | 30 | 1 | |
  | `CS_MAPCHECKSUM` | 31 | 1 | |
  | `CS_MODELS` | 32 | `MAX_MODELS`=256 | |
  | `CS_SOUNDS` | 288 | `MAX_SOUNDS`=256 | |
  | `CS_IMAGES` | 544 | `MAX_IMAGES`=256 | this is what `pic`/precache index into |
  | `CS_LIGHTS` | 800 | `MAX_LIGHTSTYLES`=256 | |
  | `CS_ITEMS` | 1056 | `MAX_ITEMS`=256 | |
  | `CS_PLAYERSKINS` | 1312 | `MAX_CLIENTS`=256 | |
  | `CS_GENERAL` | 1568 | `MAX_GENERAL` = `MAX_CLIENTS*2` = 512 | free-form game-DLL slots; **512 available**, the largest "free for the game" block, this is what `stat_string` most commonly targets for arbitrary status text |
  | `MAX_CONFIGSTRINGS` (total) | — | 2080 | `CS_GENERAL + MAX_GENERAL` = 1568+512 |

- Runtime update path: `svc_configstring` → `CL_ParseConfigString`
  (`src/client/cl_parse.c:1123-1230`). This **is** live-updatable mid-game:
  the server can send a new `svc_configstring` for any index at any time;
  the client overwrites `cl.configstrings[i]` in place (bounds/length
  checked per §2) and, for `CS_LIGHTS` range, immediately calls
  `CL_SetLightstyle`; for `CS_CDTRACK` it may restart music playback if
  `cl.refresh_prepped`. **No explicit re-render/invalidate is needed for
  layout purposes** — because `SCR_ExecuteLayoutString` re-parses the
  stored `cl.layout` / `cl.configstrings[CS_STATUSBAR]` string from scratch
  every rendered video frame (see §5), a `stat_string` reference or a `pic`
  reference into a configstring that the server just changed is picked up
  automatically on the very next drawn frame — no separate signal is
  needed, there's no caching of the *resolved* text/pic ahead of the actual
  draw call. (There is a one-time, precache-only exception for `CS_IMAGES`:
  see §5.)

## 5. Refresh mechanics

- Layouts are **not** cached/pre-rendered. `SCR_ExecuteLayoutString` is
  invoked, and the string it's given is re-tokenized top-to-bottom with
  fresh `COM_Parse` calls, on **every call to `SCR_UpdateScreen`**, i.e.
  once per rendered client video frame (not once per server tick/snapshot):
  - `SCR_DrawStats()` unconditionally executes
    `cl.configstrings[CS_STATUSBAR]` every frame — `cl_screen.c:1483-1486`,
    call site `cl_screen.c:1763`.
  - `SCR_DrawLayout()` executes `cl.layout` every frame **only if**
    `stats[STAT_LAYOUTS] & 1` — `cl_screen.c:1489-1499`, call site
    `cl_screen.c:1766-1769`.
- Because re-render is purely local/client-CPU work against whatever is
  currently sitting in `cl.layout` / `cl.configstrings[CS_STATUSBAR]`,
  **`svc_layout` costs bandwidth only when the server actually sends it**
  (the game DLL decides that cadence — nothing in the engine auto-resends
  it). A layout that never changes costs zero additional bytes per frame
  after the first send; a layout resent every server frame costs a fresh
  `svc_layout` string (see §2 budgets) every server frame it's sent, fully
  independent of the client's (usually much higher) video frame rate.
- `pic`/`picn` precache requirements:
  - `picn` takes a literal filename baked into the layout string itself.
    It requires **no server-side precache at all** — image lookup goes
    straight through `Draw_GetPicSize`/`Draw_PicScaled` → `Draw_FindPic` →
    renderer's `R_FindPic`, which lazily loads-and-caches from disk/pak on
    first use (`src/client/refresh/gl1/gl1_draw.c:142-146,189-200`; generic
    dispatch `src/client/vid/vid.c:689-726`). If the named pic can't be
    found, the renderer just prints `"Can't find pic: %s"` and skips the
    draw — no crash, no interpreter abort.
  - `pic <statIndex>` resolves the pic's *name* via
    `cl.configstrings[CS_IMAGES + stats[statIndex]]`
    (`cl_screen.c:1150-1168`). This name must have been placed into that
    `CS_IMAGES` slot by a prior `svc_configstring`. The engine additionally
    **pre-warms (precaches) every populated `CS_IMAGES` slot into
    `cl.image_precache[]`** at two points: on receipt of the configstring
    itself if index falls in the images range
    (`src/client/cl_parse.c:1214`), and in bulk during refresh-prep
    (`src/client/cl_view.c:309`) — so in practice the texture is already
    resident by the time a layout references it, but even if it weren't,
    `Draw_PicScaled`'s lazy-load fallback would still load it on demand.

## 6. Surprises / yquake2-specific deltas from stock 3.20

- **Token vocabulary is unchanged from stock id Software 3.20**: the full
  set found (`xl xr xv yt yb yv pic client ctf picn num hnum anum rnum
  stat_string cstring string cstring2 string2 if endif`) matches the
  original vanilla layout language exactly — no new tokens were added by
  yquake2.
- **`r_hudscale`** (`cl_screen.c:53,442`, comment: *"named for consistency
  with R1Q2"*) is a yquake2/R1Q2-lineage addition absent from stock 3.20.
  Every single position (`xl/xr/xv/yt/yb/yv`), every pic draw, every numeric
  field, and every string draw in the interpreter is multiplied by
  `scale = SCR_GetHUDScale()` (`cl_screen.c:1072`). Default `-1` = auto
  (`SCR_GetDefaultScale`, integer-scales to a `640x240`-ish reference,
  clamped `>=1` — `cl_screen.c:1830-1845`); `0` is a documented hack to hide
  the HUD entirely (`cl_screen.c:1900-1903`); positive values are clamped
  by `SCR_ClampScale` to never exceed `viddef.width/320` or
  `viddef.height/240` (`cl_screen.c:1805-1820`). A layout compiler targeting
  yquake2 must treat all its literal pixel coordinates as **pre-scale
  units in a 320x240 (or `xv`/`yv`-relative) virtual canvas**, not raw
  screen pixels.
- **Interpreter loop quirks (all silent, none fatal at the layout level):**
  - Unknown/garbage tokens are logged via `Com_DPrintf` (only visible with
    `developer 1`) and then simply skipped — the parser keeps consuming
    tokens from wherever `COM_Parse` left off. There is no syntax-error
    abort.
  - `if` with an out-of-range stat index silently treats the condition as
    false (`value = 0`) rather than erroring — this means a compiler bug
    that emits `if <badIndex>` will silently hide the guarded block instead
    of failing loudly.
  - `if`'s "skip to endif" scan is **not nesting-aware**: it scans for the
    literal token `"endif"` with no depth counter, so a nested `if..endif`
    inside a skipped block terminates the outer skip early at the inner
    `endif`, leaving the tokens between the inner `endif` and the intended
    outer `endif` to be interpreted as live content. A compiler must not
    emit nested `if` blocks, or must flatten/duplicate them.
  - `anum`/`rnum` have **content-dependent silent early-exits**
    (`anum`: `continue` on negative value with no draw at all; `rnum`:
    `continue` if `value < 1`) that are different from every other numeric
    token — a compiler cannot assume "one token → one guaranteed draw call"
    for these two.
  - `pic`'s two bounds checks (`stats` index range, then resolved image
    index `< MAX_IMAGES`) and `stat_string`'s two bounds checks (`stats`
    index range, then resolved configstring index range) both fail *open*
    (skip silently) rather than clamping or asserting — out-of-range
    operands are a silent no-op, not a visible error, which is worth a
    compiler-side static check since the engine won't surface the mistake
    at runtime.
  - `SCR_DrawFieldScaled` silently clamps any requested `num`/`hnum`/etc.
    field `width` down to 5 digits and no-ops for `width < 1`
    (`cl_screen.c:981-990`) — asking for a 6+-digit field doesn't error,
    it quietly truncates the left side of the rendered number (e.g. a
    6-digit value in a clamped-to-5 field loses its leading digit, not its
    trailing one, since `SCR_DrawFieldScaled` also independently clamps the
    *printed digit count* `l` to `width` at `cl_screen.c:998-1001`).
  - `MSG_ReadString`'s 2048-byte cap on the wire read, followed by
    `Q_strlcpy` truncation into the 1024-byte `cl.layout`, means a
    malicious/buggy server sending a `>1024`-byte `svc_layout` string
    doesn't crash the client — it silently truncates, potentially cutting
    an `if` off before its `endif`, which (per the linear-scan behavior
    above) reads as "condition never resolves" and the interpreter simply
    runs off the end of the (now-NULL) string harmlessly (the `while(s)`
    loop guard at `cl_screen.c:1087` and the `while(s && strcmp(...))` scan
    guard at `cl_screen.c:1460` both terminate cleanly on `s == NULL`).
