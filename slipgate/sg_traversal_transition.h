#ifndef SG_TRAVERSAL_TRANSITION_H
#define SG_TRAVERSAL_TRANSITION_H

typedef enum sg_door_lease_retirement_e
{
	SG_DOOR_LEASE_RELEASE = 0,
	SG_DOOR_LEASE_HOLD,
	SG_DOOR_LEASE_TERMINAL
} sg_door_lease_retirement_t;

qboolean SG_TraversalControllerPhysical(const sg_bot_t *bot, int action);
sg_door_lease_retirement_t SG_DoorLeaseRetirement(
	int release_proved_clear, int recovery_expired, int hold_open_ready);
void SG_StagedTraversalCancel(sg_bot_t *bot, int action);
void SG_CarryStartRetireSupersededRoute(sg_bot_t *bot, qboolean carry_started);
void SG_StrikeDutyRetireSupersededRoute(sg_bot_t *bot,
	qboolean duty_replaces_route);

#endif
