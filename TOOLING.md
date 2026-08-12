# The development environment

*This document and LEDGER.md are the two places project-internal
vocabulary is at home — waves, trials, arms, rungs live here freely.
Everything client- or inheritor-facing (ARCHITECTURE.md, STYLE.md,
release notes, code comments) speaks self-contained language instead.*

Written 2026-08-12, split out of ARCHITECTURE.md at the owner's
direction: release assets and development tooling are different worlds,
and neither document may describe the other's structure. THIS is the
tooling world. **Nothing described here ships.** The release boundary
is the build workflow's Assemble step: three game modules and the pak,
enumerated explicitly, nothing implied.

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

- **The fleet** — ten local dedicated servers in paired A/B arms,
  driven by `tools/iterate2.sh` (per-wave configs, trial arming
  arrays with the ref-vs-def check), looped by `tools/waveloop.sh`
  (auto-deploys the newest repo-root .so between waves — which is why
  local builds get deleted the moment they're verified: a stray
  mid-edit build once auto-deployed and hung all ten servers),
  watched by a systemd user timer (`wavewatch`).
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
5. **Tools never leak into release assets** — the packaging list is
   explicit; dev sidecars (runes, danger files, priors) are generated
   server-side, never bundled.
6. **The fleet never stops; refactors and trials never share a
   deploy window.**

## Boundary checklist (applied at every release)

- Assemble step packages exactly: gamex86_64.so, gamex86_64.dll,
  gamex86.dll, lmctf6-buzzmod.pak (+ SHA256SUMS). Nothing from tools/,
  no cfgs, no fixtures, no corpus.
- Release notes describe player/admin-facing behavior only; the
  development record stays in LEDGER.md and the git history.
- CI verified per job, never aggregate.
