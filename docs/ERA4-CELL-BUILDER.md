# Era-4 cell builder: design and cut list

Written 2026-09-01 before deleting the Codex carve. This is the record of what
the replacement must do, what it must produce, and what goes.

## Why

Generation of one map (bctf01, 4.4 MB) never completed. Measured on the run:
the semantics partition enumerated an arrangement of ~400 support planes per
cell (deleted in 9f30cba8); the configuration carve then took 7 minutes and
produced 79,089 cells over two stances, 3.36 million intermediate topology
regions, and 2.96 million topology portals, and its portal pass ran at ~80
portals/s (a lattice solve per portal side plus a scan over every published
portal per append). Every one of those costs exists to feed audits and
certificates that no longer exist. Generation must be fast and linear; that
is the reason era 4 exists.

## What the builder produces (the only contract)

Consumers (compact geometry, semantics, static visibility, response partition,
movement fields, static materializer) read these and nothing else:

- `identity`, `domain`
- `cells[]`: id, order key, stance validity, first_face/face_count, bounds,
  interior witness, bsp leaf/area/cluster refs, contents mask
- `faces[]`: plane (normal, distance, source kind/index/variant, reversed),
  first_vertex/vertex_count, kind (facet or constraint-only)
- `vertices[]`
- `portals[]`: from/to cell, plane, polygon, clearance

Not produced any more: certificate nodes and roots, topology regions and
portals, stance overlaps as a separate table, lattice and brush-index
statistics, topology index. Consumers that read those are fixed to stop.

## Construction (linear)

1. **One carve, crouch hull.** The crouch box shares footprint and bottom
   with the standing box; only the top differs (4 vs 32). Standing fits at p
   iff crouch fits at p and at p + (0,0,28). So carve once with the crouch
   hull.
2. **Carve = clip.** For each BSP leaf, start from the leaf polytope, clip by
   each hull-expanded brush side. Keep the polygon and clip it (linear per
   cut); never rebuild vertices from plane triples. Emit only final cells;
   record nothing about intermediate fragments.
3. **Quantize once.** Snap each final cell's vertices to Q8 after the carve.
   No lattice feasibility test per cut (no libisl).
4. **Stance validity.** For each cell, intersect with the crouch free space
   translated down by 28 (the cells overlapping it shifted up, found by the
   spatial index). The intersection is standing-valid; the remainder is
   crouch-only. A cell that straddles is split by those translated planes,
   which are known planes, not a search.
5. **Portals by plane hash.** Hash every face by its quantized plane. Opposite
   faces on the same plane from different cells: clip the two polygons; a
   non-empty overlap is a portal. Witness per side: step from the overlap
   centre along the face normal into the cell (1, 2, 4, 8, 16, 32 units),
   snap to Q8, first inside point is primary, deepest is fallback, host
   validates the pose (SG_HostCollisionClassifyPose / SG_HostCollisionTransition).
6. **Cell witness.** Same stepping off any face of the cell. Bounds from the
   vertices. Leaf/area/cluster from the host leaf at the witness.
7. **Progress.** Report per leaf carved (whole-percent changes) and per
   phase begin, through the builder progress callback already wired to the
   server console (`rune: compact construction ...`). runegen runs the
   engine under `stdbuf -oL` so lines reach the log as they happen.

## Deletion list (delete first)

- `slipgate/sg_configuration_space.c` (4,300 lines: certificate carve,
  topology regions/portals, canonical all-triples clip, lattice tests)
- `slipgate/sg_configuration_lattice.c/.h` and the libisl dependency in
  GNUmakefile/Makefile (`RUNE_COMPACT_GENERATOR_CPPFLAGS/LIBS`, the
  pkg-config requirement)
- `slipgate/sg_configuration_audit.c/.h` (replays the carve as a verifier)
- `slipgate/sg_bsp_completeness_*` (six units; proof engine on the lattice)
- `tests/sg_configuration_space_test.c`, its script, the two lattice design
  notes, the ten run scripts that list these sources
- Header fields listed above under "not produced"; `sg_configuration_space.h`
  keeps only the contract structs
- `sg_configuration_semantics.c`: `BuildMesh` (use the cell's mesh),
  `CellQ8Bounds` via lattice (use vertex min/max), `BuildSolveInterior` if it
  is the lattice solver (use the stepped witness)
- `sg_rune_compact_mechanisms_build.c`: its lattice include (check what it
  uses; replace with float geometry or the cell witness)

## Related decisions recorded in the same session

- Hook kinds: the six hook capability kinds (bolt, body, pull, release,
  coast, relaunch) are one action's state machine enumerated as kinds. The
  tactic layer already collapses them to one hook capability plus the
  successor hook phase. Collapse them in the RUNE to one kind with the phase
  in the fiber state, as support and water already are. Wire change; do it
  after a real map generates.
- Response partition likely re-clips cell faces by visibility; unmeasured
  because no run has reached it.
- Only the patched Yamagi at ~/Games/Quake2/engines/yquake2 publishes the
  four sv_rune_* identity cvars; q2repro does not. See
  "Generating on this machine" in RUNE-BUILD-CHECKLIST.md.

## Scope ruling (2026-09-01, evening)

"Pull from era 3" means fragments, never whole units. Every era-3 unit the
generator or the bot runtime still runs on is on the deletion list, replaced
by an era-4 unit, in this order, each run on a real map before the next:
compact geometry, response, mechanisms, static, movement fields, weapon
fields, composer, wire, publication; then the runtime loader, localizer,
field service, and tactic units. Their tests go with them. The cell builder
and the regions are done and are the first era-4 units in the pipeline.

## Stage status (2026-09-01, late)

- cells: era 4 (`sg_configuration_cells.c`). bctf01: 113,616 cells, 679,582 directed portals, 19 s.
- regions: era 4 (`sg_configuration_semantics.c`). 0.2 s.
- compact geometry: era 4 (`sg_rune_compact_geometry.c`, builder adapter in `_builder.c`). 469,829 facets, 809,620 incidences, 0.3 s. `sg_rune_compact_geometry_partition.c` stays only until the response partition is replaced; it is era 3.
- response, mechanisms, static, movement, weapon, composer, wire, publication, runtime: era 3, next in that order.
- `tools/cellsdump MAP.bsp` runs cells, regions, and geometry on one map with counts and timing.
