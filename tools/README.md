# tools/ reference

This directory is the development environment described in `TOOLING.md` (read that first — it is the law this README only documents the surface of, and it defines the vocabulary used below: waves, trials, arms, rungs). Nothing in `tools/` ships; the release boundary is the top-level build's Assemble step, and it packages exactly three game modules and a pak, none of them from here.

At a glance, this directory holds four layers:

- **The fleet** — shell scripts that drive ten local dedicated servers through waves/trials/campaigns, deploy new builds between them, and keep the loop alive unattended (`iterate2.sh`, `waveloop.sh`, `wavewatch.sh`, `deploy.sh`, and friends).
- **Instruments** — the blind film-judging chain: five "rung" sheet renderers (`film.py` through `outcomecard.py`) plus `conduct.py` and `tripcensus.py`, each with an in-file Stage-A validity record, plus the judging-protocol documents that govern how their output is shown to human judges.
- **Analysis** — the demo-parsing library and the census/mining/report scripts built on it: entity and kinematics extraction, per-map human defense/escape/traffic mining, carry-ledger forensics, bot telemetry grading, chat mining, hook/rune diagnostics.
- **Fixtures & data** — the corpus manifest, flag-stand positions, POV exclusion rules, and the baked/mined JSON and CSV files the instruments and analysis scripts read and write.

Two things repeat everywhere in this tree and are worth knowing before reading further: **PID-only process discipline** (fleet scripts never `pgrep -f` / `pkill -f`; a runaway `pgrep -f "q2ded"` once killed its own wrapper — see `runegen.sh`'s docstring), and **the blinding discipline** (every rung-1..5 instrument extracts identically from human and bot demos and puts nothing demo-shape-revealing on the rendered sheet — see `film.py`'s module docstring, MODULE NOTES 1-11).

Runtime debris that is intentionally skipped below but worth knowing the shape of: `iter-<N>/` directories and `iter-<N>-launch.log` files (one pair per wave, produced by `iterate2.sh`/`waveloop.sh`; hundreds accumulate), `aux-<N>/` and `aux-<N>-launch.log` (same pattern for `aux2.sh`'s side fleet), `campaign-<timestamp>/` directories (per-run logs from `campaign.sh`), `rune-logs/` (raw per-server logs from `runegen.sh` runs), `__pycache__/` (Python bytecode cache), `waveloop.log` (the fleet heartbeat's running log), `runs-archive/` (rotated launch logs, atomic-replace target of `waveloop.sh`), and one-off match logs like `ab-<map>-<timestamp>.log` at the tools/ root (from `abmatch.sh`) that appear and disappear as matches run.

---

## FLEET (server/wave scripts)

### `iterate2.sh`
- **Purpose:** the current wave launcher — ten servers, free per-server layout (map, matchup, duration all independently settable per server), the fleet's day-to-day driver.
- **Usage:** `tools/iterate2.sh <name>` (name becomes the wave number/tag; output lands in `tools/iter-<name>/`).
- **Inputs:** hardcoded per-server tables at the top of the script (maps, fill, sg_* cvars); `rune.cfg`; the live game build in `$GAMEDIR_ROOT/$GAME`.
- **Outputs:** `tools/iter-<name>/<label>-<map>.log` per server, plus a `gamestat.sh` summary block per server printed at the end.
- **Dependencies:** `gamestat.sh` (per-server summary), a `q2ded` binary, the `lmctf-hooktest` game directory. Refuses to run if `q2ded` is already up (overlap guard). PID-only process discipline; ~7s launch stagger (same-second starts duplicate Q2's RNG per the script's own comments).

### `iterate.sh`
- **Purpose:** the older fixed-format wave launcher — the "owner's mixed-density format" (2v2 duel isolation / 5v5 baseline x5 / 7v7 density stress / 5v1 control), superseded day-to-day by `iterate2.sh`'s free layout but still runnable directly.
- **Usage:** `tools/iterate.sh <name> <duel_map> <five1> <five2> <five3> <five4> <five5> <dens_map> <ctrl_map>`.
- **Inputs/Outputs/Dependencies:** same shape as `iterate2.sh` (rune.cfg, `gamestat.sh`, `tools/iter-<name>/` log dir); the five 5v5 maps rotate across servers by wave number rather than being fixed, to avoid confounding an arm comparison with map identity.

