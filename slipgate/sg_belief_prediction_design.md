# Belief horizon prediction

## Usage

The caller supplies an immutable belief state, the exact accepted RUNE
snapshot that owns it, a target time, and either an empty kernel chain or a
complete contiguous chain. An empty chain means same-phase kinematic aging.
A nonempty chain must tile the whole prediction interval exactly.

Two caller-owned scratch spans hold intermediate sparse distributions. The
destination span is separate and remains unchanged unless prediction succeeds.
Capacity and representation failures report a retry size; they never drop a
mode or publish a prefix.

## Shape

`SG_BeliefPredict` validates every kernel before using it, then streams the
distribution through the chain with ping-pong scratch. Each stage uses the same
retained-versus-witnessed branching, phase checks, capability checks, cell
containment, merge, and normalization rules as belief reduction. It performs
no graph search and derives no transition from topology alone.

The result carries the subject and the state and RUNE revisions that produced
it. Inputs are borrowed and immutable. Scratch is disposable. Output particles
and the result structure belong to the caller.

## Alternatives

A materialized composed CSR kernel was rejected because it creates another
public proof artifact and duplicates complete-kernel validation without helping
the runtime caller. A cursor API was rejected because partial iteration exposes
an easy path to caller-selected truncation. Keeping the old ballistic-only API
was rejected because it cannot consume authenticated reachability witnesses.
