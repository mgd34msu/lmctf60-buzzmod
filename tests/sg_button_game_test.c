/* Game-boundary regression for stock func_button plus the SG transaction. */
#include <stdio.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_compound_guard.h"
#include "slipgate/sg_declared_door_guard.h"
#include "slipgate/sg_rune_binding.h"
#include "slipgate/sg_rune_mechanism_catalog.h"
#include "slipgate/sg_move.h"

#define TEST_EDICTS 16
#define BUTTON_KEY 3
#define BOT_KEY 5
#define HUMAN_KEY 6
#define RELAY_KEY 7
#define FOREIGN_KEY 8
#define TEST_LINK 23

#define TEST_STATE_TOP 0
#define TEST_STATE_BOTTOM 1
#define TEST_STATE_UP 2
#define TEST_STATE_DOWN 3

void button_touch(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surf);
void button_use(edict_t *self, edict_t *other, edict_t *activator);
void button_wait(edict_t *self);
void button_killed(edict_t *self, edict_t *inflictor, edict_t *attacker,
	int damage, vec3_t point);

game_import_t gi;
game_export_t globals;
game_locals_t game;
level_locals_t level;
edict_t *g_edicts;
sg_bot_t sg_bots[SG_MAXBOTS];

static edict_t edicts[TEST_EDICTS];
static gclient_t clients[3];
static rune_t rune_fixture;
static rune_link_t link_fixture;
static rune_mechanism_plan_t plan_fixture;
static rune_mechanism_node_t entry_node;
static rune_mechanism_node_t mover_node;
static sg_rune_mechanism_binding_t binding_fixture;
static int binding_current;
static int guard_current;
static int any_claim;
static int rider_current;
static int use_targets_count;
static edict_t *use_targets_source;
static edict_t *use_targets_activator;
static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		    __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static sg_mover_owner_t Owner(uint64_t generation, int32_t id)
{
	sg_mover_owner_t owner;

	memset(&owner, 0, sizeof(owner));
	owner.generation = generation;
	owner.id = id;
	owner.kind = SG_MOVER_OWNER_BOT;
	return owner;
}

static sg_mover_ticket_t Ticket(uint64_t epoch, uint64_t serial,
	uint16_t slot)
{
	sg_mover_ticket_t ticket;

	memset(&ticket, 0, sizeof(ticket));
	ticket.epoch = epoch;
	ticket.serial = serial;
	ticket.slot = slot;
	return ticket;
}

static void LiveEdict(edict_t *entity, int key, gclient_t *client,
	int bot_flag)
{
	memset(entity, 0, sizeof(*entity));
	entity->inuse = true;
	entity->s.number = key;
	entity->client = client;
	entity->health = 100;
	entity->movetype = MOVETYPE_WALK;
	entity->groundentity = &edicts[0];
	if (bot_flag)
		entity->flags |= FL_BOT;
	if (client)
	{
		memset(client, 0, sizeof(*client));
		client->ps.pmove.pm_type = PM_NORMAL;
	}
}

