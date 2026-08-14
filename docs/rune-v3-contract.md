# RUNE v3 and Canonical Action Contract

Status: approved implementation contract

Working branch: `slipgate`

Roadmap: [`../SLIPGATE-IMPLEMENTATION-ROADMAP.md`](../SLIPGATE-IMPLEMENTATION-ROADMAP.md)

This document fixes the implementation details behind Workstreams D and E. It
exists so the generator, wire format, loader, runtime, Python tools, replay
fixtures, and reviewers share one falsifiable contract.

## Why v2 is not enough

RUNE v2 serializes native C structs: an 80-byte header, 16-byte seed, and
28-byte link. It binds a graph primarily to a map name and gives each link one
three-float anchor. Action meaning is duplicated across C and Python consumers.

That is insufficient for a temporal mechanism followed by another action. A
door-conditioned DROP, SWIM, or HOOK needs both:

- an exact mechanism witness used to identify and activate the mover; and
- the suffix action's exact control, such as a DROP lip or HOOK ray.

It also needs uninterrupted ownership of the mover's TOP window. Encoding the
phases as two ordinary links with a pseudo-seed is unsound because ordinary
seeds participate in localization, fields, SCC analysis, pruning, replanning,
and generic action selection. The temporal lease could disappear between the
two records.

The `lmctf01` objective cut demonstrates the missing class:

- root to center uses a translating floor hatch followed by a DROP;
- center to root uses a wet HOOK through that hatch;
- submerged side sliders require door-conditioned SWIM.

Ordinary DROP and HOOK are correct to reject a closed mover sweep. Ordinary
dry `RL_DOOR` is correct to reject a wet or falling suffix. The graph therefore
needs a new atomic contract; neither existing action should be weakened.

## Design rules

1. V3 records are explicitly little-endian. C structs are never written or
   read as wire images.
2. Action, provenance, and mode identifiers are append-only.
3. The canonical registry is generated from one declarative source.
4. A compound transaction is one graph edge and one runtime owner.
5. Generator and loader replay the same witness and physics law.
6. Runtime acquires exclusive mover ownership before mechanism entry, then
   re-proves the authoritative suffix at TOP while retaining that lease.
7. Unknown versions, identifiers, flags, reserved bytes, physics laws, or
   identity values fail closed.
8. No map-name-specific permission is allowed.

## Canonical identifiers

Existing action IDs 0 through 8 retain their exact values and meanings.

New actions:

| ID | Name | Meaning |
|---:|---|---|
| 9 | `RL_DOOR_DROP` | Declared mover transaction followed by DROP |
| 10 | `RL_DOOR_SWIM` | Declared mover transaction followed by SWIM |
| 11 | `RL_DOOR_HOOK` | Declared mover transaction followed by HOOK |

New provenance:

| ID | Name | Meaning |
|---:|---|---|
| 4 | `RL_CONTRACTED` | Map mechanism plus exact physics replay |

Compound modes:

| ID | Name | Meaning |
|---:|---|---|
| 0 | `NONE` | Noncompound action |
| 1 | `PREOPEN` | Wait at an exact trigger-contact pose, then traverse |
| 2 | `RIDE` | Enter and ride a validated translating support to TOP |

The explicit action IDs are intentional. A generic compound action plus a
suffix byte would duplicate dispatch, pricing, tooling, callback, and ownership
classification. Byte 43 remains reserved instead.

## V3 header: 128 bytes

| Offset | Type | Field |
|---:|---|---|
| 0 | `u32` | magic `0x454e5552` |
| 4 | `u16` | version `3` |
| 6 | `u16` | header bytes `128` |
| 8 | `u16` | seed bytes `16` |
| 10 | `u16` | link bytes `44` |
| 12 | `u32` | seed count |
| 16 | `u32` | link count |
| 20 | `u32` | CRC32 of encoded seed and link payload |
| 24 | `u32` | authoritative engine BSP checksum |
| 28 | `u32` | CRC32 of effective post-override entity text |
| 32 | `u32` | canonical action-contract CRC32 |
| 36 | `u32` | physics flags; initially zero |
| 40 | `f32` | exact `sv_gravity` |
| 44 | `f32` | exact `sv_airaccelerate` |
| 48 | `f32` | exact `sv_maxvelocity` |
| 52 | `u16` | proved Pmove substep; initially 25 ms |
| 54 | `u16` | production server frame; initially 100 ms |
| 56 | `u32` | nonzero host-physics contract ID |
| 60 | `u32` | header CRC32 with this field zero |
| 64 | `char[64]` | exact NUL-terminated map name with zero tail |

