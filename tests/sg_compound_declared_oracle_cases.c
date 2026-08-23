#include "sg_compound_oracle_fixture.h"

static void TestDeclaredActivatorRejectsCaseFoldedKilltargets(void)
{
	edict_t *door;
	edict_t *trigger;
	edict_t *source;
	edict_t *members[2] = { NULL, NULL };

	ResetGuardFixture();
	door = GuardDoor(GUARD_MASTER_KEY);
	trigger = &fixture_edicts[GUARD_TRIGGER_KEY];
	Trigger(trigger, door, 160.0f);
	trigger->s.number = GUARD_TRIGGER_KEY;
	CHECK(SG_DeclaredDoorMembers(trigger, members, 2) == 1);
	CHECK(members[0] == door);

	source = &fixture_edicts[GUARD_SOURCE_KEY];
	source->inuse = true;
	source->classname = "trigger_relay";
	trigger->targetname = "GateTrigger";
	source->killtarget = "gatetrigger";
	CHECK(SG_DeclaredDoorMembers(trigger, members, 2) == -1);

	ResetGuardFixture();
	door = GuardDoor(GUARD_MASTER_KEY);
	trigger = &fixture_edicts[GUARD_TRIGGER_KEY];
	Trigger(trigger, door, 160.0f);
	trigger->s.number = GUARD_TRIGGER_KEY;
	source = &fixture_edicts[GUARD_SOURCE_KEY];
	source->inuse = true;
	source->classname = "trigger_relay";
	door->targetname = "GateDoor";
	source->killtarget = "gatedoor";
	CHECK(SG_DeclaredDoorMembers(trigger, members, 2) == -1);
}

static void TestDeclaredActivatorAcceptsMasterThenSlaveFanout(void)
{
	edict_t *master;
	edict_t *member;
	edict_t *trigger;
	edict_t *members[2] = { NULL, NULL };
	vec3_t source = { 200.0f, 0.0f, 0.0f };

	ResetGuardFixture();
	master = GuardDoor(GUARD_MASTER_KEY);
	member = GuardDoor(GUARD_MEMBER_KEY);
	master->team = "paired-gate";
	member->team = "paired-gate";
	master->teamchain = member;
	member->teammaster = master;
	member->flags |= FL_TEAMSLAVE;
	master->targetname = "SharedGate";
	member->targetname = "sharedgate";
	trigger = &fixture_edicts[GUARD_TRIGGER_KEY];
	memset(trigger, 0, sizeof(*trigger));
	trigger->inuse = true;
	trigger->s.number = GUARD_TRIGGER_KEY;
	trigger->classname = "trigger_multiple";
	trigger->solid = SOLID_TRIGGER;
	trigger->movetype = MOVETYPE_NONE;
	trigger->touch = Touch_Multi;
	trigger->wait = 1.0f;
	trigger->target = "SHAREDGATE";
	Set3(trigger->absmin, 190.0f, -24.0f, -40.0f);
	Set3(trigger->absmax, 210.0f, 24.0f, 40.0f);

	CHECK(SG_DeclaredDoorActivatorSafe(trigger));
	CHECK(SG_DeclaredDoorMembers(trigger, members, 2) == 2);
	CHECK(members[0] == master);
	CHECK(members[1] == member);
	CHECK(SG_DeclaredDoorActivationMatches(trigger, master, source));
	CHECK(SG_DeclaredDoorActivationMatches(trigger, member, source));

	/* A slave-only target remains unrepresentable: direct door_use on it is a
	 * no-op and cannot establish controller authority for the team. */
	master->targetname = "other";
	CHECK(!SG_DeclaredDoorActivatorSafe(trigger));
	CHECK(SG_DeclaredDoorMembers(trigger, members, 2) == -1);
}

