#include "slipgate/sg_defense_supply.h"

#include <stdio.h>

static int failures;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
		    #condition); \
		failures++; \
	} \
} while (0)

static sg_defense_supply_step_t ReadyStep(void)
{
	sg_defense_supply_step_t step = { 1, 1, 1, 0, 0, 0, 1, 1, 1, 0 };

	return step;
}

static void TestExactSelectedRoute(void)
{
	int broad_weapon_field[8] = { 0, 0, 0, 0, 0, 0, 0, 600 };
	int selected_rl_field[8] = { 0, 0, 0, 0, 0, 0, 0, 2200 };
	int home_field[8] = { 0 };
	const int *route = NULL;
	sg_defense_supply_neighbor_t neighbors[] = {
		{ 531, 512, 1700 },
		{ 533, 533, 1200 },
		{ 540, 540, 1500 }
	};

	/* The broad class field has a closer excluded source (blaster/hook/
	 * grenades in the live game). The selected RL flood is the route owner. */
	CHECK(SG_DefenseSupplyRoute(SG_DEFENSE_SUPPLY_PHASE_OUTBOUND,
	    broad_weapon_field, selected_rl_field, home_field, 7, 2400, &route));
	CHECK(route == selected_rl_field);
	CHECK(route[7] == 2200);
	/* Once the exact pad is bound, an unrelated class-field refresh is not
	 * ownership. The production call passes NULL for that optional guard, so a
	 * closer/farther class reflow cannot cancel or retarget this sortie. */
	broad_weapon_field[7] = 0x3fffffff;
	CHECK(SG_DefenseSupplyRoute(SG_DEFENSE_SUPPLY_PHASE_OUTBOUND,
	    NULL, selected_rl_field, home_field, 7, 2400, &route));
	CHECK(route == selected_rl_field);
	/* The production fan's exact gradient chooses the smap05-style RL
	 * neighbor 7 -> 533, even though another admitted link exists. */
	CHECK(SG_DefenseSupplyChooseNeighbor(neighbors, 3, 2200) == 533);
	selected_rl_field[7] = 0x3fffffff;
	CHECK(!SG_DefenseSupplyRoute(SG_DEFENSE_SUPPLY_PHASE_OUTBOUND,
	    broad_weapon_field, selected_rl_field, home_field, 7, 2400,
	    &route));
	CHECK(SG_DefenseSupplyRoute(SG_DEFENSE_SUPPLY_PHASE_RETURN,
	    broad_weapon_field, selected_rl_field, home_field, 7, 2400,
	    &route));
	CHECK(route == home_field);
}

static void TestPhaseEdges(void)
{
	sg_defense_supply_step_t step = ReadyStep();
	int home_field[4] = { 0, 10, 20, 30 };
	const int *route = NULL;

	CHECK(SG_DefenseSupplyPhaseStep(SG_DEFENSE_SUPPLY_PHASE_OUTBOUND,
	                                &step) == SG_DEFENSE_SUPPLY_PHASE_OUTBOUND);
	step.weapon_available = 1;
	CHECK(SG_DefenseSupplyPhaseStep(SG_DEFENSE_SUPPLY_PHASE_OUTBOUND,
	                                &step) == SG_DEFENSE_SUPPLY_PHASE_RETURN);
	/* Acquisition flips the route authority to the immutable home field; it
	 * does not end the transaction in place or reopen item selection. */
	CHECK(SG_DefenseSupplyRoute(SG_DEFENSE_SUPPLY_PHASE_RETURN,
	    NULL, NULL, home_field, 2, 2400, &route));
	CHECK(route[2] == 20);
	step = ReadyStep();
	step.target_valid = 0;
	CHECK(SG_DefenseSupplyPhaseStep(SG_DEFENSE_SUPPLY_PHASE_OUTBOUND,
	                                &step) == SG_DEFENSE_SUPPLY_PHASE_RETURN);
	step = ReadyStep();
	step.weapon_field_valid = 0;
	CHECK(SG_DefenseSupplyPhaseStep(SG_DEFENSE_SUPPLY_PHASE_OUTBOUND,
	                                &step) == SG_DEFENSE_SUPPLY_PHASE_RETURN);
	step = ReadyStep();
	step.threat = 1;
	CHECK(SG_DefenseSupplyPhaseStep(SG_DEFENSE_SUPPLY_PHASE_OUTBOUND,
	                                &step) == SG_DEFENSE_SUPPLY_PHASE_RETURN);
	step = ReadyStep();
	step.own_flag_home = 0;
	CHECK(SG_DefenseSupplyPhaseStep(SG_DEFENSE_SUPPLY_PHASE_OUTBOUND,
	                                &step) == SG_DEFENSE_SUPPLY_PHASE_RETURN);
	step = ReadyStep();
	step.deadline_pending = 0;
	CHECK(SG_DefenseSupplyPhaseStep(SG_DEFENSE_SUPPLY_PHASE_OUTBOUND,
	                                &step) == SG_DEFENSE_SUPPLY_PHASE_RETURN);
	step = ReadyStep();
	step.other_owner = 1;
	CHECK(SG_DefenseSupplyPhaseStep(SG_DEFENSE_SUPPLY_PHASE_OUTBOUND,
	                                &step) == SG_DEFENSE_SUPPLY_PHASE_RETURN);
	step = ReadyStep();
	step.engaged = 1;
	CHECK(SG_DefenseSupplyPhaseStep(SG_DEFENSE_SUPPLY_PHASE_OUTBOUND,
	                                &step) == SG_DEFENSE_SUPPLY_PHASE_RETURN);

	/* A life/identity edge cancels the transaction; ordinary objective logic
	 * then owns the immediate home return. RETURN itself never reopens shopping. */
	step = ReadyStep();
	step.identity_valid = 0;
	CHECK(SG_DefenseSupplyPhaseStep(SG_DEFENSE_SUPPLY_PHASE_OUTBOUND,
	                                &step) == SG_DEFENSE_SUPPLY_PHASE_NONE);
	step = ReadyStep();
	step.owner_valid = 0;
	CHECK(SG_DefenseSupplyPhaseStep(SG_DEFENSE_SUPPLY_PHASE_OUTBOUND,
	                                &step) == SG_DEFENSE_SUPPLY_PHASE_NONE);
	step = ReadyStep();
	step.weapon_available = 1;
	CHECK(SG_DefenseSupplyPhaseStep(SG_DEFENSE_SUPPLY_PHASE_RETURN,
	                                &step) == SG_DEFENSE_SUPPLY_PHASE_RETURN);
	CHECK(SG_DefenseSupplyPhaseStep(SG_DEFENSE_SUPPLY_PHASE_NONE,
	                                &step) == SG_DEFENSE_SUPPLY_PHASE_NONE);
}