The seed remains 16 little-endian bytes: `<fffhh>`.

Initial v3 support permits finite positive integral `int16` gravity, including
`lmctf07`'s gravity 650; zero air acceleration; finite sufficient maximum
velocity; funky gravity off; and exact 25/100 ms cadence. Generation, loading,
and every movement frame compare the active law with the header.

### Engine identity prerequisite

The current game import can set configstrings but cannot retrieve the engine's
authoritative BSP checksum. V3 therefore requires the host engine to publish
protected read-only values before `SpawnEntities`, for example:

- `sv_rune_mapchecksum`: the same bit pattern used for `CS_MAPCHECKSUM`;
- `sv_rune_physics_id`: changed whenever host Pmove, trace, pusher, or collision
  semantics change.

These must be engine-provided and console-immutable. Map name, entity CRC,
disk-side metadata, or an operator-supplied checksum is not a safe substitute.
Until the bridge is present, v3 generation and loading fail with an actionable
identity-prerequisite error rather than silently weakening the binding.

## V3 link: 44 bytes

Canonical format string: `<IIBBBBBBh3f3fHBB>`.

| Offset | Type | Field |
|---:|---|---|
| 0 | `u32` | source seed |
| 4 | `u32` | destination seed |
| 8 | `u8` | action |
| 9 | `u8` | provenance |
| 10 | `u8` | minimum speed |
| 11 | `u8` | heading |
| 12 | `u8` | heading slack |
| 13 | `u8` | exit speed |
| 14 | `i16` | exact total transaction cost, 1–30000 ms |
| 16 | `f32[3]` | suffix action anchor or control |
| 28 | `f32[3]` | exact mechanism anchor |
| 40 | `u16` | suffix start to complete sweep-clear boundary |
| 42 | `u8` | compound mode |
| 43 | `u8` | reserved; zero |

For every noncompound action, bytes 28 through 43 are exactly zero.

Compound semantics:

- `RL_DOOR_DROP` retains ordinary DROP control bytes and lip in the suffix
  anchor. The mechanism anchor is a PREOPEN contact or RIDE ingress lip.
- `RL_DOOR_SWIM` retains ordinary SWIM control semantics and a zero suffix
  anchor. The mechanism anchor is a submerged trigger-contact pose.
- `RL_DOOR_HOOK` stores its exact pitch, yaw, and ray in the suffix anchor. The
  mechanism anchor is a wet trigger-contact pose.
- `sweep_clear_ms` is positive, divisible by 100, no greater than total cost,
  and exactly recomputed by loader replay.
- The loader requires exact cost and sweep-clear equality for compounds. V2's
  permissive stored-cost lower-bound rule does not apply.
- HOOK retains the lease until both player hull and bolt are outside the
  complete team sweep.

The 44-byte record is sufficient only if RIDE replay derives exactly one
trigger set, first contact, support member, and carried TOP pose from the source
and serialized ingress lip. Ambiguity rejects generation. If a supported mover
class later needs an independently serialized contact pose, the record grows;
unrelated bytes are never overloaded.

## Canonical action registry

`slipgate/rune_actions.json` is the declarative source of truth for:

- action, provenance, mode, anchor-kind, and rejection-reason IDs;
- endpoint water policy and allowed provenance/modes;
- ownership, localization, ballistic, mover-lease, atomic, and suffix traits;
- field-cost bias;
- per-action proof revision;
- wire sizes and proof-law constants.

`tools/gen_rune_contracts.py` consumes strict UTF-8 JSON. For the wire contract
digest it serializes exactly this semantic projection:

```json
{"contract": <the contract object>, "schema_version": <the schema version>}
```

The projection is encoded as canonical compact JSON with sorted keys,
ASCII-only escapes, and no insignificant whitespace. The top-level `display`
object is deliberately excluded: changing labels or colors cannot invalidate a
RUNE file. For schema version 1, the canonical semantic payload has CRC32
`769a7b8e` and SHA-256
`0790272cf0a34b7ba26dd318629150e3bc66b21dedb5ac448578aa8c3fdc4d59`.

The generator checks in two generated products:

- `slipgate/sg_action_contract.generated.h`;
- `tools/rune_contracts_generated.py`.

