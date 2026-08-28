# Hook visibility relation

## Decision

Build one authenticated hook-visibility catalog from the complete legal hook
action domain. Use symbolic execution of the production muzzle-clearance and
first-hit law as the proof authority. Use BSP and occlusion events to discover
and share predicates, not as visibility proof by themselves.

The catalog proves static first-hit relations. Hook/air construction later
composes those relations with pull, release, coast, air control, relaunch,
destination phase, and cost laws. Runtime tactics choose the actual aim and
timing. Human hook gameplay remains unchanged.

This design combines two independently reviewed candidates:

- Keep the complete-domain proof, explicit rejected-domain complement, and
  independently checked decision DAG from the symbolic-action candidate.
- Keep BSP-guided event discovery, shared occlusion boundaries,
  lower-dimensional boundary cells, and the strict separation of visibility,
  movement outcomes, and cost laws from the segment-complex candidate.
- Reject unrestricted singleton action fallback, configuration portals used as
  bolt portals, an unaudited continuous line-space proof, and serialization of
  equivalent tactical rays.

## Caller contract

```c
typedef struct sg_hook_visibility_catalog_s sg_hook_visibility_catalog_t;

typedef struct sg_hook_visibility_sources_s {
    const sg_host_collision_authority_t *collision;
    const sg_configuration_space_t *configuration;
    const sg_configuration_semantics_t *semantics;
    const sg_static_visibility_t *coarse_visibility;
    const sg_hook_fire_law_t *fire_law;
    const sg_hook_mover_domain_t *movers;
} sg_hook_visibility_sources_t;

typedef struct sg_hook_visibility_build_env_s {
    const char *scratch_directory;
    uint64_t memory_batch_bytes;
} sg_hook_visibility_build_env_t;

int SG_HookVisibilityBuild(
    const sg_hook_visibility_sources_t *sources,
    const sg_hook_visibility_build_env_t *environment,
    sg_hook_visibility_catalog_t **catalog_out,
    sg_hook_visibility_error_t *error_out);

int SG_HookVisibilityAudit(
    const sg_hook_visibility_sources_t *sources,
    const sg_hook_visibility_catalog_t *catalog,
    sg_hook_visibility_audit_report_t *report_out);

int SG_HookVisibilityQuery(
    const sg_hook_visibility_catalog_t *catalog,
    const sg_hook_visibility_query_t *query,
    sg_hook_visibility_choice_view_t *choices_out);

void SG_HookVisibilityDestroy(sg_hook_visibility_catalog_t *catalog);
```

The semantic sources and build environment are separate. Working memory may
change batch size only. It cannot stop discovery or change catalog bytes.
Construction succeeds only after the work frontier is empty and the
independent audit reports zero omitted, invented, and pending domains.

## Stored relation

Each published relation contains:

- an exact source-origin domain within authenticated configuration cells;
- stance, motion phase, hand, and mechanism conditions;
- a symbolic quantized-control fiber;
- the first non-sky hookable surface patch;
- a proof reference and immutable input identity.

The relation does not contain a chosen aim, release time, destination phase, or
cost label. Those belong to later layers. The runtime may select any control in
the accepted fiber and must still run the exact live trace before firing.

The RUNE stores the reduced relation and control domain needed by runtime. The
full construction DAG and rejected-domain complement remain authenticated
build evidence. Equivalent tactical controls with the same static relation are
not serialized as separate records.

## Exact host authority

The fire law binds all behavior that can change a static hook result:

- standing and crouching view heights;
- left, center, and right-handed muzzle offsets;
- legal quantized pitch and yaw;
- origin-to-muzzle clearance;
- `MASK_SHOT`, first-hit order, trace epsilon, finite range, and sky rejection;
- BSP, entity semantics, map physics, collision ABI, and mover-domain identity.

A generated one-dimensional angle authority records and audits the exact float
bits for every short-angle sine and cosine value under the supported host ABI.
Pitch/yaw directions are derived from that authority. Enumerating this bounded
one-dimensional host table is allowed; enumerating
`origin x pitch x yaw` is not.

Moving submodels require an authenticated finite or symbolic transform domain.
The builder fails closed when that authority is unavailable. It never freezes a
mover at one representative pose.

## Construction

1. Validate every source identity, hull, physics field, fire-law field, angle
   authority, and mover domain before geometry work.
2. Define roots over all admitted q8 origins, stance and motion phases, hand,
   quantized controls, and mechanism conditions.
3. Traverse the actual BSP and brush program. BSP nodes, brush sides,
   occluder edges, range boundaries, endpoint events, and host first-hit ties
   propose canonical predicates and share trace prefixes.
4. Symbolically execute the handed muzzle transform, origin-to-muzzle trace,
   and first `MASK_SHOT` hit. Split only at unresolved host predicates. A split
   proves child disjointness and union with its parent.
5. Preserve lower-dimensional boundary domains because a legal q8 origin or
   quantized control can lie exactly on one.
6. Emit visible or conditional first-hit relations. Keep blocked, sky, no-hit,
   and nonhookable domains in the proof complement.
7. Reduce visible domains by final static relation meaning. Do not retain
   separate records merely because equivalent controls followed different
   internal trace branches.
8. Write canonical content-addressed work runs and merge them in key order.
   Crashes and batch-size changes converge to the same fixed point.

Configuration-space portals describe player-hull continuity. They are never
used as bolt-visibility portals. A future shot-space portalization may be an
enumeration index only after it gains a distinct type, exact construction
certificate, and independent audit.

There is no unrestricted singleton fallback. If the symbolic representation
cannot close a domain, construction reports unsupported host semantics or the
required representation overflow. It does not publish partial coverage and
does not silently switch to action-tuple enumeration.

## Independent audit

The auditor reconstructs the legal root domain from immutable inputs and uses
a separately implemented checker to verify:

- every split covers its parent with disjoint children;
- every terminal agrees with the exact fire-law branch program;
- every visible terminal names its actual first hookable non-sky hit;
- conditional relations name authenticated mover or area state;
- the accepted roots equal the union of visible and rejected domains;
- the reduced runtime relations preserve every distinct static outcome;
- no pending frontier or incomplete work run exists;
- producer and verifier identities differ.

Exact witness traces are differential diagnostics. They may refute a proof but
cannot establish coverage.

## Feasibility gate

Before the implementation expands, a synthetic two-leaf BSP fixture must prove
the entire chain over a small bounded q8 source volume:

- authenticated quantized direction mapping;
- handed muzzle transform and clearance;
- first hit, sky, edge, vertex, and tie behavior;
- canonical domain reduction;
- independent complement checking;
- work proportional to predicates and emitted relations rather than action
  tuples.

Reject and redesign the implementation if it closes the fixture only by
enumerating origins and pitch/yaw pairs, imports builder partitions as audit
truth, omits lower-dimensional domains, or serializes equivalent tactical rays
as separate RUNE records.

## Ownership

- `sg_hook_visibility` owns the opaque catalog, exact fire relation, canonical
  reduction, query index, and construction metrics.
- A private predicate/proof module owns symbolic host branches and build
  evidence. Wire and decision-node types stay private.
- An independent audit module reconstructs roots and checks proofs without
  calling builder helpers.
- `sg_movement_hook_air` consumes accepted visibility relations and owns pull,
  release, destination, and cost-law composition.
- Runtime tactics consume accepted control fibers. Human hook fire, attach,
  release, and refire functions are read-only.

