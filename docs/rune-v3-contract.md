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
5. Generator and live execution use the same action reducer. Loader replay is
   reserved for sparse map-mechanism transactions; ordinary dense actions use
   the exact-artifact generation proof defined below.
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

Every CRC32 field uses the standard reflected IEEE algorithm (polynomial
`0xedb88320`, initial and final XOR `0xffffffff`), matching `zlib.crc32`. The
entity CRC covers the exact in-memory bytes returned by the last entity-override
stage, before parsing mutates the pointer, and excludes the terminating NUL. No
whitespace, newline, or case normalization is permitted. The map name uses the
case-preserving grammar `[A-Za-z0-9_][A-Za-z0-9_-]{0,62}`; every comparison is
exact and case-sensitive.

The seed remains 16 little-endian bytes: `<fffhh>`. `area_hint` is an unsigned
value in the stored `i16` range `0..255`; the only initial flags are WATER and
TOMBSTONE. A tombstone owns no outgoing link, every non-tombstone owns at least
one outgoing link, and no link may name a tombstone endpoint.

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

The game DLL retrieves both variables with an empty default and flags `0`; asking
for `CVAR_NOSET` would grant the consumer authority to bless a pre-existing
operator value. It requires the returned object to carry `CVAR_NOSET`, parses the
canonical decimal string directly into `uint32_t` without consulting the lossy
`cvar_t.value` float, and initially accepts exactly physics epoch `1`. Additional
epochs require an explicit game-DLL whitelist change, never a range check.

Identity publication is transactional. A level transition first invalidates the
old identity, captures the two protected engine strings during `SpawnEntities`,
hashes effective post-override entity text immediately before parsing, and
commits only after spawning completes. A map error, save-level restore, missing
bridge, malformed value, unsupported epoch, or map mismatch leaves v3 identity
unavailable; generator, loader, and runtime consumers receive only a copy of a
committed per-level record.

A successful publication emits one stable evidence line beginning `slipgate: v3
identity committed` with exact `map`, `bsp`, `entity_crc`, and `physics` values.
Failure emits `slipgate: v3 identity unavailable` and never publishes a partial
record.

The shared codec distinguishes structural decoding from runtime authentication.
Structural decoding proves the explicit wire shape, CRCs, registry laws, and
graph ownership without claiming that the file belongs to the active world.
Every generation, deployment, or live-load path must also supply the committed
level identity and require exact map, BSP, entity, host-physics, and proof-law
equality. A structurally known action is likewise not execution authorization:
runtime support and controller replay remain separate fail-closed gates.

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
Two links are duplicates when `(source, destination, action)` is identical;
cost, provenance, controls, or anchors do not create a second graph edge.

Ordinary `RL_DROP` has the narrower generated revision-2 timing law. Its
serialized `cost_ms` is at least one 100 ms server frame, strictly less than
4500 ms, and divisible by 100 ms. The proof law separately pins a 2500 ms
approach budget and 2000 ms post-walkoff travel budget; both are positive
server-frame multiples and their sum is the 4500 ms total bound. This rule does
not widen or reinterpret legacy v2, and compound `RL_DOOR_DROP` retains the
separate exact compound replay contract below.

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
`5c64bc3b` and SHA-256
`fd7b4c2288845f9c3448aa82aeabfd8921feb5c943a6ac2f4b8abacd49f36ece`.

The top-level `wire_diagnostics` table is also outside that artifact digest.
It is an append-only, generated C/Python API for reporting header, I/O, CRC,
identity, graph, allocation, and sidecar failures; those diagnostics are not
serialized into a RUNE record and cannot authorize an action. IDs and symbols
are pinned by fixtures, while wording may improve without invalidating an
otherwise identical movement contract or deployed artifact. Per-action proof,
record, live-controller, and recovery failures remain in the separate `RLR_*`
namespace inside the semantic contract.

The generator checks in two generated products:

- `slipgate/sg_action_contract.generated.h`;
- `tools/rune_contracts_generated.py`.

