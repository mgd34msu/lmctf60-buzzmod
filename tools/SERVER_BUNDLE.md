# Build and install the server bundle

`server_bundle.py` builds the release archive, activates one installed
generation, verifies the active generation, and switches back to the retained
generation.

## Write the build specification

Create one canonical JSON object with the top-level keys `files`, `format`, and
`identities`. Set `format` to `lmctf-server-bundle-build-v1`. The `identities`
object contains lowercase SHA-256 values named `source`, `engine`,
`rune_format`, `action_contract`, `mechanism_contract`, and `configuration`.
Each `files` entry contains `source`, `path`, and `role`. Sort object keys, omit
spaces, and end the file with one newline.

The shown file list is abbreviated. The complete specification contains these
roles and paths:

- `module-primary` at `game/game.so` and `module-secondary` at
  `game/gamex86_64.so`;
- `pak`, `config`, and `topmaps` at their fixed `game/` paths;
- `maplist:s01` through `maplist:s10` at
  `game/maplists/LANE.txt`;
- one `bsp:MAP` and one `rune:MAP` for every map in
  `rune-corpus-maps.txt`;
- each accepted `snag:MAP`, including all top-20 maps.

The builder rejects an extra role, a missing required role, a wrong target
path, a symlink, a changed input, a noncanonical rotation, unequal module
aliases, and a configuration identity that differs from the config bytes.

## Build and verify the release files

```sh
python3 -B tools/server_bundle.py build \
  --spec /freeze/server-bundle-build.json \
  --archive /release/lmctf-server-bundle.tar \
  --manifest /release/lmctf-server-bundle.json

python3 -B tools/server_bundle.py verify \
  --archive /release/lmctf-server-bundle.tar \
  --manifest /release/lmctf-server-bundle.json
```

The manifest binds the archive name, size, SHA-256, identities, and every file
role, path, mode, size, and SHA-256. The tar contains only sorted regular file
members. It has fixed ownership metadata and timestamps.

## Install one generation

For the first install, require an empty active state:

```sh
python3 -B tools/server_bundle.py install \
  --archive /release/lmctf-server-bundle.tar \
  --manifest /release/lmctf-server-bundle.json \
  --root /srv/lmctf-bundles \
  --expect-active none
```

For a later install, replace `none` with the current bundle ID. The command
fails if the active ID differs. It extracts and verifies a frozen generation
before one atomic replacement of `install-state.json` activates it. The prior
generation remains the rollback generation.

Verify the installed bytes before a cold load or fleet run:

```sh
python3 -B tools/server_bundle.py verify-installed \
  --root /srv/lmctf-bundles
```

Launch from the physical `generation_root` in that output. Do not infer the
active bundle from a mutable game directory. A fleet run specification stores
the exact file record for `install-state.json`; the runner also requires its
runtime copies to match the active roles by size and SHA-256.

## Roll back

Name both the active bundle and the retained target:

```sh
python3 -B tools/server_bundle.py rollback \
  --root /srv/lmctf-bundles \
  --expect-active CURRENT_BUNDLE_ID \
  --to RETAINED_BUNDLE_ID
```

The rollback changes only the atomic active-state record. Repeating the same
command when the target is already active is a verified no-op.

## Run the focused gates

```sh
make -f GNUmakefile server-bundle-test fleet-runner-test
make -f Makefile server-bundle-test fleet-runner-test
```
