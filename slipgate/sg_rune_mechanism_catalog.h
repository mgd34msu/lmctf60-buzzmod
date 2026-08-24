/* sg_rune_mechanism_catalog.h -- immutable post-spawn mechanism inventory. */
#ifndef SG_RUNE_MECHANISM_CATALOG_H
#define SG_RUNE_MECHANISM_CATALOG_H

#include <stdint.h>

#include "sg_action_contract.generated.h"

static inline int SG_MechanismControllerUsesButton(int controller_kind)
{
	return controller_kind == SG_MECHANISM_CONTROLLER_BUTTON_DOOR ||
	       controller_kind == SG_MECHANISM_CONTROLLER_TIMED_VAULT;
}

#define SG_MECH_NO_KEY UINT32_MAX
#define SG_MECH_MAX_Q8 UINT32_C(262136)

struct edict_s;
struct rune_mechanism_node_s;
struct rune_mechanism_edge_s;

/* Semantic catalog values are native. The byte codec owns
 * the checked mapping from these values to its serialized enum. */
typedef enum sg_mech_node_kind_e
{
	SG_MECH_NODE_TRIGGER = 1,
	SG_MECH_NODE_BUTTON = 2,
	SG_MECH_NODE_RELAY = 3,
	SG_MECH_NODE_DOOR_MASTER = 4,
	SG_MECH_NODE_DOOR_MEMBER = 5,
	SG_MECH_NODE_AUTO_DOOR_TRIGGER = 6,
	SG_MECH_NODE_PLATFORM = 7,
	SG_MECH_NODE_PLATFORM_TRIGGER = 8,
	SG_MECH_NODE_TRAIN = 9,
	SG_MECH_NODE_PATH_CORNER = 10,
	SG_MECH_NODE_ELEVATOR = 11,
	SG_MECH_NODE_PUSH = 12,
	SG_MECH_NODE_TELEPORTER = 13,
	SG_MECH_NODE_TELEPORT_TRIGGER = 14,
	SG_MECH_NODE_TELEPORT_DEST = 15,
	SG_MECH_NODE_OBJECTIVE = 16,
	SG_MECH_NODE_SECRET_DOOR = 17,
	SG_MECH_NODE_OTHER_TRIGGER = 18,
	SG_MECH_NODE_OTHER_MOVER = 19,
	SG_MECH_NODE_CONTEXTUAL = 20,
	SG_MECH_NODE_TARGET_SPEAKER = 21,
	SG_MECH_NODE_AREAPORTAL = 22,
	SG_MECH_NODE_TOGGLE_WALL = 23,
	SG_MECH_NODE_TRIGGER_HURT = 24,
	SG_MECH_NODE_TARGET_LASER = 25
} sg_mech_node_kind_t;

typedef enum sg_mech_node_flag_e
{
	SG_MECH_NODEF_SYNTHETIC = 1U,
	SG_MECH_NODEF_REPEATABLE = 2U,
	SG_MECH_NODEF_TOUCHABLE = 4U,
	SG_MECH_NODEF_USABLE = 8U,
	SG_MECH_NODEF_MOVER = 16U,
	SG_MECH_NODEF_TEAM_MASTER = 32U,
	SG_MECH_NODEF_TEAM_MEMBER = 64U,
	SG_MECH_NODEF_INVENTORY_ONLY = 128U,
	SG_MECH_NODEF_ONE_SHOT = 256U,
	SG_MECH_NODEF_SHOOTABLE = 512U,
	SG_MECH_NODEF_START_DISABLED = 1024U,
	SG_MECH_NODEF_FRAME_COMPLETE_MOVER = 2048U
} sg_mech_node_flag_t;

#define SG_MECH_NODE_FLAG_MASK UINT16_C(0x0fff)

