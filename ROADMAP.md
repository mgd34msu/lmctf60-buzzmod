# Roadmap

In order.

1. Carrier survival. The bots steal at the players' rate but convert one
   steal in five; Zest converts nearly one in two. Escorts should screen
   the carrier's line instead of trailing it, routes home should avoid the
   defenders' known lines, and a carrier should have armor before the run.
2. More of the players per map. Beyond bites, record where the good players
   go and where they fight, from the same demos and from play, and let the
   field favour those cells and weapon choice follow the area.
3. The rope from the air. The players fire 60% of their ropes while
   airborne; the bots fire mostly from the floor. Choose and fire rides
   mid-flight, and hold the rope rate at the players' rhythm on every map.
   On smap26 it ran to 54 a minute.
4. The last lava deaths. On maps like bctf01 some rides pass over lava.
   Reject ride records whose arc crosses hazard, and pick rescue anchors by
   the drop they leave.
5. Turtle and posts, watched live. Confirm the turtle goal in a live game
   now that captures are tallied. Move defender posts that sit on ledges
   with no room to move to the nearest floor with the same cover.
6. A regression gate. After every game, produce the same comparison table
   against Zest, Lequin and the owner per map and keep it with the logs.
7. Aim and skill. The bots rail humans hard. Tune sg_skill and the personas
   against the players' accuracy from the demos.
8. Every map validated. All 181 maps have routes. Run a pass per map for
   unreachable samples, stuck spots and mechanisms the bots cannot work.
9. Windows on a real server. The modules cross-build and stand alone but
   have not run on Windows yet.
10. Presentation. Chatter by persona, callouts trimmed to what a teammate
    wants to hear, ping and stats as ordinary clients.
