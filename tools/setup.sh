#!/usr/bin/env bash
# setup.sh -- the development environment doctor and bootstrapper.
#
# The bar this script serves: someone who has never spoken to us clones
# the repo, runs this, follows what it prints, and ends up with the
# same dev experience we have -- fleet and instruments. It CHECKS
# everything and FIXES what is safe to fix automatically;
# for everything else it prints exactly what is missing and how to
# supply it. Run it as many times as you like; it is idempotent.
#
# See tools/README.md for what each tool does and TOOLING.md for the
# environment's law.

set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(dirname "$HERE")"
OK=0; MISS=0
say()  { printf '  %s\n' "$*"; }
pass() { printf '  [ok]   %s\n' "$*"; OK=$((OK+1)); }
fail() { printf '  [MISS] %s\n' "$*"; MISS=$((MISS+1)); }

echo "== SLIPGATE dev environment doctor =="

# 1. Toolchain
command -v gcc >/dev/null    && pass "gcc" || fail "gcc -- install your distro's C toolchain"
command -v make >/dev/null   && pass "make" || fail "make"
command -v git >/dev/null    && pass "git" || fail "git"

# 2. The engine (yquake2 dedicated server)
Q2DED="${Q2DED:-$HOME/Games/Quake2/engines/yquake2/release/q2ded}"
if [ -x "$Q2DED" ]; then
    pass "q2ded at $Q2DED"
else
    fail "q2ded not found at $Q2DED -- build yquake2 (github.com/yquake2/yquake2) and either place it there or export Q2DED=/path/to/q2ded"
fi

GAMEROOT="${Q2ROOT:-$HOME/Games/Quake2}"

# 2b. Quake 2 base data (retail -- we cannot ship it)
if ls "$GAMEROOT"/baseq2/pak0.pak >/dev/null 2>&1 || ls "${Q2ROOT:-$HOME/Games/Quake2}"/baseq2/pak*.pak >/dev/null 2>&1; then
    pass "baseq2 retail data"
else
    fail "baseq2/pak0.pak not found under $GAMEROOT -- Quake 2 retail data required (Steam/GOG/CD); copy baseq2/ into your Q2 root"
fi

# 3. The game directory the fleet runs in
GAMEDIR="$GAMEROOT/lmctf-hooktest"
if [ -d "$GAMEDIR" ]; then
    pass "fleet gamedir $GAMEDIR"
    if ls "$GAMEDIR"/*.pak >/dev/null 2>&1; then
        pass "mod paks present ($(ls "$GAMEDIR"/*.pak | wc -l))"
    else
        fail "no paks in $GAMEDIR -- copy assets/lmctf6-buzzmod.pak in, plus the original LMCTF 6.0 asset paks (community mirrors)"
    fi
    [ -d "$GAMEDIR/maps" ] && pass "maps/ present" \
        || fail "maps/: the fleet's map rotation needs the map .bsp files (retail/community paks -- not distributable with this repo). Copy your LMCTF map set into $GAMEDIR/maps or a pak."
else
    fail "fleet gamedir missing -- mkdir -p $GAMEDIR, then: copy assets/lmctf6-buzzmod.pak in, add your map files, and run 'make' + tools/deploy.sh to install the game module"
fi

# 4. Demo directories (recordings land here)
BOTDEMOS="$HOME/.local/share/YamagiQ2/lmctf-hooktest/demos"
[ -d "$BOTDEMOS" ] && pass "bot demo dir $BOTDEMOS" \
    || say "  note: $BOTDEMOS appears after the first recorded game (engine-created)"
HUMDEMOS="$GAMEDIR/demos"
if [ -d "$HUMDEMOS" ] && ls "$HUMDEMOS"/*.dm2 >/dev/null 2>&1; then
    pass "human corpus present ($(ls "$HUMDEMOS"/*.dm2 | wc -l) demos)"
else
    say "  note: no human demo corpus at $HUMDEMOS -- the fleet and all"
    say "  instruments run without it, but blind judging needs human demos."
    say "  Supply your own client-recorded .dm2 games under that directory"
    say "  (see corpus-manifest.csv for the format)."
fi

# 5. The watchdog (optional but recommended)
if systemctl --user is-enabled wavewatch.timer >/dev/null 2>&1; then
    pass "wavewatch systemd timer enabled"
else
    say "  optional: install the fleet watchdog --"
    say "    cp tools/systemd/wavewatch.* ~/.config/systemd/user/"
    say "    systemctl --user daemon-reload && systemctl --user enable --now wavewatch.timer"
fi

# 6. Build check
if [ -f "$REPO/GNUmakefile" ]; then
    pass "game module buildable: cd $(basename "$REPO") && make -j\$(nproc)"
fi

echo
echo "== $OK ok, $MISS missing =="
echo "Start the fleet:   cd tools && ./waveloop.sh 1     (stop: touch tools/waveloop-stop)"
echo "One-off wave:      cd tools && ./iterate2.sh 1"
echo "Read the law:      TOOLING.md; tool reference: tools/README.md"
exit $((MISS > 0))