`--check` fails if either product differs. The semantic CRC32 described above
is stored in every v3 header. The shared 248-byte RUNE golden has header CRC32
`887334b9` and SHA-256
`a34d0f1721d6bbbe89828a76c3b477e80743b1473e9103f81a41d8b26cbf36a5`;
the bound 50-byte `HMN3` golden has header CRC32 `ce8382bd` and SHA-256
`9be29afd2eae1cc26d6c18f1caf9ea81559c6d830aa233f6633d27fac8b893a7`.

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

### `RL_DROP` controller revision 2

`RL_DROP` is at controller revision 2. Its canonical reducer policy is:

- a supported handoff before airborne continues the DROP;
- once airborne, terminal status is evaluated before recovery or handoff;
- exactly one dry grounded recovery is permitted; and
- a later grounded contact or water depth of two or more rejects.

The serialized command uses the canonical double-promoted DROP yaw byte, not
the legacy float-to-short intermediate. Its generated 2500/2000/4500 ms proof
budgets also make the reducer's admission envelope part of the semantic
contract. Revision 2 is semantic, so it
invalidates every prior global v3 RUNE and all five graph-indexed sidecars:
`HMN3`, `HML3`, `HME3`, `DPO3`, and `DNG3`. Focused contract, wire, and
sidecar golden acceptance establishes this boundary only. The separate 181-map
gate remains required after live DROP migration and corpus regeneration.

`tools/runeio.py` becomes the only Python wire parser. `runelint`, `runeview`,
`corpusgraph`, bake tools, `film`, `demorune`, `seedservo`, `mapflags`, and
`escapee` consume it or the generated metadata.

## Dense-action generation proof and loader admission

Ordinary `RL_DROP`, `RL_HOOK`, and `RL_SWIM` are dense generated actions. The
generator runs their shared reducer under the exact map, entity, host-physics,
proof-law, and per-action controller revision recorded by the v3 artifact. It
may emit a link only after that replay succeeds, and it stamps the link
`RL_PROVEN`. For all three actions, `RL_PROVEN` is the sole accepted v3
provenance (`provenance_mask = 0x0001`). The writer, loader, and runtime-v3
linter retain explicit action-level `provenance == RL_PROVEN` checks in
addition to the generated registry mask so a future metadata edit cannot
silently widen the controller contract.

At publication, the loader verifies the exact immutable payload, action
contract, structural/controller fields, map name, BSP checksum, effective
entity CRC, host-physics epoch, and complete proof law before admitting the
already-proved dense link. This is an exact-artifact generation proof, not an
instruction to replay every dense link synchronously during level load.

The accepted 135-rune control corpus contains 105,191 DROP, 1,875,703 HOOK,
and 639,934 SWIM links, all stamped `RL_PROVEN`. Replaying all 2,620,828 links
at publication would require at least the 141,561,036 25 ms traversal quanta
represented by their stored costs. The per-map stored-cost median is 919,488
quanta, the linearly interpolated p90 is 1,891,050 quanta (nearest-rank p90
1,896,824), and `xmap08` is worst at 3,494,440 quanta. Faithful replay also
requires four 25 ms aim-frame commands for each wet-source HOOK, raising the
actual 25 ms Pmove-command totals to 141,803,912 corpus-wide and 3,509,492 for
`xmap08`; including zero-millisecond categorization calls raises total Pmove
invocations to 142,504,565 and 3,562,067 respectively. That work is not a
bounded synchronous loader operation and is therefore forbidden.

Sparse `RL_DOOR` validation and future compound mechanism actions still replay
synchronously against the live map before publication, because their mover
identity and temporal witness require authoritative entities and their link
counts are sparse. This decision does not widen live dispatch or enable dormant
compound actions.

No one-byte RPF proof sidecar is added. Such a byte would merely duplicate
`RL_PROVEN` outside the payload while remaining replaceable at the same trust
boundary. The existing CRCs detect corruption and bind exact bytes to an exact
world/contract identity; they are not keyed signatures, do not identify a
signer, and do not authenticate artifacts against an adversary who can replace
local game files. The deployment trust boundary must supply locally trusted
artifacts.

