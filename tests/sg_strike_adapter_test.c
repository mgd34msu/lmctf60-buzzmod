/* Executable production-boundary probe for the live strike adapter. */
#include "slipgate/sg_strike_adapter.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
		    #condition); \
		failures++; \
	} \
} while (0)

static unsigned Bit(int slot)
{
	return (unsigned)1u << (unsigned)slot;
}

static sg_strike_frame_t Frame(float now)
{
	sg_strike_frame_t frame;
	int slot;

	memset(&frame, 0, sizeof(frame));
	frame.now = now;
	frame.own_flag_home = 1;
	frame.enemy_flag_home = 1;
	frame.carrier_slot = -1;
	for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
	{
		frame.slot[slot].enemy_flag_goal_ms = -1;
		frame.slot[slot].recover_goal_ms = -1;
		frame.slot[slot].carrier_goal_ms = -1;
	}
	return frame;
}

static void AddAttacker(sg_strike_frame_t *frame, int slot,
	uint32_t life, int tier, int enemy_goal)
{
	frame->slot[slot].present = 1;
	frame->slot[slot].alive = 1;
	frame->slot[slot].attack_eligible = 1;
	frame->slot[slot].life_id = life;
	frame->slot[slot].weapon_tier = tier;
	frame->slot[slot].enemy_flag_goal_ms = enemy_goal;
	frame->slot[slot].recover_goal_ms = enemy_goal;
	frame->slot[slot].carrier_goal_ms = enemy_goal;
}

int main(void)
{
	sg_strike_adapter_t adapter;
	sg_strike_adapter_t carrier_adapter;
	sg_strike_frame_t frames[2];
	const sg_strike_team_t *red;
	const sg_strike_team_t *blue;
	const sg_strike_frame_t *published;
	unsigned red_epoch;

	SG_StrikeAdapterReset(&adapter);
	frames[0] = Frame(10.0f);
	frames[1] = Frame(10.0f);
	AddAttacker(&frames[0], 0, 101u, 2, 900);
	AddAttacker(&frames[0], 1, 102u, 2, 1200);
	AddAttacker(&frames[0], 2, 103u, 2, 1500);
	AddAttacker(&frames[1], 8, 201u, 2, 900);
	AddAttacker(&frames[1], 9, 202u, 2, 1200);
	CHECK(SG_StrikeAdapterBeginFrame(&adapter, frames));
	red = SG_StrikeAdapterTeam(&adapter, 0);
	blue = SG_StrikeAdapterTeam(&adapter, 1);
	CHECK(red != NULL && blue != NULL);
	CHECK(adapter.frame_serial == 1u);
	CHECK(red->member_mask == (Bit(0) | Bit(1) | Bit(2)));
	CHECK(blue->member_mask == (Bit(8) | Bit(9)));
	red_epoch = red->epoch;

	/* BeginFrame copies the pre-serial snapshot; later caller mutation cannot
	 * alter what the serial bot loop consumes. */
	frames[0].slot[0].enemy_flag_goal_ms = 1;
	published = SG_StrikeAdapterFrame(&adapter, 0);
	CHECK(published != NULL && published->slot[0].enemy_flag_goal_ms == 900);

	/* A live carrier edge is converted into EGRESS by the production adapter,
	 * and the carrier receives the real CARRY duty. */
	frames[0] = Frame(10.1f);
	frames[0].own_flag_home = 1;
	frames[0].enemy_flag_home = 0;
	frames[0].carrier_slot = 0;
	AddAttacker(&frames[0], 0, 101u, 2, 900);
	frames[0].slot[0].carrying = 1;
	AddAttacker(&frames[0], 1, 102u, 2, 1200);
	AddAttacker(&frames[0], 2, 103u, 2, 1500);
	CHECK(SG_StrikeAdapterBeginFrame(&adapter, frames));
	red = SG_StrikeAdapterTeam(&adapter, 0);
	CHECK(red->phase == SG_STRIKE_EGRESS);
	CHECK(red->duty[0] == SG_STRIKE_DUTY_CARRY);
	CHECK(red->epoch == red_epoch);
	/* Slot retirement also invalidates the previous-frame carrier edge; a
	 * recycled slot must not synthesize a loss/capture event on its next life. */
	SG_StrikeAdapterForgetSlot(&adapter, 0);
	CHECK(adapter.previous[0].carrier_slot == -1);
	CHECK(adapter.frame[0].carrier_slot == -1);

	/* Loss immediately re-forms the surviving duties without waiting for a
	 * periodic clock. */
	frames[0] = Frame(10.2f);
	frames[0].enemy_flag_home = 0;
	frames[0].enemy_flag_dropped = 1;
	frames[0].carrier_slot = -1;
	AddAttacker(&frames[0], 0, 101u, 2, 900);
	AddAttacker(&frames[0], 1, 102u, 2, 1200);
	AddAttacker(&frames[0], 2, 103u, 2, 1500);
	CHECK(SG_StrikeAdapterBeginFrame(&adapter, frames));
	red = SG_StrikeAdapterTeam(&adapter, 0);
	CHECK(red->phase == SG_STRIKE_EGRESS);
	CHECK(red->duty[0] != SG_STRIKE_DUTY_CARRY);

	/* Production-boundary regression: the reducer's four-person attacker
	 * roster is already full when a fifth eligible bot takes the flag. */
	SG_StrikeAdapterReset(&carrier_adapter);
	frames[0] = Frame(20.0f);
	frames[1] = Frame(20.0f);
	for (int slot = 0; slot < 6; slot++)
		AddAttacker(&frames[0], slot, (uint32_t)(300 + slot), 2,
		    1000 + slot * 1000);
	CHECK(SG_StrikeAdapterBeginFrame(&carrier_adapter, frames));
	red = SG_StrikeAdapterTeam(&carrier_adapter, 0);
	CHECK(red->member_mask == (Bit(0) | Bit(1) | Bit(2) | Bit(3)));
	frames[0].now = 20.1f;
	frames[0].enemy_flag_home = 0;
	frames[0].carrier_slot = 5;
	frames[0].slot[5].carrying = 1;
	CHECK(SG_StrikeAdapterBeginFrame(&carrier_adapter, frames));
	red = SG_StrikeAdapterTeam(&carrier_adapter, 0);
	CHECK(red->member_mask == (Bit(0) | Bit(1) | Bit(2) | Bit(3)));
	CHECK(!SG_StrikeMember(red, 5));
	CHECK(SG_StrikeParticipant(red, 5));
	CHECK(red->duty[5] == SG_STRIKE_DUTY_CARRY);
	CHECK(red->duty[1] == SG_STRIKE_DUTY_CLEAR);
	CHECK(red->duty[0] == SG_STRIKE_DUTY_ESCORT);

	SG_StrikeAdapterForgetSlot(&adapter, 1);
	red = SG_StrikeAdapterTeam(&adapter, 0);
	CHECK((red->member_mask & Bit(1)) == 0u);
	CHECK(red->member_life[1] == 0u);

	/* An explicit level edge resets both reducers and their previous-frame
	 * event history in one adapter call. */
	frames[0] = Frame(0.0f);
	frames[1] = Frame(0.0f);
	frames[0].events = SG_STRIKE_EVENT_LEVEL_RESET;
	CHECK(SG_StrikeAdapterBeginFrame(&adapter, frames));
	red = SG_StrikeAdapterTeam(&adapter, 0);
	CHECK(red->phase == SG_STRIKE_IDLE);
	CHECK(red->epoch == 0u);

	if (failures)
		return 1;
	puts("sg_strike_adapter_test: ok");
	return 0;
}
