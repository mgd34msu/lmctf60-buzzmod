#include <stdio.h>
#include <string.h>

#include "slipgate/sg_button_live.h"

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

static void TestExactCallback(void)
{
	sg_button_callback_token_t token;
	sg_mover_owner_t owner = Owner(11U, 2);
	sg_mover_ticket_t ticket = Ticket(7U, 19U, 3U);

	memset(&token, 0, sizeof(token));
	CHECK(SG_ButtonCallbackTokenBegin(&token, 33U, 5, 22888,
	    &owner, &ticket));
	CHECK(SG_ButtonCallbackTokenOwnsSource(&token, 33U));
	/* The same physical touch can be observed again while button_fire is
	 * already moving, but it cannot replace the transaction. */
	CHECK(SG_ButtonCallbackTokenBegin(&token, 33U, 5, 22888,
	    &owner, &ticket));
	CHECK(SG_ButtonCallbackTokenConsume(&token, 33U, 5, 22888,
	    &owner, &ticket, 1) == SG_BUTTON_CALLBACK_AUTHORIZE);
	CHECK(!token.active && token.consumed);
	CHECK(SG_ButtonCallbackTokenConsume(&token, 33U, 5, 22888,
	    &owner, &ticket, 1) == SG_BUTTON_CALLBACK_DENY);
	CHECK(!token.active && token.consumed);
}

static void TestPhysicalEventLifecycle(void)
{
	sg_button_callback_token_t token;
	sg_mover_owner_t owner = Owner(15U, 4);
	sg_mover_ticket_t ticket = Ticket(6U, 17U, 2U);

	memset(&token, 0, sizeof(token));
	CHECK(SG_ButtonCallbackTokenState(&token) == SG_BUTTON_CALLBACK_EMPTY);
	CHECK(SG_ButtonCallbackTokenOrdinaryEvent(&token, 33U));
	CHECK(SG_ButtonCallbackTokenState(&token) == SG_BUTTON_CALLBACK_EMPTY);
	CHECK(!SG_ButtonCallbackTokenPlannedTouchAllowed(&token, 33U, 0));
	CHECK(SG_ButtonCallbackTokenPlannedTouchAllowed(&token, 33U, 1));
	CHECK(SG_ButtonCallbackTokenBegin(&token, 33U, 5, 22888,
	    &owner, &ticket));
	CHECK(SG_ButtonCallbackTokenState(&token) == SG_BUTTON_CALLBACK_PENDING);
	/* An in-flight declared callback cannot be overwritten by a human or
	 * relay, but repeated touches from the exact pending transaction remain
	 * admissible after the button has left BOTTOM. */
	CHECK(!SG_ButtonCallbackTokenOrdinaryEvent(&token, 33U));
	CHECK(!SG_ButtonCallbackTokenOrdinaryEvent(&token, 40U));
	CHECK(SG_ButtonCallbackTokenPlannedTouchAllowed(&token, 33U, 0));
	CHECK(!SG_ButtonCallbackTokenPlannedTouchAllowed(&token, 40U, 1));
	CHECK(SG_ButtonCallbackTokenConsume(&token, 33U, 5, 22888,
	    &owner, &ticket, 1) == SG_BUTTON_CALLBACK_AUTHORIZE);
	CHECK(SG_ButtonCallbackTokenState(&token) == SG_BUTTON_CALLBACK_CONSUMED);
	/* A tombstone cannot rearm while DOWN/UP/TOP. */
	CHECK(!SG_ButtonCallbackTokenPlannedTouchAllowed(&token, 33U, 0));
	CHECK(SG_ButtonCallbackTokenPlannedTouchAllowed(&token, 33U, 1));
	CHECK(!SG_ButtonCallbackTokenOrdinaryEvent(&token, 40U));
	CHECK(SG_ButtonCallbackTokenOrdinaryEvent(&token, 33U));
	CHECK(SG_ButtonCallbackTokenState(&token) == SG_BUTTON_CALLBACK_EMPTY);
}

