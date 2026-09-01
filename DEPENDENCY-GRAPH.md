# LMCTF BuzzMod completion dependency graph

This document defines the reviewed dependency graph for completing the project.
Three Luna Max agents produced independent requirement, implementation, and
release graphs. A Sol High agent produced a competing graph and reconciled all
four results against the source and the 51 requirements in
`PROJECT-COMPLETION-PLAN.md`.

The graph has no arbitrary agent-count limit. Assign each ready node to an
appropriate model and effort when it has isolated writable ownership. State the
assignment explicitly. Root and a Sol High reviewer check each integrated gate
against the diff and runtime evidence.

## Current implementation correction

The production `sv rune` command still calls the legacy point-seed,
action-labelled link, objective-pruning, and legacy stream path. The newer
configuration, phase, dynamics, field, and proof modules have no complete
production generator caller. Their exhaustive state products are not an
intermediate version of the target RUNE. They are a rejected second navigation
representation.

The replacement connects `sv rune` directly to one compact BSP-derived field
builder in an unshipped offline generator game module. The shipped Linux and
Windows game modules retain only the compact loader/runtime and fail closed on
generation; corpus staging installs the generator module only for the `sv rune`
pass, then restores the runtime module for cold load. The split deletes legacy
seed/link/action ownership and the unused exhaustive phase, dynamics,
refinement, and proof-provider paths after their useful host, geometry,
weapon-law, publication, and runtime-service pieces migrate.

## Core construction graph

```text
Requirements authority
        |
        v
Contract freeze
  +-- static model and identity
  +-- capability and cost interfaces
  +-- runtime navigation interfaces
  +-- artifact and acceptance format
  +-- linear load and release contracts
        |
        +------------------------------------------+
        |                                          |
        v                                          v
BSP and field construction                 Parallel runtime work
  complete BSP and entities                  strategy queue
      |                                      runtime beliefs
  host collision and Pmove                   human trace review
      |                                      live combat policy
  global hull-expanded cell complex
      |
  exact half-open faces and split-carried portals
      |
  semantic, mechanism, visibility, and occlusion regions
      |
      +-----------------------------+
      |                             |
      v                             v
  analytic movement fields      analytic weapon fields
  ground/water/air/hook/        hitscan/rail/spread/cone/
  movers/external forces        bolt/rocket/grenade/BFG
      |                             |
      +-----------------------------+
                    |
        canonical order and compact indexes
                    |
        linear artifact checks and serialization
                    |
                    v
          complete static RUNE model
```

The generator preserves topology while it partitions the BSP-derived space.
There is no production completeness reconstruction or proof-provider barrier.
Deep BSP and host comparison runs during development on one ordinary RUNE and
one hard RUNE. It establishes the generator design; it is not repeated for the
other 173 maps. Production generation performs only fast linear identity,
format, count, span, reference, order, finite-value, checksum, and bot-load
checks. Once a generated artifact passes those checks, that map is complete.
Nothing downstream depends on a corpus-wide proof, path enumeration, geometry
reconstruction, phase-space search, or hours-long per-map verification pass.

Cells are shared by movement and weapons. Movement and
weapon families attach compact analytic fields to the same cells and surfaces.
Weapon fields cover hitscan visibility, rail penetration and lanes, automatic
and spread exposure, shotgun cone occupancy, straight bolt travel, rocket
impact and splash, grenade arc, bounce and fuse, and BFG or special behavior.
Runtime players, beliefs, ammo, health, destinations, and tactics remain outside
the RUNE.

Hook and weapon construction share sparse visibility and occlusion regions.
Partition at first-hit and silhouette discontinuities. Store analytic relations
inside each unchanged region. Do not symbolically enumerate every origin,
control, ray, or trajectory. The live hook and live weapon boundaries retain
their exact traces without changing human hook or firing behavior.

## Runtime graph

Runtime branches begin when their contracts exist. They do not wait for the
entire generator.

```text
Capability cost contract
        |
        v
directional field solver
        |
cache, hierarchy, moving destinations
        |
        +-----------------------------+
        |                             |
        v                             v
cell/phase localization         typed strategy queue
        |                             |
        +--------------+--------------+
                       |
                       v
           tactical capability selection
                       |
           exact movement/mechanism controller
                       |
             bounded tactical modifiers
```

```text
cell/phase contract          earned observation contract
        |                              |
        v                              v
sparse player beliefs <---- perception adapters
        |
movement-based propagation,
diffusion, exclusion, and prediction
        |
        +-----------------------------+
        |                             |
        v                             v
weapon affordances              strategy/tactics
        |                             |
        +--------------+--------------+
                       |
                       v
      probabilistic aim and weapon decision
                       |
             exact live pre-fire trace
```

```text
human trace review
        |
wire + costs + strategy + tactics
        |
        v
verified learning of costs,
landing preferences, tactical priors,
and strategy
```

Learning cannot add geometry or connectivity.

