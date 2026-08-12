#!/usr/bin/env bash
# setup.sh -- the development environment doctor and bootstrapper.
#
# The bar this script serves: someone who has never spoken to us clones
# the repo, runs this, follows what it prints, and ends up with the
# same dev experience we have -- fleet, film, instruments. It CHECKS
# everything and FIXES what is safe to fix automatically (the venv);
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
command -v python3 >/dev/null && pass "python3" || fail "python3 (3.10+)"

# 2. The film venv (auto-created if absent)
VENV="${SLIPGATE_VENV:-$HOME/.venvs/slipgate-film}"
if [ -x "$VENV/bin/python" ]; then
    pass "film venv at $VENV"
else
    say "creating film venv at $VENV ..."
    python3 -m venv "$VENV" && "$VENV/bin/pip" install -q -r "$HERE/requirements.txt" \
        && pass "film venv created" || fail "venv creation failed -- create manually: python3 -m venv $VENV && $VENV/bin/pip install -r tools/requirements.txt"
fi

# 3. The engine (yquake2 dedicated server)
Q2DED="${Q2DED:-$HOME/Games/Quake2/engines/yquake2/release/q2ded}"
if [ -x "$Q2DED" ]; then
    pass "q2ded at $Q2DED"
else
    fail "q2ded not found at $Q2DED -- build yquake2 (github.com/yquake2/yquake2) and either place it there or export Q2DED=/path/to/q2ded"
fi

# 4. The game directory the fleet runs in
GAMEROOT="${Q2ROOT:-$HOME/Games/Quake2}"
GAMEDIR="$GAMEROOT/lmctf-hooktest"
if [ -d "$GAMEDIR" ]; then
    pass "fleet gamedir $GAMEDIR"
    [ -d "$GAMEDIR/maps" ] && pass "maps/ present" \
        || fail "maps/: the fleet's map rotation needs the map .bsp files (retail/community paks -- not distributable with this repo). Copy your LMCTF map set into $GAMEDIR/maps or a pak."
else
    fail "fleet gamedir missing -- mkdir -p $GAMEDIR, then: copy assets/lmctf6-buzzmod.pak in, add your map files, and run 'make' + tools/deploy.sh to install the game module"
fi

# 5. Demo directories (film lands here)
BOTDEMOS="$HOME/.local/share/YamagiQ2/lmctf-hooktest/demos"
[ -d "$BOTDEMOS" ] && pass "bot film dir $BOTDEMOS" \
    || say "  note: $BOTDEMOS appears after the first recorded game (engine-created)"
HUMDEMOS="$GAMEDIR/demos"
if [ -d "$HUMDEMOS" ] && ls "$HUMDEMOS"/*.dm2 >/dev/null 2>&1; then
    pass "human corpus present ($(ls "$HUMDEMOS"/*.dm2 | wc -l) demos)"
else
    say "  note: no human demo corpus at $HUMDEMOS -- the fleet and all"
    say "  instruments run without it, but blind judging needs human film."
    say "  Supply your own client-recorded .dm2 games and index them with"
    say "  the venv python: tools/ (see corpus-manifest.csv for the format)."
fi

# 6. The watchdog (optional but recommended)
if systemctl --user is-enabled wavewatch.timer >/dev/null 2>&1; then
    pass "wavewatch systemd timer enabled"
else
    say "  optional: install the fleet watchdog --"
    say "    cp tools/systemd/wavewatch.* ~/.config/systemd/user/"
    say "    systemctl --user daemon-reload && systemctl --user enable --now wavewatch.timer"
fi

# 7. Build check
if [ -f "$REPO/GNUmakefile" ]; then
    pass "game module buildable: cd $(basename "$REPO") && make -j\$(nproc)"
fi

echo
echo "== $OK ok, $MISS missing =="
echo "Start the fleet:   cd tools && ./waveloop.sh 1     (stop: touch tools/waveloop-stop)"
echo "One-off wave:      cd tools && ./iterate2.sh 1"
echo "Read the law:      TOOLING.md; tool reference: tools/README.md"
exit $((MISS > 0))