typedef enum sg_mech_callback_e
{
	SG_MECH_CALLBACK_NONE = 0,
	SG_MECH_CALLBACK_TOUCH_MULTI = 1,
	SG_MECH_CALLBACK_TOUCH_DOOR_TRIGGER = 2,
	SG_MECH_CALLBACK_BUTTON_TOUCH = 3,
	SG_MECH_CALLBACK_USE_MULTI = 4,
	SG_MECH_CALLBACK_BUTTON_USE = 5,
	SG_MECH_CALLBACK_THINK_MULTI_WAIT = 6,
	SG_MECH_CALLBACK_THINK_BUTTON_WAIT = 7,
	SG_MECH_CALLBACK_BLOCKED_DOOR = 8,
	SG_MECH_CALLBACK_USE_TRIGGER_RELAY = 9,
	SG_MECH_CALLBACK_USE_DOOR = 10,
	SG_MECH_CALLBACK_THINK_CALC_MOVE_SPEED = 11,
	SG_MECH_CALLBACK_THINK_SPAWN_DOOR_TRIGGER = 12,
	SG_MECH_CALLBACK_TRIGGER_ENABLE = 13,
	SG_MECH_CALLBACK_TOUCH_PLAT_CENTER = 14,
	SG_MECH_CALLBACK_USE_PLAT = 15,
	SG_MECH_CALLBACK_PLAT_GO_DOWN = 16,
	SG_MECH_CALLBACK_BLOCKED_PLAT = 17,
	SG_MECH_CALLBACK_TRAIN_USE = 18,
	SG_MECH_CALLBACK_FUNC_TRAIN_FIND = 19,
	SG_MECH_CALLBACK_TRAIN_NEXT = 20,
	SG_MECH_CALLBACK_TRAIN_WAIT = 21,
	SG_MECH_CALLBACK_BLOCKED_TRAIN = 22,
	SG_MECH_CALLBACK_TRIGGER_ELEVATOR_USE = 23,
	SG_MECH_CALLBACK_TRIGGER_ELEVATOR_INIT = 24,
	SG_MECH_CALLBACK_TRIGGER_PUSH_TOUCH = 25,
	SG_MECH_CALLBACK_TELEPORTER_TOUCH = 26,
	SG_MECH_CALLBACK_PATH_CORNER_TOUCH = 27,
	SG_MECH_CALLBACK_TOUCH_ITEM = 28,
	SG_MECH_CALLBACK_THINK_DELAY = 29,
	SG_MECH_CALLBACK_SECRET_DOOR_USE = 30,
	SG_MECH_CALLBACK_SECRET_DOOR_BLOCKED = 31,
	SG_MECH_CALLBACK_USE_TARGET_SPEAKER = 32,
	SG_MECH_CALLBACK_USE_AREAPORTAL = 33,
	SG_MECH_CALLBACK_USE_FUNC_WALL = 34,
	SG_MECH_CALLBACK_TOUCH_HURT = 35,
	SG_MECH_CALLBACK_USE_HURT = 36,
	SG_MECH_CALLBACK_USE_TARGET_LASER = 37,
	SG_MECH_CALLBACK_THINK_TARGET_LASER = 38,
	SG_MECH_CALLBACK_UNKNOWN = 65535
} sg_mech_callback_t;

typedef enum sg_mech_edge_kind_e
{
	SG_MECH_EDGE_TARGET = 1,
	SG_MECH_EDGE_KILLTARGET = 2,
	SG_MECH_EDGE_OWNER = 3,
	SG_MECH_EDGE_TEAM = 4,
	SG_MECH_EDGE_PATH_TARGET = 5,
	SG_MECH_EDGE_MOVE_TARGET = 6,
	SG_MECH_EDGE_TARGET_ENT = 7,
	SG_MECH_EDGE_ENEMY = 8,
	SG_MECH_EDGE_ROUTE_TARGET = 9
} sg_mech_edge_kind_t;

