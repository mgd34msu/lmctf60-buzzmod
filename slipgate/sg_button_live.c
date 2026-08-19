/* sg_button_live.c -- host-free policy for a declared button callback. */
#include "sg_button_live.h"

#include <string.h>

static int ButtonOwnerValid(const sg_mover_owner_t *owner)
{
	return owner && owner->generation != 0U && owner->id >= 0 &&
	       owner->kind == SG_MOVER_OWNER_BOT && owner->reserved[0] == 0U &&
	       owner->reserved[1] == 0U && owner->reserved[2] == 0U;
}

static int ButtonOwnerEqual(const sg_mover_owner_t *first,
	const sg_mover_owner_t *second)
{
	return first && second && first->generation == second->generation &&
	       first->id == second->id && first->kind == second->kind;
}

static int ButtonTicketValid(const sg_mover_ticket_t *ticket)
{
	return ticket && ticket->epoch != 0U && ticket->serial != 0U &&
	       ticket->slot < SG_MOVER_LEASE_MAX_RECORDS &&
	       ticket->reserved == 0U;
}

static int ButtonTicketEqual(const sg_mover_ticket_t *first,
	const sg_mover_ticket_t *second)
{
	return first && second && first->epoch == second->epoch &&
	       first->serial == second->serial && first->slot == second->slot;
}

static int ButtonReservedZero(const uint8_t *reserved, size_t count)
{
	size_t index;

	if (!reserved)
		return 0;
	for (index = 0U; index < count; index++)
		if (reserved[index] != 0U)
			return 0;
	return 1;
}

static int ButtonSourceKeyValid(uint32_t source_key)
{
	return source_key > 0U &&
	       source_key < SG_BUTTON_CALLBACK_SOURCE_CAPACITY;
}

static int ButtonTokenEmpty(const sg_button_callback_token_t *token)
{
	sg_button_callback_token_t empty;

	if (!token)
		return 0;
	memset(&empty, 0, sizeof(empty));
	return memcmp(token, &empty, sizeof(empty)) == 0;
}

static int ButtonTokenPendingValid(const sg_button_callback_token_t *token)
{
	return token && token->active == 1U && token->consumed == 0U &&
	       ButtonReservedZero(token->reserved, sizeof(token->reserved)) &&
	       ButtonSourceKeyValid(token->source_key) &&
	       token->activator_key > 0 && token->link_index >= 0 &&
	       ButtonOwnerValid(&token->owner) &&
	       ButtonTicketValid(&token->ticket);
}

static int ButtonTokenConsumedValid(const sg_button_callback_token_t *token)
{
	sg_button_callback_token_t expected;

	if (!token || token->active != 0U || token->consumed != 1U ||
	    !ButtonSourceKeyValid(token->source_key) ||
	    !ButtonReservedZero(token->reserved, sizeof(token->reserved)))
		return 0;
	memset(&expected, 0, sizeof(expected));
	expected.source_key = token->source_key;
	expected.consumed = 1U;
	return memcmp(token, &expected, sizeof(expected)) == 0;
}

static void ButtonTokenConsumeTombstone(sg_button_callback_token_t *token,
	uint32_t source_key)
{
	SG_ButtonCallbackTokenReset(token);
	if (token && ButtonSourceKeyValid(source_key))
	{
		token->source_key = source_key;
		token->consumed = 1U;
	}
}

sg_button_callback_state_t SG_ButtonCallbackTokenState(
	const sg_button_callback_token_t *token)
{
	if (ButtonTokenEmpty(token))
		return SG_BUTTON_CALLBACK_EMPTY;
	if (ButtonTokenPendingValid(token))
		return SG_BUTTON_CALLBACK_PENDING;
	if (ButtonTokenConsumedValid(token))
		return SG_BUTTON_CALLBACK_CONSUMED;
	return SG_BUTTON_CALLBACK_MALFORMED;
}

void SG_ButtonCallbackTokenReset(sg_button_callback_token_t *token)
{
	if (token)
		memset(token, 0, sizeof(*token));
}