static void TestDeclaredActivatorAcceptsSynchronousRelayDoor(void)
{
	edict_t *door;
	edict_t *trigger;
	edict_t *relay;
	edict_t *unrelated;
	edict_t *members[2] = { NULL, NULL };
	vec3_t source = { 200.0f, 0.0f, 0.0f };

	ResetGuardFixture();
	door = GuardDoor(GUARD_MASTER_KEY);
	door->targetname = "CellDoor";
	trigger = &fixture_edicts[GUARD_TRIGGER_KEY];
	memset(trigger, 0, sizeof(*trigger));
	trigger->inuse = true;
	trigger->s.number = GUARD_TRIGGER_KEY;
	trigger->classname = "trigger_multiple";
	trigger->solid = SOLID_TRIGGER;
	trigger->movetype = MOVETYPE_NONE;
	trigger->touch = Touch_Multi;
	trigger->wait = 1.0f;
	trigger->target = "CellRelay";
	Set3(trigger->absmin, 190.0f, -24.0f, -40.0f);
	Set3(trigger->absmax, 210.0f, 24.0f, 40.0f);
	relay = &fixture_edicts[GUARD_SOURCE_KEY];
	memset(relay, 0, sizeof(*relay));
	relay->inuse = true;
	relay->s.number = GUARD_SOURCE_KEY;
	relay->classname = "trigger_relay";
	relay->use = trigger_relay_use;
	relay->targetname = "CellRelay";
	relay->target = "CellDoor";

	CHECK(SG_DeclaredDoorActivatorSafe(trigger));
	CHECK(SG_DeclaredDoorMembers(trigger, members, 2) == 1);
	CHECK(members[0] == door);
	CHECK(SG_DeclaredDoorActivationMatches(trigger, door, source));
	unrelated = GuardDoor(GUARD_EXTRA_KEY);
	unrelated->targetname = "OtherDoor";
	CHECK(!SG_DeclaredDoorActivationMatches(trigger, unrelated, source));

	relay->delay = 0.1f;
	CHECK(!SG_DeclaredDoorActivatorSafe(trigger));
	CHECK(!SG_DeclaredDoorActivationMatches(trigger, door, source));
	relay->delay = 0.0f;
	relay->killtarget = "CellDoor";
	CHECK(!SG_DeclaredDoorActivatorSafe(trigger));
	relay->killtarget = NULL;
	relay->message = "opening";
	CHECK(!SG_DeclaredDoorActivatorSafe(trigger));
	relay->message = NULL;
	relay->target = "MissingDoor";
	CHECK(!SG_DeclaredDoorActivatorSafe(trigger));
	relay->target = "CellRelay2";
	memset(&fixture_edicts[GUARD_EXTRA_KEY], 0,
	    sizeof(fixture_edicts[GUARD_EXTRA_KEY]));
	fixture_edicts[GUARD_EXTRA_KEY].inuse = true;
	fixture_edicts[GUARD_EXTRA_KEY].s.number = GUARD_EXTRA_KEY;
	fixture_edicts[GUARD_EXTRA_KEY].classname = "trigger_relay";
	fixture_edicts[GUARD_EXTRA_KEY].use = trigger_relay_use;
	fixture_edicts[GUARD_EXTRA_KEY].targetname = "CellRelay2";
	fixture_edicts[GUARD_EXTRA_KEY].target = "CellDoor";
	CHECK(!SG_DeclaredDoorActivatorSafe(trigger));
}