typedef enum sg_mech_synthetic_kind_e
{
	SG_MECH_SYNTHETIC_NONE = 0,
	SG_MECH_SYNTHETIC_AUTO_DOOR,
	SG_MECH_SYNTHETIC_PLATFORM,
	SG_MECH_SYNTHETIC_TELEPORT
} sg_mech_synthetic_kind_t;

/* A sealed entity may enter only these stock callback states while an
 * authenticated controller owns it.  The catalog maps function pointers into
 * this neutral model; unknown pointers never become valid by address alone. */
typedef enum sg_mech_execution_think_e
{
	SG_MECH_EXEC_THINK_SEALED = 0,
	SG_MECH_EXEC_THINK_MULTI_WAIT,
	SG_MECH_EXEC_THINK_LINEAR_BEGIN,
	SG_MECH_EXEC_THINK_LINEAR_FINAL,
	SG_MECH_EXEC_THINK_LINEAR_DONE,
	SG_MECH_EXEC_THINK_ANGULAR_BEGIN,
	SG_MECH_EXEC_THINK_ANGULAR_FINAL,
	SG_MECH_EXEC_THINK_ANGULAR_DONE,
	SG_MECH_EXEC_THINK_ACCELERATED,
	SG_MECH_EXEC_THINK_DOOR_RETURN,
	SG_MECH_EXEC_THINK_BUTTON_RETURN,
	SG_MECH_EXEC_THINK_PLATFORM_RETURN,
	SG_MECH_EXEC_THINK_UNKNOWN
} sg_mech_execution_think_t;

typedef enum sg_mech_execution_end_e
{
	SG_MECH_EXEC_END_NONE = 0,
	SG_MECH_EXEC_END_DOOR_DESTINATION,
	SG_MECH_EXEC_END_DOOR_ORIGIN,
	SG_MECH_EXEC_END_BUTTON_DESTINATION,
	SG_MECH_EXEC_END_BUTTON_ORIGIN,
	SG_MECH_EXEC_END_PLATFORM_DESTINATION,
	SG_MECH_EXEC_END_PLATFORM_ORIGIN,
	SG_MECH_EXEC_END_TRAIN_CORNER,
	SG_MECH_EXEC_END_UNKNOWN
} sg_mech_execution_end_t;

typedef enum sg_mech_motion_state_e
{
	SG_MECH_MOTION_AT_DESTINATION = 0,
	SG_MECH_MOTION_AT_ORIGIN = 1,
	SG_MECH_MOTION_TO_DESTINATION = 2,
	SG_MECH_MOTION_TO_ORIGIN = 3
} sg_mech_motion_state_t;

typedef enum sg_mech_platform_profile_e
{
	SG_MECH_PLATFORM_PROFILE_NONE = 0,
	SG_MECH_PLATFORM_PROFILE_STOCK,
	SG_MECH_PLATFORM_PROFILE_DOOR_CARRIER,
	SG_MECH_PLATFORM_PROFILE_BUTTON_ENTRY
} sg_mech_platform_profile_t;

typedef enum sg_mech_train_gate_pose_e
{
	SG_MECH_TRAIN_GATE_CLOSED = 0,
	SG_MECH_TRAIN_GATE_OPENING,
	SG_MECH_TRAIN_GATE_OPEN,
	SG_MECH_TRAIN_GATE_CLOSING,
	SG_MECH_TRAIN_GATE_INTERRUPTED,
	SG_MECH_TRAIN_GATE_INVALID
} sg_mech_train_gate_pose_t;

/* Neutralized stock func_train facts.  The adapter derives these booleans
 * from exact entity identities and terminal origins. */
typedef struct sg_mech_train_gate_state_s
{
	uint16_t controller_kind;
	uint16_t node_kind;
	uint16_t think_role;
	uint16_t end_role;
	int fixed_callbacks_match;
	int at_closed;
	int at_open;
	int target_is_closed;
	int target_is_open;
	int target_ent_is_none;
	int target_ent_is_closed;
	int target_ent_is_open;
	int start_on;
	int moving;
	int stopped;
	int nextthink_pending;
} sg_mech_train_gate_state_t;

