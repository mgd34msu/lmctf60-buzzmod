#ifndef SG_STRIKE_MISSION_POLICY_CASES_H
#define SG_STRIKE_MISSION_POLICY_CASES_H

static void TestMissionAndFlagApproachPolicy(void)
{
	CHECK(!SG_StrikeDutyRetiresOptionalErrand(SG_STRIKE_DUTY_NONE));
	CHECK(SG_StrikeDutyRetiresOptionalErrand(SG_STRIKE_DUTY_BREACH));
	CHECK(SG_StrikeDutyRetiresOptionalErrand(SG_STRIKE_DUTY_CLEAR));
	CHECK(SG_StrikeDutyRetiresOptionalErrand(SG_STRIKE_DUTY_PRESS));
	CHECK(SG_StrikeDutyRetiresOptionalErrand(SG_STRIKE_DUTY_ESCORT));
	CHECK(SG_StrikeDutyRetiresOptionalErrand(SG_STRIKE_DUTY_RECOVER));
	CHECK(SG_StrikeDutyRetiresOptionalErrand(SG_STRIKE_DUTY_CARRY));
	CHECK(!SG_StrikeDutyRetiresOptionalErrand((sg_strike_duty_t)-1));
	CHECK(SG_StrikeDutyEnemyPressure(SG_STRIKE_DUTY_BREACH));
	CHECK(SG_StrikeDutyEnemyPressure(SG_STRIKE_DUTY_CLEAR));
	CHECK(SG_StrikeDutyEnemyPressure(SG_STRIKE_DUTY_PRESS));
	CHECK(!SG_StrikeDutyEnemyPressure(SG_STRIKE_DUTY_RECOVER));
	CHECK(!SG_StrikeDutyEnemyPressure(SG_STRIKE_DUTY_ESCORT));
	CHECK(SG_StrikeEnemyPressureActive(1, 0, SG_STRIKE_DUTY_NONE));
	CHECK(!SG_StrikeEnemyPressureActive(1, 1, SG_STRIKE_DUTY_RECOVER));
	CHECK(SG_StrikeEnemyPressureActive(0, 1, SG_STRIKE_DUTY_PRESS));
	CHECK(!SG_StrikeEnemyPressureActive(0, 1, SG_STRIKE_DUTY_ESCORT));
	/* BREACH shoots visible defenders but does not chase lost-corner combat. */
	CHECK(!SG_StrikeDutyCombatPursuit(SG_STRIKE_DUTY_BREACH));
	CHECK(SG_StrikeDutyCombatPursuit(SG_STRIKE_DUTY_CLEAR));
	CHECK(SG_StrikeDutyCombatPursuit(SG_STRIKE_DUTY_PRESS));
	CHECK(SG_StrikeDutyCombatPursuit(SG_STRIKE_DUTY_RECOVER));
	CHECK(!SG_StrikeDutyCombatPursuit(SG_STRIKE_DUTY_ESCORT));
	CHECK(!SG_StrikeDutyCombatPursuit(SG_STRIKE_DUTY_CARRY));
	CHECK(SG_StrikeCombatPursuitActive(1, 0, SG_STRIKE_DUTY_NONE));
	CHECK(!SG_StrikeCombatPursuitActive(1, 1, SG_STRIKE_DUTY_ESCORT));
	CHECK(!SG_StrikeCombatPursuitActive(1, 1, SG_STRIKE_DUTY_BREACH));
	CHECK(SG_StrikeCombatPursuitActive(0, 1, SG_STRIKE_DUTY_RECOVER));
	CHECK(SG_StrikeCombatPursuitActive(0, 1, SG_STRIKE_DUTY_CLEAR));
	CHECK(SG_StrikeThresholdMateOwnsHold(SG_STRIKE_DUTY_BREACH, 2,
	      SG_STRIKE_DUTY_CLEAR, 9));
	CHECK(!SG_StrikeThresholdMateOwnsHold(SG_STRIKE_DUTY_CLEAR, 9,
	      SG_STRIKE_DUTY_BREACH, 2));
	CHECK(SG_StrikeThresholdMateOwnsHold(SG_STRIKE_DUTY_BREACH, 2,
	      SG_STRIKE_DUTY_PRESS, 9));
	CHECK(!SG_StrikeThresholdMateOwnsHold(SG_STRIKE_DUTY_PRESS, 9,
	      SG_STRIKE_DUTY_BREACH, 2));
	CHECK(SG_StrikeThresholdMateOwnsHold(SG_STRIKE_DUTY_PRESS, 9,
	      SG_STRIKE_DUTY_PRESS, 2));
	CHECK(!SG_StrikeThresholdMateOwnsHold(SG_STRIKE_DUTY_PRESS, 2,
	      SG_STRIKE_DUTY_PRESS, 9));
	CHECK(SG_StrikeThresholdMateOwnsHold(SG_STRIKE_DUTY_NONE, 9,
	      SG_STRIKE_DUTY_NONE, 2));
	CHECK(!SG_StrikeThresholdMateOwnsHold(SG_STRIKE_DUTY_BREACH, 2,
	      SG_STRIKE_DUTY_RECOVER, 1));
	CHECK(!SG_StrikeThresholdMateOwnsHold(SG_STRIKE_DUTY_BREACH, 0,
	      SG_STRIKE_DUTY_CLEAR, 9));
	CHECK(SG_StrikeDutyRearguard(SG_STRIKE_DUTY_BREACH));
	CHECK(SG_StrikeDutyRearguard(SG_STRIKE_DUTY_CLEAR));
	CHECK(SG_StrikeDutyRearguard(SG_STRIKE_DUTY_PRESS));
	CHECK(SG_StrikeDutyRearguard(SG_STRIKE_DUTY_ESCORT));
	CHECK(!SG_StrikeDutyRearguard(SG_STRIKE_DUTY_RECOVER));
	CHECK(!SG_StrikeDutyRearguard(SG_STRIKE_DUTY_CARRY));
	CHECK(SG_StrikeRearguardActive(1, 0, SG_STRIKE_DUTY_NONE));
	CHECK(!SG_StrikeRearguardActive(1, 1, SG_STRIKE_DUTY_RECOVER));
	CHECK(SG_StrikeRearguardActive(0, 1, SG_STRIKE_DUTY_ESCORT));
	CHECK(SG_StrikeRearguardActive(0, 1, SG_STRIKE_DUTY_PRESS));
	CHECK(SG_StrikeEscortActive(1, 0, SG_STRIKE_DUTY_NONE));
	CHECK(!SG_StrikeEscortActive(1, 1, SG_STRIKE_DUTY_RECOVER));
	CHECK(SG_StrikeEscortActive(0, 1, SG_STRIKE_DUTY_ESCORT));
	CHECK(!SG_StrikeEscortActive(0, 1, SG_STRIKE_DUTY_PRESS));
	CHECK(SG_StrikePrebreachApproachAllowed(0, 0, 1, 3000));
	CHECK(!SG_StrikePrebreachApproachAllowed(0, 0, 0, 3000));
	CHECK(SG_StrikePrebreachApproachAllowed(1, 1, 0, 3000));
	CHECK(!SG_StrikePrebreachApproachAllowed(1, 0, 1, 3000));
	CHECK(!SG_StrikePrebreachApproachAllowed(1, 1, 0, 2000));
	CHECK(!SG_StrikePrebreachApproachAllowed(1, 1, 0, 5000));
	CHECK(!SG_StrikePrebreachApproachAllowed(2, 1, 1, 3000));
	CHECK(!SG_StrikePrebreachApproachAllowed(1, -1, 1, 3000));
	CHECK(SG_StrikeFlagTouchThrottle(1, 100.0f, 300.0f, 0.49f) == 0.30f);
	CHECK(SG_StrikeFlagTouchThrottle(1, 100.0f, 300.0f, 0.50f) == 0.55f);
	CHECK(SG_StrikeFlagTouchThrottle(1, 100.0f, 300.0f, 0.85f) == 1.0f);
	CHECK(SG_StrikeFlagTouchThrottle(0, 100.0f, 300.0f, 0.0f) == 1.0f);
	CHECK(SG_StrikeFlagTouchThrottle(1, 220.0f, 300.0f, 0.0f) == 1.0f);
	CHECK(SG_StrikeFlagTouchThrottle(1, 100.0f, 120.0f, 0.0f) == 1.0f);
	CHECK(SG_StrikeFlagTouchThrottle(1, 100.0f, 300.0f, NAN) == 1.0f);
	CHECK(SG_StrikeFlagTouchThrottle(2, 100.0f, 300.0f, 0.0f) == 1.0f);
	CHECK(SG_StrikeCarrierOwnFlagAimAllowed(1, 1, 0));
	CHECK(SG_StrikeCarrierOwnFlagAimAllowed(1, 0, 1));
	CHECK(!SG_StrikeCarrierOwnFlagAimAllowed(1, 0, 0));
	CHECK(!SG_StrikeCarrierOwnFlagAimAllowed(0, 1, 1));
	CHECK(!SG_StrikeCarrierOwnFlagAimAllowed(2, 1, 1));
	CHECK(!SG_StrikeCarrierOwnFlagAimAllowed(1, -1, 1));
	CHECK(!SG_StrikeCarrierOwnFlagAimAllowed(1, 1, 2));
	CHECK(SG_StrikeFlagApproachSource(1, 1, -1, 100) == SG_FLAG_APPROACH_HOME);
	CHECK(SG_StrikeFlagApproachSource(1, 0, 42, 100) == SG_FLAG_APPROACH_BELIEF);
	CHECK(SG_StrikeFlagApproachSource(1, 0, -1, 100) == SG_FLAG_APPROACH_NONE);
	CHECK(SG_StrikeFlagApproachSource(0, 1, 42, 100) == SG_FLAG_APPROACH_NONE);
	CHECK(SG_StrikeFlagApproachSource(1, 0, 100, 100) == SG_FLAG_APPROACH_NONE);
	CHECK(SG_StrikeFlagApproachSource(2, 1, 42, 100) == SG_FLAG_APPROACH_NONE);
	CHECK(SG_StrikeFlagApproachPrice(1, 0, 1, 500.0f, 300.0f, 0.0f,
	    500, 500) == -100.0f);
	CHECK(SG_StrikeFlagApproachPrice(1, 0, 1, 400.0f, 350.0f, 0.0f,
	    500, 625) == -25.0f);
	CHECK(SG_StrikeFlagApproachPrice(1, 0, 1, 400.0f, 385.0f, 0.0f,
	    500, 500) == 0.0f);
	CHECK(SG_StrikeFlagApproachPrice(1, 1, 1, 160.0f, 80.0f, 0.0f,
	    200, 100) == 0.0f);
	CHECK(SG_StrikeFlagApproachPrice(1, 0, 1, 160.0f, 80.0f, 0.0f,
	    200, 100) < 0.0f);
	CHECK(SG_StrikeFlagApproachPrice(1, 0, 1, 120.0f, 80.0f, 0.0f,
	    200, 100) < 0.0f);
	CHECK(SG_StrikeFlagApproachPrice(1, 0, 1, 601.0f, 300.0f, 0.0f,
	    700, 600) == 0.0f);
	CHECK(SG_StrikeFlagApproachPrice(1, 0, 0, 400.0f, 200.0f, 0.0f,
	    500, 400) == 0.0f);
	CHECK(SG_StrikeFlagApproachPrice(1, 0, 1, 400.0f, 200.0f, 97.0f,
	    500, 400) == 0.0f);
	CHECK(SG_StrikeFlagApproachPrice(1, 0, 1, 400.0f, 200.0f, 0.0f,
	    500, 626) == 0.0f);
	CHECK(SG_StrikeFlagApproachPrice(1, 0, 1, NAN, 200.0f, 0.0f,
	    500, 400) == 0.0f);
	CHECK(SG_StrikeFlagApproachPrice(1, 2, 1, 400.0f, 200.0f, 0.0f,
	    500, 400) == 0.0f);
	CHECK(SG_AntiLingerCarrierNearby(1, 399.0f));
	CHECK(!SG_AntiLingerCarrierNearby(1, 400.0f));
	CHECK(!SG_AntiLingerCarrierNearby(0, 100.0f));
	CHECK(!SG_AntiLingerCarrierNearby(2, 100.0f));
	CHECK(!SG_AntiLingerCarrierNearby(1, -1.0f));
	CHECK(!SG_AntiLingerCarrierNearby(1, NAN));
	CHECK(SG_OrderedEscortRouteAllowed(1, 1));
	CHECK(!SG_OrderedEscortRouteAllowed(1, 0));
	CHECK(!SG_OrderedEscortRouteAllowed(0, 1));
	CHECK(!SG_OrderedEscortRouteAllowed(2, 1));
	CHECK(!SG_OrderedEscortRouteAllowed(1, -1));
}

#endif /* SG_STRIKE_MISSION_POLICY_CASES_H */
