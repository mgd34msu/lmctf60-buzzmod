# LMCTF BuzzMod release contract

The project is not yet at its final `v1.0.0` release. The current source version
is defined once in `BuzzmodVersion.h`; a release tag must be exactly
`v<BUZZMOD_VERSION>`, and CI rejects a mismatched tag.

Do not treat an arbitrary branch, local module, generated RUNE, or running
server as a release. The complete acceptance sequence is maintained in
[`PROJECT-COMPLETION-PLAN.md`](PROJECT-COMPLETION-PLAN.md).

## Public installation files

The current public install consists of these platform/runtime files:

| File | Purpose |
|-|-|
| `gamex86_64.so` | Linux 64-bit game module |
| `gamex86_64.dll` | Windows 64-bit game module |
| `gamex86.dll` | Windows 32-bit game module |
| `lmctf6-buzzmod.pak` | Required scoreboard art and sounds |
| `VERSION` | Exact source version |
| `SHA256SUMS` | Checksums for every published payload |

The tag workflow builds and publishes that six-file set. Generated navigation
data and the authenticated production server bundle are deployment artifacts,
not separately downloadable public release payloads.

## Production server bundle

Before the final release, production must assemble, install, and verify one
authenticated server-bundle archive and its release manifest. The archive must
bind, by path, role, size, and hash:

- the two byte-identical Linux module aliases used by the server;
- `lmctf6-buzzmod.pak`;
- the production configuration;
- the exact 175 BSP/RUNE pairs, including both `lmctf02a` and `lmctf02c`;
- the ordered top-20 authority and ten cyclic rotations with offsets 0 through
  9;
- every applicable accepted sidecar;
- the source, engine, map, RUNE-format, action-contract, and configuration
  identities needed to reproduce acceptance.

The bundle is a server deployment boundary, not a second public bot library.
SLIPGATE is compiled into the game module. Bot admission uses the `sv sg`
command surface.

## Release acceptance

A release is publishable only when all of the following refer to the same
frozen source and bundle identity:

1. Both Make dialects and all host tests pass under GCC and Clang.
2. Linux x86_64 and Windows x86/x64 builds are warning-clean.
3. All 175 RUNE artifacts pass both C readers, the Python reader, lint,
   applicable semantic checks, and a fresh-process cold load.
4. The complete bundle passes assembly, install, post-verification, failure
   injection, and rollback tests.
5. The installed bundle cold-loads every top-20 map.
6. Ten persistent `q2ded` processes run the ten ordered top-20 rotations,
   starting at offsets 0 through 9 with one coordinated, unstaggered start.
7. Every process completes a full native 20-map cycle under the same PID and
   records the expected map, RUNE, roster, activity, and POV lifecycle for each
   residence.
8. The final matched bot-quality and non-regression gates pass on those promoted
   bytes.
9. Exact-SHA CI is green on `slipgate`, the no-fast-forward `main` merge has the
   identical tree and green exact-SHA CI, and the annotated version-tag run
   publishes successfully.
10. A clean download of every published asset passes `sha256sum -c` and equals
    the corresponding accepted public payload; the installed production bundle
    independently passes its authenticated manifest verification.

`v1.0.0` is reserved for that completed state. Until every gate is green, the
source and documentation must describe the remaining blockers plainly rather
than presenting partial evidence as a release.

## Operator commands

Build and verify the final server archive and manifest:

```sh
python3 -B tools/server_bundle.py build \
  --spec /freeze/server-bundle-build.json \
  --snapshot /freeze/rune-inputs \
  --corpus-root /archive/rune-corpora/CORPUS_ID \
  --archive /release/lmctf-server-bundle.tar \
  --manifest /release/lmctf-server-bundle.json
python3 -B tools/server_bundle.py verify \
  --archive /release/lmctf-server-bundle.tar \
  --manifest /release/lmctf-server-bundle.json
```

Install the first generation and verify its physical file identities:

```sh
python3 -B tools/server_bundle.py install \
  --archive /release/lmctf-server-bundle.tar \
  --manifest /release/lmctf-server-bundle.json \
  --root /srv/lmctf-bundles \
  --expect-active none
python3 -B tools/server_bundle.py verify-installed \
  --root /srv/lmctf-bundles
```

Later installs replace `none` with the verified current bundle ID. Rollback
names both the expected active ID and the retained target:

```sh
python3 -B tools/server_bundle.py rollback \
  --root /srv/lmctf-bundles \
  --expect-active CURRENT_BUNDLE_ID \
  --to RETAINED_BUNDLE_ID
```

`tools/SERVER_BUNDLE.md` defines the canonical build specification. The current
`tools/deploy.sh`, `iterate2.sh`, `waveloop.sh`, and `wavewatch.sh` remain
development tools and are not release interfaces.

Previous milestone notes and pre-SemVer release descriptions remain available
from their tags and Git history. They are intentionally not duplicated here,
because this file is the body published by the current tag workflow.
