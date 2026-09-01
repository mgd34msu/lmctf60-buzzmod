/* Executable equations for the stock mover and trigger callbacks. */
#ifndef SG_HOST_MECHANISM_LAW_H
#define SG_HOST_MECHANISM_LAW_H

#include <stdint.h>

#define SG_HOST_MECHANISM_LAW_VERSION UINT32_C(1)
#define SG_HOST_MECHANISM_LAW_ID UINT64_C(0x4d4f5645524c5731)
#define SG_HOST_MECHANISM_MOVE_EQUATION_ID UINT32_C(0x4d4f5645)
#define SG_HOST_MECHANISM_ACCEL_EQUATION_ID UINT32_C(0x41434345)
#define SG_HOST_MECHANISM_DOOR_EQUATION_ID UINT32_C(0x444f4f52)
#define SG_HOST_MECHANISM_PLAT_EQUATION_ID UINT32_C(0x504c4154)
#define SG_HOST_MECHANISM_TRIGGER_EQUATION_ID UINT32_C(0x54524947)
#define SG_HOST_MECHANISM_TRAIN_EQUATION_ID UINT32_C(0x54524149)

#define SG_HOST_MECHANISM_DOOR_TOGGLE UINT32_C(32)
#define SG_HOST_MECHANISM_DOOR_CRUSHER UINT32_C(4)
#define SG_HOST_MECHANISM_TRAIN_START_ON UINT32_C(1)
#define SG_HOST_MECHANISM_TRAIN_TOGGLE UINT32_C(2)
#define SG_HOST_MECHANISM_TRAIN_BLOCK_STOPS UINT32_C(4)
#define SG_HOST_MECHANISM_DEFAULT_DOOR_DAMAGE UINT32_C(2)
#define SG_HOST_MECHANISM_DEFAULT_TRAIN_DAMAGE UINT32_C(100)
#define SG_HOST_MECHANISM_NONCLIENT_DAMAGE UINT32_C(100000)
#define SG_HOST_MECHANISM_TRIGGER_DEFAULT_WAIT_SECONDS 0.2f

/* The stock mover state values are part of the executable equation, not
 * caller-owned labels. */
#define SG_HOST_MECHANISM_STATE_TOP 0
#define SG_HOST_MECHANISM_STATE_BOTTOM 1
#define SG_HOST_MECHANISM_STATE_UP 2
#define SG_HOST_MECHANISM_STATE_DOWN 3

typedef struct sg_host_mechanism_law_s
{
	uint32_t version;
	uint32_t frame_ms;
	uint32_t move_equation_id;
	uint32_t acceleration_equation_id;
	uint32_t door_equation_id;
	uint32_t platform_equation_id;
	uint32_t trigger_equation_id;
	uint32_t train_equation_id;
	uint64_t identity;
	uint32_t door_default_wait_ms;
	uint32_t platform_top_dwell_ms;
	uint32_t platform_top_touch_delay_ms;
	uint32_t door_trigger_debounce_ms;
	uint32_t door_message_debounce_ms;
	uint32_t train_blocked_debounce_ms;
	uint32_t trigger_default_wait_ms;
	uint32_t trigger_remove_delay_ms;
	uint32_t frame_schedule_ms;
	float door_default_speed;
	float door_rotating_default_speed;
	float button_default_speed;
	float door_default_lip;
	float button_default_lip;
	float platform_default_lip;
	float platform_default_speed;
	float platform_default_accel;
	float platform_default_decel;
	float train_default_speed;
	uint32_t train_default_damage;
} sg_host_mechanism_law_t;

typedef int (*sg_host_mechanism_live_capture_function_t)(
	sg_host_mechanism_law_t *law);

typedef struct sg_host_mechanism_move_result_s
{
	int valid;
	int accelerated;
	uint64_t first_think_ms;
	uint64_t full_speed_frames;
	uint64_t completion_ms;
	float residual_distance;
	float final_speed;
} sg_host_mechanism_move_result_t;

/* Receives each nonzero stock SV_Physics_Pusher translation distance in
 * Move_Calc/Think_AccelMove order.  It is a host-law seam for consumers that
 * must replay G_Push/SV_Push carrying without inventing a movement command. */
typedef int (*sg_host_mechanism_move_frame_fn)(void *context,
	float distance);

/* Receives every angular SV_Physics_Pusher frame in the exact
 * AngleMove_Calc/AngleMove_Begin order. */
typedef int (*sg_host_mechanism_angle_frame_fn)(void *context,
	const float angular_delta[3]);

typedef enum sg_host_mechanism_door_event_e
{
	SG_HOST_MECHANISM_DOOR_TOP = 1,
	SG_HOST_MECHANISM_DOOR_TRIGGER_TOUCH,
	SG_HOST_MECHANISM_DOOR_MESSAGE_TOUCH,
	SG_HOST_MECHANISM_DOOR_BLOCKED,
	SG_HOST_MECHANISM_DOOR_USE
} sg_host_mechanism_door_event_t;