static inline sg_mech_train_gate_pose_t SG_MechTrainGatePose(
	const sg_mech_train_gate_state_t *state)
{
	int moving_think;
	int target_count;
	int target_ent_count;

	if (!state || !state->fixed_callbacks_match ||
	    state->controller_kind != SG_MECHANISM_CONTROLLER_TRAIN ||
	    state->node_kind != SG_MECH_NODE_TRAIN)
		return SG_MECH_TRAIN_GATE_INVALID;
	target_count = !!state->target_is_closed + !!state->target_is_open;
	target_ent_count = !!state->target_ent_is_none +
		!!state->target_ent_is_closed + !!state->target_ent_is_open;
	if (target_count != 1 || target_ent_count != 1 ||
	    (state->at_closed && state->at_open))
		return SG_MECH_TRAIN_GATE_INVALID;
	moving_think = state->think_role == SG_MECH_EXEC_THINK_LINEAR_BEGIN ||
		state->think_role == SG_MECH_EXEC_THINK_LINEAR_FINAL ||
		state->think_role == SG_MECH_EXEC_THINK_LINEAR_DONE ||
		state->think_role == SG_MECH_EXEC_THINK_ACCELERATED;
	if (state->at_closed && state->target_is_open &&
	    state->target_ent_is_none && !state->start_on && !state->moving &&
	    state->stopped && !state->nextthink_pending &&
	    state->think_role == SG_MECH_EXEC_THINK_SEALED &&
	    state->end_role == SG_MECH_EXEC_END_NONE)
		return SG_MECH_TRAIN_GATE_CLOSED;
	if (state->at_closed && state->target_is_closed &&
	    state->target_ent_is_open && !state->start_on && !state->moving &&
	    state->stopped && !state->nextthink_pending && moving_think &&
	    state->end_role == SG_MECH_EXEC_END_TRAIN_CORNER)
		return SG_MECH_TRAIN_GATE_CLOSED;
	if (state->target_is_closed && state->target_ent_is_open &&
	    state->start_on && state->moving &&
	    state->nextthink_pending && moving_think &&
	    state->end_role == SG_MECH_EXEC_END_TRAIN_CORNER && !state->at_open)
		return SG_MECH_TRAIN_GATE_OPENING;
	if (state->at_open && state->target_is_open &&
	    state->target_ent_is_closed && !state->start_on && !state->moving &&
	    state->stopped && !state->nextthink_pending && moving_think &&
	    state->end_role == SG_MECH_EXEC_END_TRAIN_CORNER)
		return SG_MECH_TRAIN_GATE_OPEN;
	if (state->target_is_open && state->target_ent_is_closed &&
	    state->start_on && state->moving &&
	    state->nextthink_pending && moving_think &&
	    state->end_role == SG_MECH_EXEC_END_TRAIN_CORNER && !state->at_closed)
		return SG_MECH_TRAIN_GATE_CLOSING;
	if (!state->at_closed && !state->at_open && !state->start_on &&
	    !state->moving && state->stopped && !state->nextthink_pending &&
	    moving_think && state->end_role == SG_MECH_EXEC_END_TRAIN_CORNER)
		return SG_MECH_TRAIN_GATE_INTERRUPTED;
	return SG_MECH_TRAIN_GATE_INVALID;
}

static inline int SG_MechTrainGateExecutionStateValid(
	const sg_mech_train_gate_state_t *state)
{
	sg_mech_train_gate_pose_t pose = SG_MechTrainGatePose(state);

	return pose == SG_MECH_TRAIN_GATE_CLOSED ||
	       pose == SG_MECH_TRAIN_GATE_OPENING ||
	       pose == SG_MECH_TRAIN_GATE_OPEN;
}