Changing a semantic action field, including a dense-action provenance mask or
the DROP controller revision, changes the action-contract CRC. Every v3 RUNE
and graph-indexed sidecar carrying a prior contract CRC is stale and must be
regenerated. The accepted corpus can establish behavior compatibility, but old
bytes are not loadable under the new contract.

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

The registry's `effective_suffix` trait is a policy/classification adapter only.
It lets a compound inherit suffix pricing, tactical selection or exclusion
(including hook-ban route policy), field bias, and other explicitly classified
suffix traits. It must never choose generic execution dispatch, weapon or rope
ownership, commit release, or controller ownership. The compound outer action
retains one atomic lease and controller throughout the transaction and invokes
its DROP, SWIM, or HOOK suffix phase explicitly.

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

### Authenticated sidecar wire image

Graph-indexed sidecars use one 48-byte explicit-little-endian header. Native C
struct layout is never a file format. The header fields are:

| Offset | Type | Field |
|---:|---:|---|
| 0 | u32 | kind magic |
| 4 | u16 | sidecar format version, exactly 1 |
| 6 | u16 | header bytes, exactly 48 |
| 8 | u16 | bound rune version, exactly 3 |
| 10 | u16 | element bytes |
| 12 | u16 | plane count |
| 14 | u16 | reserved, zero |
| 16 | u32 | exact rune seed count |
| 20 | u32 | exact rune link count |
| 24 | u32 | exact rune payload CRC32 |
| 28 | u32 | exact action-contract CRC32 |
| 32 | u32 | exact rune header CRC32 |
| 36 | u32 | exact sidecar payload bytes |
| 40 | u32 | sidecar payload CRC32 |
| 44 | u32 | sidecar header CRC32 |

The sidecar header CRC covers all 48 header bytes with bytes 44–47 treated as
zero. The rune header CRC is the compact full-identity token: because it covers
the canonical case-sensitive map name, BSP checksum, entity CRC, graph counts
and CRC, action contract, and complete physics law, an exact comparison binds
the sidecar to all of them without duplicating the 64-byte map name. A sidecar
path is formed only from the already-validated map name in that rune header.

Kind magics are the little-endian byte strings `HMN3`, `HML3`, `HME3`, `DPO3`,
and `DNG3`. `HMN3`, `HML3`, and `HME3` are one u8 plane indexed by link.
`DPO3` is four u8 planes indexed by seed (red/blue defensive post followed by
red/blue intercept). `DNG3` is two explicit little-endian i32 planes indexed by
seed. Danger values must be in 0–8000. A defense or danger plane must be zero at
every tombstone seed; a tombstone can never become a learned field root.

The loader authenticates the fixed header and exact file size before allocating
the bounded payload, then verifies the payload CRC and kind-specific values into
a candidate. Missing optional sidecars are neutral. A malformed, stale, or
wrong-kind sidecar is diagnosed and ignored as a whole; it never rejects the
already-valid rune and never partially publishes a plane. Defense candidates
are available to field construction, but the rune, fields, and all sidecars
become visible only after the final fresh authority check.

Sidecar diagnostics use their own append-only `SCD_*` namespace; `RLW_*`
continues to classify the containing rune operation, and `RLW_BAD_SIDECAR` is
only its coarse fallback. The stable detailed order is `OK`, `ABSENT`,
`INVALID_ARGUMENT`, `PATH_TOO_LONG`, `IO_ERROR`, `BAD_MAGIC`,
`UNSUPPORTED_VERSION`, `BAD_HEADER_SIZE`, `BAD_RUNE_VERSION`,
`BAD_HEADER_CRC`, `NONZERO_RESERVED`, `BAD_SHAPE`, `BAD_COUNTS`,
`BAD_PAYLOAD_SIZE`, `BAD_FILE_SIZE`, `RUNE_PAYLOAD_MISMATCH`,
`ACTION_CONTRACT_MISMATCH`, `RUNE_HEADER_MISMATCH`, `BAD_PAYLOAD_CRC`,
`BAD_PAYLOAD_VALUE`, `ALLOCATION_FAILED`, `TEMP_EXHAUSTED`, `STATE_DRIFT`,
and `INTERNAL_ERROR`. Existing malformed files log this diagnostic and the
stable processing stage once; an absent optional file does not log.

