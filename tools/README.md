# Tool reference

The game module generates and loads RUNEs. The repository uses shell and Make
for process orchestration. It does not require Python.

## Generate the corpus

`runegen.sh` starts isolated `q2ded` workers and invokes `sv rune` in the frozen
generator game module. The game module owns BSP parsing, field construction,
serialization, and publication. The script only owns server processes, staging
directories, the map schedule, resume state, inspection, cold load, and final
corpus assembly.

Use `rune-corpus-maps.txt` for a full run. It is the sole ordered authority for
the 175 release maps. `topmaps.txt` is an ordinary match schedule and has no
special acceptance meaning.

The full run uses 12 isolated workers. Each worker writes to its own staging
directory. Ordinary maps finish before the hard regression maps start. A
restarted run reuses an artifact only when the canonical C inspector accepts it
and a fresh server process loads it.

Generation and review have no elapsed-time limit. The script does not run route
repair, Dijkstra, proof catalogs, geometry reconstruction, or map-specific
acceptance plugins.

## Inspect a RUNE

`runecompactread.gnu` and `runecompactread.make` are two builds of the same thin
C command-line program. The program calls the production
`SG_RuneCompactWireInspect` implementation. The build dialects do not define
independent parser contracts.

The inspector performs the release-time linear checks for identity, format,
counts, spans, references, order, finite values, and checksums. A fresh server
load exercises the same production loader after inspection.

## Install and run

`deploy.sh`, `iterate.sh`, and `iterate2.sh` are local development launchers.
They do not certify a corpus. The release path installs only artifacts produced
from the unchanged frozen source commit and accepted by `runegen.sh`.

Human movement capture is implemented in the game module. Retained playthroughs
are development evidence and learning input. They cannot add geometry or
connectivity that the BSP and bound physics do not contain.