### `waveloop.sh`
- **Purpose:** the fleet's heartbeat — runs `iterate2.sh` forever, deploying the newest build between waves.
- **Usage:** `tools/waveloop.sh <first-wave-number>`; stop with `touch tools/waveloop-stop` or by killing the process group.
- **Inputs:** the newest `*.so` in the repo root; `iterate2.sh`.
- **Outputs:** `waveloop.log` (timestamped wave/deploy lines); increments the wave number and reinvokes `iterate2.sh` each pass; per-wave launch output into `runs-archive/iter-<N>-launch.log`.
- **Dependencies:** `deploy.sh` (atomic build install), `iterate2.sh`. Refuses to increment the wave number on a run that "finished" in under two minutes (spin guard — a prior bug burned wave numbers 438-882 in fifteen minutes without this).

### `wavewatch.sh`
- **Purpose:** watchdog — relaunches `waveloop.sh` if the fleet is found dead (no `q2ded`, no `waveloop.sh` process), meant to run from a systemd `--user` timer every 5 minutes.
- **Usage:** no args; invoked by the `wavewatch` systemd user timer (see `setup.sh` step 6 for install instructions).
- **Inputs:** `waveloop.log` (to compute the next wave number), `waveloop-stop` (honors the same stop file).
- **Outputs:** appends a relaunch line to `waveloop.log`; backgrounds a fresh `waveloop.sh <next-wave>`.
- **Dependencies:** `waveloop.sh`. Born after two host crashes each cost the fleet 30-60 dark minutes.

### `deploy.sh`
- **Purpose:** the only sanctioned path to install a freshly built game module into the live game directory.
- **Usage:** `tools/deploy.sh [<path-to-so>]` (defaults to the newest `*.so` in the repo root); `FORCE=1` overrides the "fleet is live" refusal.
- **Inputs:** a `.so` build; `tools/escape-priors.json` and `tools/slipgate-weights.cfg` if present (ride along as data files).
- **Outputs:** `$GAMEDIR/{game,gamex86_64}.so` (and the two data files), installed via `mv` for atomicity.
- **Dependencies:** none beyond the game directory. Written after a plain `cp` over a live dlopen'd `.so` corrupted mapped pages and segfaulted 18 of 20 servers mid-game (waves 309-310); refuses to run while `q2ded` is up unless `FORCE=1`.

