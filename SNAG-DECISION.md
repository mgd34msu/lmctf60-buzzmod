# SNAG decision

Delete the current SNAG pipeline. Do not migrate its format.

## Evidence

The runtime reader in `slipgate/sg_snag_repair.c` binds each repair to a
legacy RUNE seed index and origin. It splits one surcharge between
`sg_snag_seed[]` and every outgoing legacy link in `sg_snag_link[]`.
`slipgate/sg_fields.c` is the only production consumer of those arrays.
`Field_FloodRun` adds the surcharges, and `Fields_Setup` requires a `.snag`
load before accepting the graph. The format is bound to the superseded seed,
link, and action model.

`tools/stallcensus.py` and `tools/snagrepair.py` produce repairs from legacy
route-stall telemetry and seed indices. `tools/snag_corpus.py` builds a separate
SNAG corpus. The corpus controller also manufactures an explicit zero-repair
SNAG before cold load. The remaining bundle, installation, finalization,
preflight, and fleet code transports or verifies those files. No producer emits
a v2 cell or capability-kernel cost annotation, and no v2 destination-field
consumer reads one.

The current format is bounded preference data at runtime, but it has no
necessary consumer in the new model. Keeping the name while replacing seed and
link ownership would hide a new format behind an obsolete contract.

## Integration work

Delete the runtime implementation and tests:

- `slipgate/sg_snag_repair.c` and `slipgate/sg_snag_repair.h`
- the `SG_SnagRepairSeedSurcharge` and `SG_SnagRepairLinkSurcharge` additions in
  `slipgate/sg_fields.c`
- the mandatory load or clear branch in `Fields_Setup`
- `tests/sg_snag_repair_test.c`, `tests/test_snagrepair.py`, and the
  `snag-repair-test` build targets and source entries

Delete the legacy producers and corpus:

- `tools/snagrepair.py` and `tools/snag_corpus.py`
- SNAG bootstrap, attestation, and cold-load records in
  `tools/rune_corpus_controller.py`
- `.snag` staging, journaling, recovery, and receipt fields in
  `tools/runegen.sh` and `tools/runegen_pair.py`

Remove the `snag_file` and `snag:<map>` roles from
`tools/server_bundle.py`, `tools/rune_pair_preflight.py`,
`tools/rune_corpus_finalizer.py`, `tools/fleet-runner.py`, and
`tools/fleet_runner_live.py`. Update their focused tests at the same time. Also
remove obsolete route-only SNAG expectations from
`tests/test_route_only_evidence.py`.

If measured runtime learning later needs a cost sidecar, introduce it as a new
v2 sidecar only after a destination-field consumer exists. Key entries by
stable cell or capability-kernel identity, require
`SG_RUNE_V2_SIDECAR_COST_ONLY`, and reject geometry or connectivity changes.
That would be a new contract, not retained SNAG.
