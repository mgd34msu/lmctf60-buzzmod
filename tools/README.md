# tools/ reference

This directory contains development, analysis, and server-operation tools.
`TOOLING.md` defines their boundaries. Tool source, raw corpora, fixtures, and
evidence are not public runtime assets. The complete authenticated server bundle
described by `PROJECT-COMPLETION-PLAN.md` is not implemented in the current
tracked tools.

At a glance, this directory holds four layers:

- **The fleet** — scripts that launch, monitor, record, stop, install, and
  recover ten dedicated servers (`iterate2.sh`, `waveloop.sh`, `wavewatch.sh`,
  and `deploy.sh`). They currently create finite per-map processes and do not
  implement the required persistent native map cycle.
- **Instruments** — the blind film-judging chain: five "rung" sheet renderers (`film.py` through `outcomecard.py`) plus `conduct.py` and `tripcensus.py`, each with an in-file Stage-A validity record, plus the judging-protocol documents that govern how their output is shown to human judges.
- **Analysis** — the demo-parsing library and the census/mining/report scripts built on it: entity and kinematics extraction, per-map human defense/escape/traffic mining, carry-ledger forensics, bot telemetry grading, chat mining, hook/rune diagnostics.
- **Fixtures & data** — the corpus manifest, flag-stand positions, POV exclusion rules, and the baked/mined JSON and CSV files the instruments and analysis scripts read and write.

Production process ownership must use exact captured PIDs, locks, ports, and
receipts rather than broad name matching. The current `wavewatch.sh` and
`deploy.sh` do not yet meet that rule and are not sanctioned production
interfaces. Blind instruments must extract the same observable from human and
bot film without leaking demo shape into rendered sheets.

Runtime debris that is intentionally skipped below but worth knowing the shape of: `iter-<N>/` directories and `iter-<N>-launch.log` files (one pair per wave, produced by `iterate2.sh`/`waveloop.sh`; hundreds accumulate), `aux-<N>/` and `aux-<N>-launch.log` (same pattern for `aux2.sh`'s side fleet), `campaign-<timestamp>/` directories (per-run logs from `campaign.sh`), `rune-logs/` (raw per-server logs from `runegen.sh` runs), `__pycache__/` (Python bytecode cache), `waveloop.log` (the fleet heartbeat's running log), `runs-archive/` (launch logs appended by `waveloop.sh`), and one-off match logs like `ab-<map>-<timestamp>.log` at the tools/ root (from `abmatch.sh`) that appear and disappear as matches run.

---

## FLEET (current tracked scripts)

The entries below document what the scripts do now. Their open production
defects are tracked in `PROJECT-COMPLETION-PLAN.md`.

### `iterate2.sh`
- **Purpose:** currently launches ten one-map server processes, waits for the
  configured duration, stops them, and summarizes their logs.
- **Usage:** `tools/iterate2.sh <name>` (name becomes the wave number/tag; output lands in `tools/iter-<name>/`).
- **Inputs:** hardcoded per-server tables at the top of the script (maps, fill, sg_* cvars); `rune.cfg`; the live game build in `$GAMEDIR_ROOT/$GAME`.
- **Outputs:** `tools/iter-<name>/<label>-<map>.log` per server, plus a `gamestat.sh` summary block per server printed at the end.
- **Dependencies:** `gamestat.sh`, a `q2ded` binary, and the configured game
  directory. Its `lmctf-hooktest` default, launch stagger, per-map termination,
  and hard-coded roster/map tables are current limitations and are not the
  production fleet contract.

### `iterate.sh`
- **Purpose:** mixed-density standalone launcher. `waveloop.sh` does not call it;
  it remains pending an operator/consumer decision in the necessity audit.
- **Usage:** `tools/iterate.sh <name> <duel_map> <five1> <five2> <five3> <five4> <five5> <dens_map> <ctrl_map>`.
- **Inputs/Outputs/Dependencies:** same shape as `iterate2.sh` (rune.cfg, `gamestat.sh`, `tools/iter-<name>/` log dir); the five 5v5 maps rotate across servers by wave number rather than being fixed, to avoid confounding an arm comparison with map identity.

### `waveloop.sh`
- **Purpose:** currently runs `iterate2.sh` repeatedly, implicitly deploys a
  discovered build, and recreates the server processes.
- **Usage:** `tools/waveloop.sh <first-wave-number>`; stop with `touch tools/waveloop-stop` or by killing the process group.
- **Inputs:** currently discovers a repo-root module and calls `iterate2.sh`;
  this is recorded as behavior to remove, not an approved interface.
- **Outputs:** `waveloop.log` (timestamped wave/deploy lines); increments the wave number and reinvokes `iterate2.sh` each pass; per-wave launch output into `runs-archive/iter-<N>-launch.log`. A failed/overlap-refused wave never name-kills `q2ded`; `iterate2.sh` owns and waits for its own child PIDs, while unrelated servers are left untouched.
- **Dependencies:** `deploy.sh`, `iterate2.sh`. Refuses to increment the wave
  number when a run finishes in under two minutes.

### `wavewatch.sh`
- **Purpose:** currently restarts `waveloop.sh` based on process-name checks.
  It does not authenticate an owner generation, lock, ports, or the exact
  ten-server state.
- **Usage:** no args; invoked by the `wavewatch` systemd user timer (see `setup.sh` step 6 for install instructions).
- **Inputs:** `waveloop.log` (to compute the next wave number), `waveloop-stop` (honors the same stop file).
- **Outputs:** appends a relaunch line to `waveloop.log`; backgrounds a fresh `waveloop.sh <next-wave>`.
- **Dependencies:** `waveloop.sh`.

### `deploy.sh`
- **Purpose:** installs a selected or discovered Linux module and optional data
  files. It does not install or roll back a complete authenticated server
  bundle.
- **Usage:** the tracked script still accepts an optional module and can discover
  a repo-root build. That default and `FORCE` bypass are current defects, not
  final operator interfaces.
- **Inputs:** a `.so` build; `tools/escape-priors.json` and `tools/slipgate-weights.cfg` if present (ride along as data files).
- **Outputs:** `$GAMEDIR/{game,gamex86_64}.so` (and the two data files), installed via `mv` for atomicity.
- **Dependencies:** none beyond the game directory. Refuses to run while
  `q2ded` is up unless `FORCE=1`; that bypass is not an approved production
  interface.

