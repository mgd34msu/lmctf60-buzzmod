#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align -fno-fast-math -ffp-contract=off'
host_strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align -fno-fast-math -ffp-contract=off'

cd "$repo_dir"

extract()
{
	awk -v start="$2" -v finish="$3" '
		$0 ~ start { active = 1 }
		active { print }
		active && $0 ~ finish { exit }
	' "$1" > "$4"
}

extract_before()
{
	awk -v start="$2" -v finish="$3" '
		$0 ~ start { active = 1 }
		active && $0 ~ finish { exit }
		active { print }
	' "$1" > "$4"
}

assert_once()
{
	count=$(grep -F -c "$2" "$1" || true)
	test "$count" -eq 1
}

assert_before()
{
	left=$(grep -F -n -m 1 "$2" "$1" | cut -d: -f1)
	right=$(grep -F -n -m 1 "$3" "$1" | cut -d: -f1)
	test -n "$left" && test -n "$right" && test "$left" -lt "$right"
}

extract p_client.c '^void player_die ' '^\tint[[:space:]]+n;' "$tmp_dir/player-die.c"
assert_once "$tmp_dir/player-die.c" 'SG_CancelCurrentBotTacticLife(self);'
assert_before "$tmp_dir/player-die.c" 'SG_CancelCurrentBotTacticLife(self);' \
	'POVLock_HandleRespawnTerminal'

extract p_client.c '^void PutClientInServer ' '^void ClientBeginDeathmatch ' \
	"$tmp_dir/put-client.c"
assert_once "$tmp_dir/put-client.c" 'SG_CancelCurrentBotTacticLife(ent);'
assert_before "$tmp_dir/put-client.c" 'SG_CancelCurrentBotTacticLife(ent);' \
	'memset (client, 0'
assert_before "$tmp_dir/put-client.c" 'SG_CancelCurrentBotTacticLife(ent);' \
	'client->ctf.ctfid = unique_id++'

extract p_client.c '^void ClientDisconnect ' '^edict_t[[:space:]]+[*]pm_passent;' \
	"$tmp_dir/disconnect.c"
assert_once "$tmp_dir/disconnect.c" 'SG_CancelCurrentBotTacticLife(ent);'
assert_before "$tmp_dir/disconnect.c" 'SG_CancelCurrentBotTacticLife(ent);' \
	'ClientHasFlag(ent)'
assert_before "$tmp_dir/disconnect.c" 'SG_CancelCurrentBotTacticLife(ent);' \
	'G_FreeEdict (dead_hook)'

extract slipgate/sg_client.c '^static void BotSlot_Reset' \
	'^static const char [*]sg_names' "$tmp_dir/bot-slot.c"
assert_once "$tmp_dir/bot-slot.c" 'SG_CancelBotSlotTacticLife(bot);'
assert_before "$tmp_dir/bot-slot.c" 'SG_CancelBotSlotTacticLife(bot);' \
	'SG_StrategyCallerDestroy'
assert_before "$tmp_dir/bot-slot.c" 'SG_CancelBotSlotTacticLife(bot);' \
	'memset(bot, 0'

extract slipgate/sg_arach.c '^static qboolean Think_Dead' \
	'^static qboolean Think_RespawnEdge' "$tmp_dir/think-dead.c"
grep -F -q 'SG_TacticExecutionOwnerCancelSubject' "$tmp_dir/think-dead.c"
extract slipgate/sg_arach.c '^void SG_CompactProductionStorageWillFree' \
	'^void SG_LevelChange' "$tmp_dir/storage-free.c"
grep -F -q 'SG_TacticExecutionOwnerCancelSubject' "$tmp_dir/storage-free.c"
extract slipgate/sg_arach.c '^void SG_LevelChange' '^static void' \
	"$tmp_dir/level-change.c"
grep -F -q 'SG_TacticExecutionOwnerCancelAll' "$tmp_dir/level-change.c"
extract slipgate/sg_arach.c '^void SG_RunFrame' '^void SG_CompactProductionStorageWillFree' \
	"$tmp_dir/run-frame.c"
grep -F -q 'SG_TacticExecutionOwnerLost' "$tmp_dir/run-frame.c"
extract_before slipgate/sg_arach.c '^static void CancelTacticSubject' \
	'^static void Bot_ResetLifeActions' \
	"$tmp_dir/sg_tactic_host_lifecycle_implementation.inc"

for cc in gcc clang
do
	$cc $strict -ffunction-sections -fdata-sections -I. -c \
		tests/sg_tactic_host_lifecycle_test.c -o "$tmp_dir/test-$cc.o"
	$cc $host_strict -ffunction-sections -fdata-sections -I. -c \
		slipgate/sg_arach.c -o "$tmp_dir/arach-$cc.o"
	$cc "$tmp_dir/test-$cc.o" "$tmp_dir/arach-$cc.o" \
		-Wl,--gc-sections -lm -o "$tmp_dir/host-lifecycle-$cc"
	"$tmp_dir/host-lifecycle-$cc"
done

clang $strict -ffunction-sections -fdata-sections -fno-omit-frame-pointer \
	-fsanitize=address,undefined -I. -c \
	tests/sg_tactic_host_lifecycle_test.c -o "$tmp_dir/test-sanitized.o"
clang $host_strict -ffunction-sections -fdata-sections -fno-omit-frame-pointer \
	-fsanitize=address,undefined -I. -c slipgate/sg_arach.c \
	-o "$tmp_dir/arach-sanitized.o"
clang "$tmp_dir/test-sanitized.o" "$tmp_dir/arach-sanitized.o" \
	-fsanitize=address,undefined -Wl,--gc-sections -lm \
	-o "$tmp_dir/host-lifecycle-sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	"$tmp_dir/host-lifecycle-sanitized"

scan-build --status-bugs -o "$tmp_dir/scan-build" \
	clang $strict -DSG_TACTIC_HOST_LIFECYCLE_IMPLEMENTATION \
	-I"$tmp_dir" -I. tests/sg_tactic_host_lifecycle_test.c -lm \
	-o "$tmp_dir/host-lifecycle-static"

tests/run_sg_tactic_execution_owner_test.sh

echo "tactic host lifecycle suite passed"