static void ResetFixture(void)
{
	edict_t *button;
	sg_bot_t *bot;

	memset(edicts, 0, sizeof(edicts));
	memset(clients, 0, sizeof(clients));
	memset(sg_bots, 0, sizeof(sg_bots));
	memset(&rune_fixture, 0, sizeof(rune_fixture));
	memset(&link_fixture, 0, sizeof(link_fixture));
	memset(&plan_fixture, 0, sizeof(plan_fixture));
	memset(&entry_node, 0, sizeof(entry_node));
	memset(&mover_node, 0, sizeof(mover_node));
	memset(&binding_fixture, 0, sizeof(binding_fixture));
	memset(&level, 0, sizeof(level));
	memset(&game, 0, sizeof(game));
	memset(&globals, 0, sizeof(globals));
	memset(&gi, 0, sizeof(gi));
	g_edicts = edicts;
	globals.edicts = edicts;
	globals.edict_size = (int)sizeof(edict_t);
	globals.num_edicts = TEST_EDICTS;
	game.maxentities = TEST_EDICTS;
	edicts[0].inuse = true;
	edicts[0].s.number = 0;
	LiveEdict(&edicts[BOT_KEY], BOT_KEY, &clients[0], 1);
	LiveEdict(&edicts[HUMAN_KEY], HUMAN_KEY, &clients[1], 0);
	LiveEdict(&edicts[RELAY_KEY], RELAY_KEY, NULL, 0);
	LiveEdict(&edicts[FOREIGN_KEY], FOREIGN_KEY, NULL, 0);

	button = &edicts[BUTTON_KEY];
	LiveEdict(button, BUTTON_KEY, NULL, 0);
	button->classname = "func_button";
	button->moveinfo.state = TEST_STATE_BOTTOM;
	button->moveinfo.speed = 40.0f;
	button->moveinfo.accel = 40.0f;
	button->moveinfo.decel = 40.0f;
	button->moveinfo.wait = 3.0f;
	VectorSet(button->moveinfo.end_origin, 0.0f, 0.0f, -2.0f);

	bot = &sg_bots[0];
	bot->active = true;
	bot->ent = &edicts[BOT_KEY];
	bot->commit_link = TEST_LINK;
	bot->declared_started = true;
	bot->declared_start_frame = 1;
	bot->compound_guard.owner = Owner(100U, 0);
	bot->compound_guard.ticket = Ticket(9U, 17U, 2U);

	plan_fixture.controller_kind = SG_MECHANISM_CONTROLLER_BUTTON_DOOR;
	link_fixture.action = RL_BUTTON_DOOR;
	link_fixture.mode = RLCM_PREOPEN;
	VectorSet(link_fixture.mechanism_anchor, 0.0f, 0.0f, -2.0f);
	entry_node.key = BUTTON_KEY;
	mover_node.key = 4U;
	binding_fixture.rune = &rune_fixture;
	binding_fixture.link = &link_fixture;
	binding_fixture.plan = &plan_fixture;
	binding_fixture.entry_node = &entry_node;
	binding_fixture.mover_node = &mover_node;
	binding_fixture.entry_entity = button;
	binding_fixture.mover_entity = &edicts[4];
	binding_fixture.link_index = TEST_LINK;
	binding_current = 1;
	guard_current = 1;
	any_claim = 0;
	rider_current = 0;
	use_targets_count = 0;
	use_targets_source = NULL;
	use_targets_activator = NULL;
	SG_ButtonExecutionLevelReset();
}

qboolean SG_OwnsBot(edict_t *entity)
{
	int index;

	for (index = 0; index < SG_MAXBOTS; index++)
		if (sg_bots[index].active && sg_bots[index].ent == entity)
			return true;
	return false;
}

rune_t *SG_Rune(void)
{
	return &rune_fixture;
}

qboolean SG_RunePhysicsCompatible(const rune_t *rune)
{
	return rune == &rune_fixture;
}

int SG_RuneMechanismBindingCapture(const rune_t *rune, uint32_t link_index,
	sg_rune_mechanism_binding_t *binding_out)
{
	if (!binding_out || rune != &rune_fixture || link_index != TEST_LINK)
		return 0;
	*binding_out = binding_fixture;
	return 1;
}

int SG_RuneMechanismBindingCaptureOwned(const rune_t *rune,
	uint32_t link_index, sg_rune_mechanism_binding_t *binding_out)
{
	return SG_RuneMechanismBindingCapture(rune, link_index, binding_out);
}

int SG_RuneMechanismBindingDoorAction(
	const sg_rune_mechanism_binding_t *binding)
{
	return binding && binding->plan == &plan_fixture;
}

int SG_RuneMechanismBindingCurrent(
	const sg_rune_mechanism_binding_t *binding)
{
	return binding_current && binding &&
	       binding->entry_entity == &edicts[BUTTON_KEY];
}