### `campaign.sh`
- **Purpose:** the validation campaign — N maps in parallel, G consecutive 5v5 games per map (fresh process per game, for honest RNG/level-state reset), one aggregate table at the end.
- **Usage:** `tools/campaign.sh` (standard five maps) / `tools/campaign.sh lmctf03 lmctf22` (explicit maps) / `GAMES=3 GAME_SECS=300 tools/campaign.sh` (shorter campaign).
- **Inputs:** `rune.cfg`, the live game build.
- **Outputs:** `tools/campaign-<timestamp>/` (one log per map per game, plus `lanes.log`).
- **Dependencies:** `gamestat.sh`. Staggers launches (same-second starts duplicate Q2's RNG — observed duplicate matches from batch runs 28448/28449 and 28450/28453).

### `abmatch.sh`
- **Purpose:** one A/B match, 4 legacy bots vs 4 SLIPGATE bots, one map — a quick head-to-head sanity check outside the wave format.
- **Usage:** `tools/abmatch.sh <map> <secs>`.
- **Inputs:** `rune.cfg`, the live game build.
- **Outputs:** `tools/ab-<map>-<timestamp>.log`, plus a stdout report (steal/capture totals, per-bot-name frag/death/steal line dump).
- **Dependencies:** none beyond `q2ded`. Documents the confirmed bot-spawn console syntax for both bot systems in its header comment (`sv addbot <name> <skin> <charfile> <charname>` for legacy bots vs `sv sg add` for SLIPGATE bots).

### `aux2.sh`
- **Purpose:** a 4-server auxiliary side-fleet, additive to the main ten — currently running the carrier-cover revalidation and a defense-package decomposition (post-only / react-only / both / neither).
- **Usage:** `tools/aux2.sh <name>`.
- **Inputs:** hardcoded per-server tables (labels, maps, fill, cover/defpost dose); `rune.cfg`; the live game build.
- **Outputs:** `tools/aux-<name>/` (per-server logs and launch log).
- **Dependencies:** same process discipline as `iterate2.sh`; ports 28530-28533, disjoint from the main fleet's range.

### `runegen.sh`
- **Purpose:** serial batch RUNE generator — boots a dedicated server per map, issues `sv rune`, verifies the `.rune` file and reports seed/link counts.
- **Usage:** `tools/runegen.sh [--dry-run] <map1> [map2 ...]`, e.g. `tools/runegen.sh --dry-run $(grep -v '^#' tools/topmaps.txt)`.
- **Inputs:** `topmaps.txt` (typical map list), the live game build.
- **Outputs:** `<gamedir>/maps/<map>.rune` per map; a per-server log under `rune-logs/`; a summary table on stdout.
- **Dependencies:** none beyond `q2ded`. Its docstring is the canonical statement of this tree's PID-only / no-`pgrep -f` rule, written after four earlier runs were killed by a self-matching `pkill -f` pattern. `--dry-run` only until told otherwise — not safe to run for real while the main fleet owns the ports (documented port conflict).

### `gamestat.sh`
- **Purpose:** every observable from one game log, in one quoting-safe pass — steals/caps/returns, kills by weapon, hook fire/land/fail counts, SG telemetry-derived attacker floors and defender occupancy, chat line count, per-weapon accuracy tail.
- **Usage:** `tools/gamestat.sh <log>`.
- **Inputs:** a wave/campaign/match log file.
- **Outputs:** stdout report; no files written.
- **Dependencies:** `python3` (inline heredoc for the SG-telemetry parse). Called by every fleet launcher (`iterate.sh`, `iterate2.sh`, `campaign.sh`) as the end-of-wave summary.

---

## INSTRUMENTS (film analysis)

The blind-judging chain, in rung order. Every sheet renderer shares one extraction discipline (see `film.py`'s docstring): identical processing of human client demos and bot serverrecord demos, no duration/roster-count leaks on the rendered PNG, a non-blind `<hash>.json` sidecar for the unblinding step only. Each of the five sheet tools ships its own `--calibrate` Stage-A gate (ROC-AUC separability on a labeled human/bot set, pass bar 0.85) and records at least one full Stage-A run as a trailing MODULE NOTE in the file — read those before trusting a panel.

### `film.py` — rung 1
- **Purpose:** the base demo walker and rung-1 film sheet: reuses the effects-bit flag-carry signal (`EF_FLAG1`/`EF_FLAG2`) — the one signal identically available in both demo shapes, unlike `svc_print` (measured: 0 print messages across every serverrecord sample checked) — to detect carry windows and render per-demo route/behavior sheets.
- **Usage:** `film.py <demo.dm2> [...more demos] --out <dir> [--runedir <dir>] [--pov-parity [--pov-ent N] [--pov-radius U] [--pov-fov DEG]]`.
- **Inputs:** `.dm2` demos; `<runedir>/<map>.rune` for the map silhouette.
- **Outputs:** `<hash>.png` (sheet) + `<hash>.json` (non-blind sidecar) per demo.
- **Dependencies:** `dm2speed.py`, `demokin.py` (byte-sync only, its output discarded), `demoents.py`-equivalent auto-detect skeleton (shared logic, reimplemented in-file); `numpy`, `matplotlib` (Agg backend) — the film venv.
- **Validity record:** coverage measured directly on this corpus — a non-recorder track in a human client demo carries only 11-42% of frames (whole-demo coverage 0.30-0.41), because a client demo only contains entity updates for players inside the recorder's PVS; a serverrecord bot demo has no PVS culling (coverage 1.000). `--pov-parity` exists specifically to remove that asymmetry by simulating a virtual recorder inside bot demos. `film.py` itself has no `--calibrate` Stage-A gate (it is the shared walker the other four rungs calibrate against); its own module docstring's "MODULE NOTES 1-11" is the limitations list every downstream rung inherits and cites.

### `routesheet.py` — rung 2 (ROUTES)
- **Purpose:** whole-match navigation quality — projects every position sample onto a fixed per-map node graph (quantized from the rune seed cloud, cached at `<runedir>/<map>.nodes.json`) and reads traversal statistics (entropy, edge density, off-graph time, occupancy divergence) off it. Zero new demo parsing — consumes `film.py`'s `d['tracks']` directly.
- **Usage:** `routesheet.py <demo.dm2> [...] --out <dir> [--runedir <dir>] [--pov-parity ...]` / `routesheet.py <demo.dm2> [...] --build-nodes [--runedir <dir>]` / `routesheet.py <demo.dm2> [...] --scalars [--pov-parity]` / `routesheet.py --calibrate [--human <glob>...] [--bot <glob>...] [--maps mactf06 ...] [--radius-check]`.
- **Inputs:** `.dm2` demos, `<runedir>/<map>.rune`, the cached `.nodes.json` per map (built by `--build-nodes`).
- **Outputs:** `<hash>.png` + `<hash>.json` sidecar per demo (same hash-naming as `film.py`, so one demo carries one hash across every rung).
- **Dependencies:** `film.py` (as a library — `walk_demo`, `anonymize`).
- **Validity record:** Stage A (mactf06, 4 human/13 bot) — of six panels, only panel 5 (map-occupancy divergence, humans spread across more of the map than bots: 0.56 vs 0.40 bits on mactf06, 0.81 vs 0.39 on lmctf22) is behaviorally stable under a pov-parity radius sweep (800/900/1000u) **and** replicates on a second map (lmctf22). `mean_route_entropy_bits` and `edge_density` swing 0.14-0.24 under the same sweep — coverage artifacts, not behavior. `revisit_spike2_mass` inverts direction between maps and must not be trusted. lmctf22 is excluded from rung-2 blind sets entirely per `set-composition.md` (it suppresses off-graph flight for both populations).

### `fightsheet.py` — rung 3 (FIGHTS)
- **Purpose:** duel/skirmish behavior — who shoots, at what range, with what weapon, closing or circling, how the fight ends. New parsing layer: decodes `svc_muzzleflash` (entnum + weapon id), previously skipped as 3 bytes by every other walker.
- **Usage:** `fightsheet.py <demo.dm2> [...] --out <dir> [--pov-parity ...]` / `--scalars [--pov-parity]` / `--verify-parser` / `--out <dir> --leak-audit` / `--calibrate [--human <glob>...] [--bot <glob>...] [--maps mactf06 ...] [--radius-check]`.
- **Inputs:** `.dm2` demos.
- **Outputs:** `<hash>.png` + `<hash>.json` sidecar per demo.
- **Dependencies:** `film.py` (shared extraction + hashing).
- **Validity record:** first Stage A (2026-08-06, mactf06, n_human=4, n_bot=38) — gate PASSES on `switch_diagonal_mass` (separability 1.000, radius-stable), but in the **opposite direction the design predicted**: measured, HUMANS are the weapon-diagonal-dominant population (0.899 vs 0.599), because human rapid-fire weapons emit long same-class muzzleflash runs while these bots alternate between two slow weapons (rail/rocket). The panel separates strongly for a different reason than briefed — a judge told the stated reason would read it backwards. Panels 2 and 4 swing >0.10 under the radius perturbation and are flagged unreliable pending a fix. n_human=4 against a design target of >=8 (only 4 of 9 mactf06 human demos clear the 300s duration floor).

### `teamsheet.py` — rung 4 (TEAM PLAY)
- **Purpose:** does a team play as a team — spacing, escort presence on carry, defense posture with steal/capture ticks, push synchronization. Zero new parsing (reuses `film.py`'s tracks + carry windows).
- **Usage:** `teamsheet.py <demo.dm2> [...] --out <dir> [--stands <file.json>] [--pov-parity ...]` / `--scalars [--stands <file.json>] [--pov-parity] [--cache <path>]`.
- **Inputs:** `.dm2` demos; `--stands <file.json>` (falls back to `F.flag_stands()`'s in-demo estimate; raises `StandsMissing` and refuses the whole sheet rather than rendering blank panels when neither source answers).
- **Outputs:** `<hash>.png` + `<hash>.json` sidecar per demo.
- **Dependencies:** `film.py`.
- **Validity record:** addendum Stage A (2026-08-07, 5-point pov-parity sweep x 4-way leave-one-out) — the **only** cell to clear 0.85 at every radius and every exclusion is `escort_fraction` on lmctf22 (0.933-0.962). Every other scalar on both maps is either coverage-sensitive (moves with the untuned pov-parity radius) or sub-gate outright; the panel-3 (defense) numbers are sub-gate everywhere. Per `rung4-protocol.md`, `escort_fraction` (panel 2) is the sole judging centerpiece; panels 1/3/4 render (blind fairness requires it) but may not be treated as evidence alone. `rung4-protocol.md`'s own status: **pre-registered, not yet run** — one dry-run pair only, no judge set shown to anyone under this protocol as of writing.

### `outcomecard.py` — rung 5 (MATCH OUTCOMES)
- **Purpose:** what actually happened — score progression, cap-timing, momentum/lead changes, pressure balance (cumulative steals + running conversion ratio). Zero new parsing, zero new outcome heuristic — reuses `F.classify_outcome`, the same capture classifier `film.py`'s own panel and `teamsheet.py`'s panel 3 already use.
- **Usage:** `outcomecard.py <demo.dm2> [...] --out <dir> [--stands <file.json>] [--pov-parity ...]` / `--scalars [--stands <file.json>] [--pov-parity] [--cache <path>]` / `--calibrate [--human <glob>...] [--bot <glob>...] [--maps mactf06 ...] [--radius-check]`.
- **Inputs:** `.dm2` demos, `--stands <file.json>`.
- **Outputs:** `<hash>.png` + `<hash>.json` sidecar per demo.
- **Dependencies:** `film.py`, shares `teamsheet.py`'s `resolve_stands`.
- **Validity record:** Stage A (2026-08-07, mactf06 + lmctf22) — only `steals_total` on mactf06 clears the gate (separability 0.964); lmctf22 fails outright (best 0.828) and is excluded from rung-5 sets by `set-composition.md`. The passing scalar is a raw carry-window-start count computed *before* `cap_radius` is ever applied, so the design's own +/-100u `cap_radius` stability check is vacuous for it by construction (it can only ever report Delta=0.000); a supplementary pov-parity-radius sweep (800/900/1000u) is the check that actually reaches this scalar and finds it stable (0.966 -> 0.964 -> 0.956). Read as "this bot AI era rushes the flag much less often than humans" (~1.3/min human vs ~0.26/min bot), not a durable population signature — the module's own note expects this to shrink once bot objective-seeking behavior changes. `rung5-protocol.md`'s status: **pre-registered, not yet run**, execution deliberately deferred; four dry-run sheets only.

### `conduct.py`
- **Purpose:** the gross-conduct audit (per-player grind/reversal/spin — "is this player visibly doing something stupid") plus the defense-regime card (approach rate, steal conversion, guard fraction) that normalizes bot-vs-bot and bot-vs-human steal volume by the defense actually faced. Born 2026-08-11: "nobody had WATCHED the bots."
- **Usage:** `conduct.py <demo.dm2> [...] --scalars` (per-demo JSON lines) / `conduct.py --compare --human <glob> --bot <glob>` (pooled two-column card).
- **Inputs:** `.dm2` demos, `--stands <file.json>` (via `--stands` arg, used by the defense-regime half).
- **Outputs:** stdout JSON (JSONL for `--scalars`, one pooled object for `--compare`); no files written directly (see `conduct-baseline.json` below for how a `--compare` run gets saved).
- **Dependencies:** reuses `film.py`'s walker. Explicitly frames its cross-population numbers as rank/ratio evidence only, per the coverage-honesty rule in `TOOLING.md`.

### `tripcensus.py`
- **Purpose:** stage-2 eyes — decomposes conduct.py's steal-gap finding trip by trip: where an attacker's approach to the enemy stand ends (ARRIVED / DIED / TURNED), trips/min, arrival fraction, median distance at death/turn-back. Born 2026-08-12.
- **Usage:** `tripcensus.py --stands <stands.json> <demo.dm2> [...]`.
- **Inputs:** `.dm2` demos, `stands.json`.
- **Outputs:** stdout report.
- **Dependencies:** reuses `film.py`'s walker; same coverage-honest denominators and cross-population caveat as `conduct.py`.

### `rung4-protocol.md`
Blind-judging protocol for `teamsheet.py`. Defines the leak checklist, sealed-caption rules, forced-choice + conviction + reasons judging format, and pins `escort_fraction` (panel 2, lmctf22) as the only validated scalar per the Stage-A addendum above. **Status: pre-registered, not yet run.** Explicitly the structural template `rung5-protocol.md` (and by extension any future rung protocol) is written from.

### `rung5-protocol.md`
Blind-judging protocol for `outcomecard.py`, structurally derived from `rung4-protocol.md`. Pins panel 4's `steals_total` step lines (mactf06 only) as the sole validated evidence; **execution is deliberately deferred** (see its section 2) pending the stage-2 behavior change the Stage-A note itself says should move this number. **Status: pre-registered, not yet run**, four dry-run sheets only.

### `set-composition.md`
The blind-set map-qualification rule: a map may appear in a rung's judging set only if that rung's own Stage-A record shows separability >=0.85 *on that map specifically* — separability recorded on a different map does not transfer. Written 2026-08-09 after two sets were damaged by map choice (a unanimous conviction-4 miscall on lmctf22 for rung 2; the outcomecard Stage-A gate failing outright on the same map). Carries the per-rung qualifying/excluded map table (rungs 2 and 4 discriminate on *opposite* maps — not a contradiction, different behaviors) and the secondary rules: roster matching, briefed (not hidden) recording-shape caveats, sealed captions (map/hash/carry-count only), fresh judges per set.

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
- **Dependencies:** `dm2speed.py`, `demokin.parse_playerstate_full` (used only to stay in byte-sync). Verified client-vs-serverrecord shape detection against `wave265-s04-5v5.dm2` (<1% of blocks parse clean under the wrong assumption).

### `demokin.py`
- **Purpose:** full-fidelity human POV kinematics — velocity, view angles, pm flags, weapon index at 10Hz; the "movement grammar" (air-strafe gain, hop cadence, view-vs-velocity divergence, touchdown friction loss).
- **Usage:** `demokin.py <demo> [...]` (prints per-demo and pooled grammar lines).
- **Inputs:** `.dm2` demos (client shape).
- **Outputs:** stdout.
- **Dependencies:** `dm2speed.py`.
- **Validity record:** first census (40 demos, 2026-08-03) — air gain median +1.5/100ms (p75 +14); view-velocity divergence airborne median 93 degrees.

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
- **Outputs:** `<out_dir>/<map>.human.json` (`{map, demos, frames, transitions, seed_dwell, events}`), merged across maps present in the input set.
- **Dependencies:** `dm2speed.py`. Feeds `humanbake.py` and `defreport.py` (via the `human/` fixture directory).

### `escapee.py`
- **Purpose:** cuts flag-carrier escape trajectories (20s window per steal, from the print-stream steal event to a capture/return/death print or window timeout) and maps them onto rune seeds. Entity-layer data, so ref-cam demos are fair game (only POV kinematics are POV-restricted, per the owner's ruling in `pov-rules.txt`).
- **Usage:** `escapee.py <demo.dm2> [<demo.dm2> ...]`.
- **Inputs:** `.dm2` demos; rune files at `/home/buzzkill/Games/Quake2/lmctf-hooktest/maps` (hardcoded `RUNE_DIR`).
- **Outputs:** `tools/human/<map>.escape.json` (`{map, windows, transitions}`); merges with any prior run's output at the same path (lets the corpus be chunked across timeout-bounded runs).
- **Dependencies:** `dm2speed.py`, `demoents.py` (`parse_delta_entity_track`, `U_REMOVE`), `demokin.parse_playerstate_full`, `demorune.load_seeds`/`SeedGrid`. Feeds `escapebake.py`.

### `escapepriors.py`
- **Purpose:** mines which direction humans leave the flag stand after a steal, as a per-map compass-bucket **distribution** (not a single argmin route) — "mine the best behavior from EACH human, never conform to one." Uses the effects-bit carry state machine (`film.py`'s `carry_windows`), not print text, so it reads the same on either demo shape (though only client demos are counted — a serverrecord win here would poison the prior with a bot's own habit).
- **Usage:** `escapepriors.py <demo.dm2|dir> [...] [--out tools/escape-priors.json] [--min-events N] [--verbose]`.
- **Inputs:** `.dm2` files or directories of them.
- **Outputs:** `tools/escape-priors.json` (hand-formatted, one line per map — read by a hand parser in the game, `sg_arach.c Escape_Load` under `sg_escapeprior`, so the shape is a contract).
- **Dependencies:** imports `film.py` for its walker only, with an inert stand-in `numpy`/`matplotlib` loader so the plotting deps aren't required just to mine priors.

### `humanbake.py`
- **Purpose:** bakes human demo traffic (`demorune.py`'s per-map transition counts) into a per-map `.hmn` binary sidecar, one log-scaled byte per rune link (0 = no human ever ran it), consumed by the game under `sg_humanprior` to price highways cheaper.
- **Usage:** `humanbake.py <rune_dir> <human_json_dir> <map> [<map> ...]`.
- **Inputs:** `<rune_dir>/<map>.rune`, `<human_json_dir>/<map>.human.json` (from `demorune.py`).
- **Outputs:** `<rune_dir>/<map>.hmn` (magic `0x484D4E31`).
- **Dependencies:** none beyond the stdlib.

### `escapebake.py`
- **Purpose:** bakes escapee (post-steal carrier) traffic (`escapee.py`'s per-map transition counts) into a per-map `.hme` binary sidecar, same log-scaled-byte-per-link shape as `humanbake.py`'s output.
- **Usage:** `escapebake.py <rune_dir> <human_json_dir> <map> [<map> ...]` (per its own in-file docstring — see FLAGS).
- **Inputs:** `<rune_dir>/<map>.rune`, `<human_json_dir>/<map>.escape.json` (from `escapee.py`).
- **Outputs:** `<rune_dir>/<map>.hme` (magic `0x484D4531`).
- **Dependencies:** none beyond the stdlib.

### `defbake.py`
- **Purpose:** bakes human defensive occupancy (`demodefense.py`'s dwell/intercept-seed weights) into a per-map `.dpo` binary sidecar (post tier + intercept tier, per team). **Proposed format — the game does not read this yet**; writing the file is harmless tools-side work, the loader/cvar/role-change is game code held pending sign-off.
- **Usage:** `defbake.py <rune_dir> <defense_json_dir> [<map> ...]` (defaults to every map with a `.defense.json` present).
- **Inputs:** `<rune_dir>/<map>.rune`, `<defense_json_dir>/<map>.defense.json` (from `demodefense.py`).
- **Outputs:** `<map>.dpo` beside the rune by default, or under `$DPO_OUT` if set (used to validate the format without touching a live game directory).
- **Dependencies:** none beyond the stdlib.

### `demodefense.py`
- **Purpose:** what human defenders actually do, per map — who is defending (frame-share inside `--defradius` of their own home flag), where they dwell (rune seeds, thinned to posts by `--postsep`), and how they react to a steal (10s window: chase / cut-off / hold, gap-close and drift-off-post metrics).
- **Usage:** `demodefense.py --gamedir DIR --out DIR [--jobs N] [--map NAME] <demo> ...` (also: `--defradius`, `--defshare`, `--minframes`, `--dwellwin`, `--dwellspan`, `--postsep`, `--postlimit`, `--minpostshare`, `--window`, `--minresp`, `--teleport`, `--mindemos`, all with defaults).
- **Inputs:** `.dm2` demos under `--gamedir`; the entity layer (same all-visible-players approach as `demoents.py`); pins players to teams from the `CS_PLAYERSKINS` table.
- **Outputs:** `<out>/<map>.defense.json` (`flags`, `flag_seed`, `defenders`, `posts_by_team`, `dwell_seed`, `dwell_secs`, `response`) — consumed by `defbake.py` and `defreport.py`.
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
- **Purpose:** pulls `info_flag_red`/`info_flag_blue` origins directly out of a map's BSP entity lump (loose `.bsp` or from inside any pak in the game directory) and resolves each to its nearest rune seed — the offline equivalent of the game's own `Rune_NearestSeed()` flag-seed lookup.
- **Usage:** `mapflags.py <gamedir> [<map> ...]` (no maps = every rune found).
- **Inputs:** the game directory (loose BSPs or paks), rune files.
- **Outputs:** stdout (and returns the origins/seeds as a library call).
- **Dependencies:** none beyond the stdlib (`functools.lru_cache`).

### `rolestat.py`
- **Purpose:** grades a wave's role discipline from telemetry — defense time near own flag, pressure time near enemy flag, escort seconds with a live nearby escort, recover-role field-cost trend, wander fraction (`goal=-1`).
- **Usage:** `rolestat.py <wave.log>`.
- **Inputs:** a wave log file (SG telemetry).
- **Outputs:** stdout.
- **Dependencies:** none beyond the stdlib.

### `runelint.py`
- **Purpose:** structural invariants for rune files — self-links, zero/huge-cost links, duplicate triples, orphan/dead-end/source-only seeds, unreachability from seed 0, hook anchors below their firing floor. Every check is a claim the rune generator implicitly makes; a violation is a generator flaw by definition.
- **Usage:** `runelint.py [<rune-or-glob> ...]` (defaults to `/home/buzzkill/Games/Quake2/lmctf-hooktest/maps/*.rune`).
- **Inputs:** `.rune` files.
- **Outputs:** stdout (`FLAW:` lines per file, total count at the end).
- **Dependencies:** none beyond the stdlib.

### `runeview.py`
- **Purpose:** permanent visual dump tool for rune files (replaces "the throwaway python that used to do that," per the SLIPGATE build order) — top-down component view, directed reachability from a goal seed, optional side-elevation slice, stats block, optional diff against an older rune.
- **Usage:** `runeview.py <rune_file> [--goal N] [--region X0,X1,Y0,Y1] [--compare OLD.rune] [-o/--output PATH]`.
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
Navigation data (`.rune` files) is NOT on the list — the game
generates it per map with `sv rune` (or batch: `runegen.sh`).

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
   `chat-corpus.json`. Each tool's section above has the exact
   command line.
4. **Respect your recorders**: `pov-rules.txt` is the pattern for
   excluding specific recorders' POV-derived kinematics while keeping
   their entity-layer data — edit it for your own corpus.

The shipped versions of these artifacts reflect OUR corpus (2020-2023
LMCTF pub play, 268 demos); they work as-is, but the more your maps
and community differ from that, the more regeneration pays.

## FIXTURES & DATA

### `stands.json`
Per-map flag-stand fixture: `{"<map>": {"red": [x,y,z], "blue": [x,y,z]}}`, 19 maps as of writing. Fallback source of flag-stand positions whenever a demo's own carry history doesn't supply one (a color that was never stolen in that demo). Consumed by `teamsheet.py`, `outcomecard.py`, `conduct.py`, and `tripcensus.py` (all via `--stands`).

### `corpus-manifest.csv`
Index of the human demo corpus: `filename, map, duration_s, players, shape, usable` — 268 demos as of writing (269 lines incl. header). `shape` distinguishes `client(human)` from serverrecord captures; `usable` flags demos too short/empty to mine. Referenced by `set-composition.md` as the source of "18 blind-set-capable maps by usable human volume."

### `escape-priors.json`
Mined output of `escapepriors.py`: per-map (and per-map:color) 8-bucket compass distribution of post-steal exit bearings, plus a `_corpus` block (268 files, 1809 steals seen, 1549 used). Hand-formatted (one map per line) because the game reads it with a hand parser (`sg_arach.c Escape_Load`, `sg_escapeprior`). Deployed to the live game directory by `deploy.sh`.

### `pov-rules.txt`
POV kinematics exclusion list (owner ruling, 2026-08-03) — substring match on recorder name; excluded recorders' *POV-derived* data (movement grammar, kinematics) is dropped, but their event-stream/entity-layer data stays usable (the ruling `demoents.py` and `escapee.py` both cite for using ref-cam demos). Carries an explicit "kept despite looking excludable" list and one identity ruling (`serverkill == buzzkill`, "pretty sure").

### `topmaps.txt`
Priority map list (one mapname per line, `#`-comments allowed), ranked by demo popularity analysis. Consumed by `runegen.sh`'s usage example (`tools/runegen.sh $(grep -v '^#' tools/topmaps.txt)`).

### `human/`
Output directory for the demo-mining pipeline — per-map `<map>.human.json` (`demorune.py`), `<map>.escape.json` (`escapee.py`), `<map>.defense.json` (`demodefense.py`); 6.8MB as of writing. Consumed by `humanbake.py`, `escapebake.py`, `defbake.py`, and `defreport.py`. Also contains `<map>.flaglive.json`, `carrywindows.json`, and an `ents/` subdirectory of `<map>.ents.json`/`playersamples.json` files with **no current producer script** in `tools/` — see FLAGS.

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

## FLAGS

- **(FIXED 2026-08-12)** `escapebake.py`'s docstring was mislabeled `humanbake.py` -- It opens with `"""humanbake.py -- bake escapee (carrier post-steal) traffic into per-map .hme sidecars."""` — a stale copy-paste from `humanbake.py` that was never corrected to the file's actual name. Functionally the file is correct (produces `.hme` from `.escape.json`, distinct magic number `0x484D4531`), so this is a documentation bug, not a behavior bug, but it will confuse anyone who reads the file top-down before checking the filename.

- **`humanbake.py` and `escapebake.py` are near-duplicate implementations** (identical struct formats, identical log-scaling tier logic, differing only in which JSON field they read and which sidecar extension/magic they write). This is exactly the copy-paste-drift risk `TOOLING.md`'s tooling law #4 warns about ("a detector lives in one importable place"); the stale docstring above is plausibly a direct symptom of that copy-paste. Worth factoring into one parameterized baker if either file changes again.

- **(RESOLVED 2026-08-12)** `tools/human/`'s producer-less file families are kept as reference data from superseded one-off analyses — documented in FIXTURES & DATA; safe to ignore.

- **(REMOVED 2026-08-12)** `tools/observer-stop` -- (empty file, tools/ root) is not referenced by any `.sh` or `.py` script in this directory (confirmed by grep). The fleet's actual stop file is `waveloop-stop` (checked by `waveloop.sh` and `wavewatch.sh`). `observer-stop` looks like either a dead-man's switch for a removed/unbuilt "observer" script, or a manual note file that never got wired to anything — currently inert either way.

- **`conduct-baseline.json`** has no automated writer or reader in `tools/` (see FIXTURES & DATA above) — flagging in case it's assumed to be kept fresh by some process; as far as the code in this directory shows, it is a manually captured, potentially stale snapshot.

- **`botkin_raw.json`** is written unconditionally every time `botkin.py` is run (not opt-in, no flag to suppress it) but has no reader anywhere in `tools/` — a write-only side effect of a tool whose primary purpose is the stdout report.

- **`setup.sh` and `requirements.txt` appeared in this directory during the course of this inventory** (newer timestamps than everything else read here, and traced to two real git commits — `47db52e` and `a5fa854` — that landed mid-session). Both are legitimate, well-documented dev tooling (environment doctor + its pinned venv requirements) and are included above, but the tree was live and being actively edited by another process while this document was being written, so a re-run of this inventory may find further drift (the same commits also moved `iter-*-launch.log` files into `runs-archive/`, reflected in the debris note at the top of this document).

- **`iterate.sh` (fixed mixed-density format) looks superseded in practice by `iterate2.sh` (free layout)** per `TOOLING.md`'s framing ("driven by tools/iterate2.sh... per the owner's format delegation, 2026-08-04") and `waveloop.sh`/`wavewatch.sh` both hardcode calls to `iterate2.sh`, never `iterate.sh`. `iterate.sh` is still a complete, runnable script, not broken — just not in the live loop's call path anymore. Worth confirming whether it should move to an `attic/` or similar, or whether it's kept deliberately as a fallback fixed format.
