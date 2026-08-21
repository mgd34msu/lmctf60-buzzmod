#ifndef SG_ROCKETJUMP_CADENCE_H
#define SG_ROCKETJUMP_CADENCE_H

#define SG_ROCKETJUMP_CADENCE_BODY_STEPS 4

typedef enum sg_rocketjump_cadence_event_e
{
	SG_ROCKETJUMP_CADENCE_DONE = 0,
	SG_ROCKETJUMP_CADENCE_BODY_STEP,
	SG_ROCKETJUMP_CADENCE_PROJECTILE_FRAME,
	SG_ROCKETJUMP_CADENCE_IMPACT
} sg_rocketjump_cadence_event_t;

typedef struct sg_rocketjump_cadence_s
{
	int flight_frames;
	int projectile_frame;
	int body_steps_pending;
	int post_launch_steps;
} sg_rocketjump_cadence_t;

int SG_RocketJumpCadenceBegin(sg_rocketjump_cadence_t *cadence,
	float flight_ms, int server_frame_ms);
sg_rocketjump_cadence_event_t SG_RocketJumpCadenceNext(
	sg_rocketjump_cadence_t *cadence);

#endif /* SG_ROCKETJUMP_CADENCE_H */
