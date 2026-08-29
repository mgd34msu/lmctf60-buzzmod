#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_strategy_caller.h"

static int failures;
static int attack_field[2] = { 900, 0 };
static int recover_field[2] = { 700, 0 };
static int order_field[2] = { 500, 0 };

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static sg_destination_handle_t Handle(sg_destination_kind_t kind,
	uint64_t id, uint64_t generation, uint64_t at_ms)
{
	sg_destination_handle_t handle;

	memset(&handle, 0, sizeof(handle));
	handle.id = id;
	handle.generation = generation;
	handle.kind = kind;
	handle.motion = kind == SG_DESTINATION_WAYPOINT
		? SG_DESTINATION_STATIC : SG_DESTINATION_MOVING;
	handle.valid = 1U;
	handle.pose.phase.phase_id = 1U;
	handle.pose.phase.cell_id = 1U;
	handle.pose.sample_time_ms = handle.motion == SG_DESTINATION_MOVING
		? at_ms : 0U;
	return handle;
}

static sg_strategy_proposal_t Proposal(uint64_t commitment,
	sg_strategy_goal_kind_t goal_kind, sg_destination_kind_t destination_kind,
	int role, const int *field, uint64_t at_ms)
{
	sg_strategy_proposal_t proposal;

	memset(&proposal, 0, sizeof(proposal));
	proposal.commitment_id = commitment;
	proposal.goal_kind = goal_kind;
	proposal.destination.kind = destination_kind;
	if (destination_kind == SG_DESTINATION_FLAG)
	{
		proposal.destination.value.flag.team = 2U;
		proposal.destination.value.flag.location =
			SG_DESTINATION_FLAG_CURRENT;
	}
	else if (destination_kind == SG_DESTINATION_CARRIER ||
	         destination_kind == SG_DESTINATION_ESCORT ||
	         destination_kind == SG_DESTINATION_INTERCEPT)
	{
		proposal.destination.value.carrier.team = 1U;
		proposal.destination.value.carrier.selector =
			SG_DESTINATION_CARRIER_ANY;
		proposal.destination.value.carrier.client_id = UINT16_MAX;
	}
	else if (destination_kind == SG_DESTINATION_ITEM ||
	         destination_kind == SG_DESTINATION_WEAPON ||
	         destination_kind == SG_DESTINATION_POWERUP)
		proposal.destination.value.item.item_id = commitment;
	else if (destination_kind == SG_DESTINATION_DEFENSIVE_POST)
		proposal.destination.value.post.region_id = 4U;
	else
		proposal.destination.value.point.point_id = commitment;
	proposal.handle = Handle(destination_kind, commitment, 1U, at_ms);
	proposal.destination_status = SG_STRATEGY_DESTINATION_REACHABLE;
	proposal.cost_ms = 900U;
	proposal.authority_rank = SG_STRATEGY_AUTHORITY_AUTONOMOUS;
	proposal.principal_kind = SG_STRATEGY_PRINCIPAL_AUTONOMOUS;
	proposal.principal_id = 1U;
	proposal.role = role;
	proposal.goal_field = field;
	return proposal;
}