`--check` fails if either product differs. The semantic CRC32 described above
is stored in every v3 header.

`slipgate/sg_action.h` and `slipgate/sg_action.c` provide the behavioral
descriptor/dispatch layer:

```text
prove
encode
validate_record
begin
emit_frame
arrived
recover_or_abort
```

Legacy actions enter through adapters first. Hard-coded classification lists
are replaced incrementally only after equivalence fixtures pass. Metadata does
not itself prove behavior; each controller retains a proof revision and replay
fixtures.

`tools/runeio.py` becomes the only Python wire parser. `runelint`, `runeview`,
`corpusgraph`, bake tools, `film`, `demorune`, `seedservo`, `mapflags`, and
`escapee` consume it or the generated metadata.

## Compound proof contract

### PREOPEN

1. Replay the suffix-specific approach from the source to the exact mechanism
   anchor. Admit only the expected trigger set and reject unrelated effects,
   hazards, movers, or contamination.
2. Prove bounded station keeping while the declared set travels to TOP.
3. Publish exact TOP collision state and replay the suffix controller.
4. Record the first 100 ms boundary at which the complete hull is outside every
   pose of the mover set. Continue proving ordinary suffix arrival afterward.

### RIDE

1. Replay exact ingress from a supported dry source through accepted trigger
   contact onto the closed leaf, including production-cadence fall checks.
2. Retain exclusive ownership and apply the canonical translating-pusher law.
   Require the same support member at each mover frame; reject support loss,
   blockers, rollback, reversal, damage, or unrelated effects.
3. At exact TOP, start a fresh suffix proof from the authoritative carried pose
   and record sweep-clear and total-arrival times.

RIDE initially supports only a unique repeatable, noncrusher, nontoggle,
nonrotating, nonscripted pure-translation team with a canonical move law and a
clear immutable rider sweep. Generator and loader replay must restore every
temporarily staged entity field and link state on every exit. They never call
live `SV_Push`, targets, think functions, or trigger side effects.

Button-only, relay-only, shot-open, one-shot, scripted, and side-effecting
mechanisms are explicitly outside the first v3 mover class. They reject with a
stable unsupported-activator reason instead of disappearing silently or being
treated as an automatic trigger. Button conditioning may be added later only
as its own proved atomic action.

The TOP hold needs to cover the suffix only through complete sweep clearance,
plus one 100 ms frame margin. It need not cover distant destination settling.

## Runtime ownership and recovery

Compound bot state contains:

- link index and phase;
- resolved trigger set and support member;
- touch and activation frame;
- phase deadline and suffix elapsed time;
- sweep-clear latch;
- captured authoritative Pmove state.

Phases:

```text
NONE -> SOURCE -> APPROACH -> TOUCHED -> OPENING -> RIDE/TOP
     -> SUFFIX_LEASED -> SUFFIX_CLEAR -> NONE
                         \
                          -> RECOVER
```

While active, the controller keeps the departure seed; suppresses localization,
generic link replacement, combat locomotion, and replanning; owns an exclusive
lease keyed by the canonical mover set; accepts only the expected callbacks;
and online-reproves the suffix from authoritative TOP state.

The exclusive mover lease is acquired in `SOURCE`, before any approach command
can enter the mechanism region, and retained through `SUFFIX_CLEAR`. Competing
bots remain outside the set and cannot activate it.

Recovery rules:

- Outside the complete sweep: stop, shelf, release, and relocalize.
- Inside the sweep: retain the exact lease and execute only an online-proved
  path to a supported point outside the complete sweep. A PREOPEN mechanism
  anchor may be used only when it independently satisfies that terminal rule;
  a RIDE ingress lip is never assumed safe merely because it was serialized.
- If no safe bounded return exists, finish through the normal death/respawn
  lifecycle. Never teleport or release to nearest-seed navigation.
- Death, disconnect, replacement, and map change clear state and lease.
- A second bot cannot enter the same canonical mover set until its owner clears,
  safely aborts, or dies.

## Migration and strict rejection

- Runtime v3 accepts only v3. There is no v2 converter because v2 lacks
  mechanism witnesses and authoritative map/physics identity.
- `runeview` may retain v1/v2 forensic reading.
- `runelint` retains historical inspection and gains strict `--runtime-v3`.
- `runegen.sh` deploys only strict-v3-clean output once the vertical slice is
  complete.
