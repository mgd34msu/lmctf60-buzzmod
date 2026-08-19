# RUNE corpus controller

This is the implementation and acceptance contract for the durable corpus
controller. Do not run the corpus until the production module is
rebuilt from the frozen source tree and the first generated artifact passes
both the C and Python acceptance paths.

## Fixed corpus

- Use `tools/rune-corpus-maps.txt` as the sole ordered map manifest.
- Require exactly 181 safe, unique names.
- Require both the original `lmctf02` BSP and its padded `lmctf02c`
  replacement as distinct corpus identities. Neither may stand in for the
  other.
- The manifest SHA-256 is
  `9bc55cb287f0b9d99fccf54cc1339e65fba30459e49b0b77cf1b67896c125452`.
- Assign stable ports by manifest index.  The reserved default range is
  62000-62180; all selected TCP and UDP ports must be bind-tested before any
  engine starts.  A different base is part of the run fingerprint.

This 181-map manifest is the conversion corpus. The production server rotation
and its cold-load/wave gate use the separate exact-20 runtime map list; passing
that runtime gate never reduces or substitutes for converting this corpus.

## Required process discipline

Implement and test these fail-closed behaviors:

- atomic file replacement followed by file and parent-directory `fsync`;
- controller `flock`, owner record, boot ID, process start ticks, pidfd capture,
  exact executable/cmdline hashes, and stale-owned-child recovery;
- per-map immutable attempt directories and monotonically numbered attempts;
- parent-death guard, exact-child shutdown, heartbeat, terminal result
  validation, summary regeneration, and contaminated-resume rejection;
- per-attempt private engine and game directories.  Never run a shared engine
  process or allow a generated artifact from an earlier attempt into staging.

No `pgrep`, `pkill`, process-name match, command-substring match, or fleet-wide
signal is permitted.  A signal may target only a captured pidfd whose boot ID,
start ticks, executable, command hash, and owner record still match.  Failure
to prove ownership leaves the process untouched and records an infrastructure
failure.

## Input freeze

Build an immutable input snapshot from the approved production build. The
snapshot must contain:

- the exact `q2ded` executable;
- both production module filenames, required to have identical SHA-256 bytes;
- the game assets used to resolve every manifest map;
- `tools/runelint.py`, `tools/runeio.py`, `tools/snagrepair.py`,
  `tools/rune_contracts_generated.py`, the distinct GNUmakefile-built
  `runeaccept.gnu`, and Makefile-built `runeaccept.make` commands;
- the semantic-checker manifest and every Python checker it names (currently
  the ten-controller/two-flag-route-core checker for `lmctf58`);
- the generator configuration and the map manifest.

Write `input-manifest.json` by hashing every regular input file, rejecting
symlinks and unsupported file types, then make the snapshot read-only.  The
controller must verify every path, mode, size, and SHA-256 before launch and
again before accepting the final summary.

The canonical run fingerprint is the SHA-256 of sorted compact JSON containing
the complete input-manifest hash, ordered map-manifest hash, engine and module
hashes, generated action and mechanism contract hashes, linter/reader,
`snagrepair.py`, both-C-acceptor hashes, semantic manifest/checker hashes and applicability, generation
and startup timeouts, job count, port base, engine arguments, and controller
source hash. Resume is allowed only when the complete stored fingerprint
document is byte-for-byte equal to the newly computed document.

## Current generation and acceptance grammar

Use these anchored success and failure lines:

```text
rune: objective roots red=RED blue=BLUE
rune: wrote PATH (SEEDS seeds, LINKS links, NODES mechanism nodes, TRIGGERS triggers, INVENTORY inventory edges, PLANS activation plans)
```

The success path requires exactly one final write line for the requested map,
a regular newly created artifact inside that attempt, two distinct in-range
objective roots, no later failure line, and clean shutdown.  Parse and retain
all six counts.

Before publishing `PASS`, execute both independently built C acceptors, both
general Python gates, and every semantic checker applicable to the map against
the same artifact bytes:

```sh
runeaccept.gnu ARTIFACT
runeaccept.make ARTIFACT
python3 tools/runeio.py ARTIFACT
python3 tools/runelint.py ARTIFACT
python3 tools/lmctf58_rune_accept.py --objective-roots RED BLUE ARTIFACT  # lmctf58
```

The GNU C, Make C, and Python JSON reports must agree on map name, seeds, links,
mechanism nodes, triggers, inventory edges, plan edges, and plans. Their counts
must also agree with the generator banner. Record every output and hash. Any
missing required plan, controller mismatch, CRC or contract error, count
disagreement, or nonzero gate exit is `LINT_FAIL`; it is never resumable as
success.

After those gates, copy the unchanged artifact into a second private game tree.
The frozen `snagrepair.py` input emits an explicit RUNE-bound `repairs 0`
bootstrap whose evidence classification is `NO_ACCEPTED_OBSERVATION`; it does
not claim that the map was observed clean. Start a new, separately authenticated
q2ded process against those two files. `PASS` requires exactly one ordinary
runtime-ready banner whose counts agree with generation, with no generator
write banner. The cold-load process identity, command hash, staged artifact,
bootstrap sidecar, and log are immutable terminal evidence and must differ from
the generation process identity.

## Durable per-map result and resume law

Each terminal `runs/MAP/result.json` contains the run fingerprint, map, stable
port, attempt number, start/end timestamps, classification and normalized
signature, exact command hash, owner-record path, server and gate-log hashes,
artifact path/hash, objective roots, all decoded counts, applicable semantic
gate labels, fresh cold-load owner/command/log hashes, and the exact bootstrap
`.snag` plus its `NO_ACCEPTED_OBSERVATION` evidence record. Publish it only
after the attempt files and directories are synced.

On resume, a previous `PASS` is reusable only if its fingerprint and stable port
match, every referenced file is regular and still has the recorded hash, the
artifact is still the exact recorded bytes, both C gates, both general Python
gates, and all applicable semantic gates pass again, and the stored cold-load
evidence authenticates a distinct process and the same artifact. Otherwise
create the next attempt; never overwrite prior evidence. The bootstrap evidence
must remain canonical, bind the run fingerprint and artifact hash, and the
retained `.snag` must still bind its exact evidence hash and declare
`repairs 0`.

## Final sidecar attribution

Bootstrap sidecars are replaced only after the persistent ten-lane fleet has
stopped cleanly and `fleet-runner.py::verify_stopped_residence_evidence`
accepts its complete ledger. Run `snag_corpus.py` with the accepted 181 RUNE
directory, stopped state root, evidence root, and the exact hash-bound fleet
runner. The final builder:

- requires residences 0 through 20 for every lane and the exact rotated
  top-20 schedule;
- analyzes residences 0 through 19, giving every top-20 map ten equally
  weighted observations, while residence 20 proves native wrap to entry 0;
- strictly replays every admitted name/team/client across every serverrecord
  snapshot and requires the exact 1 Hz `SGCENSUS` frame inventory;
- joins a visible demo stall to a controller/RUNE route episode only for the
  same player and an overlapping server-frame interval, rejecting ambiguous
  joins;
- emits a RUNE-SHA-bound sidecar and canonical evidence record for all 181
  maps, including explicit `NO_ACCEPTED_OBSERVATION` files for maps outside
  the observed top 20; and
- rechecks the runner, stopped owner, evidence ledger, receipts, demos,
  console segments, and every RUNE before immutable no-replace publication.

Run the same command with `--verify-final` against the published directory.
That pass re-derives every byte from the retained residence authority. A final
sidecar corpus is not accepted from opaque JSONL rows or from the bootstrap
classification.

After each terminal map, atomically regenerate `summary.json`, `summary.tsv`,
and `heartbeat.json`.  The final summary includes the fingerprint, total 181,
counts by classification, every map result/hash, start/end timestamps, and a
`complete` boolean that is true only when all 181 terminal results validate.

## Launch gate

First run only the controller self-tests and a dry-run that prints the 181
stable map/port assignments and fingerprint.  Then run one approved smoke map
with a fresh module, inspect its exact bytes through C and Python, and cold-load
it through the runtime.  The full corpus may start only after that evidence is
accepted by the project owner.
