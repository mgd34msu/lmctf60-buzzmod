#!/bin/sh
# Source-authenticate every private acceptance seam.  This host-free gate
# catches a shared overlay that preserves only the convenient action hook.
set -eu

move=${1:-slipgate/sg_move.c}
descend=${2:-slipgate/sg_descend.c}
header=${3:-slipgate/sg_accept_drop.h}

fail()
{
	printf '%s\n' "sg_accept_drop_authority_seam_test: $*" >&2
	exit 1
}

line_of()
{
	grep -n -F "$2" "$1" | head -1 | cut -d: -f1
}

require_call()
{
	name=$1
	file=$2
	grep -F "$name(" "$file" >/dev/null ||
		fail "missing runtime seam $name in $file"
}

inventory_names='SG_AcceptDropActionBegin
SG_AcceptDropAfterBot
SG_AcceptDropAfterStep
SG_AcceptDropArm
SG_AcceptDropBoundary
SG_AcceptDropCallback
SG_AcceptDropCommand
SG_AcceptDropCommandHistorical
SG_AcceptDropEntityPass
SG_AcceptDropFrameBegin
SG_AcceptDropFrameEvent
SG_AcceptDropGenericHandoffBegin
SG_AcceptDropGenericHandoffEnd
SG_AcceptDropLegacyAuthority
SG_AcceptDropLegacyObserverBoundary
SG_AcceptDropLevelReset
SG_AcceptDropObserverBeginCommand
SG_AcceptDropObserverEventOwner
SG_AcceptDropObserverTakeEvents
SG_AcceptDropOwnsStep
SG_AcceptDropPose
SG_AcceptDropPredicate
SG_AcceptDropPredicateResult
SG_AcceptDropPusher
SG_AcceptDropQueue
SG_AcceptDropShelf
SG_AcceptDropStopBeforeEmit
SG_AcceptDropTeach
SG_AcceptDropTrace'

for name in $(grep -o 'SG_AcceptDrop[A-Za-z0-9_]*' "$header" | sort -u)
do
	printf '%s\n' "$inventory_names" | grep -Fx "$name" >/dev/null ||
		fail "unmapped public hook declaration $name"
done
for name in $inventory_names
do
	grep -F "$name" "$header" >/dev/null ||
		fail "inventory names a non-public hook $name"
done

# Every declared hook has a concrete runtime call site (FrameEvent is called
# internally by the two public frame hooks).
require_call SG_AcceptDropQueue g_svcmds.c
require_call SG_AcceptDropLevelReset slipgate/sg_arach.c
require_call SG_AcceptDropFrameBegin slipgate/sg_arach.c
require_call SG_AcceptDropEntityPass g_main.c
require_call SG_AcceptDropArm slipgate/sg_arach.c
require_call SG_AcceptDropPusher g_phys.c
require_call SG_AcceptDropAfterBot slipgate/sg_arach.c
require_call SG_AcceptDropLegacyAuthority "$move"
require_call SG_AcceptDropActionBegin "$move"
require_call SG_AcceptDropOwnsStep "$move"
require_call SG_AcceptDropObserverEventOwner "$move"
require_call SG_AcceptDropObserverBeginCommand "$move"
require_call SG_AcceptDropObserverTakeEvents "$move"
require_call SG_AcceptDropCommandHistorical "$move"
require_call SG_AcceptDropCommand "$move"
require_call SG_AcceptDropPose "$move"
require_call SG_AcceptDropAfterStep "$move"
require_call SG_AcceptDropGenericHandoffBegin "$move"
require_call SG_AcceptDropGenericHandoffEnd "$move"
require_call SG_AcceptDropBoundary "$descend"
require_call SG_AcceptDropCallback "$descend"
require_call SG_AcceptDropPredicate "$descend"
require_call SG_AcceptDropPredicateResult "$descend"
require_call SG_AcceptDropTrace "$descend"
require_call SG_AcceptDropLegacyObserverBoundary "$descend"
require_call SG_AcceptDropStopBeforeEmit "$descend"
require_call SG_AcceptDropShelf "$move"
require_call SG_AcceptDropShelf "$descend"
require_call SG_AcceptDropTeach "$move"
require_call SG_AcceptDropTeach "$descend"
test "$(grep -F 'SG_AcceptDropFrameEvent(' slipgate/sg_accept_drop.c | wc -l | tr -d ' ')" -ge 3 ||
	fail 'frame-event does not have both private frame callers'

# Shared source preflight precedes A/B.  Clean A starts legacy before Begin;
# B reaches Begin before rev2 ActionBegin.  Rejection's Think_Emit guard is
# before its first ClientThink, so contaminated/query-failed starts are quiet.
query=$(line_of "$move" 'SG_OracleReplaySourceEvents')
authority=$(line_of "$move" 'SG_AcceptDropLegacyAuthority(bot, bestlink)')
begin=$(line_of "$move" 'SG_DropLiveBegin')
legacy=$(line_of "$move" 'SG_AcceptDropActionBegin(bot, bestlink, "legacy")')
rev2=$(line_of "$move" 'SG_AcceptDropActionBegin(bot, bestlink, "rev2")')
reject=$(line_of "$move" 'Drop_LiveRetireNonRunning(e, bot, bestlink, "begin"')
think_over=$(line_of "$move" 'tc->think_over = true;')
emit_guard=$(line_of "$move" 'if (tc->think_over)')
first_emit_clientthink=$(awk -v start="$emit_guard" \
	'NR > start && /ClientThink\(e,/ { print NR; exit }' "$move")

test -n "$query" && test -n "$authority" && test -n "$begin" ||
	fail 'missing source-preflight/authority/Begin seam'
test -n "$legacy" && test -n "$rev2" && test -n "$reject" ||
	fail 'missing A/B action or rejection seam'