int SG_MechCatalogTrainGatePose(uint32_t key,
	sg_mech_train_gate_pose_t *pose_out);
int SG_MechCatalogTrainGateSweep(uint32_t key, float mins_out[3],
	float maxs_out[3]);

typedef struct sg_mech_execution_state_s
{
	uint16_t controller_kind;
	uint16_t node_kind;
	uint16_t think_role;
	uint16_t end_role;
	uint16_t platform_profile;
	int motion_state;
	int fixed_callbacks_match;
	int touch_matches;
	int touch_cleared;
	int nextthink_pending;
	int stopped;
} sg_mech_execution_state_t;

static inline int SG_MechExecutionMovingThink(uint16_t think_role,
	int angular_allowed, int accelerated_allowed)
{
	return think_role == SG_MECH_EXEC_THINK_LINEAR_BEGIN ||
	       think_role == SG_MECH_EXEC_THINK_LINEAR_FINAL ||
	       think_role == SG_MECH_EXEC_THINK_LINEAR_DONE ||
	       (angular_allowed &&
	        (think_role == SG_MECH_EXEC_THINK_ANGULAR_BEGIN ||
	         think_role == SG_MECH_EXEC_THINK_ANGULAR_FINAL ||
	         think_role == SG_MECH_EXEC_THINK_ANGULAR_DONE)) ||
	       (accelerated_allowed &&
	        think_role == SG_MECH_EXEC_THINK_ACCELERATED);
}