static void TestDeclaredActivatorDelayedSoundTerminal(void)
{
	edict_t *door;
	edict_t *trigger;
	edict_t *relay;
	edict_t *relay2;
	edict_t *speaker;

	ResetGuardFixture();
	door = GuardDoor(GUARD_MASTER_KEY);
	door->targetname = "TimedGate";
	trigger = &fixture_edicts[GUARD_TRIGGER_KEY];
	memset(trigger, 0, sizeof(*trigger));
	trigger->inuse = true;
	trigger->s.number = GUARD_TRIGGER_KEY;
	trigger->classname = "trigger_multiple";
	trigger->solid = SOLID_TRIGGER;
	trigger->movetype = MOVETYPE_NONE;
	trigger->touch = Touch_Multi;
	trigger->wait = 1.0f;
	trigger->target = "TimedGate";
	relay = &fixture_edicts[GUARD_SOURCE_KEY];
	relay->inuse = true;
	relay->classname = "trigger_relay";
	relay->use = trigger_relay_use;
	relay->targetname = "TimedGate";
	relay->target = "TimedGateClose";
	speaker = &fixture_edicts[GUARD_EXTRA_KEY];
	speaker->inuse = true;
	speaker->classname = "target_speaker";
	speaker->use = Use_Target_Speaker;
	speaker->targetname = "TimedGateClose";

	CHECK(SG_DeclaredDoorActivatorSafe(trigger));
	CHECK(SG_DeclaredDoorTriggerWaitMs(trigger) == 1000);

	/* lmctf58 schedules only the close sound through this relay.  The plan
	 * authenticates the inbound ordinal but consumes the positive-delay relay
	 * before DelayedUse allocation, so the physical door remains representable. */
	relay->delay = 311.0f;
	CHECK(SG_DeclaredDoorActivatorSafe(trigger));
	CHECK(SG_DeclaredDoorTriggerWaitMs(trigger) == 1000);

	relay->delay = 0.0f;
	relay->target = "TimedGateRelay";
	relay2 = speaker;
	relay2->classname = "trigger_relay";
	relay2->use = trigger_relay_use;
	relay2->targetname = "TimedGateRelay";
	relay2->target = "TimedGateCloseNested";
	relay2->delay = 0.0f;
	speaker = &fixture_edicts[FIXTURE_EDICTS - 1];
	speaker->inuse = true;
	speaker->classname = "target_speaker";
	speaker->use = Use_Target_Speaker;
	speaker->targetname = "TimedGateCloseNested";
	CHECK(SG_DeclaredDoorActivatorSafe(trigger));
	relay2->delay = 0.001f;
	CHECK(SG_DeclaredDoorActivatorSafe(trigger));
	CHECK(SG_DeclaredDoorTriggerWaitMs(trigger) == 1000);
	relay2->delay = 0.0f;
	relay2->killtarget = "TimedGate";
	CHECK(!SG_DeclaredDoorActivatorSafe(trigger));
	relay2->killtarget = NULL;
	relay2->target = "TimedGateRelay";
	CHECK(!SG_DeclaredDoorActivatorSafe(trigger));
	relay2->target = "TimedGateCloseNested";
	speaker->use = door_use;
	CHECK(!SG_DeclaredDoorActivatorSafe(trigger));
	speaker->use = Use_Target_Speaker;
	CHECK(SG_DeclaredDoorActivatorSafe(trigger));

	trigger->wait = 30.0f;
	CHECK(SG_DeclaredDoorActivatorSafe(trigger));
	CHECK(SG_DeclaredDoorTriggerWaitMs(trigger) == RUNE_MAX_COST_MS);
	trigger->wait = nextafterf(30.0f, INFINITY);
	CHECK(SG_DeclaredDoorActivatorSafe(trigger));
	CHECK(SG_DeclaredDoorTriggerWaitMs(trigger) == RUNE_MAX_COST_MS);
	trigger->wait = 312.0f;
	CHECK(SG_DeclaredDoorActivatorSafe(trigger));
	CHECK(SG_DeclaredDoorTriggerWaitMs(trigger) == 312000);
	trigger->wait = 312.0004f;
	CHECK(SG_DeclaredDoorTriggerWaitMs(trigger) == 312000);

	/* Exact lmctf58 controller waits contribute only the true post-close rearm
	 * gap; a long open hold is not charged as execution time. */
	trigger->wait = 312.0f;
	door->moveinfo.distance = 90.0f;
	door->moveinfo.speed = door->moveinfo.accel = door->moveinfo.decel = 15.0f;
	door->moveinfo.wait = 300.0f;
	CHECK(SG_DeclaredDoorContractCost(trigger, 1000, 500, 500) == 14900);
	CHECK(SG_RuneTestDoorCooldownGapMs(trigger) == 0);
	trigger->wait = 63.0f;
	door->moveinfo.speed = door->moveinfo.accel = door->moveinfo.decel = 100.0f;
	door->moveinfo.wait = 60.0f;
	CHECK(SG_DeclaredDoorContractCost(trigger, 1000, 500, 500) == 5500);
	CHECK(SG_RuneTestDoorCooldownGapMs(trigger) == 800);
	trigger->wait = 304.0f;
	door->moveinfo.speed = door->moveinfo.accel = door->moveinfo.decel = 56.0f;
	door->moveinfo.wait = 300.0f;
	CHECK(SG_DeclaredDoorContractCost(trigger, 1000, 500, 500) == 6500);
	CHECK(SG_RuneTestDoorCooldownGapMs(trigger) == 384);
	trigger->wait = nextafterf((float)INT_MAX / 1000.0f, 0.0f);
	CHECK(SG_DeclaredDoorTriggerWaitMs(trigger) > INT_MAX - 1000);
	CHECK(SG_DeclaredDoorContractCost(trigger, 1000, 500, 500) == -1);
	trigger->wait = 312.0f;
	door->moveinfo.wait = FLT_MAX;
	CHECK(SG_DeclaredDoorContractCost(trigger, 1000, 500, 500) == -1);
	CHECK(SG_RuneTestDoorCooldownGapMs(trigger) == -1);
	door->moveinfo.wait = 300.0f;
	door->moveinfo.distance = FLT_MAX;
	door->moveinfo.speed = door->moveinfo.accel = door->moveinfo.decel = 1.0f;
	CHECK(SG_DeclaredDoorContractCost(trigger, 1000, 500, 500) == -1);
	CHECK(SG_RuneTestDoorCooldownGapMs(trigger) == -1);
	door->moveinfo.distance = 90.0f;
	door->moveinfo.speed = door->moveinfo.accel = door->moveinfo.decel = 56.0f;
	CHECK(SG_DeclaredDoorContractCost(trigger, INT_MAX, 1, INT_MAX) == -1);

	ResetGuardFixture();
	door = GuardDoor(GUARD_MASTER_KEY);
	door->targetname = "DoorTimedGate";
	door->target = "DoorTimedRelay";
	trigger = &fixture_edicts[GUARD_TRIGGER_KEY];
	memset(trigger, 0, sizeof(*trigger));
	trigger->inuse = true;
	trigger->s.number = GUARD_TRIGGER_KEY;
	trigger->classname = "trigger_multiple";
	trigger->solid = SOLID_TRIGGER;
	trigger->movetype = MOVETYPE_NONE;
	trigger->touch = Touch_Multi;
	trigger->wait = 1.0f;
	trigger->target = "DoorTimedGate";
	relay = &fixture_edicts[GUARD_SOURCE_KEY];
	relay->inuse = true;
	relay->classname = "trigger_relay";
	relay->use = trigger_relay_use;
	relay->targetname = "DoorTimedRelay";
	relay->target = "DoorTimedClose";
	speaker = &fixture_edicts[GUARD_EXTRA_KEY];
	speaker->inuse = true;
	speaker->classname = "target_speaker";
	speaker->use = Use_Target_Speaker;
	speaker->targetname = "DoorTimedClose";
	CHECK(SG_DeclaredDoorActivatorSafe(trigger));
	relay->delay = 61.0f;
	CHECK(SG_DeclaredDoorActivatorSafe(trigger));
	relay->killtarget = "DoorTimedGate";
	CHECK(!SG_DeclaredDoorActivatorSafe(trigger));
}

