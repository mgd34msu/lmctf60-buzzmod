# The development environment

*This document and LEDGER.md are the two places project-internal
vocabulary is at home — waves, trials, arms, rungs live here freely.
Everything client- or inheritor-facing (ARCHITECTURE.md, STYLE.md,
release notes, code comments) speaks self-contained language instead.*

Tool source, corpora, raw evidence, and test fixtures are not public runtime
assets. The current tag workflow publishes the three platform modules, pak,
`VERSION`, and `SHA256SUMS`. The current tree does not yet build the required
authenticated server bundle; that missing boundary is tracked in
`PROJECT-COMPLETION-PLAN.md`.

## Getting the environment (the zero-contact bar)

The acceptance test for everything below: someone who has never spoken
to us clones the repo, runs `tools/setup.sh`, follows what it prints,
and has our dev experience. The doctor checks the toolchain, creates
the film venv from `tools/requirements.txt`, and names every external
thing it cannot conjure (the yquake2 engine build, the map files, a
human demo corpus) with exactly how to supply it. The watchdog units
ship in `tools/systemd/`. The tool-by-tool reference is
`tools/README.md`.

## What the environment is

- **The fleet** — ten dedicated servers and the scripts that configure,
  monitor, record, stop, install, and restart them. Current `iterate2.sh`
  launches one finite `q2ded` process per lane and `waveloop.sh` recreates the
  set. `waveloop.sh` also discovers and deploys a repo-root module. These are
  current defects: production requires ten persistent processes, rotated
  ordered map lists, native transitions, and explicit bundle selection.
- **Film** — serverrecord demos landing in the Yamagi data dir, the
  human corpus (2020-2023 client demos) in the game dir, indexed by
  `tools/corpus-manifest.csv`.
- **Instruments** — film.py (rung 1), routesheet.py (rung 2),
  fightsheet.py (rung 3), teamsheet.py (rung 4), outcomecard.py
  (rung 5), conduct.py and tripcensus.py (conduct + stage-2 eyes),
  fixtures (stands.json, per-map node graphs). Each carries its own
  Stage-A validity record in-file; set composition rules live in
  tools/set-composition.md; judging protocols per rung
  (tools/rung4-protocol.md is the template).
- **Judging** — fresh blind judges per set, sealed captions, forced
  choice + conviction + reasons, pooled against pre-registered bars.
- **The durable film venv** — ~/.venvs/slipgate-film.
- **Analysis scratch** — session scratchpads; anything worth keeping
  is promoted into tools/ or the LEDGER, never left in scratch.

## Tooling law (the STYLE.md counterpart for this world)

1. **Instruments are calibrated before they judge.** Stage-A
   separability on the exact map, or the verdict measures the map.
2. **A verdict on an inert mechanism is void** — liveness peeks before
   floors exist to catch inert-by-construction and inert-by-map arms.
3. **Coverage-honest denominators everywhere** — client-POV film
   conditions what is observable; cross-population reads are ratio
   evidence (the defense-card lesson: its own caveat named the
   artifact that later misled us anyway).
4. **Analysis code obeys DRY like release code** — a detector lives in
   one importable place; copy-pasted analysis loops are how two
   instruments drift while reporting the same name.
5. **Every artifact crosses an explicit boundary.** Tool source, raw film, and
   corpora do not enter the public download. The required server bundle must
   include its manifest-bound modules, pak, production configuration, all 181
   BSP/RUNE pairs, exact top-20 rotated lists, and applicable sidecars.
6. **Deployment is deliberate and transactional.** A stop sentinel, one owner
   lock, port/process quiescence, same-filesystem staging, post-verification,
   and tested rollback are mandatory. Builds are never discovered or installed
   merely because they exist in the repository root.
7. **A fleet process owns its full map cycle.** The server changes maps through
   the game's native `gamemap` lifecycle; launchers do not simulate rotation by
   killing and recreating the process for each map.

## Current release boundary and missing server boundary

- `.github/workflows/build.yml` currently publishes the three platform modules,
  pak, `VERSION`, and `SHA256SUMS`.
- No tracked tool currently assembles and publishes the complete server bundle.
  The required bundle must bind modules, pak, production `rune.cfg`, ordered
  top-20 authority and rotated lists, all 181 BSP/RUNE pairs, and applicable
  sidecars. Bot admission uses the `sv sg` command surface and exact roster
  receipts; there is no separate static bot-roster input.
- Release notes describe player/admin-facing behavior only; the
  development record stays in LEDGER.md and the git history.
- CI verified per job, never aggregate.
- The missing bundle/install/rollback/runtime identity gates are execution-plan
  blockers, not capabilities of the current scripts.
