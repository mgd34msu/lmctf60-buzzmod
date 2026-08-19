#include "slipgate/sg_defense_shift.h"
#include "slipgate/sg_route_policy.h"
#include "g_local.h"
#include "slipgate/sg_combat.h"

#include <math.h>
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

static void TestOneExitRouteStaysMobile(void)
{
	CHECK(SG_RouteReturnPenaltyAllowed(4, 4, 1, 2, 60.0f));
	CHECK(!SG_RouteReturnPenaltyAllowed(4, 4, 1, 1, 60.0f));
	CHECK(!SG_RouteReturnPenaltyAllowed(4, 4, 1, 0, 60.0f));
	CHECK(!SG_RouteReturnPenaltyAllowed(4, 5, 1, 3, 60.0f));
	CHECK(!SG_RouteReturnPenaltyAllowed(4, 4, 0, 3, 60.0f));
	CHECK(!SG_RouteReturnPenaltyAllowed(4, 4, 1, 3, 0.0f));
	CHECK(!SG_RouteReturnPenaltyAllowed(4, 4, 1, 3, NAN));
	CHECK(!SG_RouteReturnPenaltyAllowed(-1, 4, 1, 3, 60.0f));
	CHECK(!SG_RouteReturnPenaltyAllowed(4, 4, 2, 3, 60.0f));
}

static void TestAttackDescentCannotPriceItselfStill(void)
{
	const int infinity = 0x3fffffff;

	CHECK(SG_AttackDescentFallbackAllowed(1, 1, 8000, 7875, infinity));
	CHECK(SG_AttackDescentFallbackAllowed(1, 1, 601, 600, infinity));
	CHECK(!SG_AttackDescentFallbackAllowed(1, 1, 600, 500, infinity));
	CHECK(!SG_AttackDescentFallbackAllowed(1, 0, 8000, 7000, infinity));
	CHECK(!SG_AttackDescentFallbackAllowed(0, 1, 8000, 7000, infinity));
	CHECK(!SG_AttackDescentFallbackAllowed(1, 1, 8000, 8000, infinity));
	CHECK(!SG_AttackDescentFallbackAllowed(1, 1, 8000, 8001, infinity));
	CHECK(!SG_AttackDescentFallbackAllowed(1, 1, infinity, 10, infinity));
	CHECK(!SG_AttackDescentFallbackAllowed(2, 1, 8000, 7000, infinity));
	CHECK(!SG_AttackDescentFallbackAllowed(1, 1, 8000, 7000, 600));
}

static void TestLateralChoice(void)
{
	const sg_defense_shift_candidate_t candidates[] = {
		{ 7, 70, 220, 80.0f, 0.0f, 0.0f },
		{ 4, 40, 260, 8.0f, 96.0f, 0.0f },
		{ 3, 30, 260, 8.0f, -96.0f, 0.0f }
	};
	sg_defense_shift_request_t request = {
		400.0f, 0.0f, 144.0f, 400, -1
	};
	int seed = -1;

	CHECK(SG_DefenseShiftChoose(&request, candidates, 3, &seed) == 3);
	CHECK(seed == 30);
	request.previous_seed = 30;
	CHECK(SG_DefenseShiftChoose(&request, candidates, 3, &seed) == 4);
	CHECK(seed == 40);
}

static void TestGuardBandAndGeometry(void)
{
	const sg_defense_shift_candidate_t candidates[] = {
		{ 1, 10, 401, 0.0f, 80.0f, 0.0f },
		{ 2, 20, 200, 0.0f, 180.0f, 0.0f },
		{ 3, 30, 200, 10.0f, 10.0f, 0.0f },
		{ 4, 40, 200, 70.0f, 20.0f, 0.0f },
		{ 5, 50, 300, 30.0f, 80.0f, 0.0f },
		{ 6, 60, 100, 0.0f, 80.0f, 130.0f }
	};
	sg_defense_shift_request_t request = {
		300.0f, 0.0f, 144.0f, 400, -1
	};
	int seed = -1;

	CHECK(SG_DefenseShiftChoose(&request, candidates, 6, &seed) == 5);
	CHECK(seed == 50);
}

static void TestFailClosedInputs(void)
{
	const sg_defense_shift_candidate_t candidate = {
		1, 10, 200, 0.0f, 80.0f, 0.0f
	};
	sg_defense_shift_request_t request = {
		0.0f, 0.0f, 144.0f, 400, -1
	};
	int seed = 99;

	CHECK(SG_DefenseShiftChoose(&request, &candidate, 1, &seed) == -1);
	CHECK(seed == -1);
	request.threat_x = NAN;
	CHECK(SG_DefenseShiftChoose(&request, &candidate, 1, NULL) == -1);
	request.threat_x = 1.0f;
	request.max_distance = INFINITY;
	CHECK(SG_DefenseShiftChoose(&request, &candidate, 1, NULL) == -1);
	CHECK(SG_DefenseShiftChoose(NULL, &candidate, 1, NULL) == -1);
	CHECK(SG_DefenseShiftChoose(&request, NULL, 1, NULL) == -1);
}