int SG_MechCatalogButtonEndpoints(uint32_t key,
	const rune_mechanism_node_t *node, const edict_t *entity,
	sg_mech_button_endpoints_t *endpoints_out)
{
	int axis;

	(void)node;
	if (!endpoints_out || key != BUTTON_KEY || entity != &edicts[BUTTON_KEY])
		return 0;
	memset(endpoints_out, 0, sizeof(*endpoints_out));
	for (axis = 0; axis < 3; axis++)
	{
		float start = entity->moveinfo.start_origin[axis] * 8.0f;
		float end = entity->moveinfo.end_origin[axis] * 8.0f;

		if (start != (float)(short)start || end != (float)(short)end)
			return 0;
		endpoints_out->start_q8[axis] = (short)start;
		endpoints_out->end_q8[axis] = (short)end;
	}
	return 1;
}

int SG_MechCatalogButtonBottomEndpoints(uint32_t key,
	const rune_mechanism_node_t *node, const edict_t *entity,
	sg_mech_button_endpoints_t *endpoints_out)
{
	return entity && entity->moveinfo.state == TEST_STATE_BOTTOM &&
	       SG_MechCatalogButtonEndpoints(key, node, entity, endpoints_out);
}

sg_compound_guard_result_t SG_DeclaredDoorGuardAuthorizeActivation(
	sg_bot_t *bot, int link_index)
{
	return guard_current && bot == &sg_bots[0] && link_index == TEST_LINK
	    ? SG_COMPOUND_GUARD_OK : SG_COMPOUND_GUARD_INVALID_ARGUMENT;
}

int SG_DeclaredDoorGuardAnyClaim(void)
{
	return any_claim;
}

sg_compound_guard_result_t SG_CompoundGuardValidate(
	sg_compound_guard_bot_t *guard,
	sg_mover_lease_record_t *record_out)
{
	(void)guard;
	if (record_out)
		memset(record_out, 0, sizeof(*record_out));
	return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
}

qboolean SG_ImmutableSupport(edict_t *entity)
{
	return entity == &edicts[0];
}

qboolean SG_LiftRider(edict_t *platform, edict_t *subject)
{
	return rider_current && platform == &edicts[BUTTON_KEY] &&
	       subject == &edicts[BOT_KEY];
}

void SG_MoverCompletionArm(edict_t *member)
{
	(void)member;
}

void SG_MoverCompletionDispatch(edict_t *member)
{
	(void)member;
}

void G_UseTargets(edict_t *source, edict_t *activator)
{
	use_targets_count++;
	use_targets_source = source;
	use_targets_activator = activator;
	if (activator == sg_bots[0].ent)
	{
		sg_bots[0].declared_triggered = true;
		sg_bots[0].declared_trigger_frame = level.framenum;
	}
}

static void PlannedTouch(void)
{
	button_touch(&edicts[BUTTON_KEY], &edicts[BOT_KEY], NULL, NULL);
}

static void TestExactDeferredCallback(void)
{
	edict_t *button;
	sg_bot_t *bot;

	ResetFixture();
	button = &edicts[BUTTON_KEY];
	bot = &sg_bots[0];
	PlannedTouch();
	CHECK(button->activator == bot->ent);
	CHECK(button->moveinfo.state == TEST_STATE_UP);
	CHECK(bot->declared_touched);
	CHECK(use_targets_count == 0);
	button_wait(button);
	CHECK(use_targets_count == 1);
	CHECK(use_targets_source == button);
	CHECK(use_targets_activator == bot->ent);
	CHECK(bot->declared_triggered);
	button_wait(button);
	CHECK(use_targets_count == 1);
}

