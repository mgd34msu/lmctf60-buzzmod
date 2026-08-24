/* Host-free transaction for an authenticated continuous station train. */
#ifndef SG_TRAIN_STATION_TRANSACTION_H
#define SG_TRAIN_STATION_TRANSACTION_H

#include <stdint.h>

#define SG_TRAIN_STATION_ROUTE_CORNERS 14U
#define SG_TRAIN_STATION_DWELL_FRAMES 30U

typedef enum sg_train_station_phase_e
{
	SG_TRAIN_STATION_IDLE = 0,
	SG_TRAIN_STATION_WAIT_SOURCE,
	SG_TRAIN_STATION_SOURCE_DWELL,
	SG_TRAIN_STATION_RIDE,
	SG_TRAIN_STATION_DESTINATION_DWELL,
	SG_TRAIN_STATION_COMPLETE,
	SG_TRAIN_STATION_FAILED
} sg_train_station_phase_t;

typedef enum sg_train_station_reason_e
{
	SG_TRAIN_STATION_REASON_NONE = 0,
	SG_TRAIN_STATION_REASON_INVALID,
	SG_TRAIN_STATION_REASON_FRAME_REGRESSION,
	SG_TRAIN_STATION_REASON_FRAME_OVERFLOW,
	SG_TRAIN_STATION_REASON_DEAD,
	SG_TRAIN_STATION_REASON_DISCONNECTED,
	SG_TRAIN_STATION_REASON_SOURCE_DRIFT,
	SG_TRAIN_STATION_REASON_BINDING_DRIFT,
	SG_TRAIN_STATION_REASON_FANOUT_DRIFT,
	SG_TRAIN_STATION_REASON_TRAIN_DRIFT,
	SG_TRAIN_STATION_REASON_EARLY_DEPARTURE,
	SG_TRAIN_STATION_REASON_DWELL_OVERRUN,
	SG_TRAIN_STATION_REASON_BOARDING_MISSED,
	SG_TRAIN_STATION_REASON_BODY_LOST,
	SG_TRAIN_STATION_REASON_UNEXPECTED_STOP,
	SG_TRAIN_STATION_REASON_EGRESS_EXPIRED
} sg_train_station_reason_t;

typedef enum sg_train_station_command_e
{
	SG_TRAIN_STATION_COMMAND_NONE = 0,
	SG_TRAIN_STATION_COMMAND_WAIT,
	SG_TRAIN_STATION_COMMAND_BOARD,
	SG_TRAIN_STATION_COMMAND_HOLD,
	SG_TRAIN_STATION_COMMAND_EGRESS
} sg_train_station_command_t;

typedef enum sg_train_station_endpoint_e
{
	SG_TRAIN_STATION_UPPER = 0,
	SG_TRAIN_STATION_LOWER
} sg_train_station_endpoint_t;

typedef struct sg_train_station_spec_s
{
	uint32_t source_key;
	uint32_t binding_identity;
	uint32_t fanout_identity;
	uint32_t train_identity;
	uint32_t route_corner_keys[SG_TRAIN_STATION_ROUTE_CORNERS];
	uint32_t upper_station_key;
	uint32_t lower_station_key;
	uint16_t upper_dwell_frames;
	uint16_t lower_dwell_frames;
	uint8_t route_corner_count;
	uint8_t start_on;
	sg_train_station_endpoint_t boarding_station;
} sg_train_station_spec_t;

typedef struct sg_train_station_observation_s
{
	uint32_t frame;
	uint32_t source_key;
	uint32_t binding_identity;
	uint32_t fanout_identity;
	uint32_t train_identity;
	uint32_t train_corner_key;
	uint8_t alive;
	uint8_t connected;
	uint8_t binding_current;
	uint8_t train_moving;
	uint8_t body_aboard;
	uint8_t body_clear;
	uint8_t egress_arrived;
} sg_train_station_observation_t;

typedef struct sg_train_station_state_s
{
	sg_train_station_spec_t spec;
	sg_train_station_phase_t phase;
	sg_train_station_reason_t reason;
	uint32_t source_station_key;
	uint32_t destination_station_key;
	uint32_t last_frame;
	uint32_t source_arrival_frame;
	uint32_t source_departure_frame;
	uint32_t destination_arrival_frame;
	uint32_t destination_departure_frame;
} sg_train_station_state_t;

int SG_TrainStationTransactionBegin(sg_train_station_state_t *state,
	const sg_train_station_spec_t *spec,
	const sg_train_station_observation_t *observation);
sg_train_station_command_t SG_TrainStationTransactionStep(
	sg_train_station_state_t *state,
	const sg_train_station_observation_t *observation);

#endif /* SG_TRAIN_STATION_TRANSACTION_H */