static void TestInvalidShiftRetiresOnlyExactCommitment(void)
{
	int commitment = 12;

	CHECK(SG_DefenseShiftRetireIfInvalid(12, 0, &commitment) == 1);
	CHECK(commitment == -1);
	commitment = 31;
	CHECK(SG_DefenseShiftRetireIfInvalid(12, 0, &commitment) == 1);
	CHECK(commitment == 31);
	CHECK(SG_DefenseShiftRetireIfInvalid(12, 1, &commitment) == 0);
	CHECK(SG_DefenseShiftRetireIfInvalid(-1, 0, &commitment) == 0);
}

static void TestQuietPatrolCircuit(void)
{
	const sg_defense_patrol_candidate_t candidates[] = {
		{ 8, 80, 500, 1 },
		{ 3, 30, 350, 0 },
		{ 4, 40, 999, 1 },
		{ 9, 90, 1000, 1 },
		{ -1, 20, 200, 1 }
	};
	int seed = -1;

	CHECK(SG_DefensePatrolChoose(candidates, 5, 1000, -1, 0, &seed) == 8);
	CHECK(seed == 80);
	/* Draw the return leg: another admitted road exists, so the circuit
	 * advances instead of shuffling straight back. */
	CHECK(SG_DefensePatrolChoose(candidates, 5, 1000, 80, 0, &seed) == 4);
	CHECK(seed == 40);
	CHECK(SG_DefensePatrolChoose(candidates, 5, 0, -1, 0, &seed) == -1);
	CHECK(seed == -1);
	CHECK(SG_DefensePatrolChoose(NULL, 5, 1000, -1, 0, &seed) == -1);
}

static void TestQuietPatrolThrottle(void)
{
	CHECK(SG_DefensePatrolThrottle(0.55f) == 0.55f);
	CHECK(SG_DefensePatrolThrottle(0.1f) == 0.35f);
	CHECK(SG_DefensePatrolThrottle(2.0f) == 0.75f);
	CHECK(SG_DefensePatrolThrottle(0.0f) == 0.0f);
	CHECK(SG_DefensePatrolThrottle(NAN) == 0.0f);
}

static void TestQuietPatrolDwellsOnlyAfterArrival(void)
{
	int target = 80;

	CHECK(!SG_DefensePatrolFinishLeg(79, &target));
	CHECK(target == 80);
	CHECK(SG_DefensePatrolFinishLeg(80, &target));
	CHECK(target == -1);
	CHECK(!SG_DefensePatrolFinishLeg(80, &target));
	CHECK(!SG_DefensePatrolFinishLeg(-1, &target));
	CHECK(!SG_DefensePatrolFinishLeg(80, NULL));
}

static sg_defense_combat_request_t CombatRequest(void)
{
	sg_defense_combat_request_t request;

	memset(&request, 0, sizeof(request));
	request.enabled = 1;
	request.hold_post = 1;
	request.defend_stand = 1;
	request.own_flag_home = 1;
	request.engaged = 1;
	request.live_enemy = 1;
	request.identity_valid = 1;
	request.movement_clear = 1;
	request.self_x = 72.0f;
	request.self_y = 0.0f;
	request.stand_x = 0.0f;
	request.stand_y = 0.0f;
	request.enemy_x = 300.0f;
	request.enemy_y = 0.0f;
	request.camp_scale = 1.0f;
	request.identity = 4;
	request.phase = 8;
	return request;
}

static void TestCombatAdmissionAndDeterminism(void)
{
	sg_defense_combat_request_t request = CombatRequest();
	sg_defense_combat_move_t first, repeat, opposite;

	CHECK(SG_DefenseCombatChoose(&request, &first));
	CHECK(fabsf(first.x) < 0.01f);
	CHECK(fabsf(fabsf(first.y) - 1.0f) < 0.01f);
	CHECK(SG_DefenseCombatChoose(&request, &repeat));
	CHECK(first.tangent_sign == repeat.tangent_sign);
	CHECK(fabsf(first.x - repeat.x) < 0.0001f);
	request.phase++;
	CHECK(SG_DefenseCombatChoose(&request, &opposite));
	CHECK(first.tangent_sign == -opposite.tangent_sign);

	/* The command writer pins this value for one short, trace-approved
	 * engagement interval. The chooser must not reintroduce the old 0.5s
	 * phase flip while that preference is valid. */
	request = CombatRequest();
	request.preferred_tangent_sign = 1;
	CHECK(SG_DefenseCombatChoose(&request, &first));
	request.phase++;
	CHECK(SG_DefenseCombatChoose(&request, &repeat));
	CHECK(first.tangent_sign == 1);
	CHECK(repeat.tangent_sign == 1);
	request.preferred_tangent_sign = -1;
	CHECK(SG_DefenseCombatChoose(&request, &opposite));
	CHECK(opposite.tangent_sign == -1);

	request = CombatRequest();
	request.enabled = 0;             /* cvar off: old statue remains */
	CHECK(!SG_DefenseCombatChoose(&request, &first));
	request = CombatRequest(); request.live_enemy = 0;
	CHECK(!SG_DefenseCombatChoose(&request, &first));
	request = CombatRequest(); request.own_flag_home = 0;
	CHECK(!SG_DefenseCombatChoose(&request, &first));
	request = CombatRequest(); request.hold_post = 0;
	CHECK(!SG_DefenseCombatChoose(&request, &first));
	request = CombatRequest(); request.defend_stand = 0;
	CHECK(!SG_DefenseCombatChoose(&request, &first));
	request = CombatRequest(); request.engaged = 0;
	CHECK(!SG_DefenseCombatChoose(&request, &first));
	request = CombatRequest(); request.identity_valid = 0;
	CHECK(!SG_DefenseCombatChoose(&request, &first));
	request = CombatRequest(); request.movement_clear = 0;
	CHECK(!SG_DefenseCombatChoose(&request, &first));
	request = CombatRequest(); request.self_x = 140.0f; /* hard leash */
	CHECK(!SG_DefenseCombatChoose(&request, &first));
	request = CombatRequest(); request.self_x = 40.0f;  /* pedestal correction */
	CHECK(SG_DefenseCombatChoose(&request, &first));
	CHECK(first.x > 0.0f);
}