static void TestDeadlineBound(void)
{
	sg_defense_supply_step_t step = ReadyStep();

	/* The production timer aliases this deterministic policy constant.  At the
	 * hard boundary, OUTBOUND must hand route authority to RETURN rather than
	 * continuing to shop. */
	CHECK(SG_DEFENSE_SUPPLY_DEADLINE_SECONDS == 5.0f);
	CHECK(SG_DEFENSE_SUPPLY_DEADLINE_SECONDS <= 5.0f);
	step.deadline_pending = 0;
	CHECK(SG_DefenseSupplyPhaseStep(SG_DEFENSE_SUPPLY_PHASE_OUTBOUND,
	                                &step) == SG_DEFENSE_SUPPLY_PHASE_RETURN);
}

static void TestOutboundActionBoundary(void)
{
	CHECK(SG_DefenseSupplyActionAllowed(
	    SG_DEFENSE_SUPPLY_PHASE_OUTBOUND, 1));
	CHECK(!SG_DefenseSupplyActionAllowed(
	    SG_DEFENSE_SUPPLY_PHASE_OUTBOUND, 0));
	CHECK(SG_DefenseSupplyActionAllowed(
	    SG_DEFENSE_SUPPLY_PHASE_RETURN, 1));
	CHECK(SG_DefenseSupplyActionAllowed(
	    SG_DEFENSE_SUPPLY_PHASE_RETURN, 0));
	CHECK(!SG_DefenseSupplyActionAllowed(
	    SG_DEFENSE_SUPPLY_PHASE_NONE, 1));

	CHECK(SG_DefenseSupplyGenericRetryAllowed(
	    SG_DEFENSE_SUPPLY_PHASE_NONE, 0));
	CHECK(!SG_DefenseSupplyGenericRetryAllowed(
	    SG_DEFENSE_SUPPLY_PHASE_NONE, 1));
	CHECK(!SG_DefenseSupplyGenericRetryAllowed(
	    SG_DEFENSE_SUPPLY_PHASE_OUTBOUND, 1));
	CHECK(!SG_DefenseSupplyGenericRetryAllowed(
	    SG_DEFENSE_SUPPLY_PHASE_RETURN, 1));
	CHECK(!SG_DefenseSupplyGenericRetryAllowed(
	    SG_DEFENSE_SUPPLY_PHASE_RETURN, 0));
}

int main(void)
{
	TestExactSelectedRoute();
	TestPhaseEdges();
	TestDeadlineBound();
	TestOutboundActionBoundary();
	if (failures)
	{
		fprintf(stderr, "%d sg_defense_supply tests failed\n", failures);
		return 1;
	}
	puts("sg_defense_supply_test: ok");
	return 0;
}
