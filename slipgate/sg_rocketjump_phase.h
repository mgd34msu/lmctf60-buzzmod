#ifndef SG_ROCKETJUMP_PHASE_H
#define SG_ROCKETJUMP_PHASE_H

typedef enum sg_rocketjump_phase_e
{
	SG_ROCKETJUMP_IDLE = 0,
	SG_ROCKETJUMP_EQUIP,
	SG_ROCKETJUMP_ARMED,
	SG_ROCKETJUMP_FLIGHT,
	SG_ROCKETJUMP_COMPLETE,
	SG_ROCKETJUMP_FAILED
} sg_rocketjump_phase_t;

static inline int SG_RocketJumpPhasePhysical(sg_rocketjump_phase_t phase)
{
	return phase == SG_ROCKETJUMP_ARMED || phase == SG_ROCKETJUMP_FLIGHT;
}

#endif