### `campaign.sh`
- **Purpose:** the validation campaign — N maps in parallel, G consecutive 5v5 games per map (fresh process per game, for honest RNG/level-state reset), one aggregate table at the end.
- **Usage:** `tools/campaign.sh` (standard five maps) / `tools/campaign.sh lmctf03 lmctf22` (explicit maps) / `GAMES=3 GAME_SECS=300 tools/campaign.sh` (shorter campaign).
- **Inputs:** `rune.cfg`, the live game build.
- **Outputs:** `tools/campaign-<timestamp>/` (one log per map per game, plus `lanes.log`).
- **Dependencies:** `q2ded` and the configured game directory. It implements
  its own aggregate grep summary rather than calling `gamestat.sh`.

### `abmatch.sh`
- **Status:** not a working current launcher. It attempts `sv addbot` for the
  legacy side, but the current `ServerCommand` dispatcher has no `addbot`
  command, so it cannot create the documented 4-vs-4 match.
- **Disposition:** keep out of production use; the necessity audit must either
  remove it or pair it with a restored, tested legacy-bot command path.

### `aux2.sh`
- **Purpose:** a 4-server auxiliary side-fleet, additive to the main ten — currently running the carrier-cover revalidation and a defense-package decomposition (post-only / react-only / both / neither).
- **Usage:** `tools/aux2.sh <name>`.
- **Inputs:** hardcoded per-server tables (labels, maps, fill, cover/defpost dose); `rune.cfg`; the live game build.
- **Outputs:** `tools/aux-<name>/` (per-server logs and launch log).
- **Dependencies:** same process discipline as `iterate2.sh`; ports 28530-28533, disjoint from the main fleet's range.

### `runegen.sh`
- **Purpose:** single-attempt RUNE generation primitive — boots explicitly named
  maps in an isolated game-directory mirror, issues `sv rune`, runs artifact
  gates, and leaves the selected destination unchanged on failure. The 181-map
  corpus controller owns retries, dual-reader agreement, applicable semantic
  checks, fresh-process cold loads, and terminal accounting.
- **Usage:** `tools/runegen.sh [--dry-run] <map1> [map2 ...]`. The authoritative
  conversion list is `rune-corpus-maps.txt`; `topmaps.txt` is only the
  production fleet's ordered 20-map rotation.
- **Inputs:** explicit map names; `Q2DED`; `GAMEDIR_ROOT`/`GAME` containing the
  selected module, maps, and configs; and `RUNE_ACCEPT` plus its declared build
  file/target. Snapshot hashing and all-map accounting are controller duties,
  not standalone `runegen.sh` behavior.
- **Outputs:** `<gamedir>/maps/<map>.rune` only after the gate passes; the prior rune under `rune-logs/backups/`; server and lint logs under `rune-logs/`; a summary table on stdout. Any failure leaves the deployed rune untouched.
- **Dependencies:** `q2ded`, Python 3, `runelint.py`, and the configured C
  acceptor. Run it only in an isolated root on ports disjoint from the active
  fleet.

### `gamestat.sh`
- **Purpose:** every observable from one game log, in one quoting-safe pass — steals/caps/returns, kills by weapon, hook fire/land/fail counts, SG telemetry-derived attacker floors and defender occupancy, chat line count, per-weapon accuracy tail.
- **Usage:** `tools/gamestat.sh <log>`.
- **Inputs:** a wave/campaign/match log containing production SG rows in
  `role=... seed=... goal=... sgoal=... spd=... org=(...)` order. Rows without `sgoal`
  remain readable for existing fixtures.
- **Outputs:** stdout report; no files written.
- **Failure:** exits nonzero when no SG telemetry row is recognized; an empty
  role report is not a successful game summary.
- **Dependencies:** `python3` (inline heredoc for the SG-telemetry parse).
  `iterate.sh` and `iterate2.sh` call it; `campaign.sh` currently implements a
  separate summary rather than invoking this script.

---

## INSTRUMENTS (film analysis)

