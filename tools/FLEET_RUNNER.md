# Persistent fleet runner

`fleet-runner.py` owns the production ten-lane real-match cycle. It does not
install a bundle, discover files from a checkout, or call the development
launchers.

## Prerequisites

Install and verify the release with `server_bundle.py` first. The fleet run
specification must contain the exact file record for the resulting
`install-state.json`. It must also bind these inputs by path, size, device,
inode, and SHA-256:

- the engine and client images;
- both byte-identical module aliases;
- the production configuration;
- the pinned demo decoder and `fleet_runner_live.py` runtime;
- the ten map-list files;
- every top-20 BSP, RUNE, SNAG, and flag-origin authority.

Each lane record names its private root, exact engine and client argument
vectors, serverrecord directory, POV demo path, canonical offset, and complete
artifact inventory. The runner rejects missing fields, extra fields, symlinks,
map-list drift, development-launcher arguments, and a runtime helper that is
not the sibling of the exact runner being executed. It imports the exact
sibling `server_bundle.py`, verifies the active installed generation, and
requires each runtime module, config, maplist, BSP, RUNE, and SNAG copy to match
its installed role by size and SHA-256. Every residence records that bundle ID.

The state and evidence roots must not exist before `run`. Stop the old
`wavewatch` and `waveloop` development fleet first. They are neither a parent
nor a recovery mechanism for this command.

## Run one native cycle

```sh
python3 -B tools/fleet-runner.py run \
  --spec /installed/lmctf/fleet-run-spec.json \
  --state-root /evidence/lmctf-fleet-state \
  --evidence-root /evidence/lmctf-fleet-run
```

The runner starts lanes `s01` through `s10` behind one coordinated release.
Their offsets are exactly 0 through 9. Every q2ded process advances through its
20-map list through native level changes and then enters one wrapped residence.
The following native commit closes that residence, so the engine generation
contains 21 accepted residences and 22 observed map commits without a PID
change.

For every residence, the runner starts a map-local serverrecord, authenticates
the exact ten-bot roster and census, controls one persistent spectator's POV
lifecycle, and binds the console, demo, BSP, RUNE, SNAG, process, client, and
frame identities into a receipt. It signals only captured pidfds. Successful
shutdown freezes 210 receipts, a hash-chained ledger, and a `SAFE_STOPPED`
owner record.

Any failure stops only the captured engine and client generations. It leaves a
failed state directory that the stopped verifier will reject. Do not reuse the
failed roots.

## Verify stopped evidence

```sh
python3 -B tools/fleet-runner.py verify \
  --state-root /evidence/lmctf-fleet-state \
  --evidence-root /evidence/lmctf-fleet-run
```

The verifier requires frozen trees, an unheld lock, absent engine and client
generations, the exact schedule and wrap, one unchanged generation per lane,
complete POV and serverrecord lifecycles, and a canonical 210-entry ledger.
`snag_corpus.py` imports this same verifier from the exact runner bytes and
repeats it around final sidecar derivation.

## Focused gate

```sh
make -f GNUmakefile fleet-runner-test
make -f Makefile fleet-runner-test
```

The executable test uses ten native fake engine processes. It proves the
coordinated release, same-PID native cycle, receipt publication, clean stop,
and independent stopped verification without using any development fleet
script.

## Route-only ordinary-match proof

These ten maps are the only development `ROUTE_ONLY` candidate universe. They
are deliberately outside the top-20 fleet and are not SNAG-corpus input:

```text
r01 lmctf01   r02 lmctf06   r03 lmctf12   r04 lmctf15   r05 lmctf19
r06 lmctf25   r07 tomb05    r08 xmap13    r09 xmap18   r10 xmap26
```

After normal complete-route acceptance runs across all 175 maps, run this
separate one-match-per-selected-lane proof only if the final controller leaves
one or more of these candidates as `ROUTE_ONLY`. The spec's `lanes` is the
ordered subset of the fixed lane/map pairs selected by that final authority;
it may be empty.

```sh
python3 -B tools/fleet-runner.py route-only-run \
  --spec /installed/lmctf/route-only-run-spec.json \
  --state-root /evidence/lmctf-route-only-state \
  --evidence-root /evidence/lmctf-route-only-run

python3 -B tools/fleet-runner.py route-only-verify \
  --state-root /evidence/lmctf-route-only-state \
  --evidence-root /evidence/lmctf-route-only-run
```

The spec fixes the candidate lane/map order, engine/client/module/config/film
identities, active bundle state, and controller authority. The controller
reopens every final corpus result in manifest order: every noncandidate must
be `PASS`, and the selected subset is exactly the candidate maps classified
`ROUTE_ONLY`/`local_only`. A candidate classified `PASS` cannot appear in the
spec, and omitting or adding a selected lane fails before launch. Selected
roots and private game roots must be pairwise disjoint. Each lane has an
initially absent `route-only-session.db` destination,
and exact loaded `game.so`, `gamex86_64.so`, empty maplist, BSP, RUNE, and SNAG
copies. Each selected controller result must match its RUNE and BSP bytes,
module bytes, and the controller's stable port assignment.

If all ten candidates are `PASS`, use the same command with an empty `lanes`
array. It authenticates the full final controller result set, writes a frozen
`SAFE_STOPPED` no-op owner and a zero-entry ledger, then verifies them. It does
not launch an engine or client, load a film, issue `quit`, or create receipts.

`route-only-match.cfg` is an ordinary ten-minute 5v5 configuration. It uses
the required, bundled empty `route-only-maplist.txt`; that empty file prevents
the game's absent-maplist fallback from selecting ambient `maplist.txt`.

Each receipt requires an exact 5v5 roster, RUNE-ready local-only runtime,
authenticated serverrecord and POV demos, full ten-minute consecutive server
frames, `Timelimit hit.` followed by native `EXITLEVEL`, and a safe SQLite
backup containing exactly one match and the same ten bots. Every admitted bot
must have at least one alive census sample. Every team must produce RUNE-bound
ATTACK and DEFEND samples within the production
`rolestat.py` pressure/defense thresholds, and each team must participate in
combat. Every SG origin is also matched to that client's same-frame
serverrecord position using the runtime coordinate rounding rule. Captures,
steals, and returns are recorded from that database but are
informational: a local-only RUNE is not required to reach or close a
cross-map flag route.

The successful owner records a normal `quit` request and clean engine exit
before it is frozen as `SAFE_STOPPED`; verifier cleanup never signals any
uncaptured process. Failed roots are evidence only and cannot be resumed or
stitched. The canonical fleet, `tools/topmaps.txt`, and `snag_corpus.py` remain
unchanged.

```sh
make -f GNUmakefile route-only-match-test
make -f Makefile route-only-match-test
```
