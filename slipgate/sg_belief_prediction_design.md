# Belief horizon prediction

## Data shape

`SG_BeliefPredict` borrows an immutable belief state, its exact accepted RUNE
snapshot, a target time, and optionally an opaque horizon authority. Null
authority means same-phase kinematic aging over
`[state.updated_at_ms, request.at_time_ms]`. A non-null authority owns a
contiguous chain that tiles that exact interval.

The authority binds the RUNE identity and topology revision; belief generation,
revision, frame sequence, subject, and source time; predecessor issuer, source,
generation, and fixed-point identities; target time; and the content identity
of every kernel, origin span, outcome, likelihood, and witness step. Prediction
revalidates that binding before reading the owned chain.

Two caller-owned scratch spans hold intermediate sparse distributions. The
destination span is separate and remains unchanged unless prediction succeeds.
Capacity and representation failures report retry sizes and never publish a
prefix. The caller owns output particles and the result structure.

## Authority boundary

Raw kernels cannot enter `SG_BeliefPredict`. `SG_BeliefHorizonAuthorityAccept`
requires an opaque source publication from the authenticated phase-space
topology and runtime-localization predecessor. Acceptance independently
validates every candidate witness, exact interval tiling, and exact equality
with the predecessor's complete fixed point, then deep-copies the predecessor
chain. A caller-set completeness byte is not acceptance evidence. Later caller
mutation cannot change the published chain, and content tampering invalidates
its identity.

This repository has no production publisher for that predecessor publication.
The production API therefore exposes no source constructor. Tests use a
test-only predecessor fixture to exercise independent comparison and the
consumer semantics. The runtime-localization owner must publish the real opaque
source before a live caller can request witnessed cross-phase prediction.

## Prediction

Each accepted stage uses the same retained-versus-witnessed branching, exact
phase checks, capability checks, cell containment, merge, and stable
normalization rules as belief reduction. It performs no graph search and
derives no transition from topology alone. The result reports the subject,
belief and RUNE revisions, and the complete influencing authority chain
identity.
