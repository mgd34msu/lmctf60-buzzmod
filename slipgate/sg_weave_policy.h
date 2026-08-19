#ifndef SG_WEAVE_POLICY_H
#define SG_WEAVE_POLICY_H

#include <math.h>
#include <stdint.h>

/* The dodge clock belongs to the admitted bot life, never to its recyclable
 * client slot or the squad-wide level clock. */
static uint64_t
SG_WeaveIdentityMix(uint64_t bot_instance, unsigned long client_ctfid)
{
	uint64_t value = bot_instance ^
	    ((uint64_t)client_ctfid + UINT64_C(0x9e3779b97f4a7c15));

	value ^= value >> 30;
	value *= UINT64_C(0xbf58476d1ce4e5b9);
	value ^= value >> 27;
	value *= UINT64_C(0x94d049bb133111eb);
	value ^= value >> 31;
	return value;
}

static float
SG_WeavePeriod(uint64_t bot_instance, unsigned long client_ctfid)
{
	uint64_t mixed = SG_WeaveIdentityMix(bot_instance, client_ctfid);

	return 0.4f + 0.05f * (float)(mixed % UINT64_C(10));
}

static int
SG_WeaveSideAt(uint64_t bot_instance, unsigned long client_ctfid,
    float level_time)
{
	uint64_t mixed = SG_WeaveIdentityMix(bot_instance, client_ctfid);
	float period = SG_WeavePeriod(bot_instance, client_ctfid);
	float phase = period *
	    (float)((mixed >> 12) & UINT64_C(0xffff)) / 65536.0f;
	float clock = fmodf(level_time + phase, period);

	if (clock < 0.0f)
		clock += period;
	return clock < period * 0.5f ? 1 : -1;
}

/* A chain begins from a private lean instead of resetting every bot to the
 * same sine origin.  Use a distinct part of the identity mix so sharing a
 * weave period does not imply sharing an air-strafe shoulder. */
static float
SG_AirStrafeInitialPhase(uint64_t bot_instance, unsigned long client_ctfid)
{
	uint64_t mixed = SG_WeaveIdentityMix(
	    bot_instance ^ UINT64_C(0xd6e8feb86659fd93), client_ctfid);

	return 6.2831853071795864769f *
	    (float)(mixed & UINT64_C(0x00ffffff)) / 16777216.0f;
}

#endif