Danger persistence uses a same-directory exclusive temporary whose name carries
a fresh per-process/per-transaction nonce, plus a bounded `O_EXCL` retry suffix.
Crash remnants therefore cannot consume one global finite namespace. It uses
exact writes, file flush/sync/close, and atomic replacement. It snapshots only while the
outgoing authenticated rune is still live: normal rotation saves before
`ExitLevel` queues the next map, and clean shutdown saves before identity reset
and tag teardown. An external engine `map` command exposes no pre-switch game
callback, so that transition deliberately skips persistence rather than binding
old lessons to incoming engine identity. Read-level restoration remains
reset-only until save files carry v3 identity.

Persistence is opt-in and fail-closed. `sg_dangerpersistport` defaults to the
canonical string `0`, which disables both DNG3 reads and writes. A nonzero value
must exactly match the engine's canonical protected effective server port
(`ip_hostport`, then `hostport`, then `port`); the game reads the cvar strings
and never their lossy floating values. Matching the selector is necessary but
not sufficient: before any DNG3 read, the process must acquire a nonblocking
whole-level advisory lock on the stable sibling
`<map>.rune.danger.lock`. The lock is held through the final save and level
reset. A contending process stays ephemeral and performs no DNG3 I/O. The lock
file is an empty regular single-link file, is never deleted, and is distinct
from the atomically replaced DNG3 inode. This closes shared-gamedir same-map
fleet races. The exact game directory used to derive the lock and DNG3 path is
latched with the lease; directory drift refuses the checkpoint instead of
writing through a lock held on another destination. Changing the selector
during a level may revoke a save but can never grant a lease mid-level.

DNG3 decay measures authenticated active play only. Offline wall-clock time,
intermission/scoreboard time, time spent under a mismatched physics law, and
direct-map transition time do not age the planes; the 48-byte DNG3 format
intentionally has no timestamp.
There is no periodic checkpoint in this slice. Dirty state is captured with a
monotonic revision at the two final lifecycle boundaries above, and is marked
clean only after durable replacement of the exact revision. Immediately before
replacement the callback rechecks the live authority, held lease, owner policy,
revision, and the installed rune's exact 128-byte header and file size. An
absent DNG3 starts neutral and may be created after new learning; a stale,
malformed, or unreadable existing DNG3 disables persistence for that level, so
a failed read can never authorize overwriting the prior file. Save failure is
reported and retains dirty state, but never blocks rotation or shutdown.

Live in-process rune replacement is unsupported until all per-bot link indices,
shelves, commitments, sticky/watch state, fields, and sidecars can be replaced
transactionally. Normal map-level loading resets them before publication.

## Implementation slices

| Slice | Scope | Done condition |
|---|---|---|
| E0 | Engine checksum and physics-ID bridge | Protected values exist before `SpawnEntities`, change with map/host law, and cannot be changed by console |
| S1 | Canonical JSON, generator, generated C/Python metadata, action descriptor adapters | IDs 0–8 unchanged; IDs 9–11 and provenance 4 appended; C/Python metadata match; `--check` clean; legacy pricing/classification unchanged |
| S2 | Explicit v3 I/O, identity binding, shared Python parser, strict compatibility | Golden 128/16/44 vectors round-trip in C/Python; corruptions reject; v2/v3 incompatibility is actionable; gravity-650 fixture loads |
| B4 | Authenticated graph sidecars | Shared 48-byte C/Python vectors round-trip; bakers consume decoded v3 records; stale/corrupt/tombstone data stays neutral; danger survives normal rotation and clean shutdown atomically |
| S3 | Shared pose-based DROP/SWIM/HOOK replay | Generator and runtime use the same commands and terminal predicates; loader admits only exact-artifact `RL_PROVEN` dense links and synchronously replays sparse mechanism transactions; `lmctf09` exact hooks remain |
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
