# Repository hygiene and evidence retention

This document records the August 18, 2026 whole-tree necessity audit. It does
not replace the execution plan. Its purpose is to keep generated debris out of
Git without deleting source, reproducible inputs, or evidence that is still
needed to close the project.

## Decision rule

Every tracked or retained path must have all applicable relationships recorded:

- **owner** — the source or operator responsible for it;
- **consumer** — runtime, build, workflow, tool, test, document, or operator
  path that reads it;
- **producer/reproducer** — the command and immutable inputs that can recreate
  generated content;
- **release role** — public asset, server-bundle input, development-only input,
  evidence archive, or no release role;
- **identity** — source/module/BSP/RUNE/config/demo hashes when the contents
  make a behavioral claim.

A path may be removed only when searches cover source, build files, workflows,
launchers, tests, documentation, and operator entry points; no current consumer
or unreproduced evidence depends on it; and the removal passes both build
dialects and the relevant focused tests. Deletion uses an explicit reviewed path
list, never `git add -A` or a broad recursive cleanup.

## Retained and required

- Game and SLIPGATE C/H source, root symlink aliases, tests, both Make dialects,
  Visual Studio projects, workflow, generated action contracts, and their
  authoring JSON are active build inputs.
- `sqlite3.c` and `sqlite3.h` are the vendored SQLite 3.7.13 amalgamation used
  directly by every platform build. Their small local delta is intentional and
  provenance-audited; they are not generated junk.
- `assets/lmctf6-buzzmod.pak` is a runtime/public asset and belongs in the final
  authenticated server bundle.
- `tools/rune-corpus-maps.txt` is the exact 181-map conversion authority.
  `tools/topmaps.txt` is the distinct ordered 20-map production rotation. Both
  are required and must never substitute for each other.
- Human/demo analysis corpora that have a live producer and consumer remain
  development inputs. Seed-indexed material cannot become final authority until
  it is bound to the exact final RUNE identity or regenerated from retained,
  hash-bound observations.
- `RELEASES.md` defines the current publish contract. `LEDGER.md` and
  `TRIALS.md` retain experiment inputs that are still referenced by analysis
  tools and judging protocols; they are not current status authorities.
- `SLIPGATE-IMPLEMENTATION-ROADMAP.md` is a short redirect to the current plan.
  Keeping the redirect prevents old links from silently landing on no guidance;
  the superseded roadmap body remains available through Git history.

## Retained pending a final disposition

- `recovery/` contains source fragments, a recovery patch, and three COFF
  objects. It has no runtime role, but it may be the only provenance for the
  recovered stats implementation. Keep it until an owner either documents that
  provenance role or proves that Git history contains an equivalent source.
- `tools/human/ents/*.json`, `tools/human/pro/*.json`,
  `tools/human/carrywindows.json`, `tools/human/ents/playersamples.json`, and
  `tools/botledger.csv` have incomplete current producer/consumer or final
  receipt chains. They are not final behavioral authority. Preserve them until
  the data-provenance workstream either binds/reproduces them or explicitly
  archives/removes them.
- The tracked standalone launchers remain readable implementation inputs until their
  final persistent-fleet replacements and operator docs are accepted. After
  cutover, each old entry point requires a separate consumer/operator check
  before removal.

## Generated state removed from version control

`.goodvibes/` contains session cache and health state, is already ignored, and
has no build, runtime, test, release, or evidence role. Its three tracked files
are removed from the index in the documentation-hygiene change; local copies
may be recreated by the tool and remain ignored.

The obsolete `assets/bots.cfg` and `tools/abmatch.sh` paths were also removed.
Both depended on the deleted legacy `sv addbot` command; current bot admission
uses the tested `sv sg` command surface, and no runtime, build, or operator path
consumed the old roster file.

The dated HOOK migration manifest and verbose pre-SemVer release narratives
were removed from the working documentation. Their temporary paths and old
milestone claims are not current operator inputs; the exact records remain in
Git history and tags. `RELEASES.md` now contains the release contract consumed
by the tag workflow.

The following classes are transient and must not be committed:

- root host-test binaries (`*.gnu`, `*.make`) and `tools/pov-supervisor`;
- `__pycache__/` and Python bytecode;
- `.pov-native-live-*` sandboxes, GL caches, launcher statuses, and temporary
  client configs;
- active campaign, wave, auxiliary, RUNE, and one-off server logs;
- stale one-run GDB probes and other root diagnostics;
- `docs-layout-isa.md`, an untracked byte-for-byte duplicate of the tracked
  `docs/layout-isa.md`.

Do not globally ignore `.rune`, `.dm2`, or `.log`: intentional fixtures and
authenticated evidence can use those extensions. Ignore only the known
transient roots and names.

## Untracked evidence cleanup gate

The current workspace contains diagnostics, modules, runes, demos, and logs
from earlier runs. Some are stale—the GDB probes embed missing temporary game
roots, obsolete line numbers, and compiled-image offsets—but they are not
deleted while current RUNE/fleet work may still need their hashes or traces.
After final evidence is frozen:

1. write an explicit inventory with path, size, hash, producer, and retained
   claim;
2. move evidence that must survive outside the Git worktree into a read-only,
   content-addressed archive;
3. remove only the reviewed transient list;
4. prove `git status`, both build dialects, and final acceptance are unchanged.
