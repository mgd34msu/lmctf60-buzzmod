#!/bin/sh
set -eu

source_file=${1:-slipgate/sg_move.c}

awk '
function reject(message) {
	print "sg_drop_begin_wiring_test: " message > "/dev/stderr"
	failed = 1
}

/^void Think_Move\(sg_bot_t \*bot, sg_think_t \*tc\)/ {
	in_move = 1
}
/^void Think_Emit\(sg_bot_t \*bot, sg_think_t \*tc\)/ {
	in_move = 0
	in_emit = 1
}

in_move && /tc->think_over = true;/ {
	move_rejections++
}

in_emit && /if \(tc->think_over\)/ {
	guard_seen++
	in_guard = 1
	next
}

in_emit && /if \(!SG_CompoundDropGameIdleAdmission\(bot\)\)/ {
	compound_idle_guards++
	in_compound_idle_guard = 1
	next
}

in_compound_idle_guard {
	if (/SG_CompoundDropLiveBegin|ClientThink/)
		reject("D_DROP idle-admission rejection reaches Begin or ClientThink")
	if (/bot->commit_link = -1;/)
		compound_commit_clear++
	if (/bot->sticky_link = -1;/)
		compound_sticky_clear++
	if (/return;/) {
		compound_idle_returns++
		in_compound_idle_guard = 0
	}
}

in_guard {
	if (/ClientThink|Drop_LiveZeroFrame|SG_DropLiveZeroCommand/)
		reject("Begin-rejection guard submits a host command")
	if (/return;/) {
		direct_return++
		in_guard = 0
	}
}

/Drop_LiveZeroFrame/ {
	zero_frame_symbols++
}

END {
	if (move_rejections != 1)
		reject("Think_Move must have one Begin-rejection disposition")
	if (guard_seen != 1 || direct_return != 1)
		reject("Think_Emit must directly return on that disposition")
	if (zero_frame_symbols != 0)
		reject("obsolete zero-ClientThink Begin path remains")
	if (compound_idle_guards != 1 || compound_idle_returns != 1 ||
	    compound_commit_clear != 1 || compound_sticky_clear != 1)
		reject("D_DROP idle rejection must retire its candidate and return")
	if (failed)
		exit 1
	print "sg_drop_begin_wiring_test: ok"
}
' "$source_file"