static inline int SG_MechExecutionStateValid(
	const sg_mech_execution_state_t *state)
{
	int door_mover;
	int moving;
	int toward_destination;
	int endpoint;

	if (!state || !state->fixed_callbacks_match)
		return 0;
	door_mover = state->node_kind == SG_MECH_NODE_DOOR_MASTER ||
	             state->node_kind == SG_MECH_NODE_DOOR_MEMBER;
	if (state->controller_kind == SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR &&
	    state->node_kind == SG_MECH_NODE_TRIGGER)
		return state->touch_matches && !state->touch_cleared &&
		       state->end_role == SG_MECH_EXEC_END_NONE &&
		       (state->think_role == SG_MECH_EXEC_THINK_SEALED ||
		        state->think_role == SG_MECH_EXEC_THINK_MULTI_WAIT);
	if (state->controller_kind == SG_MECHANISM_CONTROLLER_PLATFORM &&
	    state->node_kind == SG_MECH_NODE_TRIGGER)
		return state->touch_matches && !state->touch_cleared &&
		       state->end_role == SG_MECH_EXEC_END_NONE &&
		       (state->think_role == SG_MECH_EXEC_THINK_SEALED ||
		        state->think_role == SG_MECH_EXEC_THINK_MULTI_WAIT);
	if ((state->controller_kind == SG_MECHANISM_CONTROLLER_AUTO_DOOR ||
	     state->controller_kind ==
	         SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR ||
	     state->controller_kind == SG_MECHANISM_CONTROLLER_BUTTON_DOOR ||
	     state->controller_kind == SG_MECHANISM_CONTROLLER_TIMED_VAULT ||
	     state->controller_kind == SG_MECHANISM_CONTROLLER_TRAIN_SHOOT ||
	     state->controller_kind == SG_MECHANISM_CONTROLLER_PLATFORM) &&
	    door_mover)
	{
		if ((!state->touch_matches && !state->touch_cleared) ||
		    state->end_role == SG_MECH_EXEC_END_UNKNOWN)
			return 0;
		moving = SG_MechExecutionMovingThink(state->think_role, 1, 0);
		toward_destination =
			state->motion_state == SG_MECH_MOTION_TO_DESTINATION;
		endpoint = state->motion_state == SG_MECH_MOTION_AT_DESTINATION ||
		           state->motion_state == SG_MECH_MOTION_AT_ORIGIN;
		if (state->think_role == SG_MECH_EXEC_THINK_SEALED)
			return state->motion_state == SG_MECH_MOTION_AT_ORIGIN &&
			       state->end_role == SG_MECH_EXEC_END_NONE && state->stopped;
		if (state->think_role == SG_MECH_EXEC_THINK_DOOR_RETURN)
			return state->motion_state == SG_MECH_MOTION_AT_DESTINATION &&
			       state->end_role == SG_MECH_EXEC_END_DOOR_DESTINATION &&
			       state->nextthink_pending && state->stopped;
		if (!moving)
			return 0;
		if (toward_destination)
			return state->end_role ==
			       SG_MECH_EXEC_END_DOOR_DESTINATION;
		if (state->motion_state == SG_MECH_MOTION_TO_ORIGIN)
			return state->end_role == SG_MECH_EXEC_END_DOOR_ORIGIN;
		return endpoint && state->stopped &&
		       ((state->motion_state == SG_MECH_MOTION_AT_DESTINATION &&
		         state->end_role == SG_MECH_EXEC_END_DOOR_DESTINATION) ||
		        (state->motion_state == SG_MECH_MOTION_AT_ORIGIN &&
		         state->end_role == SG_MECH_EXEC_END_DOOR_ORIGIN));
	}
	if ((((state->controller_kind == SG_MECHANISM_CONTROLLER_BUTTON_DOOR ||
	       state->controller_kind == SG_MECHANISM_CONTROLLER_TIMED_VAULT) &&
	      state->node_kind == SG_MECH_NODE_BUTTON) ||
	     (state->controller_kind == SG_MECHANISM_CONTROLLER_PLATFORM &&
	      state->node_kind == SG_MECH_NODE_PLATFORM_TRIGGER &&
	      state->platform_profile == SG_MECH_PLATFORM_PROFILE_BUTTON_ENTRY) ||
	     state->controller_kind == SG_MECHANISM_CONTROLLER_TRAIN ||
	     state->controller_kind == SG_MECHANISM_CONTROLLER_TRAIN_SHOOT) &&
	    (state->node_kind == SG_MECH_NODE_BUTTON ||
	     state->platform_profile == SG_MECH_PLATFORM_PROFILE_BUTTON_ENTRY))
	{
		if (!state->touch_matches || state->touch_cleared ||
		    state->end_role == SG_MECH_EXEC_END_UNKNOWN)
			return 0;
		moving = SG_MechExecutionMovingThink(state->think_role, 0, 1);
		if (state->think_role == SG_MECH_EXEC_THINK_SEALED)
			return state->motion_state == SG_MECH_MOTION_AT_ORIGIN &&
			       state->end_role == SG_MECH_EXEC_END_NONE && state->stopped;
		if (state->think_role == SG_MECH_EXEC_THINK_BUTTON_RETURN)
			return state->motion_state == SG_MECH_MOTION_AT_DESTINATION &&
			       state->end_role == SG_MECH_EXEC_END_BUTTON_DESTINATION &&
			       state->nextthink_pending && state->stopped;
		if (!moving)
			return 0;
		if (state->motion_state == SG_MECH_MOTION_TO_DESTINATION)
			return state->end_role ==
			       SG_MECH_EXEC_END_BUTTON_DESTINATION;
		if (state->motion_state == SG_MECH_MOTION_TO_ORIGIN)
			return state->end_role == SG_MECH_EXEC_END_BUTTON_ORIGIN;
		return state->stopped &&
		       ((state->motion_state == SG_MECH_MOTION_AT_DESTINATION &&
		         state->end_role == SG_MECH_EXEC_END_BUTTON_DESTINATION) ||
		        (state->motion_state == SG_MECH_MOTION_AT_ORIGIN &&
		         state->end_role == SG_MECH_EXEC_END_BUTTON_ORIGIN));
	}
	if (state->controller_kind == SG_MECHANISM_CONTROLLER_PLATFORM &&
	    state->node_kind == SG_MECH_NODE_PLATFORM_TRIGGER)
	{
		if (!state->touch_matches || state->touch_cleared ||
		    state->end_role != SG_MECH_EXEC_END_NONE || !state->stopped)
			return 0;
		if (state->think_role == SG_MECH_EXEC_THINK_SEALED)
			return !state->nextthink_pending;
		return state->think_role == SG_MECH_EXEC_THINK_MULTI_WAIT &&
		       state->nextthink_pending;
	}
	if (state->controller_kind == SG_MECHANISM_CONTROLLER_PLATFORM &&
	    state->node_kind == SG_MECH_NODE_PLATFORM)
	{
		int carrier = state->platform_profile ==
			SG_MECH_PLATFORM_PROFILE_DOOR_CARRIER;
		int stock = state->platform_profile ==
			SG_MECH_PLATFORM_PROFILE_STOCK;

		if (!carrier && !stock)
			return 0;
		if (!state->touch_matches || state->touch_cleared ||
		    state->end_role == SG_MECH_EXEC_END_UNKNOWN)
			return 0;
		moving = SG_MechExecutionMovingThink(state->think_role, 0, stock);
		if (state->think_role == SG_MECH_EXEC_THINK_SEALED)
			return (state->motion_state == SG_MECH_MOTION_AT_ORIGIN ||
			        state->motion_state == SG_MECH_MOTION_AT_DESTINATION) &&
			       state->end_role == SG_MECH_EXEC_END_NONE && state->stopped;
		if ((stock && state->think_role ==
		         SG_MECH_EXEC_THINK_PLATFORM_RETURN) ||
		    (carrier && state->think_role == SG_MECH_EXEC_THINK_DOOR_RETURN))
			return state->motion_state == SG_MECH_MOTION_AT_DESTINATION &&
			       state->end_role == (stock
			           ? SG_MECH_EXEC_END_PLATFORM_DESTINATION
			           : SG_MECH_EXEC_END_DOOR_DESTINATION) &&
			       state->nextthink_pending && state->stopped;
		if (!moving)
			return 0;
		if (state->motion_state == SG_MECH_MOTION_TO_DESTINATION)
			return state->end_role == (stock
			    ? SG_MECH_EXEC_END_PLATFORM_DESTINATION
			    : SG_MECH_EXEC_END_DOOR_DESTINATION);
		if (state->motion_state == SG_MECH_MOTION_TO_ORIGIN)
			return state->end_role == (stock
			    ? SG_MECH_EXEC_END_PLATFORM_ORIGIN
			    : SG_MECH_EXEC_END_DOOR_ORIGIN);
		return state->stopped &&
		       ((state->motion_state == SG_MECH_MOTION_AT_DESTINATION &&
		         state->end_role == (stock
		             ? SG_MECH_EXEC_END_PLATFORM_DESTINATION
		             : SG_MECH_EXEC_END_DOOR_DESTINATION)) ||
		        (state->motion_state == SG_MECH_MOTION_AT_ORIGIN &&
		         state->end_role == (stock
		             ? SG_MECH_EXEC_END_PLATFORM_ORIGIN
		             : SG_MECH_EXEC_END_DOOR_ORIGIN)));
	}
	return state->touch_matches && !state->touch_cleared &&
	       state->think_role == SG_MECH_EXEC_THINK_SEALED &&
	       state->end_role == SG_MECH_EXEC_END_NONE;
}

