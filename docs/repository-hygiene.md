# Repository hygiene and evidence retention

Keep source and reproducible inputs in Git. Keep transient runs, build products,
and unbound evidence out of Git.

## Path decisions

Before retaining or deleting a path, identify:

- its owner and current consumer;
- its producer and reproducible inputs;
- whether it is a public asset, server-bundle input, development input, or
  retained evidence;
- the source, module, BSP, RUNE, configuration, demo, and time identities needed
  for any behavioral claim.

Delete only an explicit reviewed path list. Search source, builds, workflows,
tests, tools, documentation, and operator entry points first. Run both build
dialects and the relevant focused tests afterward.

## Active tracked inputs

- Game and SLIPGATE C/H source, root `sg_*.c` aliases, tests, both Make dialects,
  Visual Studio projects, workflow files, generated action contracts, and
  `slipgate/rune_actions.json` are active build inputs.
- `sqlite3.c` and `sqlite3.h` are the vendored SQLite 3.7.13 source used by all
  platform builds.
- `assets/lmctf6-buzzmod.pak` is a required runtime and public asset.
- `tools/rune-corpus-maps.txt` is the 175-map conversion authority.
  `tools/topmaps.txt` is the separate ordered 20-map fleet rotation.
- Demo-derived corpora remain development inputs only while their producer and
  consumer are live. Seed-indexed data must be regenerated or rebound after a
  RUNE identity change.
- Historical experiments and superseded plans belong in Git history, not in
  tracked diary documents or source comments.

## Authored source limits

The authored-source policy covers `slipgate/`, `tools/`, `tests/`, `GNUmakefile`,
and `Makefile`. It excludes generated files and `tests/support/` imports. It does
not classify the root Quake II and LMCTF source or vendored SQLite as new project
code.

`tools/deslop_audit.py` reads `tools/source-size-budget.json`. The audit rejects
new files over 800 lines. It also rejects lines over 100 columns in files that
have no exception. An existing exception may not grow. A smaller exception makes
the audit fail until the budget is lowered. Run `make deslop-test` with either
Make dialect.

Split source at an ownership or lifecycle boundary. Keep the public interface in
one header. Do not use arbitrary line ranges or numbered files as boundaries.

The human entity/pro files, carry-window data, `botledger.csv`, and similar
analysis data do not have complete final receipt chains. Preserve them until
the completion plan either reproduces and binds them or removes them through an
explicit data-provenance decision.

## Transient state

Do not commit:

- module objects, shared libraries, host-test binaries, or Python bytecode;
- temporary game roots, GL caches, launcher state, active locks, and stop files;
- campaign, wave, auxiliary, RUNE-generation, or one-off server logs;
- unaccepted demos, generated RUNEs, debugger scripts, or local diagnostics.

Do not globally ignore `.rune`, `.dm2`, or `.log`; tracked fixtures and accepted
evidence may use those extensions. Ignore known transient roots and names.

Evidence that must survive a run belongs in a content-addressed archive outside
the worktree. Record its path, size, hash, producer, inputs, and claim before
removing the worktree copy. A cleanup is complete only when `git status`, both
build dialects, and the applicable acceptance checks are unchanged.