static void TestTacticalInterruptionsAndLife(void)
{
	sg_strategy_caller_t caller;
	sg_strategy_caller_output_t output;
	sg_strategy_proposal_t attack = Proposal(10U,
		SG_STRATEGY_GOAL_CAPTURE_FLAG, SG_DESTINATION_FLAG, 0,
		attack_field, 100U);
	uint64_t plan;
	uint64_t activation;

	CHECK(SG_StrategyCallerInit(&caller));
	CHECK(SG_StrategyCallerStep(&caller, &attack, 1U, 100U,
		SG_STRATEGY_BLOCK_NONE, &output));
	CHECK(output.instruction.kind == SG_STRATEGY_INSTRUCTION_EXECUTE);
	CHECK(output.goal_field == attack_field);
	plan = output.plan_id;
	activation = output.activation_id;

	CHECK(SG_StrategyCallerStep(&caller, &attack, 1U, 110U,
		SG_STRATEGY_BLOCK_COMBAT, &output));
	CHECK(output.instruction.kind == SG_STRATEGY_INSTRUCTION_SUSPENDED);
	CHECK(output.instruction.block_reason == SG_STRATEGY_BLOCK_COMBAT);
	CHECK(output.plan_id == plan);
	CHECK(output.activation_id == activation);
	CHECK(output.goal_field == attack_field);

	CHECK(SG_StrategyCallerStep(&caller, &attack, 1U, 120U,
		SG_STRATEGY_BLOCK_OBSTRUCTION, &output));
	CHECK(output.instruction.block_reason == SG_STRATEGY_BLOCK_OBSTRUCTION);
	CHECK(output.plan_id == plan);
	CHECK(output.activation_id == activation);

	CHECK(SG_StrategyCallerStep(&caller, &attack, 1U, 130U,
		SG_STRATEGY_BLOCK_HOOK_OPPORTUNITY, &output));
	CHECK(output.instruction.block_reason ==
		SG_STRATEGY_BLOCK_HOOK_OPPORTUNITY);
	CHECK(output.plan_id == plan);

	CHECK(SG_StrategyCallerPulse(&caller, 0U, 140U,
		SG_STRATEGY_BLOCK_CONTROLLER, &output));
	CHECK(output.instruction.kind == SG_STRATEGY_INSTRUCTION_WAIT_LIFE);
	CHECK(output.plan_id == plan);
	CHECK(output.goal_field == NULL);

	CHECK(SG_StrategyCallerPulse(&caller, 1U, 150U,
		SG_STRATEGY_BLOCK_NONE, &output));
	CHECK(output.instruction.kind == SG_STRATEGY_INSTRUCTION_EXECUTE);
	CHECK(output.plan_id == plan);
	CHECK(output.activation_id != 0U);
	CHECK(output.goal_field == attack_field);
}

static void TestRoleReplacementAndHumanAuthority(void)
{
	sg_strategy_caller_t caller;
	sg_strategy_caller_output_t output;
	sg_strategy_proposal_t attack = Proposal(20U,
		SG_STRATEGY_GOAL_CAPTURE_FLAG, SG_DESTINATION_FLAG, 0,
		attack_field, 200U);
	sg_strategy_proposal_t recover = Proposal(21U,
		SG_STRATEGY_GOAL_RECOVER_FLAG, SG_DESTINATION_FLAG, 3,
		recover_field, 210U);
	sg_strategy_proposal_t order = Proposal(22U,
		SG_STRATEGY_GOAL_DEFEND_POST, SG_DESTINATION_DEFENSIVE_POST, 1,
		order_field, 220U);
	uint64_t attack_plan;
	uint64_t order_plan;

	CHECK(SG_StrategyCallerInit(&caller));
	CHECK(SG_StrategyCallerStep(&caller, &attack, 1U, 200U,
		SG_STRATEGY_BLOCK_NONE, &output));
	attack_plan = output.plan_id;
	CHECK(SG_StrategyCallerStep(&caller, &recover, 1U, 210U,
		SG_STRATEGY_BLOCK_NONE, &output));
	CHECK(output.plan_id != attack_plan);
	CHECK(output.goal_field == recover_field);
	CHECK(caller.reducer.plan.goals[0].kind ==
		SG_STRATEGY_GOAL_RECOVER_FLAG);

	order.authority_rank = SG_STRATEGY_AUTHORITY_HUMAN;
	order.principal_kind = SG_STRATEGY_PRINCIPAL_HUMAN;
	order.principal_id = 7U;
	CHECK(SG_StrategyCallerStep(&caller, &order, 1U, 220U,
		SG_STRATEGY_BLOCK_NONE, &output));
	order_plan = output.plan_id;
	CHECK(output.goal_field == order_field);
	CHECK(caller.reducer.authority.rank == SG_STRATEGY_AUTHORITY_HUMAN);

	/* Expiry/removal of the human order explicitly releases its principal
	 * before autonomous role ownership can replace the plan. */
	attack.handle.generation = 2U;
	CHECK(SG_StrategyCallerStep(&caller, &attack, 1U, 230U,
		SG_STRATEGY_BLOCK_NONE, &output));
	CHECK(output.plan_id != order_plan);
	CHECK(output.goal_field == attack_field);
	CHECK(caller.reducer.authority.rank ==
		SG_STRATEGY_AUTHORITY_AUTONOMOUS);
	CHECK(caller.reducer.history_sequence >= 1U);
}