typedef enum sg_mech_catalog_status_e
{
	SG_MECH_CATALOG_NOT_READY = 0,
	SG_MECH_CATALOG_BUILDING,
	SG_MECH_CATALOG_READY,
	SG_MECH_CATALOG_FAILED
} sg_mech_catalog_status_t;

typedef struct sg_mech_catalog_view_s
{
	const struct rune_mechanism_node_s *nodes;
	uint32_t num_nodes;
	const struct rune_mechanism_edge_s *edges;
	uint32_t num_edges;
	const unsigned char *strings;
	uint32_t string_bytes;
} sg_mech_catalog_view_t;

/* Process-local motion authority for a sealed stock button. The byte codec
 * authenticates the displacement in each BUTTON_DOOR link; these catalog
 * endpoints bind that displacement to the exact post-spawn entity
 * incarnation without expanding the wire node layout. */
typedef struct sg_mech_button_endpoints_s
{
	int16_t start_q8[3];
	int16_t end_q8[3];
} sg_mech_button_endpoints_t;

/* Begin is called after the new level identity starts and before entity parse.
 * Declared preserves source identity even when spawn code rewrites classname.
 * Synthetic records the three stock post-spawn trigger creation sites. */
void SG_MechCatalogBegin(void);
void SG_MechCatalogEntityInitialized(struct edict_s *entity);
void SG_MechCatalogDeclared(struct edict_s *entity, uint32_t source_ordinal,
	const char *original_classname);