static void TestDeclaredActivatorAcceptsVerticalDoorWithEmptyDelay(void)
{
	edict_t *door;
	edict_t *trigger;
	edict_t *speaker;

	ResetGuardFixture();
	door = GuardDoor(GUARD_MASTER_KEY);
	door->targetname = "CabinGate";
	door->delay = 1.0f;
	door->moveinfo.distance = 120.0f;
	VectorCopy(door->moveinfo.start_origin, door->moveinfo.end_origin);
	door->moveinfo.end_origin[2] += 120.0f;
	trigger = &fixture_edicts[GUARD_TRIGGER_KEY];
	memset(trigger, 0, sizeof(*trigger));
	trigger->inuse = true;
	trigger->s.number = GUARD_TRIGGER_KEY;
	trigger->classname = "trigger_multiple";
	trigger->solid = SOLID_TRIGGER;
	trigger->movetype = MOVETYPE_NONE;
	trigger->touch = Touch_Multi;
	trigger->wait = 0.2f;
	trigger->target = "CabinGate";

	CHECK(SG_DeclaredDoorActivatorSafe(trigger));

	speaker = &fixture_edicts[GUARD_EXTRA_KEY];
	speaker->inuse = true;
	speaker->classname = "target_speaker";
	speaker->use = Use_Target_Speaker;
	speaker->targetname = "CabinBell";
	door->target = "CabinBell";
	CHECK(!SG_DeclaredDoorActivatorSafe(trigger));
	door->target = NULL;
	door->message = "opening";
	CHECK(!SG_DeclaredDoorActivatorSafe(trigger));
}

static void TestCompoundLiftAdmitsOnlySameDoorSetSibling(void)
{
	edict_t *door;
	edict_t *foreign;
	edict_t *selected;
	edict_t *sibling;

	ResetGuardFixture();
	door = GuardDoor(GUARD_MASTER_KEY);
	door->targetname = "CabinGate";
	foreign = GuardDoor(GUARD_MEMBER_KEY);
	foreign->targetname = "ForeignGate";
	selected = &fixture_edicts[GUARD_TRIGGER_KEY];
	sibling = &fixture_edicts[GUARD_SOURCE_KEY];
	memset(selected, 0, sizeof(*selected));
	selected->inuse = true;
	selected->classname = "trigger_multiple";
	selected->solid = SOLID_TRIGGER;
	selected->movetype = MOVETYPE_NONE;
	selected->touch = Touch_Multi;
	selected->wait = 0.2f;
	selected->target = door->targetname;
	*sibling = *selected;
	sibling->s.number = GUARD_SOURCE_KEY;
	CHECK(SG_OracleDeclaredApproachTriggerAllowed(
	    RL_LIFT, selected, sibling));

	sibling->target = foreign->targetname;
	CHECK(!SG_OracleDeclaredApproachTriggerAllowed(
	    RL_LIFT, selected, sibling));
	sibling->target = door->targetname;
	CHECK(!SG_OracleDeclaredApproachTriggerAllowed(
	    RL_TELEPORT, selected, sibling));
}

static void TestCompoundLiftDelayedTopDoorShape(void)
{
	edict_t *door;
	edict_t *trigger;
	uint32_t delay_ms = 0U;

	ResetGuardFixture();
	door = GuardDoor(GUARD_MASTER_KEY);
	door->targetname = "TopGate";
	trigger = &fixture_edicts[GUARD_TRIGGER_KEY];
	memset(trigger, 0, sizeof(*trigger));
	trigger->inuse = true;
	trigger->classname = "trigger_multiple";
	trigger->solid = SOLID_TRIGGER;
	trigger->movetype = MOVETYPE_NONE;
	trigger->touch = Touch_Multi;
	trigger->wait = 0.2f;
	trigger->delay = 0.5f;
	trigger->target = door->targetname;
	CHECK(SG_DeclaredDoorDelayedActivatorSafe(trigger, &delay_ms));
	CHECK(delay_ms == 500U);
	CHECK(!SG_DeclaredDoorDirectActivatorSafe(trigger));

	trigger->delay = 0.0f;
	CHECK(!SG_DeclaredDoorDelayedActivatorSafe(trigger, &delay_ms));
	trigger->delay = 0.5f;
	trigger->killtarget = "TopGate";
	CHECK(!SG_DeclaredDoorDelayedActivatorSafe(trigger, &delay_ms));
}

static void TestCompoundLiftDirectTopDoorMembers(void)
{
	edict_t *door;
	edict_t *trigger;
	vec3_t top_body = { 160.0f, 0.0f, 0.0f };

	ResetGuardFixture();
	door = GuardDoor(GUARD_MASTER_KEY);
	door->targetname = "TopGate";
	trigger = &fixture_edicts[GUARD_TRIGGER_KEY];
	memset(trigger, 0, sizeof(*trigger));
	trigger->inuse = true;
	trigger->classname = "trigger_multiple";
	trigger->solid = SOLID_TRIGGER;
	trigger->movetype = MOVETYPE_NONE;
	trigger->touch = Touch_Multi;
	trigger->wait = 0.2f;
	trigger->target = door->targetname;
	Set3(trigger->absmin, 159.0f, -24.0f, -40.0f);
	Set3(trigger->absmax, 161.0f, 24.0f, 40.0f);
	CHECK(SG_DeclaredDoorDirectActivatorSafe(trigger));
	CHECK(SG_RuneTestLiftEgressDoorMemberCount(top_body) == 1);
}

