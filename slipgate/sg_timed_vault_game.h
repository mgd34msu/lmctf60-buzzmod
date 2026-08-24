/* sg_timed_vault_game.h -- host-free adapter for stock timed-vault events. */
#ifndef SG_TIMED_VAULT_GAME_H
#define SG_TIMED_VAULT_GAME_H

#include <stdint.h>

#include "sg_delayed_use_ticket.h"
#include "sg_timed_vault_transaction.h"

typedef enum sg_timed_vault_game_begin_result_e
{
	SG_TIMED_VAULT_GAME_REJECTED = 0,
	SG_TIMED_VAULT_GAME_BYPASS,
	SG_TIMED_VAULT_GAME_ACTIVE
} sg_timed_vault_game_begin_result_t;

typedef struct sg_timed_vault_game_spec_s
{
	sg_timed_vault_spec_t transaction;
	uint32_t link_index;
	uint32_t short_relay_generation;
	uint32_t restore_relay_generation;
	uint64_t activator_identity;
	uint64_t short_ticket_id;
	uint64_t restore_ticket_id;
} sg_timed_vault_game_spec_t;

typedef struct sg_timed_vault_game_observation_s
{
	uint32_t frame;
	uint32_t binding_source_key;
	uint32_t binding_fanout_identity;
	uint8_t alive;
	uint8_t connected;
	uint8_t binding_current;
	uint8_t door_top_count;
	uint8_t body_clear;
	uint8_t bot_controlled;
} sg_timed_vault_game_observation_t;

typedef struct sg_timed_vault_game_state_s
{
	sg_timed_vault_game_spec_t spec;
	sg_timed_vault_state_t transaction;
	sg_delayed_use_ticket_t short_ticket;
	sg_delayed_use_ticket_t restore_ticket;
	sg_timed_vault_command_t command;
} sg_timed_vault_game_state_t;

sg_timed_vault_game_begin_result_t SG_TimedVaultGameBegin(
	sg_timed_vault_game_state_t *state,
	const sg_timed_vault_game_spec_t *spec,
	const sg_timed_vault_game_observation_t *observation);
int SG_TimedVaultGameDispatch(sg_timed_vault_game_state_t *state,
	const sg_timed_vault_game_observation_t *observation);
int SG_TimedVaultGameShortRelay(sg_timed_vault_game_state_t *state,
	const sg_delayed_use_ticket_observation_t *ticket_observation,
	const sg_timed_vault_game_observation_t *observation);
sg_timed_vault_command_t SG_TimedVaultGameFlagPickup(
	sg_timed_vault_game_state_t *state,
	const sg_timed_vault_game_observation_t *observation);
sg_timed_vault_command_t SG_TimedVaultGameStep(
	sg_timed_vault_game_state_t *state,
	const sg_timed_vault_game_observation_t *observation);
int SG_TimedVaultGameRestoreRelay(sg_timed_vault_game_state_t *state,
	const sg_delayed_use_ticket_observation_t *ticket_observation,
	const sg_timed_vault_game_observation_t *observation);
int SG_TimedVaultGameRetireActivator(sg_timed_vault_game_state_t *state,
	uint64_t activator_identity);
sg_timed_vault_command_t SG_TimedVaultGameCommand(
	const sg_timed_vault_game_state_t *state);

#endif /* SG_TIMED_VAULT_GAME_H */
