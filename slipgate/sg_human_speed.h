/*
 * sg_human_speed.h -- ordinary bot strafe-jump physics at human cadence.
 *
 * The engine stores movement timers in 8 ms units.  A normal 100 ms command
 * spends 12 units; eight 12/13 ms bot subcommands each spend only one unless
 * their fractional milliseconds are carried across the subcommands.  This
 * adapter carries that remainder for the ordinary air-strafe chain only.  It
 * never writes velocity and it never touches water-jump or teleport timers.
 */
#ifndef SG_HUMAN_SPEED_H
#define SG_HUMAN_SPEED_H

#include "../g_local.h"

typedef struct sg_human_speed_timer_s
{
	unsigned remainder_ms;       /* elapsed landing time modulo 8, 0..7 */
	qboolean tracking;
} sg_human_speed_timer_t;

typedef struct sg_human_speed_step_s
{
	unsigned extra_ticks;        /* correction applied before this Pmove */
	unsigned remainder_ms;
	qboolean expired;
	qboolean began;
} sg_human_speed_step_t;

typedef struct sg_human_speed_air_s
{
	float lean;                  /* signed swing, -1..1 */
	float view_yaw;
	float view_pitch;
	qboolean chain;
} sg_human_speed_air_t;

#define SG_HUMAN_SPEED_AIR_ACCEL 1.0f

void SG_HumanSpeedTimerReset(sg_human_speed_timer_t *state);
void SG_HumanSpeedCommandBoundary(sg_human_speed_timer_t *state,
	qboolean owned);
sg_human_speed_step_t SG_HumanSpeedLandingPrepare(
	sg_human_speed_timer_t *state, pmove_state_t *pmove,
	unsigned command_msec, qboolean enabled);
sg_human_speed_step_t SG_HumanSpeedLandingObserve(
	sg_human_speed_timer_t *state, byte flags_before,
	const pmove_state_t *pmove, unsigned command_msec, qboolean enabled);

void SG_HumanSpeedAirCommand(usercmd_t *cmd,
	const sg_human_speed_air_t *air, const vec3_t velocity,
	float speed2d, float frametime);

#endif /* SG_HUMAN_SPEED_H */