static void TestTerminalSettlementWhileSuspended(void)
{
	sg_strategy_caller_t caller;
	sg_strategy_caller_output_t output;
	sg_strategy_proposal_t carry = Proposal(30U,
		SG_STRATEGY_GOAL_CARRY_FLAG, SG_DESTINATION_FLAG, 2,
		attack_field, 300U);

	CHECK(SG_StrategyCallerInit(&caller));
	CHECK(SG_StrategyCallerStep(&caller, &carry, 1U, 300U,
		SG_STRATEGY_BLOCK_COMBAT, &output));
	CHECK(output.instruction.kind == SG_STRATEGY_INSTRUCTION_SUSPENDED);
	CHECK(SG_StrategyCallerSettle(&caller, SG_STRATEGY_OUTCOME_COMPLETED,
		SG_STRATEGY_FAILURE_NONE, 310U, &output));
	CHECK(output.instruction.kind == SG_STRATEGY_INSTRUCTION_COMPLETED);
	CHECK(caller.reducer.goals[0].phase == SG_STRATEGY_GOAL_SUCCEEDED);
}

static void TestStrategicCallerKinds(void)
{
	static const sg_strategy_goal_kind_t goals[] = {
		SG_STRATEGY_GOAL_COLLECT_ITEM,
		SG_STRATEGY_GOAL_DEFEND_POST,
		SG_STRATEGY_GOAL_ESCORT_CARRIER,
		SG_STRATEGY_GOAL_INTERCEPT_CARRIER,
		SG_STRATEGY_GOAL_CARRY_FLAG,
		SG_STRATEGY_GOAL_RECOVER_FLAG
	};
	static const sg_destination_kind_t destinations[] = {
		SG_DESTINATION_POWERUP,
		SG_DESTINATION_DEFENSIVE_POST,
		SG_DESTINATION_ESCORT,
		SG_DESTINATION_INTERCEPT,
		SG_DESTINATION_FLAG,
		SG_DESTINATION_FLAG
	};
	sg_strategy_caller_t caller;
	sg_strategy_caller_output_t output;
	uint64_t at_ms = 400U;
	size_t index;

	CHECK(SG_StrategyCallerInit(&caller));
	for (index = 0U; index < sizeof(goals) / sizeof(goals[0]); index++)
	{
		sg_strategy_proposal_t proposal = Proposal(40U + index,
			goals[index], destinations[index], (int)index,
			order_field, at_ms);

		CHECK(SG_StrategyCallerStep(&caller, &proposal, 1U, at_ms,
			SG_STRATEGY_BLOCK_NONE, &output));
		CHECK(output.instruction.kind == SG_STRATEGY_INSTRUCTION_EXECUTE);
		CHECK(caller.reducer.plan.goals[0].kind == goals[index]);
		at_ms += 10U;
	}
}

int main(void)
{
	TestTacticalInterruptionsAndLife();
	TestRoleReplacementAndHumanAuthority();
	TestTerminalSettlementWhileSuspended();
	TestStrategicCallerKinds();
	if (failures)
	{
		fprintf(stderr, "sg_strategy_caller_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_strategy_caller_test: ok");
	return 0;
}