static void TestAbortAndInactiveOwner(void)
{
	sg_button_callback_token_t token;
	sg_mover_owner_t owner = Owner(21U, 1);
	sg_mover_ticket_t ticket = Ticket(8U, 20U, 4U);

	memset(&token, 0, sizeof(token));
	CHECK(SG_ButtonCallbackTokenBegin(&token, 40U, 6, 22945,
	    &owner, &ticket));
	/* Logical abort/release before button_wait cannot downgrade to stock. */
	CHECK(SG_ButtonCallbackTokenConsume(&token, 40U, 6, 22945,
	    &owner, &ticket, 0) == SG_BUTTON_CALLBACK_DENY);
	CHECK(!token.active && token.consumed);

	SG_ButtonCallbackTokenReset(&token);
	CHECK(SG_ButtonCallbackTokenBegin(&token, 40U, 6, 22945,
	    &owner, &ticket));
	/* Death, disconnect, or slot reset supplies no current owner identity. */
	CHECK(SG_ButtonCallbackTokenConsume(&token, 40U, 6, -1,
	    NULL, NULL, 0) == SG_BUTTON_CALLBACK_DENY);
	CHECK(!token.active && token.consumed);
}

static void TestMismatchOverwriteAndIncarnation(void)
{
	sg_button_callback_token_t token;
	sg_mover_owner_t owner = Owner(31U, 0);
	sg_mover_owner_t other_owner = Owner(32U, 0);
	sg_mover_ticket_t ticket = Ticket(9U, 30U, 5U);
	sg_mover_ticket_t other_ticket = Ticket(9U, 31U, 5U);

	memset(&token, 0, sizeof(token));
	CHECK(SG_ButtonCallbackTokenBegin(&token, 33U, 1, 100,
	    &owner, &ticket));
	CHECK(!SG_ButtonCallbackTokenBegin(&token, 33U, 2, 101,
	    &other_owner, &other_ticket));
	CHECK(SG_ButtonCallbackTokenOwnsSource(&token, 33U));
	/* Same source with a different activator is the one callback boundary:
	 * deny it and consume the old token. */
	CHECK(SG_ButtonCallbackTokenConsume(&token, 33U, 2, 100,
	    &owner, &ticket, 1) == SG_BUTTON_CALLBACK_DENY);
	CHECK(!token.active && token.consumed);

	SG_ButtonCallbackTokenReset(&token);
	CHECK(SG_ButtonCallbackTokenBegin(&token, 33U, 1, 100,
	    &owner, &ticket));
	CHECK(SG_ButtonCallbackTokenConsume(&token, 33U, 1, 100,
	    &other_owner, &other_ticket, 1) == SG_BUTTON_CALLBACK_DENY);
	CHECK(!token.active && token.consumed);

	/* A level reset or rejected edict incarnation invalidates the record before
	 * the replacement source may begin its own transaction. */
	SG_ButtonCallbackTokenReset(&token);
	CHECK(SG_ButtonCallbackTokenBegin(&token, 33U, 1, 100,
	    &owner, &ticket));
	SG_ButtonCallbackTokenReset(&token);
	CHECK(!token.active);
	CHECK(SG_ButtonCallbackTokenBegin(&token, 33U, 2, 101,
	    &other_owner, &other_ticket));
	SG_ButtonCallbackTokenReset(&token);
}

