# Development tooling

Development tools are not runtime assets. The public release contains the
declared game modules, pak, version, and checksums. The server bundle adds its
manifest-bound configuration, BSP/RUNE corpus, sidecars, and fleet inputs.

## Setup

Run `tools/setup.sh` for C toolchain, engine, map, and asset checks.
External inputs remain explicit:

- a compatible q2ded engine;
- map BSP files;
- human or serverrecord demos used as retained development evidence.

## Tool groups

- RUNE generation and corpus assembly: `runegen.sh` plus the server-side
  `sv rune` implementation.
- RUNE inspection: `runecompactread.gnu` or `runecompactread.make`. Both call
  the same production C wire inspector.
- Local launch and deployment: `deploy.sh`, `iterate.sh`, and `iterate2.sh`.
- Human capture, telemetry, and learning: the corresponding C game modules.

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

The current branch is replacing the Python tool layer. The final path uses the
game module, one C RUNE inspector, shell process orchestration, and Make release
targets. Optional fleet machinery does not block release.