static void TestCarrierEgressEnvelopeIncludesPlayerHull(void)
{
	float nearest = sqrtf(144.0f * 144.0f + 32.0f * 32.0f);
	float brush_and_lattice = sqrtf(49.0f * 49.0f + 49.0f * 49.0f) +
	    64.0f;
	float radius = SG_RuneTestLiftEgressSearchRadius(49.0f, 49.0f);

	CHECK(brush_and_lattice < nearest);
	CHECK(radius >= nearest);
	CHECK(fabsf(radius - brush_and_lattice - 16.0f) < 0.001f);
}

static void TestCarrierDirectionIsSignedNeutral(void)
{
	edict_t platform;
	int ascending;
	int descending;

	memset(&platform, 0, sizeof(platform));
	platform.moveinfo.speed = 20.0f;
	platform.moveinfo.accel = 20.0f;
	platform.moveinfo.decel = 20.0f;
	ascending = SG_RuneTestPlatformTravelMs(&platform, 0.0f, 128.0f);
	descending = SG_RuneTestPlatformTravelMs(&platform, 128.0f, 0.0f);
	CHECK(ascending == 6400);
	CHECK(descending == ascending);
	CHECK(SG_RuneTestLiftEgressSpans(0.0f, 128.0f, 0.0f, 100.0f));
	CHECK(!SG_RuneTestLiftEgressSpans(0.0f, 128.0f, 0.0f, 50.0f));
	CHECK(SG_RuneTestLiftEgressSpans(128.0f, 0.0f, 128.0f, 28.0f));
	CHECK(!SG_RuneTestLiftEgressSpans(128.0f, 0.0f, 128.0f, 78.0f));
}

static void TestDeclaredActivatorRejectsTeamAuthorityDrift(void)
{
	edict_t *master;
	edict_t *member;
	edict_t *trigger;
	edict_t *foreign;

	ResetGuardFixture();
	master = GuardDoor(GUARD_MASTER_KEY);
	member = GuardDoor(GUARD_MEMBER_KEY);
	foreign = GuardDoor(GUARD_EXTRA_KEY);
	master->team = "paired-gate";
	member->team = "paired-gate";
	master->teamchain = member;
	member->teammaster = master;
	member->flags |= FL_TEAMSLAVE;
	master->targetname = "SharedGate";
	member->targetname = "sharedgate";
	trigger = &fixture_edicts[GUARD_TRIGGER_KEY];
	memset(trigger, 0, sizeof(*trigger));
	trigger->inuse = true;
	trigger->s.number = GUARD_TRIGGER_KEY;
	trigger->classname = "trigger_multiple";
	trigger->solid = SOLID_TRIGGER;
	trigger->movetype = MOVETYPE_NONE;
	trigger->touch = Touch_Multi;
	trigger->wait = 1.0f;
	trigger->target = "SHAREDGATE";
	CHECK(SG_DeclaredDoorActivatorSafe(trigger));

	member->flags &= ~FL_TEAMSLAVE;
	CHECK(!SG_DeclaredDoorActivatorSafe(trigger));
	member->flags |= FL_TEAMSLAVE;
	master->teammaster = foreign;
	CHECK(!SG_DeclaredDoorActivatorSafe(trigger));
	master->teammaster = master;
	member->teammaster = foreign;
	CHECK(!SG_DeclaredDoorActivatorSafe(trigger));
	member->teammaster = master;
	member->team = "other-team";
	CHECK(!SG_DeclaredDoorActivatorSafe(trigger));
	member->team = "paired-gate";
	member->teamchain = master;
	CHECK(!SG_DeclaredDoorActivatorSafe(trigger));
	member->teamchain = NULL;
	master->teamchain = NULL;
	CHECK(!SG_DeclaredDoorActivatorSafe(trigger));

	/* The synthesized trigger calls door_use(owner), not owner->teammaster.
	 * Only the exact canonical captain may own it. */
	master->teamchain = member;
	Trigger(trigger, master, 160.0f);
	trigger->s.number = GUARD_TRIGGER_KEY;
	CHECK(SG_DeclaredDoorActivatorSafe(trigger));
	member->flags &= ~FL_TEAMSLAVE;
	trigger->owner = member;
	CHECK(!SG_DeclaredDoorActivatorSafe(trigger));

	/* A coherent-looking singleton is not canonical when an earlier live edict
	 * has the same team: G_FindTeams would make the earlier edict captain. */
	ResetGuardFixture();
	master = GuardDoor(GUARD_MASTER_KEY);
	member = GuardDoor(GUARD_MEMBER_KEY);
	master->team = "same-team";
	member->team = "same-team";
	member->targetname = "LaterGate";
	trigger = &fixture_edicts[GUARD_TRIGGER_KEY];
	memset(trigger, 0, sizeof(*trigger));
	trigger->inuse = true;
	trigger->s.number = GUARD_TRIGGER_KEY;
	trigger->classname = "trigger_multiple";
	trigger->solid = SOLID_TRIGGER;
	trigger->movetype = MOVETYPE_NONE;
	trigger->touch = Touch_Multi;
	trigger->wait = 1.0f;
	trigger->target = "latergate";
	CHECK(!SG_DeclaredDoorActivatorSafe(trigger));

	ResetGuardFixture();
	master = GuardDoor(GUARD_MASTER_KEY);
	member = GuardDoor(GUARD_MEMBER_KEY);
	foreign = GuardDoor(GUARD_EXTRA_KEY);
	master->team = "ordered-team";
	member->team = "ordered-team";
	foreign->team = "ordered-team";
	master->teamchain = foreign;
	foreign->teamchain = member;
	foreign->teammaster = master;
	member->teammaster = master;
	foreign->flags |= FL_TEAMSLAVE;
	member->flags |= FL_TEAMSLAVE;
	master->targetname = "OrderedGate";
	trigger = &fixture_edicts[GUARD_TRIGGER_KEY];
	memset(trigger, 0, sizeof(*trigger));
	trigger->inuse = true;
	trigger->s.number = GUARD_TRIGGER_KEY;
	trigger->classname = "trigger_multiple";
	trigger->solid = SOLID_TRIGGER;
	trigger->movetype = MOVETYPE_NONE;
	trigger->touch = Touch_Multi;
	trigger->wait = 1.0f;
	trigger->target = "orderedgate";
	CHECK(!SG_DeclaredDoorActivatorSafe(trigger));
}

