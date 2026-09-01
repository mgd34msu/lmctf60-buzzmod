# Compact destination field cache design

## Problem

The compact field owns the authoritative directed state transitions, Q52.12
cost arithmetic, destination localization, and clean fixed-point solve. The
field service owns semantic target identity, immutable plan versions, and
leases. Moving targets currently rebuild a complete plan and the service's
undirected component epochs do not participate in the calculation.

The cache must keep old leased plans immutable, refresh only states whose
directional cost can change, and publish exactly the cost vector produced by a
clean solve. `UINT64_MAX` remains unavailable. The design adds no legacy
seed/link records, priority-queue solver, proof engine, work cutoff, or partial
result.

## Usage

Callers keep the existing service API. A newer moving generation derives an
unpublished plan from the current plan, publishes it, then retires the old
version. Static targets and repeated identical moving versions reuse the
current immutable plan.

```c
SG_RuneCompactFieldServiceResolve(service, &static_target, &static_handle);
SG_RuneCompactFieldServiceRefresh(service, &moving_handle,
	&moved_target, &moved_handle);
SG_RuneCompactFieldServiceQuery(service, &moved_handle, &local, &result);
SG_RuneCompactFieldServiceRelease(service, &moved_handle);
```

The service calls one field-private operation. It cannot see arcs, cost rows,
destination-state sets, or the update queue.

```c
sg_rune_compact_field_refresh_report_t report;

SG_RuneCompactFieldPlanDerive(previous_plan, &target.destination,
	&next_plan, &report);
```

## Shape

`sg_rune_compact_field_region_hierarchy.[ch]` owns a deterministic hierarchy
derived from immutable cell provenance:

```text
field root
  model and BSP area coarse region
    model, area, and cluster leaf region
      compact cells
```

Exact state dependencies select the repair set. The hierarchy batches its
queue as root, coarse region, leaf region, then state, and measures the regions
that changed. It never changes reachability or cost semantics.

`sg_rune_compact_field.c` keeps precomputed directional Q52.12 transition
costs beside its existing arcs. A destination plan stores a copied destination
and a flat immutable cost vector. Plan derivation copies the predecessor cost
vector, repairs the copy, and publishes only after success. Existing leases
therefore continue to query their original generation.

For each state `v`, the one authoritative equation remains:

```text
C(v) = 0                                      when v is a destination state
C(v) = min(weight(v,u) + C(u))                otherwise
C(v) = UINT64_MAX                             when no term is finite
```

All weights are positive stored Q52.12 units. Addition reserves
`UINT64_MAX` and returns the same overflow status as a clean solve.

## Incremental derivation

1. Resolve the old and new destinations with the same POINT, CELL, SURFACE,
   and multi-cell ITEM rules used by clean creation.
2. Removed destination states lose their zero-cost support.
3. Walk incoming old equality dependencies through the hierarchical work
   queue. A predecessor is invalidated only after every old minimum-cost
   support is removed. Equal alternatives remain valid.
4. Set invalidated costs to unavailable and install every new destination
   state at zero.
5. Recompute invalidated states from retained boundary values, batching work by
   coarse and leaf region. Propagate every strict decrease through incoming
   transitions until the fixed point stops changing.
6. Count the leaf and coarse regions containing changed states. No unrelated
   state equation is evaluated.

Positive weights make the old equality dependency relation acyclic by cost.
The invalidation pass therefore finds exactly the states that lost their old
minimum. The repair pass starts those states at unavailable and decreases them
under the new destination equation. States outside the invalid set retain an
old valid support; added destinations can only decrease them and the incoming
propagation covers that case. The final vector is the same least fixed point as
clean creation, including ties and disconnected unavailable states.

## Synthesis decision

Candidate A is the base because it keeps the solver and its invariants inside
the field. Candidate B's exported-edge bridge was rejected because it leaked
the private transition representation and implemented a second Bellman solver
in the cache.

The chosen design takes these ideas from Candidate B:

- preserve the current field handle, rune identity, topology revision, and
  retired-lease rules;
- treat identical old and new destination-state sets as a zero-region update;
- verify one-way movement, allocation rollback, invalidation, and
  skipped-region metrics explicitly.

The initial grid-based sketches were rejected because they modeled neither the
compact destination domain nor Q52.12 state costs.

## Tradeoffs accepted

- We copy the predecessor cost vector before repair so published plans remain
  immutable.
- We store the region hierarchy once per field so every later refresh can
  report work without rediscovering cell provenance.
- We keep a FIFO fixed-point update rather than adding a priority queue.

## Verification

The focused C test compares every state cost after each derive with an
independent `SG_RuneCompactFieldPlanCreate` result. Cases cover additions,
removals, retained equal alternatives, disconnected regions, one-way movement,
same-cell point movement, multi-cell items, unavailable values, and allocation
rollback. The shell runner builds with GCC, Clang, static analysis, ASan, and
UBSan.