int SG_ButtonCallbackTokenBegin(sg_button_callback_token_t *token,
	uint32_t source_key, int32_t activator_key, int32_t link_index,
	const sg_mover_owner_t *owner, const sg_mover_ticket_t *ticket)
{
	if (!token || !ButtonSourceKeyValid(source_key) || activator_key <= 0 ||
	    link_index < 0 ||
	    !ButtonOwnerValid(owner) || !ButtonTicketValid(ticket))
		return 0;
	if (SG_ButtonCallbackTokenState(token) == SG_BUTTON_CALLBACK_PENDING)
	{
		return token->source_key == source_key &&
		       token->activator_key == activator_key &&
		       token->link_index == link_index &&
		       ButtonOwnerEqual(&token->owner, owner) &&
		       ButtonTicketEqual(&token->ticket, ticket);
	}
	if (SG_ButtonCallbackTokenState(token) != SG_BUTTON_CALLBACK_EMPTY)
		return 0;
	SG_ButtonCallbackTokenReset(token);
	token->source_key = source_key;
	token->activator_key = activator_key;
	token->link_index = link_index;
	token->owner = *owner;
	token->ticket = *ticket;
	token->active = 1U;
	return 1;
}

int SG_ButtonCallbackTokenOwnsSource(
	const sg_button_callback_token_t *token, uint32_t source_key)
{
	return ButtonTokenPendingValid(token) &&
	       token->source_key == source_key;
}

int SG_ButtonCallbackTokenMatchesPending(
	const sg_button_callback_token_t *token, uint32_t source_key,
	int32_t activator_key, int32_t link_index,
	const sg_mover_owner_t *owner, const sg_mover_ticket_t *ticket)
{
	return SG_ButtonCallbackTokenOwnsSource(token, source_key) &&
	       token->activator_key == activator_key &&
	       token->link_index == link_index && ButtonOwnerValid(owner) &&
	       ButtonTicketValid(ticket) &&
	       ButtonOwnerEqual(&token->owner, owner) &&
	       ButtonTicketEqual(&token->ticket, ticket);
}

int SG_ButtonCallbackTokenBlocksSource(
	const sg_button_callback_token_t *token, uint32_t source_key)
{
	return source_key == (token ? token->source_key : 0U) &&
	       (ButtonTokenPendingValid(token) ||
	        ButtonTokenConsumedValid(token));
}

int SG_ButtonCallbackTokenOrdinaryEvent(
	sg_button_callback_token_t *token, uint32_t source_key)
{
	sg_button_callback_state_t state = SG_ButtonCallbackTokenState(token);

	if (state == SG_BUTTON_CALLBACK_EMPTY)
		return 1;
	if (state != SG_BUTTON_CALLBACK_CONSUMED ||
	    !SG_ButtonCallbackTokenBlocksSource(token, source_key))
		return 0;
	SG_ButtonCallbackTokenReset(token);
	return 1;
}

int SG_ButtonCallbackTokenPlannedTouchAllowed(
	const sg_button_callback_token_t *token, uint32_t source_key,
	int at_bottom)
{
	sg_button_callback_state_t state = SG_ButtonCallbackTokenState(token);

	if (!ButtonSourceKeyValid(source_key))
		return 0;
	if (state == SG_BUTTON_CALLBACK_EMPTY)
		return at_bottom != 0;
	if (state == SG_BUTTON_CALLBACK_PENDING)
		return SG_ButtonCallbackTokenOwnsSource(token, source_key);
	if (state == SG_BUTTON_CALLBACK_CONSUMED)
		return at_bottom != 0 &&
		       SG_ButtonCallbackTokenBlocksSource(token, source_key);
	return 0;
}

sg_button_callback_result_t SG_ButtonCallbackTokenConsume(
	sg_button_callback_token_t *token, uint32_t source_key,
	int32_t activator_key, int32_t link_index,
	const sg_mover_owner_t *owner, const sg_mover_ticket_t *ticket,
	int authority_current)
{
	int exact;

	if (SG_ButtonCallbackTokenState(token) == SG_BUTTON_CALLBACK_EMPTY)
		return SG_BUTTON_CALLBACK_PASSTHROUGH;
	/* The adapter passes the table slot selected by source_key.  Corruption,
	 * wrong-source reuse, and callback replay all fail closed at that slot. */
	if (!ButtonTokenPendingValid(token) || token->source_key != source_key)
	{
		ButtonTokenConsumeTombstone(token, source_key);
		return SG_BUTTON_CALLBACK_DENY;
	}
	exact = SG_ButtonCallbackTokenMatchesPending(token, source_key,
	    activator_key, link_index, owner, ticket) && authority_current;
	ButtonTokenConsumeTombstone(token, source_key);
	return exact ? SG_BUTTON_CALLBACK_AUTHORIZE : SG_BUTTON_CALLBACK_DENY;
}
