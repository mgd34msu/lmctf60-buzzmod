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
