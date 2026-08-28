# LMCTF BuzzMod completion dependency graph

This document defines the reviewed dependency graph for completing the project.
Three Luna Max agents produced independent requirement, implementation, and
release graphs. A Sol High agent produced a competing graph and reconciled all
four results against the source and the 51 requirements in
`PROJECT-COMPLETION-PLAN.md`.

The graph has no arbitrary agent-count limit. Assign each ready node to a Sol
High implementation agent when that agent has isolated writable ownership.
Root reviews each integrated gate against the diff and its verification
evidence.

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
  +-- proof and release contracts
        |
        +------------------------------------------+
        |                                          |
        v                                          v
BSP and configuration work                 Parallel foundation work
  BSP parser                                 wire codec and readers
      |                                      strategy queue
  host collision and Pmove                   belief representation
      |                                      weapon profiles
  hull-valid configuration space             human trace review
      |
  3D cells and portals
      |
  contents, landmarks, mechanisms,
  visibility, and hookable surfaces
      |
  independent BSP completeness proof
      |
      +----------------+------------------------+----------------+
      |                |                        |                |
      v                v                        v                v
 ground movement     water movement      hook visibility      mechanisms
 walk/crouch/ramp    full 3D volume      relation refinement  doors/lifts/etc.
 jump/drop                               and coverage audit
                                                 |
                                                 v
                                           hook and air
                                           bolt and body
                                           pull/release
      |                |                        |                |
      +----------------+------------------------+----------------+
                               |
                               v
                    directional time-cost kernels
                               |
                    destination-independent fixed point
                               |
                               v
                    complete static RUNE model
```

The independent BSP completeness proof is a hard barrier before movement
capability generation. It must prove that all valid cells and portals exist and
that the generator invented no connection.

Hook/air construction also depends on a complete hook-visibility relation
catalog. The base static-visibility API proves exact point rays and coarse
PVS/area exclusions; it does not prove continuous player-origin to surface
coverage. The refinement must partition that coupled domain at occluder
boundaries and carry an independently auditable no-omission/no-invention
certificate. Finite point sampling is not a substitute.

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
    +-- independent GNU C reader
    +-- independent Make C reader
    +-- independent Python reader
    +-- lint and semantic completeness
    +-- sidecar migration
    +-- SNAG retain-or-delete decision
    |
    v
independently readable new-model artifact
```

This branch can use synthetic structures before the generator exists. It joins
the static generator at artifact integration.

## Sol High implementation ownership

Any ready node can receive its own Sol High implementation agent. The shared
files require designated integrators.

| Integrator | Exclusive ownership |
|---|---|
| Contract | Shared model headers, generated contracts, identity, and common declarations |
| Static model | `sg_rune.h`, `sg_rune.c`, configuration construction, and fixed-point integration |
| Artifact | Codec, file, stream, loader, writer, publication, installation, and sidecars |
| Navigation | `sg_bot.h`, `sg_arach.c`, `sg_fields.c`, `sg_goal.c`, `sg_descend.c`, `sg_move.c`, and `sg_price.c` |
| Perception and combat | `sg_caco.c`, `sg_combat.c`, belief integration, and weapon policy |
| Host | Human hook boundary, game lifecycle, event hooks, physics, weapons, and root host callers |
| Tools | Controller, finalizer, corpus policy, bundle, installer, fleet, and provenance formats |
| Build | `GNUmakefile`, `Makefile`, generated source lists, and final test aggregation |
| Documentation | `PROJECT-COMPLETION-PLAN.md`, `ARCHITECTURE.md`, tool documentation, and catalog status |

Sol High agents implement isolated modules and tests. They hand completed
branches to the relevant integrator. Root reviews each integrated gate against
the actual diff and runtime evidence.

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
objective validity, route-only handling,
production Dijkstra repair, and obsolete tests
        |
        v
real-BSP and real-engine proof
        |
determinism, batching, memory, and scaling proof
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
per-map independent validation and cold load
        |
immutable 175-map corpus
        |
independent corpus verification
        |
authenticated bundle
        |
install, recovery, rollback, and installed cold load
        |
ordinary match evidence
        |
local tag, supported builds, and clean release verification
```

Per-map validation starts when each map finishes. Corpus finalization waits for
all 175 accepted results.

## Corrected dependencies

The Sol High review rejected these dependencies from the initial analyses:

- Beliefs, weapon profiles, strategy reducers, wire work, and human capture do
  not wait for the completed generator.
- Artifact work does not wait for beliefs, combat, or learning.
- Tactical controller implementation does not wait for the final deterministic
  builder. Only real-artifact integration waits.
- Proof tooling does not wait for final documentation.
- Fleet is not a separate mandatory release phase unless it is the selected
  ordinary-match test system.
- The completion plan does not require a post-fleet SNAG phase.
- Every route-only release branch is obsolete and must be deleted.
- BSP-3 is full 3D configuration space. BSP-4 is hull-valid adjacency. BSP-7
  owns contents and static semantics.

## Decisions that block the first implementation wave

The contract wave must settle these decisions:

- Cell geometry, portal representation, and standing or crouching overlap.
- Phase representation for velocity, support, water, air, movers, and time.
- Quantization, stable identifiers, deterministic ordering, and overflow
  behavior.
- Formal configuration-space completeness and its independent checker.
- Capability-kernel and runtime-controller interfaces.
- Directional cost representation and the field-solver contract.
- Strategy authority and tactical suspend, resume, completion, and failure
  events.
- Sparse belief representation and authenticated observations.
- Wire sections, checksums, hostile-count limits, and reader agreement.
- Whether SNAG remains a bounded cost sidecar or is deleted.
- The authoritative hard-regression map set and its strict wave barrier.