static void TestDeclaredActivatorRejectsMalformedWorldBounds(void)
{
	edict_t *door;
	edict_t *trigger;
	edict_t *members[2] = { NULL, NULL };

	ResetGuardFixture();
	door = GuardDoor(GUARD_MASTER_KEY);
	trigger = &fixture_edicts[GUARD_TRIGGER_KEY];
	Trigger(trigger, door, 160.0f);
	trigger->s.number = GUARD_TRIGGER_KEY;
	trigger->targetname = "bounded-trigger";
	globals.num_edicts = MAX_EDICTS;
	CHECK(SG_DeclaredDoorMembers(trigger, members, 2) == -1);

	ResetGuardFixture();
	door = GuardDoor(GUARD_MASTER_KEY);
	trigger = &fixture_edicts[GUARD_TRIGGER_KEY];
	Trigger(trigger, door, 160.0f);
	trigger->s.number = GUARD_TRIGGER_KEY;
	trigger->targetname = "bounded-trigger";
	globals.max_edicts = FIXTURE_EDICTS - 1;
	CHECK(SG_DeclaredDoorMembers(trigger, members, 2) == -1);

	ResetGuardFixture();
	door = GuardDoor(GUARD_MASTER_KEY);
	trigger = &fixture_edicts[GUARD_TRIGGER_KEY];
	Trigger(trigger, door, 160.0f);
	trigger->s.number = GUARD_TRIGGER_KEY;
	trigger->targetname = "bounded-trigger";
	game.maxentities = MAX_EDICTS + 1;
	globals.max_edicts = game.maxentities;
	CHECK(SG_DeclaredDoorMembers(trigger, members, 2) == -1);
}

static void TestDeclaredDoorHoldMembersIsAtomic(void)
{
	edict_t *master;
	edict_t *member;
	edict_t *members[2];
	float master_before;
	float member_before;

	GuardDoorPair(&master, &member);
	members[0] = master;
	members[1] = member;
	master_before = master->nextthink;
	member_before = member->nextthink;
	member->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	CHECK(!SG_DeclaredDoorHoldMembers(members, 2, 500));
	CHECK(master->nextthink == master_before);
	CHECK(member->nextthink == member_before);

	member->moveinfo.state = SG_PLAT_STATE_TOP;
	master->moveinfo.endfunc = door_hit_top;
	member->moveinfo.endfunc = door_hit_top;
	PublishDoorCompletion(master, SG_MOVER_COMPLETION_TOP);
	PublishDoorCompletion(member, SG_MOVER_COMPLETION_TOP);
	CHECK(SG_DeclaredDoorHoldMembers(members, 2, 500));
	CHECK(master->nextthink == level.time + 0.5f);
	CHECK(member->nextthink == level.time + 0.5f);
}

static void TestDeclaredDoorHoldMembersRequiresClosedTeam(void)
{
	edict_t *master;
	edict_t *member;
	edict_t *extra;
	edict_t *members[2];
	float master_before;
	float member_before;

	GuardDoorPair(&master, &member);
	members[0] = master;
	master_before = master->nextthink;
	CHECK(!SG_DeclaredDoorHoldMembers(members, 1, 500));
	CHECK(master->nextthink == master_before);

	GuardDoorPair(&master, &member);
	extra = GuardDoor(GUARD_EXTRA_KEY);
	member->teamchain = extra;
	extra->teammaster = master;
	members[0] = master;
	members[1] = member;
	master_before = master->nextthink;
	member_before = member->nextthink;
	CHECK(!SG_DeclaredDoorHoldMembers(members, 2, 500));
	CHECK(master->nextthink == master_before);
	CHECK(member->nextthink == member_before);

	GuardDoorPair(&master, &member);
	member->teamchain = master;
	members[0] = master;
	members[1] = member;
	master_before = master->nextthink;
	member_before = member->nextthink;
	CHECK(!SG_DeclaredDoorHoldMembers(members, 2, 500));
	CHECK(master->nextthink == master_before);
	CHECK(member->nextthink == member_before);
}

