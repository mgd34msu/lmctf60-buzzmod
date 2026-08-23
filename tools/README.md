# Tool reference

Run each Python tool with `--help` for its exact command line. This file records
how the tools fit together and which artifacts they own.

## RUNE generation and acceptance

`rune_corpus_controller.py`

: Runs the authoritative map corpus. A map reaches PASS only after generation,
  both native readers, Python decoding, lint, applicable semantic checks, and a
  separate bounded cold load agree on the artifact.

`rune-corpus-maps.txt`

: Exact ordered list of 175 required maps. An unsuffixed base is excluded when
  alphabetic variants exist. Both `lmctf02a` and `lmctf02c` remain required.

`runegen.sh`

: Generates one map in a private game root. It is a controller worker, not a
  production deployment command.

`runeaccept.c`

: Native artifact reader. Build independent GNU and Make variants so one parser
  implementation cannot certify itself.

`runeio.py`

: Strict Python codec and identity checker for RUNE artifacts.

`runelint.py`

: Validates graph structure and objective reachability. Pass explicit artifact
  paths.

`bspmechanisms.py`, `mapflags.py`, `runeview.py`

: Inspect BSP mechanisms, derive map flag metadata, and render graph data for
  diagnosis. Their output is development data unless a manifest binds it.

## Snag evidence

`stallcensus.py`

: Joins authenticated residence telemetry with strict serverrecord decoding and
  emits player and frame-bounded stall episodes.

`snagrepair.py`

: Correlates route stalls with visible motion evidence and writes repairs bound
  to the exact RUNE and evidence digests.

`snag_corpus.py`

: Builds the 175-map snag corpus from verified stopped-residence receipts. It
  fails on missing, ambiguous, or changed authority.

## Demo and match analysis

`film.py`

: Decodes demos and renders movement sheets. Strict consumers must require the
  terminal marker and consecutive wire frames; reporting mode remains useful
  for damaged historical demos.

`routesheet.py`

: Maps movement to the RUNE graph and reports route occupancy and off-graph
  share.

`fightsheet.py`

: Reports engagements, weapon use, aim, and damage. Target attribution is an
  analysis inference, not production event authority.

`teamsheet.py`

: Reports spacing, escort coverage, defense occupancy, and simultaneous attack
  pressure.

`outcomecard.py`

: Summarizes match outcomes. Production `F Pickup`, `F Capture`, and stats are
  the authority for steals and captures; geometric demo events are diagnostic.

`conduct.py`

: Measures stand approaches, carry starts, and defense behavior. Approach rate
  uses observed stand-minutes. Close conversion uses qualifying pickups divided
  by approaches.

`stealstage.py`

: Reconciles matched treatment evidence after a controller change. It is an
  acceptance tool, not a bot implementation milestone.

## Telemetry and diagnostics

`humantrace.py`

: Imports the server-side `sg_humantrace` stream as exact Pmove replay
  evidence. A client demo is not a substitute because it contains snapshots,
  not the `usercmd_t` values that the server executed.

  Create the output directory before starting the server. Set these cvars in
  the server configuration, then load the map and traverse the route as a
  human player:

  ```text
  set sg_humantrace_dir "/absolute/path/to/traces"
  set sg_humantrace 1
  map lmctf01
  ```

  The server prints the trace path when the first normal human Pmove runs. Each
  JSONL step contains the raw command, the exact fixed-point state before and
  after Pmove, the touched entity keys, the ground entity, and the water state.
  The writer excludes bots and flushes every step.

  Import one player's route after the traversal:

  ```sh
  python3 tools/humantrace.py \
    /absolute/path/to/traces/humantrace-lmctf01.jsonl \
    --map lmctf01 --client 1 --from-frame 120 --through-frame 760 \
    --output /absolute/path/to/traces/lmctf01.replay.json
  ```

  Omit the frame options to import the full session. The importer validates and
  preserves the captured map, BSP, entity, physics, and module identity. It
  also starts a new replay segment when the authoritative state changes
  between commands. Such a change can identify a pusher, teleporter, grapple
  update, or other server-frame effect that a Pmove-only replay must model
  explicitly.

`gamestat.sh`, `rolestat.py`

: Parse production rows of the form `role`, `seed`, `goal`, `sgoal`, `spd`, and
  `org`. They fail if no SG rows are recognized.

`hookevents.py`, `hookdiag.py`, `hookclose.py`

: Decode hook lifecycle events and diagnose incomplete fire, attach, pull, and
  release sequences.

`dm2speed.py`, `demoents.py`, `demokin.py`, `demoprints.py`, `demorune.py`

: Focused demo inspectors. Use strict decoding when their output becomes release
  evidence.

`carryforensics.py`, `carryreport.py`, `escapee.py`, `escapepriors.py`

: Inspect carrier deaths and derive escape-route inputs. Seed-indexed output must
  be regenerated or rebound when the RUNE changes.

`humanbake.py`, `flaglivebake.py`, `escapebake.py`, `defbake.py`

: Convert retained demo observations into sidecar inputs. A final bundle may use
  only outputs bound to its exact RUNE identities.

Other census and report scripts are narrow development utilities. Their module
docstrings and `--help` output define current arguments and output schemas.

## Fleet scripts

`iterate.sh`, `iterate2.sh`, `campaign.sh`, `aux2.sh`, `waveloop.sh`,
`wavewatch.sh`, and `deploy.sh` are development-era launchers. They are not the
production boundary. Do not use them to install or certify a release.

Production requires one authenticated persistent runner with ten cyclic map
lists, exact process ownership, immutable engine and client images, residence
receipts, and stopped-evidence verification. See
`PROJECT-COMPLETION-PLAN.md`.

`topmaps.txt` is the exact ordered 20-map production rotation. It is not the
175-map conversion corpus.

## Data

`stands.json`

: Flag stand coordinates for analysis. Release evidence must bind stand
  positions to the exact BSP or derive them from it.

`corpus-manifest.csv`

: Human demo index. It is an input catalog, not evidence that the current module
  passed an acceptance gate.

`escape-priors.json`, `human/`

: Demo-derived development inputs. Treat seed-indexed files as stale after any
  RUNE identity change until regenerated or explicitly rebound.

`botkin_raw.json`, `botledger.csv`, `chat-corpus.json`,
`conduct-baseline.json`

: Historical or generated analysis data. None is final release authority on its
  own.

`pov-rules.txt`

: Defines which demo observations require the recorded player's point of view.

## Setup

`setup.sh` checks the local toolchain and creates the analysis environment from
`requirements.txt`. It does not make historical launchers safe for production
and does not supply proprietary maps, demos, or an engine build.