static void TestCombatHullProbeLaw(void)
{
	sg_defense_combat_probe_t probe = { 1, 1, 1, 72.0f, 0.0f };
	sg_defense_combat_probe_t opposite = { 1, 1, 1, 72.0f, 0.0f };

	CHECK(SG_DefenseCombatProbeAllowed(&probe));
	probe.body_clear = 0;            /* both tangent sides are walled */
	opposite.body_clear = 0;
	CHECK(!SG_DefenseCombatProbeAllowed(&probe));
	CHECK(!SG_DefenseCombatProbeAllowed(&opposite));
	probe.body_clear = 1;
	probe.player_clear = 0;          /* player hull collision */
	CHECK(!SG_DefenseCombatProbeAllowed(&probe));
	probe.player_clear = 1;
	probe.floor_clear = 0;           /* no floor */
	CHECK(!SG_DefenseCombatProbeAllowed(&probe));
	probe.floor_clear = 1;
	probe.stand_distance = 47.0f;    /* pedestal clearance */
	CHECK(!SG_DefenseCombatProbeAllowed(&probe));
	probe.stand_distance = 129.0f;   /* hard leash */
	CHECK(!SG_DefenseCombatProbeAllowed(&probe));
	probe.stand_distance = 72.0f;
	probe.vertical_step = 25.0f;
	CHECK(!SG_DefenseCombatProbeAllowed(&probe));
}

static void TestCombatPreviewCandidateLaw(void)
{
	sg_combat_preview_candidate_t candidate;

	memset(&candidate, 0, sizeof(candidate));
	candidate.self_team_valid = true;
	candidate.target_team_valid = true;
	candidate.target_inuse = true;
	candidate.target_client = true;
	candidate.target_health = 100;
	candidate.distance = 512.0f;
	candidate.range_limit = 2000.0f;
	candidate.forward_dot = 0.75f;
	candidate.visibility_known = true;
	candidate.visible = true;
	CHECK(SG_CombatPreviewCandidateEligible(&candidate));

	candidate.target_dead = true;
	CHECK(!SG_CombatPreviewCandidateEligible(&candidate));
	candidate.target_dead = false;
	candidate.target_inuse = false;
	CHECK(!SG_CombatPreviewCandidateEligible(&candidate));
	candidate.target_inuse = true;
	candidate.target_client = false;
	CHECK(!SG_CombatPreviewCandidateEligible(&candidate));
	candidate.target_client = true;
	candidate.target_noclip = true;
	CHECK(!SG_CombatPreviewCandidateEligible(&candidate));
	candidate.target_noclip = false;
	candidate.target_team_valid = false;
	CHECK(!SG_CombatPreviewCandidateEligible(&candidate));
	candidate.target_team_valid = true;
	candidate.same_team = true;
	CHECK(!SG_CombatPreviewCandidateEligible(&candidate));
	candidate.same_team = false;
	candidate.distance = candidate.range_limit;
	CHECK(!SG_CombatPreviewCandidateEligible(&candidate));
	candidate.distance = 512.0f;
	candidate.forward_dot = 0.49f;
	CHECK(!SG_CombatPreviewCandidateEligible(&candidate));
	candidate.forward_dot = 0.75f;
	candidate.visible = false;
	CHECK(!SG_CombatPreviewCandidateEligible(&candidate));
}

int main(void)
{
	TestOneExitRouteStaysMobile();
	TestAttackDescentCannotPriceItselfStill();
	TestLateralChoice();
	TestGuardBandAndGeometry();
	TestFailClosedInputs();
	TestInvalidShiftRetiresOnlyExactCommitment();
	TestQuietPatrolCircuit();
	TestQuietPatrolThrottle();
	TestQuietPatrolDwellsOnlyAfterArrival();
	TestCombatAdmissionAndDeterminism();
	TestCombatHullProbeLaw();
	TestCombatPreviewCandidateLaw();
	if (failures)
	{
		fprintf(stderr, "%d sg_defense_shift tests failed\n", failures);
		return 1;
	}
	puts("sg_defense_shift_test: ok");
	return 0;
}
