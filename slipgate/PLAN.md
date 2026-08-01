# SLIPGATE — dependency graph and execution state

Every open item, its dependencies, its lane. Nodes carry the numbering from
the session inventory. Status: [D]=done, [R]=running now, [A]=agent building,
[B]=blocked on arrow, [Q]=queued, [?]=needs owner ruling.

```mermaid
graph TD
    subgraph GENERATOR["RUNE generator (sg_rune.c — main line)"
        ]
        G0["gravity in oracle phantoms [D]<br/>(the levitation bug: pms.gravity=0)"]
        N1["1 drop links [D] (6977 in 50k rune)"]
        N2["2 hook climb anchors [D] (11850)"]
        N3["3 swim volume seeds [D-code] (SWIM=0 on lmctf03 — verify wet map)"]
        N4["4 lift links (func_plat) [D]"]
        N5["5 teleporter links [D]"]
        N6["6 south-strip one-way pairs [D] (dissolved by rebuild: frontier 2)"]
        N8["8 entry envelopes populated [D]"]
    end

    subgraph COVERAGE["The gate"]
        N7["7 coverage >90% both flags [D]<br/>99.8% both (1533/1536), in-engine"]
    end

    subgraph BODY["ARACHNOTRON (sg_arach.c + sg_combat.c)"]
        N10["10 movement policy port [D] (strafe live 82/1204)"]
        N11["11 hook execution verified [D-fires]<br/>66 fires; catch/convert rate pending;<br/>Rune oscillates anchors y≈-1790"]
        N12["12 drop-lip execution verified [B]"]
        N13["13 combat module [D]"]
        N14["14 precision braking [D] (in 10)"]
        N15["15 weight sweeps [Q]"]
        N16["16 per-item detour fields [Q]"]
        N17["17 escort + recover roles [Q]"]
    end

    subgraph EYE["CACO (sg_caco.c)"]
        N18["18 team callouts [D]"]
        N19["19 human sightings feed belief [D]"]
        N20["20 staleness advection [D]<br/>full reachable-set projection [D]"]
        N21["21 rune/quad state via belief [D]<br/>(wired into sg_fields.c, r190)"]
    end

    subgraph SYSTEM["System"]
        N22["22 FIRST STEAL / CAPTURE [B]"]
        N23["23 A/B vs legacy 289/5.3 [B]<br/>(script [A]; RUNS blocked on freeze)"]
        N24["24 multi-map validation [B]"]
        N25["25 batch rune generation [A]"]
        N26["26 learning (costs, links, danger, weights) [Q]"]
        N27["27 commit everything since a1821db [B]"]
        FREEZE["integration freeze [D]:<br/>all agents merged, r190~a1821db built,<br/>deployed md5 8626935…; verification match pending"]
        TOOL["runeview tool [D]"]
    end

    subgraph MAIN["main-branch loose ends"]
        N28["28 toss flag NULL [?] owner ruling — LMCTF change"]
        N29["29 lmctf01 bspc sink [Q]"]
        N30["30 legacy blocked-fire 37-43% [Q]"]
    end

    G0 --> N1
    G0 --> N8
    N1 --> N7
    N2 --> N7
    N3 --> N7
    N4 --> N7
    N5 --> N7
    N6 -. minor .-> N7
    N7 --> N22
    N10 --> FREEZE
    N13 --> FREEZE
    N18 --> FREEZE
    N19 --> FREEZE
    N20 --> FREEZE
    N3 --> FREEZE
    N4 --> FREEZE
    N5 --> FREEZE
    FREEZE --> N11
    FREEZE --> N12
    FREEZE --> N27
    N7 --> N23
    FREEZE --> N23
    N22 --> N23
    N23 --> N15
    N15 --> N24
    N25 --> N24
    N7 --> N24
    N24 --> N26
    N8 --> N26
    N21 --> N15
    N16 --> N15
    N17 --> N15
    TOOL -. verifies .-> N7
```

## Reading the graph

- **G0 (phantom gravity) is the root of the generator subtree.** pms.gravity
  was 0 from memset; every drop proof levitated, jumps flew away, four prover
  designs failed on top of one uninitialized short. Fix is one line, in the
  pipeline running now.
- **N7 (coverage) is the single gate to N22 (first steal)** — the project's
  definition of working. Nothing downstream of the freeze unblocks steals;
  only coverage does. Drops (N1) + hooks (N2) are expected to carry it;
  swim/lifts/teleporters (N3-5) mop up.
- **FREEZE**: agents finish → merge their tree state → one build → one md5
  deploy → verification match. A/B runs (N23) require BOTH coverage and the
  freeze — measuring a body under active rewrite is not a measurement.
- **Learning (N26) is last** by design: it adjusts costs and weights that
  must first exist and first be measured.

## Lane assignments right now

| lane | items | state |
|---|---|---|
| main line (this session) | verification match on r190, N22 diagnosis, N27 commit | match running (port 28431) |
| agents | N3-5, N10, N13-14, N18-21, scripts, runeview | ALL DONE, merged, built as r190 |
| blocked on server free | N23 A/B runs, N24/25 batch generation | scripts dry-run verified |
| owner | N28 ruling | waiting |

## Known defects on the board

- Rune[SG] hook-fires the same 4-5 anchors on y≈-1790 in a loop (fire →
  release → re-select). Not converting traversal into progress; closest
  flag approach in match so far ~2100u.
- SWIM links 0 on lmctf03 — water seeding unexercised; needs a map with
  real water volumes to verify (N24 batch will surface one).
- bspc rs_maxfallheight parser row lives outside the repo
  (Downloads tree aas_cfg.c:83) — needs vendoring or it dies with a
  cache clean.
```