static void TestDeferredDenials(void)
{
	edict_t *button;
	sg_bot_t *bot;
	sg_mover_owner_t original_owner;
	sg_mover_ticket_t original_ticket;
	int mutation;

	for (mutation = 0; mutation < 7; mutation++)
	{
		ResetFixture();
		button = &edicts[BUTTON_KEY];
		bot = &sg_bots[0];
		PlannedTouch();
		original_owner = bot->compound_guard.owner;
		original_ticket = bot->compound_guard.ticket;
		switch (mutation)
		{
		case 0: /* logical abort */
			bot->declared_started = false;
			break;
		case 1: /* death/inactive owner */
			bot->active = false;
			bot->ent->deadflag = DEAD_DEAD;
			break;
		case 2: /* passive client-slot incarnation reuse */
			bot->active = false;
			bot->ent->client = &clients[2];
			break;
		case 3:
			binding_current = 0;
			break;
		case 4:
			bot->compound_guard.owner.generation++;
			break;
		case 5:
			bot->compound_guard.ticket.epoch++;
			break;
		case 6:
			bot->compound_guard.ticket.serial++;
			bot->compound_guard.ticket.slot++;
			break;
		}
		button_wait(button);
		CHECK(use_targets_count == 0);
		button_wait(button);
		CHECK(use_targets_count == 0);
		/* Restore only to make any accidental mutation visible to sanitizers and
		 * keep the loop's ownership objects well formed before reset. */
		bot->compound_guard.owner = original_owner;
		bot->compound_guard.ticket = original_ticket;
	}
}

static void TestPendingTakeoverAndOrdinaryRecovery(void)
{
	edict_t *button;
	edict_t *planned;

	ResetFixture();
	button = &edicts[BUTTON_KEY];
	planned = &edicts[BOT_KEY];
	PlannedTouch();
	button_touch(button, &edicts[HUMAN_KEY], NULL, NULL);
	CHECK(button->activator == planned);
	button_use(button, NULL, &edicts[HUMAN_KEY]);
	CHECK(button->activator == planned);
	button_use(button, NULL, &edicts[RELAY_KEY]);
	CHECK(button->activator == planned);
	button->health = 7;
	button->max_health = 25;
	button->takedamage = DAMAGE_YES;
	button_killed(button, NULL, &edicts[HUMAN_KEY], 99, vec3_origin);
	CHECK(button->activator == planned);
	CHECK(button->health == 7);
	CHECK(button->takedamage == DAMAGE_YES);
	button_wait(button);
	CHECK(use_targets_count == 1);
	CHECK(use_targets_activator == planned);

	/* A genuine later ordinary event at BOTTOM retires the consumed SG
	 * tombstone and restores the unmodified stock callback path. */
	button->moveinfo.state = TEST_STATE_BOTTOM;
	button_use(button, NULL, &edicts[HUMAN_KEY]);
	CHECK(button->activator == &edicts[HUMAN_KEY]);
	button_wait(button);
	CHECK(use_targets_count == 2);
	CHECK(use_targets_activator == &edicts[HUMAN_KEY]);
}

static void TestEmptyOrdinaryAndBotProvenance(void)
{
	edict_t *button;

	ResetFixture();
	button = &edicts[BUTTON_KEY];
	button_touch(button, &edicts[HUMAN_KEY], NULL, NULL);
	button_wait(button);
	CHECK(use_targets_count == 1);
	CHECK(use_targets_activator == &edicts[HUMAN_KEY]);

	ResetFixture();
	button = &edicts[BUTTON_KEY];
	button_use(button, NULL, &edicts[RELAY_KEY]);
	button_wait(button);
	CHECK(use_targets_count == 1);
	CHECK(use_targets_activator == &edicts[RELAY_KEY]);

	/* A valid in-use bot owned by another implementation retains stock
	 * behavior. */
	ResetFixture();
	button = &edicts[BUTTON_KEY];
	sg_bots[0].active = false;
	button_touch(button, &edicts[BOT_KEY], NULL, NULL);
	button_wait(button);
	CHECK(use_targets_count == 1);
	CHECK(use_targets_activator == &edicts[BOT_KEY]);

	/* A later physical event from that same valid unowned implementation also
	 * retires a consumed tombstone before stock fanout. */
	ResetFixture();
	button = &edicts[BUTTON_KEY];
	PlannedTouch();
	button_wait(button);
	button->moveinfo.state = TEST_STATE_BOTTOM;
	sg_bots[0].active = false;
	button_touch(button, &edicts[BOT_KEY], NULL, NULL);
	button_wait(button);
	CHECK(use_targets_count == 2);
	CHECK(use_targets_activator == &edicts[BOT_KEY]);

	/* A stale disconnected SG provenance pointer cannot fan out targets after
	 * its transaction table was reset. */
	ResetFixture();
	button = &edicts[BUTTON_KEY];
	sg_bots[0].active = false;
	edicts[BOT_KEY].inuse = false;
	button_touch(button, &edicts[BOT_KEY], NULL, NULL);
	CHECK(button->activator == NULL);
	button->activator = &edicts[BOT_KEY];
	button_wait(button);
	CHECK(use_targets_count == 0);

	/* An active SG client without a declared mechanism binding keeps the old
	 * unplanned stock path when no shared claim exists. */
	ResetFixture();
	button = &edicts[BUTTON_KEY];
	sg_bots[0].commit_link = TEST_LINK + 1;
	button_touch(button, &edicts[BOT_KEY], NULL, NULL);
	button_wait(button);
	CHECK(use_targets_count == 1);
	CHECK(use_targets_activator == &edicts[BOT_KEY]);
}

