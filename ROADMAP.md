# Roadmap

In order.

1. Carrier survival. The bots convert about one steal in five into a
   capture. Escorts should screen
   the carrier's line instead of trailing it, routes home should avoid the
   defenders' known lines, and a carrier should have armor before the run.
2. More route hints per map. Beyond hook anchors, record travelled cells and
   fight positions from demos and from play, and let the field favour those
   cells and weapon choice follow the area.
3. The rope from the air. The bots fire mostly from the floor. Choose and
   fire rides mid-flight, and hold the rope rate steady on every map.
4. The last lava deaths. On maps like bctf01 some rides pass over lava.
   Reject ride records whose arc crosses hazard, and pick rescue anchors by
   the drop they leave.
5. Turtle and posts, watched live. Confirm the turtle goal in a live game
   now that captures are tallied. Move defender posts that sit on ledges
   with no room to move to the nearest floor with the same cover.
6. A regression gate. After every game, produce the same movement table per
   map and keep it with the logs.
7. Aim and skill. The bots' railgun accuracy is high. Tune sg_skill and the
   personas against measured human accuracy.
8. Every map validated. All 181 maps have routes. Run a pass per map for
   unreachable samples, stuck spots and mechanisms the bots cannot work.
9. Windows on a real server. The modules cross-build and stand alone but
   have not run on Windows yet.
10. Presentation. Chatter by persona, callouts trimmed to what a teammate
    wants to hear, ping and stats as ordinary clients.
