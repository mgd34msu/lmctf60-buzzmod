# Development tooling

Development tools are not runtime assets. The public release contains the
declared game modules, pak, version, and checksums. The server bundle adds its
manifest-bound configuration, BSP/RUNE corpus, sidecars, and fleet inputs.

## Setup

Run `tools/setup.sh` for toolchain checks and the analysis virtual environment.
External inputs remain explicit:

- a compatible q2ded engine;
- map BSP files;
- human or serverrecord demos for analysis.

`tools/requirements.txt` pins Python analysis dependencies. Most RUNE readers,
controllers, and corpus checks use only the standard library.

## Tool groups

- RUNE: `runeio.py`, `runelint.py`, `runeaccept.c`, `runegen.sh`,
  `rune_corpus_controller.py`, and `bspmechanisms.py`.
- Runtime evidence: `film.py`, `stallcensus.py`, `conduct.py`, and the sheet
  renderers.
- Telemetry and diagnostics: `gamestat.sh`, `rolestat.py`, `hookevents.py`,
  `hookdiag.py`, and the focused census tools.
- Fleet: the current tracked launch scripts are development-only. Production
  requires the persistent authenticated runner described in the completion
  plan.

Run a tool with `--help` for its current arguments. `tools/README.md` explains
cross-tool inputs and outputs without duplicating every option.

## Rules

1. Bind evidence to source, module, BSP, RUNE, configuration, roster, and time
   window before it can support a release claim.
2. Keep detectors in one importable implementation.
3. Fail when no input rows are recognized.
4. Keep raw demos, corpora, and reports out of the public runtime payload.
5. Never install a module because it happens to exist in the repository root.
6. Stop only owned process generations. Verify ports and process images before
   and after launch.
7. Stage managed bundle paths on the target filesystem, verify them, and retain
   rollback state until finalization.
8. Let q2ded perform native map transitions. A launcher must not emulate a map
   list by restarting the server for each map.

## Current boundary

The current branch has the 175-map RUNE controller and strict readers. It does
not yet contain the reviewed persistent fleet runner or release-bundle
transaction. Integrate those before any production cutover.