static void TestDeclaredDoorHoldMembersRejectsForeignPointers(void)
{
	edict_t *master;
	edict_t *member;
	edict_t *members[2];
	edict_t *foreign = fixture_edicts + FIXTURE_EDICTS;
	float master_before;
	float member_before;

	GuardDoorPair(&master, &member);
	members[0] = foreign;
	CHECK(!SG_DeclaredDoorHoldMembers(members, 1, 500));

	GuardDoorPair(&master, &member);
	master->teammaster = foreign;
	members[0] = master;
	members[1] = member;
	master_before = master->nextthink;
	member_before = member->nextthink;
	CHECK(!SG_DeclaredDoorHoldMembers(members, 2, 500));
	CHECK(master->nextthink == master_before);
	CHECK(member->nextthink == member_before);

	GuardDoorPair(&master, &member);
	master->teamchain = foreign;
	members[0] = master;
	master_before = master->nextthink;
	CHECK(!SG_DeclaredDoorHoldMembers(members, 1, 500));
	CHECK(master->nextthink == master_before);
}

static void TestDeclaredDoorMembersTerminalRequiresPhysicalPose(void)
{
	edict_t *master;
	edict_t *member;
	edict_t *members[2];

	GuardDoorPair(&master, &member);
	members[0] = master;
	members[1] = member;
	CHECK(!SG_DeclaredDoorMembersTerminal(members, 2));

	master->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	VectorCopy(master->moveinfo.end_origin, master->s.origin);
	HostLinkEntity(master);
	CHECK(!SG_DeclaredDoorMembersTerminal(members, 2));

	VectorCopy(master->moveinfo.start_origin, master->s.origin);
	HostLinkEntity(master);
	member->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	master->nextthink = 0.0f;
	member->nextthink = 0.0f;
	/* An untouched door has no movement completion callback yet. */
	CHECK(SG_DeclaredDoorMembersTerminal(members, 2));

	/* SV_Push independently rounds every linear delta to one eighth.  Stock
	 * completion evidence remains authoritative even when a diagonal door's
	 * final linked pose is not bit-equal to its unquantized endpoint. */
	Set3(master->s.origin, 0.125f, 0.125f, 0.0f);
	master->moveinfo.endfunc = door_hit_bottom;
	member->moveinfo.endfunc = door_hit_bottom;
	HostLinkEntity(master);
	CHECK(!SG_DeclaredDoorMembersTerminal(members, 2));
	PublishDoorCompletion(master, SG_MOVER_COMPLETION_BOTTOM);
	PublishDoorCompletion(member, SG_MOVER_COMPLETION_BOTTOM);
	CHECK(SG_DeclaredDoorMembersTerminal(members, 2));
	master->s.origin[0] += 100000.0f;
	HostLinkEntity(master);
	CHECK(!SG_DeclaredDoorMembersTerminal(members, 2));
	Set3(master->s.origin, 0.125f, 0.125f, 0.0f);
	HostLinkEntity(master);
	PublishDoorCompletion(master, SG_MOVER_COMPLETION_BOTTOM);
	master->moveinfo.endfunc = door_hit_top;
	CHECK(!SG_DeclaredDoorMembersTerminal(members, 2));
	master->moveinfo.endfunc = door_hit_bottom;

	member->moveinfo.state = SG_PLAT_STATE_TOP;
	member->moveinfo.wait = -1.0f;
	VectorCopy(member->moveinfo.end_origin, member->s.origin);
	VectorCopy(member->moveinfo.end_angles, member->s.angles);
	member->moveinfo.endfunc = door_hit_top;
	HostLinkEntity(member);
	PublishDoorCompletion(member, SG_MOVER_COMPLETION_TOP);
	CHECK(SG_DeclaredDoorMembersTerminal(members, 2));
	member->nextthink = level.time + 1.0f;
	CHECK(!SG_DeclaredDoorMembersTerminal(members, 2));
	member->nextthink = 0.0f;
	member->velocity[0] = 1.0f;
	CHECK(!SG_DeclaredDoorMembersTerminal(members, 2));

	/* AngleMove_Final divides and the pusher multiplies by 0.1f.  The stock
	 * terminal can therefore land just beyond the nominal endpoint. */
	ResetGuardFixture();
	master = GuardDoor(GUARD_MASTER_KEY);
	members[0] = master;
	master->classname = "func_door_rotating";
	master->teamchain = NULL;
	master->teammaster = master;
	Set3(master->moveinfo.start_angles, 0.0f, -167.3114776611328f, 0.0f);
	Set3(master->moveinfo.end_angles, 0.0f, 5.26661491394043f, 0.0f);
	Set3(master->s.angles, 0.0f, 5.266632080078125f, 0.0f);
	master->moveinfo.distance = 172.57809448242188f;
	master->moveinfo.state = SG_PLAT_STATE_TOP;
	master->moveinfo.wait = -1.0f;
	master->nextthink = 0.0f;
	master->moveinfo.endfunc = door_hit_top;
	VectorClear(master->velocity);
	VectorClear(master->avelocity);
	HostLinkEntity(master);
	PublishDoorCompletion(master, SG_MOVER_COMPLETION_TOP);
	CHECK(SG_DeclaredDoorMembersTerminal(members, 1));
	CHECK(SG_DeclaredDoorHoldMembers(members, 1, 500));
	master->s.angles[1] = master->moveinfo.end_angles[1] + 0.01f;
	HostLinkEntity(master);
	CHECK(!SG_DeclaredDoorMembersTerminal(members, 1));
}