## Artifact graph

```text
wire contract
    |
    +-- new codec
    +-- hostile-input validation
    +-- fail-closed loader
    +-- atomic publication
    +-- one production C wire inspector
    +-- thin command-line caller of that inspector
    +-- linear identity, structure, checksum, and finite-value checks
    +-- sidecar migration
    +-- obsolete repair-sidecar deletion
    |
    v
runtime-readable new-model artifact
```

This branch can use synthetic structures before the generator exists. It joins
the static generator at artifact integration.

## Parallel implementation ownership

Any ready node can receive its own appropriately assigned implementation agent.
Shared files require designated integrators.

| Integrator | Exclusive ownership |
|---|---|
| Contract | Shared model headers, generated contracts, identity, and common declarations |
| Static model | `sg_rune.h`, `sg_rune.c`, configuration construction, and fixed-point integration |
| Artifact | Codec, file, stream, loader, writer, publication, installation, and sidecars |
| Navigation | `sg_bot.h`, `sg_arach.c`, `sg_fields.c`, `sg_goal.c`, `sg_descend.c`, `sg_move.c`, and `sg_price.c` |
| Perception and combat | `sg_caco.c`, `sg_combat.c`, belief integration, and weapon policy |
| Host | Human hook boundary, game lifecycle, event hooks, physics, weapons, and root host callers |
| Tools | The minimum shell and Make generation, corpus, install, rollback, and release commands. Delete Python tools and optional fleet machinery that the final path does not use. |
| Build | `GNUmakefile`, `Makefile`, generated source lists, and final test aggregation |
| Documentation | `PROJECT-COMPLETION-PLAN.md`, `ARCHITECTURE.md`, tool documentation, and catalog status |

Agents implement isolated modules and tests, then hand completed branches to the
relevant integrator. Root and a Sol High agent review each integrated gate
against the actual diff and runtime evidence.

## Integration and release graph

```text
complete static RUNE model
        +
independently readable artifact
        +
navigation, strategy, and tactics
        +
beliefs, combat, and learning
        +
production toolchain
        |
        v
mandatory review of all retained code and callers
        |
        v
delete seed/link/action ownership,
objective-derived artifact validity, release-routing branches,
production Dijkstra repair, and obsolete tests
        |
        v
deep development check of one ordinary and one hard RUNE
        |
development timing, memory, and determinism check
        |
both final host-build dialects
        |
unchanged source commit
        |
immutable snapshot with exactly 175 BSPs
        |
        v
Wave A: ordinary maps with 12 isolated workers
        |
strict all-ordinary completion barrier
        |
Wave B: hard regression maps with 12 isolated workers
        |
        v
per-map linear checks and bot cold load
        |
immutable 175-map corpus
        |
manifest, identity, and count agreement
        |
authenticated bundle
        |
install, recovery, rollback, and installed cold load
        |
ordinary match evidence
        |
local tag, supported builds, and clean release verification
```

Each map receives only the linear artifact checks and bot cold load when it
finishes. Deep geometry comparison does not run against the 175-map corpus.
Corpus finalization waits for all 175 generated and loadable results; it does
not wait for proof records or an additional semantic-verification wave.

## Corrected dependencies

The Sol High review rejected these dependencies from the initial analyses:

- Beliefs, weapon profiles, strategy reducers, wire work, and human capture do
  not wait for the completed generator.
- Artifact work does not wait for beliefs, combat, or learning.
- Tactical controller implementation does not wait for the final deterministic
  builder. Only real-artifact integration waits.
- The two-map development checker does not enter production generation or
  artifact acceptance.
- Fleet is not a separate mandatory release phase unless it is the selected
  ordinary-match test system.
- The completion plan does not require a post-fleet repair phase.
- Every objective-gated release branch is obsolete and must be deleted.
- BSP-3 is full 3D configuration space. BSP-4 is hull-valid adjacency. BSP-7
  owns contents and static semantics.

## Frozen interfaces for the first implementation wave

The implementation wave is bound by these decisions:

- Shared exact cells, half-open portals, and stance-validity ownership.
- Compact analytic fibers for velocity, support, water, air, hook, movers, and
  time.
- Canonical quantization, stable provenance-derived identity, dense canonical
  indices, deterministic order, and loud overflow failure.
- A two-map deep development checker that never enters production acceptance.
- Analytic movement and weapon kernels consumed by the runtime field service.
- Continuous directional costs, runtime destination gradients, and incremental
  moving-target updates.
- Typed strategy authority and tactical suspend, resume, completion, and
  failure events.
- Sparse runtime beliefs from authenticated observations only.
- Versioned wire sections, checksums, hostile-count limits, and one canonical C
  inspector shared by runtime and command-line acceptance.
- The obsolete seed/link repair sidecar is deleted; any future learned-cost
  sidecar requires its own stable-cell or capability-kernel contract.
- The authoritative hard-regression map set and its strict wave barrier.