- Existing sidecars are invalidated by version and encoded-payload CRC.
- Unknown identifiers, physics flags, nonzero reserved/tail bytes, malformed
  floats, count/size mismatch, trailing bytes, and identity mismatch reject.

### Compatibility consumers

The registry's `effective_suffix` trait is required wherever behavior currently
tests an action ID directly. In particular, `RL_DOOR_HOOK` must inherit HOOK
pricing, hook-ban route exclusion, hook-ban commit release, rope ownership, and
weapon/controller dispatch. The same rule applies to DROP and SWIM policies.

Field propagation continues to consume the declared minimum speed, heading,
heading slack, and exit-speed buckets. Each compound stores the suffix's honest
arrival envelope; door wait/travel is already included in exact `cost_ms` and
must not be silently double-priced or left at a generic default.

The generation success line beginning `rune: wrote` remains a stable automation
interface until the corpus runner migrates to a structured result. Tooling must
not infer v3 success from process exit alone.

Human-use and defense sidecars are indexed by seed/link number. Every v3
sidecar format must bind to the exact v3 payload CRC, counts, and action-contract
CRC before allocation or publication. A stale v2 sidecar or a sidecar from a
different v3 numbering rejects; length equality is insufficient.

Live in-process rune replacement is unsupported until all per-bot link indices,
shelves, commitments, sticky/watch state, fields, and sidecars can be replaced
transactionally. Normal map-level loading resets them before publication.

## Implementation slices

| Slice | Scope | Done condition |
|---|---|---|
| E0 | Engine checksum and physics-ID bridge | Protected values exist before `SpawnEntities`, change with map/host law, and cannot be changed by console |
| S1 | Canonical JSON, generator, generated C/Python metadata, action descriptor adapters | IDs 0–8 unchanged; IDs 9–11 and provenance 4 appended; C/Python metadata match; `--check` clean; legacy pricing/classification unchanged |
| S2 | Explicit v3 I/O, identity binding, shared Python parser, strict compatibility | Golden 128/16/44 vectors round-trip in C/Python; corruptions reject; v2/v3 incompatibility is actionable; gravity-650 fixture loads |
| S3 | Shared pose-based DROP/SWIM/HOOK replay | Generator, loader, runtime use the same commands and terminal predicates; `lmctf09` exact hooks remain |
| S4 | `RL_DOOR_DROP` | PREOPEN/RIDE generate, validate, load, and execute; ambiguous support rejects; `lmctf01` root-to-center appears |
| S5 | `RL_DOOR_SWIM` | Submerged side-door class generates, validates, loads, and executes without weakening ordinary water actions |
| S6 | `RL_DOOR_HOOK` | Water-origin hook and body/bolt sweep clearance pass; `lmctf01` reverse cut closes; `lmctf09` remains exact |
| S7 | Acceptance | `lmctf01` core closes; v3 lint, graph, loader, join, and live actions pass; representative maps and all 181 maps are terminal |

E0 and S1 may proceed independently. S2 and later slices are serialized because
they share generator, loader, runtime, and tool consumers.

## Required fixtures

- Golden little-endian header, seed, and link vectors shared by C and Python.
- One mutation for every size, CRC, identity, enum, reserved byte, float, count,
  endpoint, duplicate, and tail invariant.
- Exact 25 ms/100 ms DROP, SWIM, and HOOK trajectories.
- Synthetic PREOPEN water door, RIDE hatch, and HOOK bolt-clear fixtures.
- Negative rotating, toggle, crusher, scripted, blocked, ambiguous,
  short-window, and wrong-support cases.
- Perturbation before, inside, and after sweep; two-bot contention; death,
  disconnect, replacement, and map-change cleanup.
- Real-map gates: `lmctf01`, `lmctf07`, `lmctf09`, then the 181-map corpus.

## Review risks

- Do not replace the mechanism witness with `from`, `to`, and sweep geometry;
  current generation has multiple valid contacts for one team.
- Do not use a pseudo/controller seed.
- Do not let `RL_CONTRACTED` weaken generic collision, damage, or suffix proof.
- Do not infer authoritative BSP identity from entity text or map name.
- Do not call live pusher/trigger behavior during offline or loader replay.
- Do not release compound ownership while a body or bolt remains in the sweep.
- Enlarge the format if deterministic RIDE uniqueness cannot be proved.
- Enlarge the format if a supported mover class needs a serialized recovery
  witness and cannot deterministically derive one from its proved transaction.