static void TestLiftSwimApproach(void)
{
	fixture_config_t config = DefaultConfig(1, FIXTURE_SUFFIX_SUCCESS);
	sg_phantom_t phantom;
	sg_swim_proof_t proof;
	vec3_t anchor = { 0.0f, 0.0f, 0.0f };
	edict_t *platform;
	edict_t *entry;

	/* Existing overlap is an exact zero-command proof only when trigger and
	 * matched-platform support coincide. */
	config.mechanism_x = 0.0f;
	config.source_x = 0.0f;
	config.lift_support = true;
	ResetFixture(&config);
	platform = &fixture_edicts[1];
	entry = &fixture_edicts[2];
	platform->classname = "func_plat";
	entry->touch = Touch_Multi;
	InitPhantom(&phantom, false);
	CHECK(SG_OracleLiftSwimApproach(&phantom, anchor, entry, platform,
	    0.0f, &proof, NULL, true));
	CHECK(proof.arrival_ms == 0);
	CHECK(fixture_observation.pmove_calls == 0);

	/* A six-unit separation from the expanded trigger fringe must be crossed by
	 * authoritative Pmove before the same support/contact observation commits. */
	config.source_x = 24.0f;
	ResetFixture(&config);
	platform = &fixture_edicts[1];
	entry = &fixture_edicts[2];
	platform->classname = "func_plat";
	entry->touch = Touch_Multi;
	InitPhantom(&phantom, false);
	CHECK(SG_OracleLiftSwimApproach(&phantom, anchor, entry, platform,
	    0.0f, &proof, NULL, true));
	CHECK(proof.arrival_ms == SG_SWIM_STEP_MSEC);
	CHECK(fixture_observation.pmove_calls == 1);

	/* Wrong trigger, wrong platform, trigger without support, and support
	 * without a proved trigger approach all fail closed. */
	ResetFixture(&config);
	platform = &fixture_edicts[1];
	platform->classname = "func_plat";
	InitPhantom(&phantom, false);
	CHECK(!SG_OracleLiftSwimApproach(&phantom, anchor, &fixture_edicts[3],
	    platform, 0.0f, &proof, NULL, true));

	ResetFixture(&config);
	entry = &fixture_edicts[2];
	entry->touch = Touch_Multi;
	InitPhantom(&phantom, false);
	CHECK(!SG_OracleLiftSwimApproach(&phantom, anchor, entry,
	    &fixture_edicts[4], 0.0f, &proof, NULL, true));

	config.source_x = 0.0f;
	config.lift_support = false;
	ResetFixture(&config);
	platform = &fixture_edicts[1];
	entry = &fixture_edicts[2];
	platform->classname = "func_plat";
	entry->touch = Touch_Multi;
	InitPhantom(&phantom, false);
	CHECK(!SG_OracleLiftSwimApproach(&phantom, anchor, entry, platform,
	    0.0f, &proof, NULL, true));

	config.source_x = 24.0f;
	config.lift_support = true;
	config.touch_substep = 0;
	ResetFixture(&config);
	platform = &fixture_edicts[1];
	entry = &fixture_edicts[2];
	platform->classname = "func_plat";
	entry->touch = Touch_Multi;
	InitPhantom(&phantom, false);
	phantom.groundentity = true;
	CHECK(!SG_OracleLiftSwimApproach(&phantom, anchor, entry, platform,
	    0.0f, &proof, NULL, true));

	/* A dry unstable body is outside the serialized water-entry contract. */
	config.source_x = 0.0f;
	ResetFixture(&config);
	platform = &fixture_edicts[1];
	entry = &fixture_edicts[2];
	platform->classname = "func_plat";
	entry->touch = Touch_Multi;
	InitPhantom(&phantom, false);
	phantom.waterlevel = 0;
	phantom.watertype = 0;
	phantom.groundentity = false;
	CHECK(!SG_OracleLiftSwimApproach(&phantom, anchor, entry, platform,
	    0.0f, &proof, NULL, true));
}


int SG_CompoundDeclaredOracleCasesRun(void)
{
	fixture_config_t config = DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	int before = failures;

	ResetFixture(&config);
	CHECK(SG_RuneTestDropPrefixCacheCases() == 0);
	CHECK(SG_OracleTestDoorBoundsCacheCases() == 0);
	CHECK(SG_OracleTestDoorEgressReplayCacheCases() == 0);
	TestDeclaredActivatorRejectsCaseFoldedKilltargets();
	TestDeclaredActivatorAcceptsMasterThenSlaveFanout();
	TestDeclaredActivatorAcceptsSynchronousRelayDoor();
	TestDeclaredActivatorDelayedSoundTerminal();
	TestDeclaredActivatorAcceptsVerticalDoorWithEmptyDelay();
	TestCompoundLiftAdmitsOnlySameDoorSetSibling();
	TestCompoundLiftDelayedTopDoorShape();
	TestCompoundLiftDirectTopDoorMembers();
	TestCarrierEgressEnvelopeIncludesPlayerHull();
	TestCarrierDirectionIsSignedNeutral();
	TestDeclaredActivatorRejectsTeamAuthorityDrift();
	TestDeclaredActivatorRejectsMalformedWorldBounds();
	TestDeclaredDoorHoldMembersIsAtomic();
	TestDeclaredDoorHoldMembersRequiresClosedTeam();
	TestDeclaredDoorHoldMembersRejectsForeignPointers();
	TestDeclaredDoorMembersTerminalRequiresPhysicalPose();
	TestLiftSwimApproach();
	return failures - before;
}
