# RUNE corpus controller

This is the implementation and acceptance contract for the durable corpus
controller. Do not run the corpus until the production module is
rebuilt from the frozen source tree and the first generated artifact passes
both the C and Python acceptance paths.

## Fixed corpus

- Use `tools/rune-corpus-maps.txt` as the sole ordered map manifest.
- Require exactly 175 safe, unique names.
- Exclude an unsuffixed base whenever an alphabetic-suffix variant exists.
  Multiple variants remain separate identities, including `lmctf02a` and
  `lmctf02c`.
- The manifest SHA-256 is
  `dc87ed408d299999501173ab65754e3d555a3505c7d8daf172ee542d710af98a`.
- Assign stable ports by manifest index.  The reserved default range is
  62000-62174; all selected TCP and UDP ports must be bind-tested before any
  engine starts.  A different base is part of the run fingerprint.

This 175-map manifest is the conversion corpus. The production server rotation
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

The private-Python runtime preflight is Linux-only: it uses a private ELF
loader, pidfds, and `/proc` process diagnostics. It fails before launch on
Windows and macOS rather than attempting those Linux checks. This limitation is
only for the corpus controller; the game RUNE reader uses exact content
snapshots on native Windows APIs and POSIX systems, including macOS.
The Linux preflight also requires permission to open each mutable or relocated
mapping through `/proc/<pid>/map_files`; it fails closed when the kernel denies
that authority.

## Input freeze

Build an immutable input snapshot from the approved production build. The
snapshot must contain:

- the exact `q2ded` executable;
- both production module filenames, required to have identical SHA-256 bytes;
- the game assets used to resolve every manifest map;
- `tools/runelint.py`, `tools/runeio.py`,
  `tools/rune_contracts_generated.py`, the distinct GNUmakefile-built
  `runeaccept.gnu`, and Makefile-built `runeaccept.make` commands;
- the semantic-checker manifest and every Python checker it names (currently
  the ten-controller/two-flag-route-core checker for `lmctf58`);
- the generator configuration and the map manifest.

Use the standalone `tools/rune.cfg` as the generator configuration. Snapshot it
with this role and logical path:

```text
generator_config@game/rune.cfg=/absolute/path/to/tools/rune.cfg
```

The file sets `deathmatch 1`, `maxclients 16`, `timelimit 0`, `capturelimit 8`,
`ctfflags 16`, `minimumplayers 0`, `bot_grapple 0`, `bot_groundhook 0`,
`maplist_file "__none__"`, and `sv_botfill 0`. It must contain no `exec`,
`botfile`, `botlib`, map, or port command. The tracked
`rune-onebot-validation.cfg` and `rune-solo-validation.cfg` files are
validation-only overlays. Do not snapshot either overlay.

Write `input-manifest.json` by hashing every regular input file, rejecting
symlinks and unsupported file types, then make the snapshot read-only.  The
controller must verify every path, mode, size, and SHA-256 before launch and
again before accepting the final summary.

The canonical run fingerprint is the SHA-256 of sorted compact JSON containing
the complete input-manifest hash, ordered map-manifest hash, engine and module
hashes, generated action and mechanism contract hashes, linter and reader
hashes, both C acceptor hashes, semantic manifest and checker hashes,
applicability, generation, startup, and cold-load timeouts, job count, port
base, engine arguments, and controller source hash. Resume is allowed only when
the complete stored fingerprint document is byte-for-byte equal to the newly
computed document.

The external acceptor `--contracts` metadata probe has a justified 300-second
bound to contain a hung metadata handshake. It does not bound generation or
review.

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

Before publishing an accepted result (`PASS` or `ROUTE_ONLY`), execute both
independently built C acceptors, both general Python gates, and every
map-specific diagnostic applicable to the map against the same artifact bytes:

```sh
runeaccept.gnu ARTIFACT
runeaccept.make ARTIFACT
python3 tools/runeio.py ARTIFACT
python3 tools/runelint.py ARTIFACT
python3 tools/lmctf58_rune_accept.py --objective-roots RED BLUE ARTIFACT  # lmctf58
```

The GNU C, Make C, and Python JSON reports must agree on map name, seeds, links,
mechanism nodes, triggers, inventory edges, plan edges, and plans. Their counts
must also agree with the generator banner. Record every output and hash. Exit 1
from a general gate is a conclusive artifact rejection. Record a map-specific
diagnostic result, but do not let that result reject an artifact that passed the
general readers, graph contract, lint, and cold load. Usage, process, I/O,
allocation, protocol, identity, and malformed-success failures in a general
gate are infrastructure failures and do not authorize replacement. A
map-specific diagnostic may return either a clean or finding result, but a
process failure, malformed report, or map-identity mismatch remains an
infrastructure failure.