static void TestRotatedAuthorityAndWrongSource(void)
{
	sg_button_callback_token_t token;
	sg_mover_owner_t owner = Owner(51U, 2);
	sg_mover_owner_t rotated_owner = owner;
	sg_mover_ticket_t ticket = Ticket(11U, 50U, 7U);
	sg_mover_ticket_t rotated_ticket = ticket;

	memset(&token, 0, sizeof(token));
	CHECK(SG_ButtonCallbackTokenBegin(&token, 33U, 5, 200,
	    &owner, &ticket));
	/* The adapter selects the callback-source table slot.  A corrupted record
	 * naming another source denies and leaves a tombstone for the actual
	 * callback key. */
	CHECK(SG_ButtonCallbackTokenConsume(&token, 40U, 5, 200,
	    &owner, &ticket, 1) == SG_BUTTON_CALLBACK_DENY);
	CHECK(SG_ButtonCallbackTokenState(&token) == SG_BUTTON_CALLBACK_CONSUMED);
	CHECK(SG_ButtonCallbackTokenBlocksSource(&token, 40U));
	CHECK(!SG_ButtonCallbackTokenBlocksSource(&token, 33U));

	rotated_owner.generation++;
	SG_ButtonCallbackTokenReset(&token);
	CHECK(SG_ButtonCallbackTokenBegin(&token, 33U, 5, 200,
	    &owner, &ticket));
	CHECK(SG_ButtonCallbackTokenConsume(&token, 33U, 5, 200,
	    &rotated_owner, &ticket, 1) == SG_BUTTON_CALLBACK_DENY);

	rotated_ticket.epoch++;
	SG_ButtonCallbackTokenReset(&token);
	CHECK(SG_ButtonCallbackTokenBegin(&token, 33U, 5, 200,
	    &owner, &ticket));
	CHECK(SG_ButtonCallbackTokenConsume(&token, 33U, 5, 200,
	    &owner, &rotated_ticket, 1) == SG_BUTTON_CALLBACK_DENY);

	rotated_ticket = ticket;
	rotated_ticket.serial++;
	SG_ButtonCallbackTokenReset(&token);
	CHECK(SG_ButtonCallbackTokenBegin(&token, 33U, 5, 200,
	    &owner, &ticket));
	CHECK(SG_ButtonCallbackTokenConsume(&token, 33U, 5, 200,
	    &owner, &rotated_ticket, 1) == SG_BUTTON_CALLBACK_DENY);

	rotated_ticket = ticket;
	rotated_ticket.slot++;
	SG_ButtonCallbackTokenReset(&token);
	CHECK(SG_ButtonCallbackTokenBegin(&token, 33U, 5, 200,
	    &owner, &ticket));
	CHECK(SG_ButtonCallbackTokenConsume(&token, 33U, 5, 200,
	    &owner, &rotated_ticket, 1) == SG_BUTTON_CALLBACK_DENY);
}

static void TestMalformedIdentity(void)
{
	sg_button_callback_token_t token;
	sg_mover_owner_t owner = Owner(41U, 3);
	sg_mover_ticket_t ticket = Ticket(10U, 40U, 6U);

	memset(&token, 0, sizeof(token));
	CHECK(!SG_ButtonCallbackTokenBegin(NULL, 33U, 1, 1,
	    &owner, &ticket));
	CHECK(!SG_ButtonCallbackTokenBegin(&token, 0U, 1, 1,
	    &owner, &ticket));
	CHECK(!SG_ButtonCallbackTokenBegin(&token,
	    SG_BUTTON_CALLBACK_SOURCE_CAPACITY, 1, 1, &owner, &ticket));
	owner.generation = 0U;
	CHECK(!SG_ButtonCallbackTokenBegin(&token, 33U, 1, 1,
	    &owner, &ticket));
	owner = Owner(41U, 3);
	ticket.serial = 0U;
	CHECK(!SG_ButtonCallbackTokenBegin(&token, 33U, 1, 1,
	    &owner, &ticket));

	owner = Owner(41U, 3);
	ticket = Ticket(10U, 40U, 6U);
	CHECK(SG_ButtonCallbackTokenBegin(&token, 33U, 1, 1,
	    &owner, &ticket));
	token.owner.reserved[0] = 1U;
	CHECK(SG_ButtonCallbackTokenState(&token) == SG_BUTTON_CALLBACK_MALFORMED);
	CHECK(!SG_ButtonCallbackTokenOrdinaryEvent(&token, 33U));
	CHECK(SG_ButtonCallbackTokenConsume(&token, 33U, 1, 1,
	    &owner, &ticket, 1) == SG_BUTTON_CALLBACK_DENY);
	CHECK(!token.active && token.consumed);
	SG_ButtonCallbackTokenReset(&token);
	CHECK(SG_ButtonCallbackTokenBegin(&token, 33U, 1, 1,
	    &owner, &ticket));
	token.active = 2U;
	CHECK(SG_ButtonCallbackTokenConsume(&token, 33U, 1, 1,
	    &owner, &ticket, 1) == SG_BUTTON_CALLBACK_DENY);
	CHECK(!token.active && token.consumed);
}

int main(void)
{
	TestExactCallback();
	TestPhysicalEventLifecycle();
	TestAbortAndInactiveOwner();
	TestMismatchOverwriteAndIncarnation();
	TestRotatedAuthorityAndWrongSource();
	TestMalformedIdentity();
	if (failures)
	{
		fprintf(stderr, "%d button-live test(s) failed\n", failures);
		return 1;
	}
	puts("button-live tests passed");
	return 0;
}
