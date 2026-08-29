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
requires a live opaque source issued by `SG_BeliefHorizonSourceIssue`. The
issuer reads only the accepted belief binding and its immutable RUNE snapshot.
For the exact requested interval it emits the same-phase outcome and every
finite sequence of phase-transition and capability witnesses whose aggregate
authenticated duration contains the interval. Positive movement duration gives
the closure a derived finite depth; acyclic zero-minimum-duration witnesses are
included between positive steps. A zero-minimum-duration cycle is rejected
because it has no finite complete enumeration. The outcomes receive a
deterministic equal topology prior; no actor observation or hidden actor
location can select an outcome or weight.

The module registry owns every source and accepted-authority handle. It assigns
a monotonic issuance identity that does not use belief state generation. A
copied or fabricated object therefore has no authority, even if another
translation unit reproduces the private layout. Destroy frees the kernel
payload and retires the handle, but retains its registry record as a tombstone.
The module checks registry membership before dereferencing a handle, and no
later issuance can reuse a retired handle address.

Acceptance validates every candidate witness, exact interval tiling, and
byte-exact semantic equality with the issued fixed point before it deep-copies
the chain. The caller-set completeness byte is validated data, not evidence of
completeness. Issue, source view, and acceptance validate every writable output
range before writing. Those ranges must be mutually disjoint and disjoint from
the snapshot, belief, source, candidate, and nested kernel storage they borrow.
Every public belief operation that accepts caller-writable storage rejects
overlap with every live or retired source and authority record and every live
registry-owned kernel payload.

The chain identity is SHA-256 over a versioned, fixed-width, little-endian
encoding of its provenance, kernels, spans, outcomes, likelihoods, and witness
steps. This avoids ABI padding and host-endian identity drift. Prediction
requires both live issuance and an exact identity match before consuming the
owned chain.

## Prediction

Each accepted stage uses the same retained-versus-witnessed branching, exact
phase checks, capability checks, cell containment, merge, and stable
normalization rules as belief reduction. It performs no graph search and
admits no transition beyond the issued topology outcomes. The result reports
the subject, belief and RUNE revisions, and the complete influencing authority
chain identity. Capability displacement intervals remain uncertainty: their
midpoint locates the sparse mode while the interval half-width expands its
spread.