After those gates, copy the unchanged artifact into a second private game tree.
Start a new, separately authenticated q2ded process against that artifact. An
accepted result (`PASS` or `ROUTE_ONLY`) requires exactly one ordinary
runtime-ready banner whose counts agree with generation, with no generator
write banner. The cold-load process identity, command hash, staged artifact,
and log are immutable terminal evidence and must differ from the generation
process identity. The cold-load readiness bound starts after the controller
authenticates that second process. Production leaves the generation timeout
unset, so generation and post-readiness review have no elapsed-time deadline.
An optional safety override is allowed only when deliberately supplied for a
controlled run and is included in the fingerprint. The heartbeat continues
every five seconds while either process is active.

## Durable per-map result and resume law

Each terminal `runs/MAP/result.json` contains the run fingerprint, map, stable
port, attempt number, start/end timestamps, classification and normalized
signature, exact command hash, owner-record path, server and gate-log hashes,
artifact path/hash, objective roots, all decoded counts, applicable semantic
gate labels, and fresh cold-load owner, command, and log hashes. Publish it only
after the attempt is frozen and synced, its held logs still match, and a
separate commit record binds the result. A crash before that commit creates a
bound abort record. Aborted adoption and missing-generation attempts retry but
never authorize replacement. An aborted replacement consumes the sole
replacement allowance and leaves the run incomplete.

On resume, a previous accepted result (`PASS` or `ROUTE_ONLY`) is reusable only
if its fingerprint and stable port match, every referenced file is regular and
still has the recorded hash, the artifact is still the exact recorded bytes,
both C gates and both general Python gates pass again, all applicable
map-specific diagnostics run again, and the stored cold-load
evidence authenticates a distinct process and the same artifact. Otherwise
create the next attempt; never overwrite prior evidence.

The snapshot defines the adoption set. The controller does not hardcode a
candidate count. A snapshot with no adopted RUNEs generates every manifest map.
If a later snapshot intentionally includes candidates, validate each candidate
before generating anything for that map. Preserve passing bytes. Retry
infrastructure failures as adoption attempts. Only a committed, authenticated
artifact rejection permits one replacement generation, and the replacement
intent consumes that one allowance.

Full runs use `jobs > 1`. When an adoption queue exists, its validation can
overlap missing-artifact generation.

## Final corpus publication

After all 175 maps are accepted, publish the release authority with the exact
frozen snapshot and generation run root:

```sh
python3 -B tools/rune_corpus_controller.py finalize \
  --snapshot /freeze/rune-inputs \
  --run-root /evidence/rune-generation \
  --output-parent /archive/rune-corpora
```

Finalization rechecks every accepted result and its gates, permits
`ROUTE_ONLY` only for the approved candidate policy, and publishes the
content-addressed authority at `OUTPUT_PARENT/CORPUS_ID` without replacing an
existing directory. It does not relocate the attempts: retained process
evidence names the absolute private engine path that actually ran. The
authority binds the selected immutable attempt results in the sealed source
run root. `run` and `smoke` cannot resume that root. A repeated command may
only verify and return the same corpus identity.

Retain the sealed generation root permanently at the same absolute path as
the content-addressed authority. Moving or deleting it invalidates the process
and evidence paths recorded by the accepted attempts.

Verify the published bytes independently before bundle assembly:

```sh
python3 -B tools/rune_corpus_controller.py verify-final \
  --snapshot /freeze/rune-inputs \
  --corpus-root /archive/rune-corpora/CORPUS_ID
```

The canonical authority binds the input fingerprint, shared route-only policy,
ordered classifications, stable ports, selected attempt results, artifacts,
and retained evidence. Mutable per-map result pointers and reports do not
authorize the final corpus.

After each terminal map, atomically regenerate `summary.json`, `summary.tsv`,
and `heartbeat.json`.  The final summary includes the fingerprint, total 175,
counts by classification, every map result/hash, start/end timestamps, and a
`complete` boolean that is true only when all 175 terminal results validate.

## Launch gate

First run only the controller self-tests and a dry-run that prints the 175
stable map/port assignments and fingerprint.  Then run one approved smoke map
with a fresh module, inspect its exact bytes through C and Python, and cold-load
it through the runtime.  The full corpus may start only after the smoke-map
evidence satisfies the project acceptance contract.
