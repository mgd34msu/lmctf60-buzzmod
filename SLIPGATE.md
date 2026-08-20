# SLIPGATE

SLIPGATE is LMCTF's native bot system. Navigation, combat, perception, team
logic, client presentation, and RUNE loading are built into the game module.
The project does not use the removed Q2/Q3 bot library.

## Runtime model

- **Navigation:** Each map uses a physics-proved RUNE graph. Links cover ground
  movement, jumps, drops, swimming, grapple rides, lifts, teleports, and
  declared mechanisms. Live cost fields combine objectives, items, danger,
  cover, and teammate support.
- **Movement:** Route descent emits normal `usercmd_t` input. Collision feelers,
  edge guards, braking, action ownership, and fallback rules keep movement
  inside proved runtime contracts.
- **Combat:** Bots use reaction delay, skill-scaled aim error, weapon range
  policy, switch discipline, fire windows, and splash-safety checks. Damage and
  kills still pass through the normal game code.
- **Perception:** Enemy state comes from visible, audible, damage, chat, and
  public objective events. Beliefs age. Decision code does not receive hidden
  opponent state.
- **Team play:** Bots take attack, defend, carry, recover, and escort roles from
  shared public state. Defenders cover approaches, attackers pressure the enemy
  stand, and escorts support a live carrier.
- **Presentation:** Bots are normal visible clients with stable identities,
  personas, chat, synthetic ping, and ordinary statistics.

## Source layout

The controller lives under `slipgate/`:

- `sg_client.c` owns fake-client lifecycle;
- `sg_caco.c` owns perception and learned state;
- `sg_arach.c` and `sg_strike*.c` own team and attack policy;
- `sg_fields.c`, `sg_goal.c`, and `sg_price.c` own route costs;
- `sg_descend.c` and `sg_move.c` own route selection and actuation;
- `sg_combat.c` owns weapon, aim, and fire decisions;
- `sg_net.c`, `sg_chat.c`, identity, and persona modules own presentation;
- `sg_rune*.c` and mechanism modules own graph generation, loading, proof, and
  runtime action contracts.

Host code remains authoritative for flag touches, captures, damage, death,
weapons, map transitions, logging, and persistent statistics. SLIPGATE chooses
client commands and consumes named host events. It does not synthesize outcomes.

## Current work

The source implements the complete controller path, 181-map RUNE authority,
ordered native map rotation, and all ten required `lmctf58` declared-door
controllers. Open work is direct bot quality, final 181-map generation,
persistent fleet integration, transactional bundle integration, production
acceptance, and release publication.

Bot changes must modify the production controller, receive an executable policy
or live-path test, and survive observed play. Reports and matched evidence help
decide whether to keep a change; they are not substitutes for implementation.

[`PROJECT-COMPLETION-PLAN.md`](PROJECT-COMPLETION-PLAN.md) is the current work
order and completion authority. [`ARCHITECTURE.md`](ARCHITECTURE.md) documents
the runtime boundaries in more detail.
