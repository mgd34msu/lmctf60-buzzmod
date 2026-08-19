/* Production frame adapter for the host-free strike coordinator. */
#include "slipgate/sg_strike_adapter.h"

#include <string.h>

static int StrikeAdapterTeamIndexValid(int team_index)
{
	return team_index >= 0 && team_index < 2;
}

static unsigned StrikeAdapterBit(int slot)
{
	return (unsigned)1u << (unsigned)slot;
}

static unsigned StrikeAdapterEvents(const sg_strike_frame_t *previous,
	const sg_strike_frame_t *current)
{
	unsigned events = current->events;

	if (!previous || !current)
		return events;
	if (current->carrier_slot >= 0 && previous->carrier_slot < 0)
		events |= SG_STRIKE_EVENT_PICKUP;
	if (previous->carrier_slot >= 0 && current->carrier_slot < 0)
	{
		if (current->enemy_flag_dropped || !current->enemy_flag_home)
			events |= SG_STRIKE_EVENT_CARRIER_LOSS;
		else if (current->enemy_flag_home && current->own_flag_home)
			events |= SG_STRIKE_EVENT_CAPTURE;
	}
	if (!previous->own_flag_home && current->own_flag_home)
		events |= SG_STRIKE_EVENT_FLAG_RETURN;
	return events;
}

void SG_StrikeAdapterReset(sg_strike_adapter_t *adapter)
{
	int team_index;

	if (!adapter)
		return;
	memset(adapter, 0, sizeof(*adapter));
	for (team_index = 0; team_index < 2; team_index++)
	{
		SG_StrikeReset(&adapter->team[team_index]);
		adapter->frame[team_index].carrier_slot = -1;
		adapter->previous[team_index].carrier_slot = -1;
	}
}

int SG_StrikeAdapterBeginFrame(sg_strike_adapter_t *adapter,
	const sg_strike_frame_t frames[2])
{
	sg_strike_team_t next_team[2];
	sg_strike_frame_t next_frame[2];
	int team_index;

	if (!adapter || !frames)
		return 0;
	memcpy(next_frame, frames, sizeof(next_frame));
	for (team_index = 0; team_index < 2; team_index++)
	{
		next_frame[team_index].events = StrikeAdapterEvents(
			adapter->previous_valid[team_index]
			    ? &adapter->previous[team_index] : NULL,
			&next_frame[team_index]);
		next_team[team_index] = adapter->team[team_index];
		if (!SG_StrikeStep(&next_team[team_index], &next_frame[team_index]))
			return 0;
	}
	memcpy(adapter->frame, next_frame, sizeof(adapter->frame));
	memcpy(adapter->team, next_team, sizeof(adapter->team));
	memcpy(adapter->previous, next_frame, sizeof(adapter->previous));
	for (team_index = 0; team_index < 2; team_index++)
		adapter->previous_valid[team_index] = 1;
	adapter->frame_serial++;
	if (adapter->frame_serial == 0u)
		adapter->frame_serial = 1u;
	return 1;
}

const sg_strike_team_t *SG_StrikeAdapterTeam(
	const sg_strike_adapter_t *adapter, int team_index)
{
	if (!adapter || !StrikeAdapterTeamIndexValid(team_index))
		return NULL;
	return &adapter->team[team_index];
}

const sg_strike_frame_t *SG_StrikeAdapterFrame(
	const sg_strike_adapter_t *adapter, int team_index)
{
	if (!adapter || !StrikeAdapterTeamIndexValid(team_index))
		return NULL;
	return &adapter->frame[team_index];
}

void SG_StrikeAdapterForgetSlot(sg_strike_adapter_t *adapter, int slot)
{
	int team_index;
	unsigned bit;

	if (!adapter || slot < 0 || slot >= SG_STRIKE_MAX_SLOTS)
		return;
	bit = StrikeAdapterBit(slot);
	for (team_index = 0; team_index < 2; team_index++)
	{
		sg_strike_team_t *team = &adapter->team[team_index];

		team->member_mask &= ~bit;
		team->weapon_ready_mask &= ~bit;
		team->mission_ready_mask &= ~bit;
		team->hold_mask &= ~bit;
		team->rush_mask &= ~bit;
		team->arrived_mask &= ~bit;
		team->attempt_mask &= ~bit;
		team->member_life[slot] = 0u;
		team->weapon_deadline[slot] = 0.0f;
		team->duty[slot] = SG_STRIKE_DUTY_NONE;
		if (team->carrier_slot == slot)
			team->carrier_slot = -1;
		if (adapter->frame[team_index].carrier_slot == slot)
			adapter->frame[team_index].carrier_slot = -1;
		if (adapter->previous[team_index].carrier_slot == slot)
			adapter->previous[team_index].carrier_slot = -1;
		adapter->frame[team_index].slot[slot] =
			(sg_strike_slot_input_t){ 0 };
		adapter->previous[team_index].slot[slot] =
			(sg_strike_slot_input_t){ 0 };
	}
}
