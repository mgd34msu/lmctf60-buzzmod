#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_client_ownership.h"
#include "slipgate/sg_compact_runtime_level.h"
#include "slipgate/sg_rune_compact_production.h"
#include "slipgate/sg_tactic_execution_owner_private.h"

#include <stdio.h>
#include <string.h>

sg_bot_t sg_bots[SG_MAXBOTS];

static edict_t entities[3];
static gclient_t clients[3];
static unsigned char owner_storage;
static int owner_available;
static uint32_t cancel_count;
static sg_localization_subject_t cancelled_subject;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		return 0; \
	} \
} while (0)

qboolean SG_OwnsBot(edict_t *entity)
{
	int slot;

	if (!entity || (entity->flags & FL_BOT) == 0)
		return false;
	for (slot = 0; slot < SG_MAXBOTS; slot++)
		if (sg_bots[slot].active && sg_bots[slot].ent == entity)
			return true;
	return false;
}

sg_tactic_execution_owner_t *SG_CompactRuntimeLevelExecutionOwner(
	sg_compact_runtime_level_t *runtime)
{
	(void)runtime;
	return owner_available ?
		(sg_tactic_execution_owner_t *)(void *)&owner_storage : NULL;
}

void SG_TacticExecutionOwnerCancelSubject(sg_tactic_execution_owner_t *owner,
	const sg_localization_subject_t *subject)
{
	if (!owner || !subject)
		return;
	cancel_count++;
	cancelled_subject = *subject;
}

#ifdef SG_TACTIC_HOST_LIFECYCLE_IMPLEMENTATION
static sg_rune_compact_production_t sg_compact_production =
	SG_RUNE_COMPACT_PRODUCTION_INITIALIZER;
#include "sg_tactic_host_lifecycle_implementation.inc"
#endif

static void ResetFixture(void)
{
	memset(sg_bots, 0, sizeof(sg_bots));
	memset(entities, 0, sizeof(entities));
	memset(clients, 0, sizeof(clients));
	memset(&cancelled_subject, 0, sizeof(cancelled_subject));
	owner_available = 1;
	cancel_count = 0U;
	entities[1].client = &clients[1];
	entities[1].flags = FL_BOT;
	entities[1].s.number = 1;
	entities[1].client->ctf.ctfid = UINT64_C(91);
	sg_bots[4].active = true;
	sg_bots[4].ent = &entities[1];
	sg_bots[4].localization_subject.client_id = 1U;
	sg_bots[4].localization_subject.spawn_generation = UINT64_C(91);
}

static int TestCurrentLifeFence(void)
{
	ResetFixture();
	SG_CancelCurrentBotTacticLife(&entities[1]);
	CHECK(cancel_count == 1U);
	CHECK(cancelled_subject.client_id == 1U);
	CHECK(cancelled_subject.spawn_generation == UINT64_C(91));

	/* A duplicate host fence asks for the same exact subject. The real owner
	 * consumes the first matching pending slot and makes this second call a
	 * no-op; the bridge must never broaden the subject between calls. */
	SG_CancelCurrentBotTacticLife(&entities[1]);
	CHECK(cancel_count == 2U);
	CHECK(cancelled_subject.client_id == 1U);
	CHECK(cancelled_subject.spawn_generation == UINT64_C(91));
	return 1;
}

static int TestStaleLifeAndHumanNoOp(void)
{
	ResetFixture();
	entities[1].client->ctf.ctfid++;
	SG_CancelCurrentBotTacticLife(&entities[1]);
	CHECK(cancel_count == 0U);
	entities[1].client->ctf.ctfid = UINT64_C(91);
	sg_bots[4].localization_subject.client_id = 2U;
	SG_CancelCurrentBotTacticLife(&entities[1]);
	CHECK(cancel_count == 0U);
	entities[1].s.number = 0;
	sg_bots[4].localization_subject.client_id = 0U;
	SG_CancelCurrentBotTacticLife(&entities[1]);
	CHECK(cancel_count == 0U);

	ResetFixture();
	entities[1].flags &= ~FL_BOT;
	SG_CancelCurrentBotTacticLife(&entities[1]);
	CHECK(cancel_count == 0U);
	SG_CancelCurrentBotTacticLife(&entities[2]);
	CHECK(cancel_count == 0U);
	return 1;
}

static int TestTrustedSlotRetirement(void)
{
	sg_bot_t foreign;

	ResetFixture();
	/* Slot retirement must not depend on an edict that was already cleared or
	 * replaced by a human. The stored subject is the retirement authority. */
	entities[1].flags &= ~FL_BOT;
	entities[1].client = NULL;
	sg_bots[4].active = false;
	sg_bots[4].ent = NULL;
	SG_CancelBotSlotTacticLife(&sg_bots[4]);
	CHECK(cancel_count == 1U);
	CHECK(cancelled_subject.client_id == 1U);
	CHECK(cancelled_subject.spawn_generation == UINT64_C(91));

	memset(&foreign, 0, sizeof(foreign));
	foreign.localization_subject = sg_bots[4].localization_subject;
	SG_CancelBotSlotTacticLife(&foreign);
	CHECK(cancel_count == 1U);
	sg_bots[4].localization_subject.reserved = 1U;
	SG_CancelBotSlotTacticLife(&sg_bots[4]);
	CHECK(cancel_count == 1U);
	sg_bots[4].localization_subject.reserved = 0U;
	sg_bots[4].localization_subject.client_id = 0U;
	SG_CancelBotSlotTacticLife(&sg_bots[4]);
	CHECK(cancel_count == 1U);
	owner_available = 0;
	sg_bots[4].localization_subject.client_id = 1U;
	SG_CancelBotSlotTacticLife(&sg_bots[4]);
	CHECK(cancel_count == 1U);
	return 1;
}

int main(void)
{
	CHECK(TestCurrentLifeFence());
	CHECK(TestStaleLifeAndHumanNoOp());
	CHECK(TestTrustedSlotRetirement());
	puts("tactic host lifecycle checks passed");
	return 0;
}
