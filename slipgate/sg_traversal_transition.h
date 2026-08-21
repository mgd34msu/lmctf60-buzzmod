#ifndef SG_TRAVERSAL_TRANSITION_H
#define SG_TRAVERSAL_TRANSITION_H

typedef enum sg_door_lease_retirement_e
{
	SG_DOOR_LEASE_RELEASE = 0,
	SG_DOOR_LEASE_HOLD,
	SG_DOOR_LEASE_TERMINAL
} sg_door_lease_retirement_t;

typedef enum sg_speedhook_terminal_e
{
	SG_SPEEDHOOK_TERMINAL_NOATTACH = 0,
	SG_SPEEDHOOK_TERMINAL_BURST,
	SG_SPEEDHOOK_TERMINAL_BURSTSTALL
} sg_speedhook_terminal_t;

qboolean SG_TraversalControllerPhysical(const sg_bot_t *bot, int action);
qboolean SG_DeclaredDoorRouteRequiresRelease(const sg_bot_t *bot, int action);
sg_door_lease_retirement_t SG_DoorLeaseRetirement(
	int release_proved_clear, int recovery_expired, int hold_open_ready);
void SG_StagedTraversalCancel(sg_bot_t *bot, int action);
void SG_SpeedHookReleaseFinish(sg_bot_t *bot);
sg_speedhook_terminal_t SG_SpeedHookTerminalFinish(sg_bot_t *bot,
	qboolean reached_speed, int hookstate, qboolean hook_present);
void SG_CarryStartRetireSupersededRoute(sg_bot_t *bot, qboolean carry_started);
qboolean SG_NonCarryHandoffRetireSupersededRoute(sg_bot_t *bot,
	int previous_role, int current_role);
void SG_StrikeDutyRetireSupersededRoute(sg_bot_t *bot,
	qboolean duty_replaces_route);
qboolean SG_DefensePatrolRetire(sg_bot_t *bot, qboolean patrol_allowed);

#endif
