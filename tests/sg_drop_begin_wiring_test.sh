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
	if (failed)
		exit 1
	print "sg_drop_begin_wiring_test: ok"
}
' "$source_file"
