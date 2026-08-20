# Layout system

LMCTF UI code compiles bounded Quake II layout programs and serves them through
shared caches. [`layout-isa.md`](layout-isa.md) documents the client protocol.

## Limits

| Resource | Limit |
|-|-|
| Network message | 1400 bytes (`MAX_MSGLEN`) |
| Client layout buffer | 1024 bytes |
| Statusbar program | 1535 bytes |
| Player stats | 32 signed 16-bit slots |
| General configstrings | 512 slots of 64 bytes |

A layout update shares its network message with the rest of the frame. UI code
therefore uses a lower enforced budget and never relies on client truncation.

## Components

- `ui_stats.h` assigns stat slots in one registry.
- `ui_text.c` provides bounded text assembly.
- `ui_layout.c` compiles rows and cells, validates tokens, enforces the wire
  budget, and selects a complete density variant. It never publishes a partial
  screen.
- `ui_boards.c` owns board data and cache lifetimes.

## Serving policy

- **Settled boards** rebuild after the match-end statistics commit. One result
  serves every viewer for the next map.
- **Ticked boards** rebuild at most once per second when their input is dirty.
- **Milestone boards** rebuild immediately for rare events such as captures and
  match end.

Layouts carry structure. Frequently changing values use stats or configstrings
so the client can redraw without receiving a new program. Content too deep for
one bounded passive screen belongs in console output rather than pagination.

## Change rules

- Register new stat slots centrally and prove that indices do not collide.
- Add tokens only when the client interpreter supports them.
- Test the largest roster and longest values against the compiled byte budget.
- Select full, condensed, or minimal variants as a whole.
- Keep database queries out of per-viewer render paths.
