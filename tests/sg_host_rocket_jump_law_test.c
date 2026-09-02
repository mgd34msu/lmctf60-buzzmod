/* The rocket-jump law composes the host's damage, knockback, and movement
 * constants.  These checks pin the composition to the numbers the game
 * produces, so a change to any host constant shows up here. */
#include <math.h>
#include <stdio.h>

#include "slipgate/sg_host_rocket_jump_law.h"

static int failures;

#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			failures++; \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, \
				__LINE__, #condition); \
		} \
	} while (0)

static int Near(float value, float expected, float tolerance)
{
	return fabsf(value - expected) <= tolerance;
}

/* Standard gravity, one 100 ms frame of four 25 ms substeps. */
static void CheckStandardLaunch(void)
{
	sg_host_rocket_jump_launch_t launch;

	CHECK(SG_HostRocketJumpLaunch(800.0f, 100U, 25U, 0, &launch));
	/* One frame of jump: 0.025 * (250 + 230 + 210 + 190). */
	CHECK(launch.lead_frames == 1U);
	CHECK(Near(launch.pre_blast_rise, 22.0f, 1e-3f));
	CHECK(Near(launch.pre_blast_velocity, 190.0f, 1e-3f));
	/* Blast at the floor, body centre 50 up and 8 to the side: 120 - 25.3,
	 * halved for a self hit, truncated. */
	CHECK(launch.self_damage == 47);
	/* 1600 * 47 / 200 = 376 along (8, 46) normalised. */
	CHECK(Near(launch.kick_vertical, 376.0f * 46.0f / sqrtf(8.0f * 8.0f +
		46.0f * 46.0f), 1e-2f));
	CHECK(Near(launch.kick_lateral, 376.0f * 8.0f / sqrtf(8.0f * 8.0f +
		46.0f * 46.0f), 1e-2f));
	CHECK(launch.kick_vertical > launch.kick_lateral);
	CHECK(Near(launch.vertical_velocity,
		launch.pre_blast_velocity + launch.kick_vertical, 1e-3f));
	CHECK(Near(launch.rise, launch.pre_blast_rise +
		launch.vertical_velocity * launch.vertical_velocity / 1600.0f, 1e-2f));
	/* Well above a plain jump's 45.6 and below a full storey. */
	CHECK(launch.rise > 200.0f && launch.rise < 240.0f);
}

/* Gravity changes the pre-blast rise, and with it the blast distance and
 * the kick; the law follows rather than caching a number. */
static void CheckGravityFollowsThrough(void)
{
	sg_host_rocket_jump_launch_t low;
	sg_host_rocket_jump_launch_t high;

	CHECK(SG_HostRocketJumpLaunch(400.0f, 100U, 25U, 0, &low));
	CHECK(SG_HostRocketJumpLaunch(1600.0f, 100U, 25U, 0, &high));
	CHECK(low.pre_blast_rise > high.pre_blast_rise);
	CHECK(low.rise > high.rise);
	CHECK(low.self_damage <= high.self_damage);
}

static void CheckRejectsImpossibleInputs(void)
{
	sg_host_rocket_jump_launch_t launch;

	CHECK(!SG_HostRocketJumpLaunch(0.0f, 100U, 25U, 0, &launch));
	CHECK(!SG_HostRocketJumpLaunch(-800.0f, 100U, 25U, 0, &launch));
	CHECK(!SG_HostRocketJumpLaunch(800.0f, 0U, 25U, 0, &launch));
	CHECK(!SG_HostRocketJumpLaunch(800.0f, 100U, 0U, 0, &launch));
	CHECK(!SG_HostRocketJumpLaunch(800.0f, 100U, 30U, 0, &launch));
	CHECK(!SG_HostRocketJumpLaunch(800.0f, 100U, 25U, 0, NULL));
}

/* The balanced rule set kicks less: 75 damage over a 240 radius with the
 * linear falloff, scaled 1800. */
static void CheckBalancedLaunchIsWeaker(void)
{
	sg_host_rocket_jump_launch_t standard;
	sg_host_rocket_jump_launch_t balanced;

	CHECK(SG_HostRocketJumpLaunch(800.0f, 100U, 25U, 0, &standard));
	CHECK(SG_HostRocketJumpLaunch(800.0f, 100U, 25U, 1, &balanced));
	CHECK(balanced.self_damage < standard.self_damage);
	CHECK(balanced.rise < standard.rise);
	CHECK(balanced.rise > 45.6f);
}

static void CheckHealthCost(void)
{
	CHECK(SG_HostRocketJumpHealthCost(47, 0.0f, 0) == 47);
	/* Jacket 30%: ceil(14.1) = 15 saved. */
	CHECK(SG_HostRocketJumpHealthCost(47, 0.30f, 50) == 32);
	/* Armor short of the save: only what it has. */
	CHECK(SG_HostRocketJumpHealthCost(47, 0.30f, 10) == 37);
	/* Body 80%: ceil(37.6) = 38 saved. */
	CHECK(SG_HostRocketJumpHealthCost(47, 0.80f, 100) == 9);
	CHECK(SG_HostRocketJumpHealthCost(0, 0.80f, 100) == 0);
}

int main(void)
{
	CheckStandardLaunch();
	CheckGravityFollowsThrough();
	CheckRejectsImpossibleInputs();
	CheckBalancedLaunchIsWeaker();
	CheckHealthCost();
	if (failures != 0)
		return 1;
	puts("sg_host_rocket_jump_law_test: ok");
	return 0;
}
