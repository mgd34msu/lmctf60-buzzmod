/* sg_button_live.h -- host-free policy for a declared button callback. */
#ifndef SG_BUTTON_LIVE_H
#define SG_BUTTON_LIVE_H

#include <stdint.h>

#include "sg_mover_lease.h"

/* Quake II protocol edict keys are 10-bit.  The game adapter asserts this
 * against MAX_EDICTS where it indexes the per-source table. */
#define SG_BUTTON_CALLBACK_SOURCE_CAPACITY 1024U

typedef struct sg_button_callback_token_s
{
	uint32_t source_key;
	int32_t activator_key;
	int32_t link_index;
	sg_mover_owner_t owner;
	sg_mover_ticket_t ticket;
	uint8_t active;
	uint8_t consumed;
	uint8_t reserved[6];
} sg_button_callback_token_t;

typedef enum sg_button_callback_result_e
{
	SG_BUTTON_CALLBACK_PASSTHROUGH = 0,
	SG_BUTTON_CALLBACK_DENY,
	SG_BUTTON_CALLBACK_AUTHORIZE
} sg_button_callback_result_t;

typedef enum sg_button_callback_state_e
{
	SG_BUTTON_CALLBACK_EMPTY = 0,
	SG_BUTTON_CALLBACK_PENDING,
	SG_BUTTON_CALLBACK_CONSUMED,
	SG_BUTTON_CALLBACK_MALFORMED
} sg_button_callback_state_t;

void SG_ButtonCallbackTokenReset(sg_button_callback_token_t *token);
sg_button_callback_state_t SG_ButtonCallbackTokenState(
	const sg_button_callback_token_t *token);

/* Begin is idempotent only for the same authenticated transaction.  A second
 * owner cannot replace a button callback which is already in flight. */
int SG_ButtonCallbackTokenBegin(sg_button_callback_token_t *token,
	uint32_t source_key, int32_t activator_key, int32_t link_index,
	const sg_mover_owner_t *owner, const sg_mover_ticket_t *ticket);

int SG_ButtonCallbackTokenOwnsSource(
	const sg_button_callback_token_t *token, uint32_t source_key);
int SG_ButtonCallbackTokenMatchesPending(
	const sg_button_callback_token_t *token, uint32_t source_key,
	int32_t activator_key, int32_t link_index,
	const sg_mover_owner_t *owner, const sg_mover_ticket_t *ticket);

/* Includes the terminal one-callback tombstone.  The adapter uses this to
 * deny replay until a genuine new physical activation or level reset. */
int SG_ButtonCallbackTokenBlocksSource(
	const sg_button_callback_token_t *token, uint32_t source_key);

/* Policy for an actual non-SG touch/use event.  Empty is ordinary stock;
 * PENDING and malformed states deny takeover; CONSUMED is retired by this
 * genuine later event and stock behavior resumes. */
int SG_ButtonCallbackTokenOrdinaryEvent(
	sg_button_callback_token_t *token, uint32_t source_key);

/* A planned physical touch can begin only from the sealed BOTTOM pose.  The
 * same pending transaction may observe repeated touches while the button is
 * moving; a consumed tombstone can begin a later transaction only after the
 * button has physically returned to BOTTOM. */
int SG_ButtonCallbackTokenPlannedTouchAllowed(
	const sg_button_callback_token_t *token, uint32_t source_key,
	int at_bottom);

/* A source callback consumes its token exactly once.  SG callbacks are
 * authorized only when the complete transaction identity and current
 * authority still match.  A different activator observed only at the deferred
 * callback cannot replace a pending SG transaction. */
sg_button_callback_result_t SG_ButtonCallbackTokenConsume(
	sg_button_callback_token_t *token, uint32_t source_key,
	int32_t activator_key, int32_t link_index,
	const sg_mover_owner_t *owner, const sg_mover_ticket_t *ticket,
	int authority_current);

#endif /* SG_BUTTON_LIVE_H */