The blind-judging chain, in rung order. Every sheet renderer shares one
extraction discipline (see `film.py`'s docstring): identical processing of
human client demos and bot serverrecord demos, no duration/roster-count leaks
on the rendered PNG, and a non-blind `<hash>.json` sidecar for unblinding only.
Calibration records and limitations live with each instrument and its protocol;
this reference documents current interfaces, not old trial results.

### `film.py` — rung 1
- **Purpose:** the base demo walker and rung-1 film sheet: reuses the effects-bit flag-carry signal (`EF_FLAG1`/`EF_FLAG2`) — the one signal identically available in both demo shapes, unlike `svc_print` (measured: 0 print messages across every serverrecord sample checked) — to detect carry windows and render per-demo route/behavior sheets.
- **Usage:** `film.py <demo.dm2> [...more demos] --out <dir> [--runedir <dir>] [--pov-parity [--pov-ent N] [--pov-radius U] [--pov-fov DEG]]`.
- **Inputs:** `.dm2` demos; `<runedir>/<map>.rune` for the map silhouette.
- **Outputs:** `<hash>.png` (sheet) + `<hash>.json` (non-blind sidecar) per demo.
- **Dependencies:** `dm2speed.py`, `demokin.py` (byte-sync only, its output discarded), `demoents.py`-equivalent auto-detect skeleton (shared logic, reimplemented in-file); `numpy`, `matplotlib` (Agg backend) — the film venv.
- **Measurement boundary:** client demos contain only PVS-visible entity
  updates, while serverrecord demos do not have that culling. `--pov-parity`
  simulates the client observation boundary for bot film. `film.py` is the
  shared walker and has no standalone `--calibrate` gate.

### `routesheet.py` — rung 2 (ROUTES)
- **Purpose:** whole-match navigation quality — projects every position sample onto a fixed per-map node graph (quantized from the rune seed cloud, cached at `<runedir>/<map>.nodes.json`) and reads traversal statistics (entropy, edge density, off-graph time, occupancy divergence) off it. Zero new demo parsing — consumes `film.py`'s `d['tracks']` directly.
- **Usage:** `routesheet.py <demo.dm2> [...] --out <dir> [--runedir <dir>] [--pov-parity ...]` / `routesheet.py <demo.dm2> [...] --build-nodes [--runedir <dir>]` / `routesheet.py <demo.dm2> [...] --scalars [--pov-parity]` / `routesheet.py --calibrate [--human <glob>...] [--bot <glob>...] [--maps mactf06 ...] [--radius-check]`.
- **Inputs:** `.dm2` demos, `<runedir>/<map>.rune`, the cached `.nodes.json` per map (built by `--build-nodes`).
- **Outputs:** `<hash>.png` + `<hash>.json` sidecar per demo (same hash-naming as `film.py`, so one demo carries one hash across every rung).
- **Dependencies:** `film.py` (as a library — `walk_demo`, `anonymize`).

### `fightsheet.py` — rung 3 (FIGHTS)
- **Purpose:** duel/skirmish behavior — who shoots, at what range, with what weapon, closing or circling, how the fight ends. New parsing layer: decodes `svc_muzzleflash` (entnum + weapon id), previously skipped as 3 bytes by every other walker.
- **Usage:** `fightsheet.py <demo.dm2> [...] --out <dir> [--pov-parity ...]` / `--scalars [--pov-parity]` / `--verify-parser` / `--out <dir> --leak-audit` / `--calibrate [--human <glob>...] [--bot <glob>...] [--maps mactf06 ...] [--radius-check]`.
- **Inputs:** `.dm2` demos.
- **Outputs:** `<hash>.png` + `<hash>.json` sidecar per demo.
- **Dependencies:** `film.py` (shared extraction + hashing).

### `teamsheet.py` — rung 4 (TEAM PLAY)
- **Purpose:** does a team play as a team — spacing, escort presence on carry, defense posture with steal/capture ticks, push synchronization. Zero new parsing (reuses `film.py`'s tracks + carry windows).
- **Usage:** `teamsheet.py <demo.dm2> [...] --out <dir> [--stands <file.json>] [--pov-parity ...]` / `--scalars [--stands <file.json>] [--pov-parity] [--cache <path>]`.
- **Inputs:** `.dm2` demos; `--stands <file.json>` (falls back to `F.flag_stands()`'s in-demo estimate; raises `StandsMissing` and refuses the whole sheet rather than rendering blank panels when neither source answers).
- **Outputs:** `<hash>.png` + `<hash>.json` sidecar per demo.
- **Dependencies:** `film.py`.

### `outcomecard.py` — rung 5 (MATCH OUTCOMES)
- **Purpose:** what actually happened — score progression, cap-timing, momentum/lead changes, pressure balance (cumulative steals + running conversion ratio). Zero new parsing, zero new outcome heuristic — reuses `F.classify_outcome`, the same capture classifier `film.py`'s own panel and `teamsheet.py`'s panel 3 already use.
- **Usage:** `outcomecard.py <demo.dm2> [...] --out <dir> [--stands <file.json>] [--pov-parity ...]` / `--scalars [--stands <file.json>] [--pov-parity] [--cache <path>]` / `--calibrate [--human <glob>...] [--bot <glob>...] [--maps mactf06 ...] [--radius-check]`.
- **Inputs:** `.dm2` demos, `--stands <file.json>`.
- **Outputs:** `<hash>.png` + `<hash>.json` sidecar per demo.
- **Dependencies:** `film.py`, shares `teamsheet.py`'s `resolve_stands`.

### `conduct.py`
- **Purpose:** per-player grind/reversal/spin audit plus a defense-regime card
  with approach rate, steal conversion, and guard fraction.
- **Usage:** `conduct.py <demo.dm2> [...] --scalars` (per-demo JSON lines) / `conduct.py --compare --human <glob> --bot <glob>` (pooled two-column card).
- **Inputs:** `.dm2` demos, `--stands <file.json>` (via `--stands` arg, used by the defense-regime half).
- **Outputs:** stdout JSON (JSONL for `--scalars`, one pooled object for `--compare`); no files written directly (see `conduct-baseline.json` below for how a `--compare` run gets saved).
- **Dependencies:** reuses `film.py`'s walker. Explicitly frames its cross-population numbers as rank/ratio evidence only, per the coverage-honesty rule in `TOOLING.md`.

### `tripcensus.py`
- **Purpose:** decomposes enemy-stand approaches by terminal state (ARRIVED,
  DIED, TURNED), trips per minute, arrival fraction, and terminal distance.
- **Usage:** `tripcensus.py --stands <stands.json> <demo.dm2> [...]`.
- **Inputs:** `.dm2` demos, `stands.json`.
- **Outputs:** stdout report.
- **Dependencies:** reuses `film.py`'s walker; same coverage-honest denominators and cross-population caveat as `conduct.py`.

### `rung4-protocol.md`
Blind-judging protocol for `teamsheet.py`: qualification, leak checks, sealed
captions, forced choice, conviction, and reason capture. It must be rerun on the
exact final-build receipts before acceptance.

### `rung5-protocol.md`
Blind-judging protocol for `outcomecard.py`. It defines the outcome-card leak
checks and judging procedure; qualified final-build execution remains open.

### `set-composition.md`
Defines map-specific qualification, roster matching, disclosed recording-shape
limits, sealed captions, and fresh judges. Qualification does not transfer
between maps or instruments.

---

## ANALYSIS (censuses/eyes)

The shared parsing library and the mining/report/forensics scripts built on it. `dm2speed.py` is the low-level foundation — `import dm2speed as D` appears in `demoents.py`, `demokin.py`, `demoprints.py`, `film.py`, `fightsheet.py`, `botkin.py`, `escapee.py` (and, indirectly through `demokin`/`demoents`, most of the rest of this section).

### `dm2speed.py`
- **Purpose:** the shared `.dm2` protocol-34 byte reader and low-level message decoder (entity-bits parser, sound/temp-entity shapes, POV velocity trace) — every other demo-parsing tool in this tree is built on it rather than re-deriving the wire format.
- **Usage:** `dm2speed.py <demo.dm2>` (standalone: prints per-frame speed/hspeed/vz plus a high-speed episode digest, threshold `--hi` default 700).
- **Inputs:** `.dm2` demos.
- **Outputs:** stdout (standalone use); as a library, exposes `R` (byte reader), `parse_entity_bits`, `parse_delta_entity`, `parse_packetentities`, `parse_playerstate`, `parse_sound`, `parse_temp_entity`.
- **Dependencies:** none beyond the stdlib.

### `demoents.py`
- **Purpose:** the entity layer — decodes every visible player's trajectory (not just the POV), auto-detecting client vs serverrecord `.dm2` shape. The corpus multiplier: one POV demo carries partial trajectories of every player the recorder ever saw.
- **Usage:** `demoents.py <demo.dm2> [...]` (prints per-demo track counts).
- **Inputs:** `.dm2` demos.
- **Outputs:** stdout; as a library, `walk_entities()` returns `{map, pov, skins, tracks, frames, svrecord}`.
- **Dependencies:** `dm2speed.py`, `demokin.parse_playerstate_full` (used only
  to stay in byte-sync). The parser distinguishes client and serverrecord demo
  shapes, but catches per-demo parse exceptions and can return partial or empty
  data; callers must enforce their own nonzero/coverage gate.

### `demokin.py`
- **Purpose:** full-fidelity human POV kinematics — velocity, view angles, pm flags, weapon index at 10Hz; the "movement grammar" (air-strafe gain, hop cadence, view-vs-velocity divergence, touchdown friction loss).
- **Usage:** `demokin.py <demo> [...]` (prints per-demo and pooled grammar lines).
- **Inputs:** `.dm2` demos (client shape).
- **Outputs:** stdout.
- **Dependencies:** `dm2speed.py`.

### `demoprints.py`
- **Purpose:** scouting tool — dumps the `svc_print` stream and the playerskin table verbatim, so other extractors can key off real strings instead of guesses.
- **Usage:** `demoprints.py <demo.dm2> [...]`.
- **Inputs:** `.dm2` demos.
- **Outputs:** stdout (first 200 prints per demo + skin table sample).
- **Dependencies:** `dm2speed.py`, `demokin.parse_playerstate_full`, `demoents.parse_delta_entity_track`. `walk_prints()` is imported and reused by `chatmine.py`.

### `demorune.py`
- **Purpose:** maps human POV routing onto a map's rune seed graph — per demo, compresses the position trace to a seed-visit sequence and aggregates seed-to-seed transition counts plus flag-event timestamps.
- **Usage:** `demorune.py <rune_dir> <out_dir> <demo> [<demo> ...]`.
- **Inputs:** `.dm2` demos (map read from configstring 33), `<rune_dir>/<map>.rune`.
- **Outputs:** atomically written `<out_dir>/<map>.human.json` (`{map, rune identity, demos, frames, transitions, seed_dwell, events}`), merged across maps present in the input set. Tombstones retain their encoded indices but are excluded from localization.
- **Dependencies:** `dm2speed.py`. Feeds `humanbake.py` and `defreport.py` (via the `human/` fixture directory).

### `escapee.py`
- **Purpose:** cuts flag-carrier escape trajectories (20s window per steal, from the print-stream steal event to a capture/return/death print or window timeout) and maps them onto rune seeds. Entity-layer data, so ref-cam demos are fair game (only POV kinematics are POV-restricted, per the owner's ruling in `pov-rules.txt`).
- **Usage:** `escapee.py [--rune-dir DIR] [--out DIR] [--replace] <demo.dm2> [<demo.dm2> ...]`.
- **Inputs:** `.dm2` demos and rune files under `--rune-dir` (default `/home/buzzkill/Games/Quake2/lmctf-hooktest/maps`).
- **Outputs:** `<out>/<map>.escape.json`, identity-stamped from the same decoded rune snapshot used for localization. The default is an identity-checked merge so timeout-bounded chunks retain earlier work; `--replace` explicitly discards the previous output. Writes are atomic.
- **Dependencies:** `dm2speed.py`, `demoents.py` (`parse_delta_entity_track`, `U_REMOVE`), `demokin.parse_playerstate_full`, `demorune.load_seed_graph`/`SeedGrid`. Feeds `escapebake.py`.

### `escapepriors.py`
- **Purpose:** mines which direction humans leave the flag stand after a steal, as a per-map compass-bucket **distribution** (not a single argmin route) — "mine the best behavior from EACH human, never conform to one." Uses the effects-bit carry state machine (`film.py`'s `carry_windows`), not print text, so it reads the same on either demo shape (though only client demos are counted — a serverrecord win here would poison the prior with a bot's own habit).
- **Usage:** `escapepriors.py <demo.dm2|dir> [...] [--out tools/escape-priors.json] [--min-events N] [--verbose]`.
- **Inputs:** `.dm2` files or directories of them.
- **Outputs:** `tools/escape-priors.json` (hand-formatted, one line per map — read by a hand parser in the game, `sg_arach.c Escape_Load` under `sg_escapeprior`, so the shape is a contract).
- **Dependencies:** imports `film.py` for its walker only, with an inert stand-in `numpy`/`matplotlib` loader so the plotting deps aren't required just to mine priors.

### `humanbake.py`
- **Purpose:** bakes human demo traffic (`demorune.py`'s per-map transition counts) into one log-scaled byte per ordered rune link. It accepts only an authenticated rune and a matching identity-stamped corpus.
- **Usage:** `humanbake.py <rune_dir> <human_json_dir> <map> [<map> ...]`.
- **Inputs:** `<rune_dir>/<map>.rune`, `<human_json_dir>/<map>.human.json` (from `demorune.py`).
- **Outputs:** `<rune_dir>/<map>.hmn` (wire magic `HMNR`).
- **Dependencies:** `corpusgraph.py`, `sidecario.py`; otherwise stdlib.

### `flaglivebake.py`
- **Purpose:** bakes newly mined flag-live traffic into `.hml`; input derived from any different graph is rejected and must be mined again.
- **Usage:** `flaglivebake.py <rune_dir> <human_json_dir> <map> [<map> ...]`.
- **Inputs:** only a newly mined `<map>.flaglive.json` carrying the exact RUNE identity. Unstamped JSON is not a bake input.
- **Outputs:** given a valid current corpus, writes `<map>.hml` (wire magic `HMLR`) beside the rune.
- **Dependencies:** `corpusgraph.py`, `sidecario.py`; otherwise stdlib.

### `escapebake.py`
- **Purpose:** bakes escapee (post-steal carrier) traffic using the same byte-per-ordered-link shape as `humanbake.py`.
- **Usage:** `escapebake.py <rune_dir> <human_json_dir> <map> [<map> ...]`.
- **Inputs:** `<rune_dir>/<map>.rune`, `<human_json_dir>/<map>.escape.json` (from `escapee.py`).
- **Outputs:** `<rune_dir>/<map>.hme` (wire magic `HMER`).
- **Dependencies:** `corpusgraph.py`, `sidecario.py`; otherwise stdlib.

### `defbake.py`
- **Purpose:** bakes human defensive occupancy (`demodefense.py`'s dwell/intercept-seed weights) into four seed-indexed post/intercept planes. Any nonzero tier assigned to a tombstone is rejected.
- **Usage:** `defbake.py <rune_dir> <defense_json_dir> [<map> ...]` (defaults to every map with a `.defense.json` present).
- **Inputs:** `<rune_dir>/<map>.rune`, `<defense_json_dir>/<map>.defense.json` (from `demodefense.py`).
- **Outputs:** `<map>.dpo` beside the rune by default, or under `$DPO_OUT` if set (used to validate the format without touching a live game directory).
- **Dependencies:** `corpusgraph.py`, `sidecario.py`; otherwise stdlib.

`sidecario.py` defines the common 48-byte explicit-little-endian header
(`struct <I6H8I`). Fields in order are: kind magic; zero reserved; header
bytes `48`; zero reserved; element bytes; plane count; zero reserved;
`num_seeds`; `num_links`; exact rune payload CRC; action-contract CRC; exact
rune header CRC; payload bytes; payload CRC; and header CRC. The header CRC is
computed with its own four bytes zero. `HMNR`, `HMLR`, and `HMER` are one
`u8[num_links]` plane; `DPOR` is four `u8[num_seeds]` planes; `DNGR` is two
explicit-LE `i32[num_seeds]` planes. Unknown magic or shape, nonzero
reserved data, count or exact-size mismatch, trailing bytes, any CRC mismatch,
or any rune binding mismatch fails closed under the stable `SCD_*` diagnostic
domain. Every `DNGR` value must be in `0..8000`, and both `DNGR` and `DPOR`
require zero in every plane at a rune tombstone. The C runtime loader and
Python producer are checked against the same golden vector. At level setup the runtime
loads `HMNR`, `HMLR`, `HMER`, and `DPOR` independently into candidates; absent
files are neutral, malformed or stale files are logged and ignored whole, and
none becomes visible before the rune, fields, and fresh authority check publish
together. `DNGR` uses the authenticated runtime lifecycle below.

The runtime owns that danger-sidecar lifecycle. It is opt-in: the shipped
`sg_dangerpersistport 0` performs no danger-sidecar read or write. A nonzero selector must
match the engine's canonical protected effective port and acquire the stable
`<map>.rune.danger.lock` advisory lease before loading. Only that whole-level
lease holder may load or save; contenders run an ephemeral neutral model.
The game directory that names the lease is immutable for that level; drift
refuses a save. Danger decays during compatible active play only, not across
offline wall-clock or intermission time, and checkpoints only immediately
before normal `ExitLevel` rotation and clean shutdown. Replacement uses a
same-directory nonce-scoped exclusive temporary so crash remnants do not
exhaust one fixed namespace. The final atomic replacement rechecks the live
authority, policy, held lease, unchanged model state, and exact installed
rune header.
A rejected existing danger sidecar disables persistence for that level; an absent file
may be created after new learning. Direct engine `map` and savegame restoration
remain reset-only because neither boundary carries an authenticated outgoing
transaction.

Every baker fails nonzero on a missing or malformed
requested corpus and uses same-directory atomic replacement, so a failed bake
leaves the destination unchanged. The atomic precommit rereads the RUNE binding and
reports `SCD_STATE_DRIFT` rather than replacing a current sidecar with output
derived from a concurrently regenerated rune. Seed-indexed JSON carries the
exact-case map,
seed count/CRC, BSP/entity identity, and proof physics from the
single decoded RUNE snapshot used for localization. Unstamped JSON is not
accepted: re-mine it rather than manually adding a stamp or trying to
reinterpret unrelated seed numbers.

### `demodefense.py`
- **Purpose:** what human defenders actually do, per map — who is defending (frame-share inside `--defradius` of their own home flag), where they dwell (rune seeds, thinned to posts by `--postsep`), and how they react to a steal (10s window: chase / cut-off / hold, gap-close and drift-off-post metrics).
- **Usage:** `demodefense.py --gamedir DIR --out DIR [--jobs N] [--map NAME] <demo> ...` (also: `--defradius`, `--defshare`, `--minframes`, `--dwellwin`, `--dwellspan`, `--postsep`, `--postlimit`, `--minpostshare`, `--window`, `--minresp`, `--teleport`, `--mindemos`, all with defaults).
- **Inputs:** `.dm2` demos under `--gamedir`; the entity layer (same all-visible-players approach as `demoents.py`); pins players to teams from the `CS_PLAYERSKINS` table.
- **Outputs:** atomically written, RUNE-identity-stamped `<out>/<map>.defense.json` (`flags`, `flag_seed`, `defenders`, `posts_by_team`, `dwell_seed`, `dwell_secs`, `response`) — consumed by `defbake.py` and `defreport.py`. A fresh rollup read rejects graph drift across workers, and tombstones are excluded from localization.
- **Dependencies:** `multiprocessing.Pool` (`--jobs`).

### `defreport.py`
- **Purpose:** readable tables out of `demodefense.py`'s `.defense.json` corpus — per-map corpus/dwell/response summary, top defensive posts per team, pooled response-kind breakdown and weighted median delay.
- **Usage:** `defreport.py <human_dir> [--posts N]` (default `N=8`).
- **Inputs:** `<human_dir>/*.defense.json`.
- **Outputs:** stdout tables only.
- **Dependencies:** none beyond the stdlib.

### `carryforensics.py`
- **Purpose:** builds the per-carry ledger from wave logs — for every `CARRY X begins`..`ends` episode: route progress (goal cost at steal, minimum reached, fraction remaining), how/who ended it, escort geometry, and steal-stand->home-stand axis projection.
- **Usage:** `carryforensics.py [<root>] [<lo>] [<hi>] [<outfile>]` (positional, no flags; defaults: root=`tools/`, lo=274, hi=308, outfile=`/dev/stdout`) — reads `<root>/iter-<N>/*.log` for N in `[lo, hi]`.
- **Inputs:** `iter-<N>/` wave log directories.
- **Outputs:** one JSON record per carry, JSONL, to stdout or `<outfile>`.
- **Dependencies:** `multiprocessing.Pool(8)`. Feeds `carryreport.py`.

### `carryreport.py`
- **Purpose:** reads the carry ledger (`carryforensics.py`'s JSONL) and answers route-stage questions: where carries end by stage (early/middle/late), killer profile by stage, respawn-stream timing, escort presence at the kill, cap-vs-death comparison, per-map breakdown, progress dynamics (reach-then-regress).
- **Usage:** `carryreport.py <ledger.jsonl>`.
- **Inputs:** a carry-ledger JSONL file (from `carryforensics.py`).
- **Outputs:** stdout tables (Q1-Q5 plus several breakdowns).
- **Dependencies:** none beyond the stdlib.

### `botkin.py`
- **Purpose:** bot movement grammar from serverrecord demos — the same air_gain/view_div/touch_loss/relaunch metrics as `demokin.py`'s human grammar, plus "visible jank" metrics (stop-start frequency, in-place 180-turns, wall bumps, still-time share, straight-vs-curved mix, 1Hz turn gauge). Self-contained (doesn't reuse `demoents`'s tracker because it also needs yaw/`ANGLE2` for a body-facing proxy); bot entities carry no `player_state_t`, so all kinematics are derived from 10Hz origin deltas.
- **Usage:** `botkin.py <demo.dm2> [...]` (prints per-bot and pooled grammar).
- **Inputs:** serverrecord `.dm2` demos.
- **Outputs:** stdout; also always writes `tools/botkin_raw.json` (full per-track stats dump) as a side effect of running.
- **Dependencies:** `dm2speed.py`. Reused as a library by `seedservo.py`.

### `botledger.py`
- **Purpose:** appends per-bot per-wave rows to a longitudinal CSV ledger — one row per bot per game (wave, server, bot, main role, efficiency, speed, distance, progress, steals, caps, kills, deaths) — so any bot's line can be traced across every wave it ever played.
- **Usage:** `botledger.py <wave> <log> [<log> ...]`.
- **Inputs:** wave/game log files (parses `SG <bot>: role=... sgoal=...` telemetry lines and the kill feed).
- **Outputs:** appends to `tools/botledger.csv` (writes the header row on first creation).
- **Dependencies:** none beyond the stdlib.

### `chatmine.py`
- **Purpose:** mines real pub chat out of the human demo corpus (public `say` and team `say_team` prints at `PRINT_CHAT` level), filtered down to genuine human voice — drops team-channel item-report binds (mostly scripted), match-logistics lines, and anything matching a real player's name (dropped outright, not scrubbed, to avoid echoing a stranger's handle back). Bucketed into categories (GREETING, TAUNT, GRUMBLE, GG_ENDGAME, REACTION, CALL) meant to replace hand-written bot chat lines that "sound like 1998 Quake 2 CTF chat" without being it.
- **Usage:** `chatmine.py [demo-dir]` (default `~/Games/Quake2/lmctf-hooktest/demos`).
- **Inputs:** a directory of `.dm2` demos.
- **Outputs:** `tools/chat-corpus.json` (`source, demos, players_seen, chat_lines_seen, kept_unique, drops, buckets, counts, promotions_absent_from_corpus`); prints the kept, bucketed list to stdout for review.
- **Dependencies:** `demoprints.walk_prints` (reused, not re-derived); `multiprocessing.Pool`.

### `effstat.py`
- **Purpose:** progress-efficiency grades for one game log — distance walked vs route progress made, measured against `sgoal=` telemetry (static stand field cost; falls back to `goal=` on old logs). ~300 u/s clean route execution converts to ~3.3 ms/u; near-zero efficiency is motion without progress.
- **Usage:** `effstat.py <log>`.
- **Inputs:** a wave/game log file.
- **Outputs:** stdout — one `EFF` line per role (aggregate) and one `BOT` line per individual (a fleet average hides the laggard or the star).
- **Dependencies:** none beyond the stdlib.

### `hookclose.py`
- **Purpose:** did the grapple rope actually pull? Compares distance-to-anchor immediately before `HOOKFIRE` vs immediately after `HOOKEND`; a successful pull overwrites velocity with a flat 800 u/s straight at the anchor (`p_weapon.c:2088-2092`), making a real pull unambiguous. `burst`/`apex`/`drop` are known-pulled controls; `noattach` is the population under test.
- **Usage:** `hookclose.py <iter-dir> [...]`.
- **Inputs:** `iter-<N>/` wave log directories (SG telemetry + HOOKFIRE/HOOKEND lines).
- **Outputs:** stdout report.
- **Dependencies:** none beyond the stdlib.

### `hookdiag.py`
- **Purpose:** cross-references HOOKFIRE/HOOKEND/HOOKABORT/HOOKBITE against 1Hz SG telemetry to bucket the `noattach` mass by cause. Reads logs only — no game code touched.
- **Usage:** `hookdiag.py <iter-dir> [<iter-dir> ...]`.
- **Inputs:** `iter-<N>/` wave log directories.
- **Outputs:** stdout report.
- **Dependencies:** none beyond the stdlib.

### `mapflags.py`
- **Purpose:** pulls `info_flag_red`/`info_flag_blue` origins from a loose or packed `.ent` override when present, otherwise from the map's loose or packed BSP entity lump, and resolves each with `Rune_NearestSeed()`'s linked-seed, vertical, and weighted-distance rules. The engine's world-collision trace remains the runtime authority on ambiguous stacked geometry.
- **Usage:** `mapflags.py [--out PATH] <gamedir> [<map> ...]` (no maps = every loose rune found).
- **Inputs:** the game directory (loose BSPs or paks), rune files.
- **Outputs:** stdout by default (and returns the origins/seeds as a library call); `--out PATH` explicitly requests an atomic aggregate JSON write. It never rewrites `tools/human/mapflags.json` implicitly.
- **Dependencies:** none beyond the stdlib (`functools.lru_cache`).

### `rolestat.py`
- **Purpose:** grades a wave's role discipline from telemetry — defense time near own flag, pressure time near enemy flag, escort seconds with a live nearby escort, recover-role field-cost trend, wander fraction (`goal=-1`).
- **Usage:** `rolestat.py <wave.log>`.
- **Inputs:** one or more wave logs. Current rows use `sgoal` as the stable
  destination cost; rows without it fall back to `goal`.
- **Failure:** exits nonzero when a file contains no recognized SG telemetry.
- **Outputs:** stdout.
- **Dependencies:** none beyond the stdlib.

### `runelint.py`
- **Purpose:** structural invariants for rune files — magic/reserved/count/map/exact-size checks, runtime record bounds, link/action controls, duplicates/orphans/dead ends, and reverse reachability to both flag objective seeds. Every check is a claim the generator implicitly makes; a violation is a generator flaw by definition.
- **Usage:** `runelint.py --objective-roots RED BLUE <one-rune>`. The default glob is `/home/buzzkill/Games/Quake2/lmctf-hooktest/maps/*.rune`.
- **Inputs:** `.rune` files. Inspection mode can approximate objectives from loose/packed ENT or BSP assets or fall back to a sampled graph root. Deployment requires the exact red/blue seed indices that `Rune_Generate` prints from the server's post-spawn flag entities; `runegen.sh` captures and supplies them automatically, avoiding disagreement with engine overrides and collision settling.
- **Outputs:** stdout (`FLAW:` lines per file, total count at the end); exits nonzero if any flaw is found or an input glob matches nothing.
- **Dependencies:** none beyond the stdlib.

### `runeio.py`
- **Purpose:** authenticates one generated artifact, decodes its seed, link, mechanism-node, inventory-edge, and activation-plan records, and enforces the generated action/controller admission contract. A plan-required link with no plan, an unexpected plan, or a mismatched controller is a hard failure.
- **Usage:** `runeio.py ARTIFACT`. Use `runeio.py --expected-identity REFERENCE_RUNE ARTIFACT` to require the artifact's authenticated map/BSP/entity/physics identity to match a reference RUNE, or add `--require-mechanisms` for a focused fixture gate that additionally requires nonzero trigger, node, inventory-edge, and plan counts. Corpus generation uses normal structural acceptance because valid maps may omit any mechanism class; every plan-required traversal link still requires a unique valid binding.
- **Inputs:** one `.rune` artifact.
- **Outputs:** one JSON object including `trigger_count`, `node_count`, `inventory_edge_count`, `plan_edge_count`, and `plan_count`; exits nonzero on any wire, CRC, contract, graph, or plan-law failure.
- **Dependencies:** none beyond the stdlib.

### `runeview.py`
- **Purpose:** visual RUNE dump: top-down components, directed reachability
  from a goal seed, optional side elevation, statistics, and artifact diff.
- **Usage:** `runeview.py <rune> [--goal N] [--region X0,X1,Y0,Y1] [--compare OTHER.rune] [-o/--output PATH]`.
- **Inputs:** a `.rune` file (and optionally a second one to diff against); looks for the map's `info_flag*` origin next to the rune file to default `--goal`.
- **Outputs:** a single self-contained HTML file (inlined SVG/CSS/JS, no external resources), default `<rune_file>.html`; also prints a plaintext stats summary.
- **Dependencies:** none beyond the stdlib.

### `turnsplit.py`
- **Purpose:** the "honest" turn gauge, split three ways so combat dodging and hook-swing arcs don't get counted as navigation jank — median 1Hz heading change separately for ground travel (`eng=0 hp=0`), hook travel (`eng=0 hp>0`), and combat (`eng=1`).
- **Usage:** `turnsplit.py <iter-dir> [<iter-dir> ...]`.
- **Inputs:** `iter-<N>/` wave log directories (1Hz SG telemetry: `org=`, `eng=`, `hp=`).
- **Outputs:** stdout.
- **Dependencies:** none beyond the stdlib.

### `seedservo.py`
- **Purpose:** tests whether bot heading noise is caused by the navigator steering at the *center* of the current rune link's destination seed (`sg_arach.c` ~3808) rather than a smoother pursuit target — measures retarget rate, chain-turn-vs-body-turn at each retarget, and a counterfactual "pure pursuit" steering rule replayed along the same recorded trajectory, to price a proposed fix before touching game code.
- **Usage:** `seedservo.py <rune_dir> <demo.dm2> [<demo.dm2> ...]` (env `SS_LOOK=250` sets the pure-pursuit look-ahead distance in units).
- **Inputs:** serverrecord `.dm2` demos, `<rune_dir>/<map>.rune`.
- **Outputs:** stdout.
- **Dependencies:** `botkin.py` (kinematics), `demorune.SeedGrid`.

---

## WHAT YOU SUPPLY

This repository ships source code, our own pak
(`assets/lmctf6-buzzmod.pak`), and every mined/derived data artifact.
Five things it cannot ship, with where each comes from:

| You supply | Why we can't ship it | Where to get it |
|---|---|---|
| **Quake 2 base data** (`baseq2/pak0.pak`) | retail id Software content | a Quake 2 purchase (Steam/GOG/original CD); place `baseq2/` under your Q2 root |
| **The engine** (`q2ded` + client) | built for your machine | build yquake2 from source (github.com/yquake2/yquake2); `setup.sh` looks for it or honors `$Q2DED` |
| **LMCTF base mod assets** (models, sounds, textures) | the original mod's distribution | the LMCTF 6.0 release archives (community mirrors); unpack into your mod directory alongside our pak |
| **Map files** (`.bsp`, loose or in paks) | community/retail map content | LMCTF map archives; the fleet's rotation maps are listed in `iterate2.sh`'s MAPS table — supply at least those |
| **A human demo corpus** (`.dm2`) | private recordings | your own — see below |

`tools/setup.sh` checks for all of these and names what's missing.
The accepted server bundle contains the exact 181 BSP/RUNE pairs generated and
accepted from the final source. They are server runtime inputs, not loose public
downloads.

### Bring your own demos

The human demo corpus itself is NOT distributed with this repository —
the `.dm2` files we mined are private recordings. Everything derived
from them ships (priors, baked sidecars, the mined chat corpus, the
manifest), and every one of those artifacts can be regenerated from
YOUR own demo collection:

1. **Drop your client-recorded `.dm2` files** into the fleet game
   directory's `demos/` folder (default
   `~/Games/Quake2/lmctf-hooktest/demos/`; `setup.sh` checks for it).
   Any LMCTF protocol-34 demos work — your own recordings, ref-cam
   captures, tournament archives.
2. **Index them**: `corpus-manifest.csv` is six columns
   (`filename, map, duration_s, players, shape, usable`); regenerate
   it by running any walker over your set — `demoents.py <demos>`
   prints per-demo map/track counts, and a demo is "usable" when it
   has real players and enough duration for the instrument floors
   (300s for judging sheets).
3. **Re-mine the priors and traffic** for the maps you play:
   `demorune.py` (route traffic) → `humanbake.py`; `escapee.py`
   (carrier escapes) → `escapebake.py`; `demodefense.py` (defensive
   posts) → `defbake.py`; `escapepriors.py` (exit-bearing priors) →
   `escape-priors.json`; `chatmine.py` (chat voice) →
   `chat-corpus.json`. Mine `<map>.flaglive.json` against the exact RUNE before
   baking `.hml`; seed numbers are never reused across different graphs. Each
   tool's section above has the exact command line.
4. **Respect your recorders**: `pov-rules.txt` is the pattern for
   excluding specific recorders' POV-derived kinematics while keeping
   their entity-layer data — edit it for your own corpus.

The tracked corpus manifest indexes the LMCTF client-demo inputs used by these
analysis tools. Final behavioral claims require exact receipt binding as defined
by the completion plan.
Non-seed-indexed priors and reports work as reference artifacts, but unstamped
seed-indexed JSON is not a RUNE input and must be re-mined. For regenerable
artifacts, the more your maps and community differ from ours, the more
regeneration pays.

## FIXTURES & DATA

### `stands.json`
Per-map flag-stand fixture: `{"<map>": {"red": [x,y,z], "blue": [x,y,z]}}`, 19 maps as of writing. Fallback source of flag-stand positions whenever a demo's own carry history doesn't supply one (a color that was never stolen in that demo). Consumed by `teamsheet.py`, `outcomecard.py`, `conduct.py`, and `tripcensus.py` (all via `--stands`).

### `corpus-manifest.csv`
Index of the human demo corpus: `filename, map, duration_s, players, shape, usable` — 268 demos as of writing (269 lines incl. header). `shape` distinguishes `client(human)` from serverrecord captures; `usable` flags demos too short/empty to mine. Referenced by `set-composition.md` as the source of "18 blind-set-capable maps by usable human volume."

### `escape-priors.json`
Mined output of `escapepriors.py`: per-map (and per-map:color) 8-bucket compass distribution of post-steal exit bearings, plus a `_corpus` block (268 files, 1809 steals seen, 1549 used). Hand-formatted (one map per line) because the game reads it with a hand parser (`sg_arach.c Escape_Load`, `sg_escapeprior`). Deployed to the live game directory by `deploy.sh`.

### `pov-rules.txt`
POV kinematics exclusion list using recorder-name substring matching. Excluded
recorders are omitted from POV-derived movement/kinematics but remain usable for
event-stream and entity-layer analysis. Ambiguous identity entries must not be
treated as final authority without a receipt-backed identity decision.

### `topmaps.txt`
Exact ordered 20-map production fleet list (one mapname per line,
`#`-comments allowed), ranked by demo popularity analysis. It is not the RUNE
conversion authority; all 181 names live in `rune-corpus-maps.txt`.

### `human/`
Output directory for the demo-mining pipeline — per-map `<map>.human.json` (`demorune.py`), `<map>.escape.json` (`escapee.py`), `<map>.defense.json` (`demodefense.py`); 6.8MB as of writing. Consumed by `humanbake.py`, `escapebake.py`, `defbake.py`, and `defreport.py`. Seed-indexed `<map>.flaglive.json` files without a matching current rune identity are not bake inputs and must be re-mined or spatially migrated before use. The directory also contains `carrywindows.json` and an `ents/` subdirectory of `<map>.ents.json`/`playersamples.json` reference files — see FLAGS.

### `botkin_raw.json`, `botledger.csv`, `chat-corpus.json`
Data files produced by (respectively) `botkin.py`, `botledger.py`, `chatmine.py` — see those entries above for shape and producer detail. `botkin_raw.json` has no consumer script in `tools/` (write-only side effect); `botledger.csv` is the appended longitudinal ledger read only by ad hoc analysis outside this tree; `chat-corpus.json` is likewise a terminal artifact (meant for human/manual review, per `chatmine.py`'s own usage text).

### `conduct-baseline.json`
A `{"human": ..., "bot": ...}` snapshot matching the shape `conduct.py --compare` prints to stdout. No `--out` flag exists on `conduct.py` and no script in `tools/` reads this file back — it reads as a manually saved `conduct.py --compare --human ... --bot ... > conduct-baseline.json` reference snapshot, not part of an automated pipeline.

### `setup.sh`
- **Purpose:** the development-environment doctor/bootstrapper — checks toolchain, film venv (auto-creates it via `requirements.txt` if missing), the `q2ded` engine, the fleet game directory and map assets, the human/bot demo corpora, and the `wavewatch` systemd timer; prints exactly what's missing and how to supply it. Idempotent, safe to rerun.
- **Usage:** `tools/setup.sh` (no args).
- **Inputs:** environment (`$SLIPGATE_VENV`, `$Q2DED`, `$Q2ROOT`), the repo tree.
- **Outputs:** stdout doctor report; may create the film venv as a side effect. Exit code is the missing-item count (0 = clean).
- **Dependencies:** `requirements.txt` (film venv package pins: matplotlib 3.11.1, numpy 2.5.1, and their transitive deps). Self-documenting: explicitly names `tools/README.md` (this file) and `TOOLING.md` as its companion docs.

### `requirements.txt`
Pinned film-venv dependencies (`contourpy`, `cycler`, `fonttools`, `kiwisolver`, `matplotlib==3.11.1`, `numpy==2.5.1`, `packaging`, `pillow`, `pyparsing`, `python-dateutil`, `six`). Installed by `setup.sh` into `~/.venvs/slipgate-film` (or `$SLIPGATE_VENV`).

---

## MISC

### `systemd/wavewatch.service`, `systemd/wavewatch.timer`
- **Purpose:** the fleet watchdog's systemd `--user` unit pair — `wavewatch.service` is a `oneshot` that runs `wavewatch.sh`; `wavewatch.timer` fires it every 5 minutes (`OnBootSec=3min`, `OnUnitActiveSec=5min`).
- **Usage:** not invoked directly — install with `cp tools/systemd/wavewatch.* ~/.config/systemd/user/ && systemctl --user daemon-reload && systemctl --user enable --now wavewatch.timer` (per `setup.sh` step 6 and `wavewatch.sh`'s own docstring).
- **Inputs:** none beyond invoking `wavewatch.sh`.
- **Outputs:** none directly; the service's `ExecStart` is `tools/wavewatch.sh`'s absolute path.
- **Dependencies:** `wavewatch.sh`. `KillMode=process` so a `oneshot` run doesn't drag down anything `wavewatch.sh` backgrounds (the detached `waveloop.sh` it may relaunch).

---

## Current maintenance facts

- `humanbake.py` and `escapebake.py` still duplicate their log-scaling tier
  builder. Authentication and byte encoding are shared in `sidecario.py`.
- `conduct-baseline.json` has no automated writer or reader. It is a manually
  captured result and cannot be used as final evidence without a receipt.
- `botkin_raw.json` is an unconditional write-only side effect of `botkin.py`;
  no tracked tool reads it.
- `botledger.csv` is appended by `botledger.py`; no tracked tool reads it.
- `iterate.sh` is not called by `waveloop.sh` or `wavewatch.sh`. It remains a
  standalone entry point until an operator-use decision is recorded.
- `setup.sh` and `requirements.txt` are active environment bootstrap inputs.

File retention and removal decisions are recorded in
`docs/repository-hygiene.md`.