test -n "$think_over" && test -n "$emit_guard" && test -n "$first_emit_clientthink" ||
	fail 'missing no-ClientThink rejection guard'
test "$query" -lt "$authority"
test "$authority" -lt "$legacy"
test "$legacy" -lt "$begin"
test "$begin" -lt "$rev2"
test "$begin" -lt "$reject"
test "$reject" -lt "$think_over"
test "$think_over" -lt "$emit_guard"
test "$emit_guard" -lt "$first_emit_clientthink"
grep -F 'SG_AcceptDropLegacyAuthority(bot, bestlink) &&' "$move" >/dev/null
grep -F '!live_events.contaminated' "$move" >/dev/null

# A's feed has production's clear-before-observe ordering and leaves command
# four until the deferred boundary, where pusher/trigger/solid evidence folds
# into that one observer snapshot.
begin_events=$(line_of "$move" 'SG_AcceptDropObserverBeginCommand(bot, bestlink,')
take_events=$(line_of "$move" 'SG_AcceptDropObserverTakeEvents(bot, bestlink, step,')
clientthink=$(awk -v start="$begin_events" \
	'NR > start && /ClientThink\(e, cmd\)/ { print NR; exit }' "$move")
test -n "$begin_events" && test -n "$take_events" && test -n "$clientthink" ||
	fail 'missing A observer event feed'
test "$begin_events" -lt "$clientthink"
test "$clientthink" -lt "$take_events"
grep -F 'SG_DropLiveEventsBeginCommand(events, source_door_pending)' \
	slipgate/sg_accept_drop.c >/dev/null
grep -F 'AcceptObserverEventsApply(step, &observation)' \
	slipgate/sg_accept_drop.c >/dev/null
grep -F 'SG_DROP_LIVE_FRAME_STEPS - 1, &observation' \
	slipgate/sg_accept_drop.c >/dev/null
grep -F 'SG_DropLiveEventsLatch(&accept_drop.observer_events,' \
	slipgate/sg_accept_drop.c >/dev/null
grep -F 'SG_OracleReplayDoorPassage(bot->drop_live_step_origin,' \
	"$descend" >/dev/null

# The fixture boundary models the real host lifecycle, not an invented
# acceptance counter: ClientEndServerFrame first syncs pmove and rolls
# oldvelocity after command four, then G_RunFrame copies origin into old_origin
# before EntityPass/SG_RunFrame unless FL_OLDORGNOTSET blocks that copy.
grep -F 'ClientEndServerFrames ();' g_main.c >/dev/null
grep -F 'current_client->ps.pmove.origin[i] = ent->s.origin[i]*8.0;' \
	p_view.c >/dev/null
grep -F 'current_client->ps.pmove.velocity[i] = ent->velocity[i]*8.0;' \
	p_view.c >/dev/null
grep -F 'VectorCopy (ent->velocity, ent->client->oldvelocity);' p_view.c >/dev/null
grep -F 'if (!(ent->flags & FL_OLDORGNOTSET))' g_main.c >/dev/null
grep -F 'VectorCopy (ent->s.origin, ent->s.old_origin);' g_main.c >/dev/null
grep -F '((ent->flags & FL_OLDORGNOTSET) ?' slipgate/sg_accept_drop.c >/dev/null
grep -A 2 -F '((ent->flags & FL_OLDORGNOTSET) ?' \
	slipgate/sg_accept_drop.c | \
	grep -F 'snapshot->old_origin_bits[axis] : snapshot->origin_bits[axis]))' >/dev/null
grep -A 1 -F 'AcceptFloatBits(ent->client->oldvelocity[axis]) !=' \
	slipgate/sg_accept_drop.c | grep -F 'snapshot->velocity_bits[axis]' >/dev/null
grep -F 'snapshot_mismatch=%016llx' slipgate/sg_accept_drop.c >/dev/null
grep -F 'snapshot_first=%s' slipgate/sg_accept_drop.c >/dev/null
grep -F 'SGAD_SNAPSHOT_OLDVELOCITY' slipgate/sg_accept_drop.c >/dev/null
grep -F 'EmulateClientEndServerFrame' \
	acceptance/sg_accept_drop_fixture_test.c >/dev/null
grep -F 'EmulateGRunFrameOldOriginCopy' \
	acceptance/sg_accept_drop_fixture_test.c >/dev/null
grep -F 'SG_AcceptDropEntityPass();' \
	acceptance/sg_accept_drop_fixture_test.c >/dev/null
grep -F 'SG_AcceptDropFrameBegin();' \
	acceptance/sg_accept_drop_fixture_test.c >/dev/null

# Both direct DROP and aggregate host-test recipes must execute the frozen
# Begin-rejection wiring gate; the aggregate recipe runs the binary directly.
test "$(grep -F 'sh tests/sg_drop_begin_wiring_test.sh' GNUmakefile | \
	wc -l | tr -d ' ')" = 2
test "$(grep -F 'sg_drop_begin_wiring_test.sh' Makefile | \
	wc -l | tr -d ' ')" = 2

# Exact known-good recovery telemetry remains at the command and boundary
# failure sites, including the previously dropped descend pair.
grep -F 'SG_AcceptDropShelf(bot, bot->commit_link,' "$descend" >/dev/null
grep -F 'SG_AcceptDropTeach(bot->commit_link, "recovery-lost")' "$descend" >/dev/null
grep -F 'SG_AcceptDropShelf(bot, bestlink, "command-recovery-lost")' "$move" >/dev/null
grep -F 'SG_AcceptDropTeach(bestlink, "command-recovery-lost")' "$move" >/dev/null

printf '%s\n' 'sg_accept_drop_authority_seam_test: ok inventory=29 a-b=source-authenticated observer-events=deferred-boundary telemetry=complete'
