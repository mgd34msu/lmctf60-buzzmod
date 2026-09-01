#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM

cd "$repo_dir"

fail()
{
	printf '%s\n' "human hook release regression: $*" >&2
	exit 1
}

sed -n '/^void Cmd_Unhook_f /,/^void Cmd_Ctfmenu_f /p' g_cmds.c \
	> "$tmp_dir/cmd-unhook.c"
sed -n '/^[[:space:]]*if (ent->client->pers.weapon == it)/,/^[[:space:]]*else/p' \
	"$tmp_dir/cmd-unhook.c" > "$tmp_dir/selected-hook.c"
sed -n '/^void Weapon_Hook (/,/^\/\/ END CTF CODE/p' p_weapon.c \
	> "$tmp_dir/weapon-hook.c"
sed -n '/^[[:space:]]*if ( !((ent->client->latched_buttons/,/^[[:space:]]*Weapon_Generic (ent/p' \
	"$tmp_dir/weapon-hook.c" > "$tmp_dir/release-block.c"
sed -n '/^static edict_t \*LMCTF_FireHumanHook/,/^edict_t \*fire_hook /p' \
	p_weapon.c > "$tmp_dir/human-fire.c"
sed -n '/^[[:space:]]*if (tr.fraction < 1.0)/,/^[[:space:]]*return bolt;/p' \
	"$tmp_dir/human-fire.c" > "$tmp_dir/immediate-touch.c"

selected_compact=$(sed '$d' "$tmp_dir/selected-hook.c" | tr -d '[:space:]')
expected_selected='if(ent->client->pers.weapon==it){ForceCommand(ent,"-attack\n");return;}'
test "$selected_compact" = "$expected_selected" ||
	fail 'selected-hook command must only send -attack and return'

release_compact=$(sed '$d' "$tmp_dir/release-block.c" | tr -d '[:space:]')
expected_release='if(!((ent->client->latched_buttons|ent->client->buttons)&BUTTON_ATTACK)){SG_HumanTraceHookRelease(ent);ctf_hook_abort(ent);}'
test "$release_compact" = "$expected_release" ||
	fail 'Weapon_Hook must release on the exact cleared-button transition'
grep -F -q 'Weapon_Generic (ent, 9, 13, 34, 38, pause_frames, fire_frames, Weapon_Hook_Fire);' \
	"$tmp_dir/weapon-hook.c" ||
	fail 'Weapon_Hook must retain the base generic-weapon cadence'

grep -F -q 'bolt->touch = hook_touch;' "$tmp_dir/human-fire.c" ||
	fail 'ordinary human bolt collision callback changed'
grep -F -q 'tr = gi.trace(self->s.origin, NULL, NULL, bolt->s.origin' \
	"$tmp_dir/human-fire.c" ||
	fail 'immediate launch obstruction trace changed'
immediate_compact=$(sed '$d' "$tmp_dir/immediate-touch.c" |
	sed '/\/\*/,/\*\//d' | tr -d '[:space:]')
expected_immediate='if(tr.fraction<1.0){VectorMA(bolt->s.origin,-10,dir,bolt->s.origin);bolt->touch(bolt,tr.ent,&tr.plane,NULL);}'
test "$immediate_compact" = "$expected_immediate" ||
	fail 'trace plane exception must stay inside the immediate-hit branch'
test "$(grep -F -c '&tr.plane' "$tmp_dir/human-fire.c")" -eq 1 ||
	fail 'trace plane exception escaped the immediate obstruction branch'
test "$(grep -F -c 'bolt->touch(bolt,' "$tmp_dir/human-fire.c")" -eq 1 ||
	fail 'direct touch escaped the immediate obstruction branch'

for cc in gcc clang
do
	"$cc" -std=c11 -Wall -Wextra -Werror -Wpedantic \
		tests/sg_human_hook_release_regression_test.c \
		-o "$tmp_dir/human-hook-release-$cc"
	"$tmp_dir/human-hook-release-$cc"
done

printf '%s\n' 'run_sg_human_hook_release_regression_test: ok'