void SG_MechCatalogSynthetic(struct edict_s *entity,
	struct edict_s *parent, sg_mech_synthetic_kind_t kind);
void SG_MechCatalogInvalidate(struct edict_s *entity);

/* Seal exactly once after the first complete G_RunEntity pass.  The resulting
 * TAG_LEVEL snapshot is immutable and is captured by Rune_Generate before it
 * changes door solidity for proof. */
sg_mech_catalog_status_t SG_MechCatalogSeal(void);
sg_mech_catalog_status_t SG_MechCatalogSnapshot(
	sg_mech_catalog_view_t *view_out);
const char *SG_MechCatalogReason(void);
int SG_MechCatalogMatches(const struct rune_mechanism_node_s *nodes,
	uint32_t num_nodes, const struct rune_mechanism_edge_s *inventory_edges,
	uint32_t num_inventory_edges, const unsigned char *strings,
	uint32_t string_bytes);
int SG_MechCatalogEntityMatches(uint32_t key,
	const struct rune_mechanism_node_s *node);
int SG_MechCatalogEntityTopologyMatches(uint32_t key,
	const struct rune_mechanism_node_s *node);
int SG_MechCatalogEntityExecutionMatches(uint32_t key,
	const struct rune_mechanism_node_s *node, uint16_t controller_kind);
/* Continuous station trains keep their sealed identity, team, callbacks, and
 * kinematics while stock train_next changes target, target_ent, think,
 * endfunc, wait, and origin.  This narrower matcher authenticates only the
 * immutable portion; the station adapter validates the live phase. */
int SG_MechCatalogStationTrainImmutableMatches(uint32_t key,
	const struct rune_mechanism_node_s *node);
struct edict_s *SG_MechCatalogResolveStationEntity(uint32_t key,
	const struct rune_mechanism_node_s *node);
int SG_MechCatalogEntityRetired(uint32_t key,
	const struct rune_mechanism_node_s *node);
/* Return the exact current process-local incarnation for one live edict.
 * This is deliberately independent of sealed-node selection: immutable
 * support geometry can belong to an ordinary map entity.  Both outputs are
 * cleared on failure; generation zero never names a live incarnation. */
int SG_MechCatalogEntityGeneration(const struct edict_s *entity,
	uint32_t *key_out, uint32_t *generation_out);
struct edict_s *SG_MechCatalogResolveEntity(uint32_t key,
	const struct rune_mechanism_node_s *node);
int SG_MechCatalogButtonBottomEndpoints(uint32_t key,
	const struct rune_mechanism_node_s *node, const struct edict_s *entity,
	sg_mech_button_endpoints_t *endpoints_out);
int SG_MechCatalogButtonEndpoints(uint32_t key,
	const struct rune_mechanism_node_s *node, const struct edict_s *entity,
	sg_mech_button_endpoints_t *endpoints_out);

#endif /* SG_RUNE_MECHANISM_CATALOG_H */