typedef enum sg_host_mechanism_platform_event_e
{
	SG_HOST_MECHANISM_PLATFORM_TOP = 1,
	SG_HOST_MECHANISM_PLATFORM_TRIGGER_TOUCH,
	SG_HOST_MECHANISM_PLATFORM_BLOCKED
} sg_host_mechanism_platform_event_t;

typedef enum sg_host_mechanism_train_event_e
{
	SG_HOST_MECHANISM_TRAIN_WAIT = 1,
	SG_HOST_MECHANISM_TRAIN_BLOCKED,
	SG_HOST_MECHANISM_TRAIN_USE
} sg_host_mechanism_train_event_t;

typedef enum sg_host_mechanism_blocker_kind_e
{
	SG_HOST_MECHANISM_BLOCKER_NONE = 0,
	SG_HOST_MECHANISM_BLOCKER_CLIENT,
	SG_HOST_MECHANISM_BLOCKER_MONSTER,
	SG_HOST_MECHANISM_BLOCKER_OTHER
} sg_host_mechanism_blocker_kind_t;

typedef struct sg_host_mechanism_transition_s
{
	int accepted;
	int stopped;
	int reversed;
	int started;
	int immediate;
	int damaged;
	int destroyed;
	int next_state;
	sg_host_mechanism_blocker_kind_t blocker_kind;
	uint32_t damage;
	uint64_t next_think_ms;
	uint64_t next_debounce_ms;
} sg_host_mechanism_transition_t;

void SG_HostMechanismLawDefault(sg_host_mechanism_law_t *law_out);
/* Implemented by the live mover owner (g_func.c). */
int SG_HostMechanismLiveCapture(sg_host_mechanism_law_t *law_out);
int SG_HostMechanismLawValid(const sg_host_mechanism_law_t *law);
int SG_HostMechanismMoveSchedule(const sg_host_mechanism_law_t *law,
	float distance, float speed, float accel, float decel, int current_entity,
	sg_host_mechanism_move_result_t *result_out);
int SG_HostMechanismMoveFrames(const sg_host_mechanism_law_t *law,
	float distance, float speed, float accel, float decel, int current_entity,
	sg_host_mechanism_move_frame_fn frame, void *frame_context,
	sg_host_mechanism_move_result_t *result_out);
/* Exact AngleMove_Calc/AngleMove_Begin completion schedule for a finite
 * angular pusher.  Unlike Move_Calc, stock AngleMove ignores acceleration
 * fields and derives velocity from the whole angular displacement. */
int SG_HostMechanismAngleMoveSchedule(const sg_host_mechanism_law_t *law,
	const float angular_delta[3], float speed, int current_entity,
	sg_host_mechanism_move_result_t *result_out);
int SG_HostMechanismAngleMoveFrames(const sg_host_mechanism_law_t *law,
	const float angular_delta[3], float speed, int current_entity,
	sg_host_mechanism_angle_frame_fn frame, void *frame_context,
	sg_host_mechanism_move_result_t *result_out);
/* Exact G_Push/SV_Push component clamp used for every pusher frame. */
int SG_HostMechanismPushDisplacement(float input, float *output);
int SG_HostMechanismDoorStep(const sg_host_mechanism_law_t *law,
	sg_host_mechanism_door_event_t event, uint32_t flags, int state,
	float wait_seconds, uint64_t now_ms, uint64_t debounce_until_ms,
	sg_host_mechanism_transition_t *result_out);
int SG_HostMechanismDoorStepEx(const sg_host_mechanism_law_t *law,
	sg_host_mechanism_door_event_t event, uint32_t flags, int state,
	float wait_seconds, uint64_t now_ms, uint64_t debounce_until_ms,
	sg_host_mechanism_blocker_kind_t blocker_kind, uint32_t damage,
	sg_host_mechanism_transition_t *result_out);
int SG_HostMechanismPlatformStep(const sg_host_mechanism_law_t *law,
	sg_host_mechanism_platform_event_t event, int state, uint64_t now_ms,
	uint64_t debounce_until_ms, sg_host_mechanism_transition_t *result_out);
int SG_HostMechanismPlatformStepEx(const sg_host_mechanism_law_t *law,
	sg_host_mechanism_platform_event_t event, int state, uint64_t now_ms,
	uint64_t debounce_until_ms, sg_host_mechanism_blocker_kind_t blocker_kind,
	uint32_t damage, sg_host_mechanism_transition_t *result_out);
int SG_HostMechanismTriggerStep(const sg_host_mechanism_law_t *law,
	int already_triggered, float wait_seconds, uint64_t now_ms,
	sg_host_mechanism_transition_t *result_out);
int SG_HostMechanismTrainStep(const sg_host_mechanism_law_t *law,
	sg_host_mechanism_train_event_t event, uint32_t flags, float wait_seconds,
	int state, int has_target, int has_current_target,
	sg_host_mechanism_blocker_kind_t blocker_kind, uint32_t damage,
	uint64_t now_ms, uint64_t debounce_until_ms,
	sg_host_mechanism_transition_t *result_out);

#endif /* SG_HOST_MECHANISM_LAW_H */
