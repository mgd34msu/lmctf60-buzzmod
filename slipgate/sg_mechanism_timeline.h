/* sg_mechanism_timeline.h -- host-free bounded mechanism transaction. */
#ifndef SG_MECHANISM_TIMELINE_H
#define SG_MECHANISM_TIMELINE_H

#include <stdint.h>

#define SG_MECHANISM_TIMELINE_FRAME_UNSET UINT32_MAX

typedef enum sg_mechanism_timeline_phase_e
{
	SG_MECHANISM_TIMELINE_IDLE = 0,
	SG_MECHANISM_TIMELINE_APPROACH,
	SG_MECHANISM_TIMELINE_ACTIVATION,
	SG_MECHANISM_TIMELINE_DELAY_PENDING,
	SG_MECHANISM_TIMELINE_ACTIVE,
	SG_MECHANISM_TIMELINE_EGRESS,
	SG_MECHANISM_TIMELINE_COMPLETE,
	SG_MECHANISM_TIMELINE_FAILED
} sg_mechanism_timeline_phase_t;

typedef enum sg_mechanism_timeline_command_e
{
	SG_MECHANISM_TIMELINE_COMMAND_NONE = 0,
	SG_MECHANISM_TIMELINE_COMMAND_APPROACH,
	SG_MECHANISM_TIMELINE_COMMAND_ZERO,
	SG_MECHANISM_TIMELINE_COMMAND_EGRESS
} sg_mechanism_timeline_command_t;

typedef enum sg_mechanism_timeline_reason_e
{
	SG_MECHANISM_TIMELINE_REASON_NONE = 0,
	SG_MECHANISM_TIMELINE_REASON_INVALID,
	SG_MECHANISM_TIMELINE_REASON_FRAME_REGRESSION,
	SG_MECHANISM_TIMELINE_REASON_FRAME_OVERFLOW,
	SG_MECHANISM_TIMELINE_REASON_DEAD,
	SG_MECHANISM_TIMELINE_REASON_DISCONNECTED,
	SG_MECHANISM_TIMELINE_REASON_BINDING_DRIFT,
	SG_MECHANISM_TIMELINE_REASON_SOURCE_DRIFT,
	SG_MECHANISM_TIMELINE_REASON_FANOUT_DRIFT,
	SG_MECHANISM_TIMELINE_REASON_APPROACH_TIMEOUT,
	SG_MECHANISM_TIMELINE_REASON_ACTIVATION_TIMEOUT,
	SG_MECHANISM_TIMELINE_REASON_UNEXPECTED_EVENT,
	SG_MECHANISM_TIMELINE_REASON_EARLY_ACTIVE,
	SG_MECHANISM_TIMELINE_REASON_READY_MISSED,
	SG_MECHANISM_TIMELINE_REASON_ACTIVE_LOST,
	SG_MECHANISM_TIMELINE_REASON_LEASE_EXPIRED,
	SG_MECHANISM_TIMELINE_REASON_STATION_EXPIRED,
	SG_MECHANISM_TIMELINE_REASON_EGRESS_TIMEOUT
} sg_mechanism_timeline_reason_t;

typedef struct sg_mechanism_timeline_spec_s
{
	uint32_t source_key;
	uint32_t fanout_identity;
	uint32_t approach_timeout_frames;
	uint32_t activation_timeout_frames;
	uint32_t trigger_delay_frames;
	uint32_t cooldown_frames;
	uint32_t lease_frames;
	uint32_t station_wait_frames;
	uint32_t egress_timeout_frames;
} sg_mechanism_timeline_spec_t;

typedef struct sg_mechanism_timeline_observation_s
{
	uint32_t frame;
	uint32_t source_key;
	uint32_t fanout_identity;
	uint8_t alive;
	uint8_t connected;
	uint8_t binding_current;
	uint8_t approach_arrived;
	uint8_t activation_authenticated;
	uint8_t mechanism_active;
	uint8_t egress_requested;
	uint8_t egress_arrived;
} sg_mechanism_timeline_observation_t;

typedef struct sg_mechanism_timeline_state_s
{
	sg_mechanism_timeline_spec_t spec;
	sg_mechanism_timeline_phase_t phase;
	sg_mechanism_timeline_reason_t reason;
	uint32_t begin_frame;
	uint32_t last_frame;
	uint32_t approach_deadline_frame;
	uint32_t activation_deadline_frame;
	uint32_t activation_frame;
	uint32_t ready_frame;
	uint32_t cooldown_ready_frame;
	uint32_t lease_deadline_frame;
	uint32_t station_deadline_frame;
	uint32_t egress_deadline_frame;
} sg_mechanism_timeline_state_t;

int SG_MechanismTimelineBegin(sg_mechanism_timeline_state_t *state,
	const sg_mechanism_timeline_spec_t *spec,
	const sg_mechanism_timeline_observation_t *observation);
sg_mechanism_timeline_command_t SG_MechanismTimelineStep(
	sg_mechanism_timeline_state_t *state,
	const sg_mechanism_timeline_observation_t *observation);

#endif /* SG_MECHANISM_TIMELINE_H */