static void TestBottomOnlyMintAndResetHooks(void)
{
	edict_t *button;
	int states[] = { TEST_STATE_UP, TEST_STATE_TOP, TEST_STATE_DOWN };
	size_t index;

	for (index = 0U; index < sizeof(states) / sizeof(states[0]); index++)
	{
		ResetFixture();
		button = &edicts[BUTTON_KEY];
		button->moveinfo.state = states[index];
		PlannedTouch();
		CHECK(!sg_bots[0].declared_touched);
		CHECK(button->activator == NULL);
		CHECK(button->moveinfo.state == states[index]);
		button->moveinfo.state = TEST_STATE_BOTTOM;
		PlannedTouch();
		button_wait(button);
		CHECK(use_targets_count == 1);
	}

	ResetFixture();
	button = &edicts[BUTTON_KEY];
	PlannedTouch();
	SG_ButtonExecutionEntityFreed(button);
	button_wait(button);
	CHECK(use_targets_count == 0);

	ResetFixture();
	button = &edicts[BUTTON_KEY];
	PlannedTouch();
	SG_ButtonExecutionLevelReset();
	button_wait(button);
	CHECK(use_targets_count == 0);
}

static void TestExactEntrySupport(void)
{
	edict_t *subject;
	sg_rune_mechanism_binding_t malformed;
	sg_mover_ticket_t original_ticket;

	ResetFixture();
	subject = &edicts[BOT_KEY];
	subject->groundentity = &edicts[BUTTON_KEY];
	CHECK(!SG_ButtonExecutionSupportValid(&binding_fixture,
	    &sg_bots[0], subject));

	ResetFixture();
	subject = &edicts[BOT_KEY];
	PlannedTouch();
	subject->groundentity = &edicts[BUTTON_KEY];
	CHECK(!SG_ButtonExecutionSupportValid(&binding_fixture,
	    &sg_bots[0], subject));
	subject->groundentity = &edicts[FOREIGN_KEY];
	CHECK(!SG_ButtonExecutionSupportValid(&binding_fixture,
	    &sg_bots[0], subject));
	subject->groundentity = NULL;
	rider_current = 1;
	CHECK(!SG_ButtonExecutionSupportValid(&binding_fixture,
	    &sg_bots[0], subject));
	rider_current = 0;
	CHECK(!SG_ButtonExecutionSupportValid(&binding_fixture,
	    &sg_bots[0], subject));
	subject->groundentity = &edicts[0];
	CHECK(SG_ButtonExecutionSupportValid(&binding_fixture,
	    &sg_bots[0], subject));
	binding_current = 0;
	subject->groundentity = &edicts[BUTTON_KEY];
	CHECK(!SG_ButtonExecutionSupportValid(&binding_fixture,
	    &sg_bots[0], subject));

	ResetFixture();
	subject = &edicts[BOT_KEY];
	link_fixture.mode = RLCM_RIDE;
	subject->groundentity = &edicts[BUTTON_KEY];
	PlannedTouch();
	subject->groundentity = &edicts[BUTTON_KEY];
	original_ticket = sg_bots[0].compound_guard.ticket;
	sg_bots[0].compound_guard.ticket.serial++;
	CHECK(!SG_ButtonExecutionSupportValid(&binding_fixture,
	    &sg_bots[0], subject));
	sg_bots[0].compound_guard.ticket = original_ticket;

	malformed = binding_fixture;
	malformed.plan = NULL;
	CHECK(!SG_ButtonExecutionSupportValid(&malformed, &sg_bots[0], subject));
	malformed = binding_fixture;
	malformed.entry_node = NULL;
	CHECK(!SG_ButtonExecutionSupportValid(&malformed, &sg_bots[0], subject));
	malformed = binding_fixture;
	malformed.entry_entity = &edicts[FOREIGN_KEY];
	CHECK(!SG_ButtonExecutionSupportValid(&malformed, &sg_bots[0], subject));
	malformed = binding_fixture;
	malformed.entry_node = &mover_node;
	CHECK(!SG_ButtonExecutionSupportValid(&malformed, &sg_bots[0], subject));

	/* After the one deferred callback consumes the token, the exact callback
	 * latch remains the authority for riding the owned entry through egress. */
	sg_bots[0].compound_guard.ticket = original_ticket;
	subject->groundentity = &edicts[BUTTON_KEY];
	button_wait(&edicts[BUTTON_KEY]);
	CHECK(use_targets_count == 1);
	CHECK(sg_bots[0].declared_triggered);
	CHECK(SG_ButtonExecutionSupportValid(&binding_fixture,
	    &sg_bots[0], subject));
	subject->groundentity = NULL;
	rider_current = 1;
	CHECK(SG_ButtonExecutionSupportValid(&binding_fixture,
	    &sg_bots[0], subject));
	subject->groundentity = &edicts[FOREIGN_KEY];
	CHECK(!SG_ButtonExecutionSupportValid(&binding_fixture,
	    &sg_bots[0], subject));
	binding_current = 0;
	subject->groundentity = &edicts[BUTTON_KEY];
	CHECK(!SG_ButtonExecutionSupportValid(&binding_fixture,
	    &sg_bots[0], subject));
}

static void TestExecutionAnchor(void)
{
	edict_t *button;
	edict_t *subject;
	vec3_t bottom = { 32.0f, 48.0f, 44.125f };
	vec3_t effective;
	sg_button_execution_anchor_state_t state;

	/* A world-supported contact remains STATIC even where the button's bounds
	 * geometrically overlap the player. */
	ResetFixture();
	button = &edicts[BUTTON_KEY];
	subject = &edicts[BOT_KEY];
	PlannedTouch();
	CHECK(sg_bots[0].declared_button_latched);
	CHECK(!sg_bots[0].declared_button_rider);
	button->moveinfo.state = TEST_STATE_TOP;
	VectorCopy(button->moveinfo.end_origin, button->s.origin);
	VectorCopy(button->s.origin, button->s.old_origin);
	state = SG_ButtonExecutionAnchor(&binding_fixture, &sg_bots[0], subject,
	    bottom, link_fixture.mechanism_anchor, link_fixture.mode, effective);
	CHECK(state == SG_BUTTON_EXECUTION_ANCHOR_TOP);
	CHECK(VectorCompare(effective, bottom));
	CHECK(SG_ButtonExecutionSupportValid(&binding_fixture, &sg_bots[0],
	    subject));
	subject->groundentity = button;
	CHECK(!SG_ButtonExecutionSupportValid(&binding_fixture, &sg_bots[0],
	    subject));

	/* xmap28's floor plate carries a RIDER exactly two units down. */
	ResetFixture();
	button = &edicts[BUTTON_KEY];
	subject = &edicts[BOT_KEY];
	link_fixture.mode = RLCM_RIDE;
	subject->groundentity = button;
	PlannedTouch();
	CHECK(sg_bots[0].declared_button_latched);
	CHECK(sg_bots[0].declared_button_rider);
	button->moveinfo.state = TEST_STATE_TOP;
	VectorCopy(button->moveinfo.end_origin, button->s.origin);
	VectorCopy(button->s.origin, button->s.old_origin);
	state = SG_ButtonExecutionAnchor(&binding_fixture, &sg_bots[0], subject,
	    bottom, link_fixture.mechanism_anchor, link_fixture.mode, effective);
	CHECK(state == SG_BUTTON_EXECUTION_ANCHOR_TOP);
	CHECK(effective[0] == bottom[0] && effective[1] == bottom[1] &&
	      effective[2] == 42.125f);
	CHECK(SG_ButtonExecutionSupportValid(&binding_fixture, &sg_bots[0],
	    subject));
	subject->groundentity = &edicts[0];
	CHECK(!SG_ButtonExecutionSupportValid(&binding_fixture, &sg_bots[0],
	    subject));

	/* Exact on-segment motion is recognizable for an already-latched retreat;
	 * an off-line q8 point or endpoint drift fails closed. */
	subject->groundentity = button;
	button->moveinfo.state = TEST_STATE_DOWN;
	VectorSet(button->s.origin, 0.0f, 0.0f, -1.0f);
	state = SG_ButtonExecutionAnchor(&binding_fixture, &sg_bots[0], subject,
	    bottom, link_fixture.mechanism_anchor, link_fixture.mode, effective);
	CHECK(state == SG_BUTTON_EXECUTION_ANCHOR_MOVING);
	CHECK(effective[2] == 43.125f);
	button->s.origin[0] = 0.125f;
	CHECK(SG_ButtonExecutionAnchor(&binding_fixture, &sg_bots[0], subject,
	    bottom, link_fixture.mechanism_anchor, link_fixture.mode, effective) ==
	    SG_BUTTON_EXECUTION_ANCHOR_INVALID);
	button->s.origin[0] = 0.0f;
	button->moveinfo.end_origin[2] = -2.125f;
	CHECK(SG_ButtonExecutionAnchor(&binding_fixture, &sg_bots[0], subject,
	    bottom, link_fixture.mechanism_anchor, link_fixture.mode, effective) ==
	    SG_BUTTON_EXECUTION_ANCHOR_INVALID);
	SG_ButtonExecutionActionReset(&sg_bots[0]);
	CHECK(!sg_bots[0].declared_button_latched);

	/* A distinct q8 point can be arbitrarily close to a world-scale diagonal.
	 * Literal integer collinearity must still reject it; no float residual
	 * envelope may turn the moving pose into retreat authority. */
	ResetFixture();
	button = &edicts[BUTTON_KEY];
	subject = &edicts[BOT_KEY];
	link_fixture.mode = RLCM_RIDE;
	VectorSet(button->moveinfo.start_origin, -2048.0f, -2047.875f, 0.0f);
	VectorSet(button->moveinfo.end_origin, 2047.875f, 2047.875f, 0.0f);
	VectorSet(link_fixture.mechanism_anchor, 4095.875f, 4095.75f, 0.0f);
	subject->groundentity = button;
	PlannedTouch();
	button->moveinfo.state = TEST_STATE_DOWN;
	VectorSet(button->s.origin, -2047.875f, -2047.75f, 0.0f);
	CHECK(SG_ButtonExecutionAnchor(&binding_fixture, &sg_bots[0], subject,
	    bottom, link_fixture.mechanism_anchor, link_fixture.mode, effective) ==
	    SG_BUTTON_EXECUTION_ANCHOR_INVALID);
}

int main(void)
{
	TestExactDeferredCallback();
	TestDeferredDenials();
	TestPendingTakeoverAndOrdinaryRecovery();
	TestEmptyOrdinaryAndBotProvenance();
	TestBottomOnlyMintAndResetHooks();
	TestExactEntrySupport();
	TestExecutionAnchor();
	if (failures)
	{
		fprintf(stderr, "%d button game test(s) failed\n", failures);
		return 1;
	}
	puts("button game tests passed");
	return 0;
}
