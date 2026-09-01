/* sg_rune_codec.h -- isolated, allocation-free RUNE codec. */
#ifndef SG_RUNE_CODEC_H
#define SG_RUNE_CODEC_H

#include <stddef.h>
#include <stdint.h>

#include "sg_action.h"
#include "sg_rune_contract.h"

/* Serialized record geometry belongs only to this codec boundary. */
#define SG_RUNE_CODEC_MAGIC RUNE_ARTIFACT_MAGIC
#define SG_RUNE_CODEC_HEADER_BYTES RUNE_ARTIFACT_HEADER_BYTES
#define SG_RUNE_CODEC_SEED_BYTES 16U
#define SG_RUNE_CODEC_LINK_BYTES 48U
#define SG_RUNE_CODEC_MAP_NAME_BYTES RUNE_MAP_NAME_BYTES
#define SG_RUNE_CODEC_HEADER_CRC_OFFSET 60U
#define SG_RUNE_CODEC_ACTIVATION_PLAN_OFFSET 44U

#define SG_RUNE_CODEC_ACTIVATION_NODE_BYTES 92U
#define SG_RUNE_CODEC_ACTIVATION_EDGE_BYTES 16U
#define SG_RUNE_CODEC_ACTIVATION_PLAN_BYTES 32U
#define SG_RUNE_CODEC_MAX_SEEDS RUNE_MAX_SEEDS
#define SG_RUNE_CODEC_MAX_LINKS RUNE_MAX_LINKS
#define SG_RUNE_CODEC_MAX_ACTIVATION_NODES RUNE_MAX_MECHANISM_NODES
#define SG_RUNE_CODEC_MAX_ACTIVATION_EDGES RUNE_MAX_MECHANISM_EDGES
#define SG_RUNE_CODEC_MAX_ACTIVATION_PLANS RUNE_MAX_MECHANISM_PLANS
#define SG_RUNE_CODEC_MAX_PLAN_EDGES 65536U
#define SG_RUNE_CODEC_MAX_STRING_BYTES RUNE_MAX_MECHANISM_STRING_BYTES
#define SG_RUNE_CODEC_MAX_TIME_MS RUNE_MAX_COST_MS
#define SG_RUNE_CODEC_MAX_Q8 UINT32_C(262136)
#define SG_RUNE_CODEC_MAX_TEAM_MEMBERS RUNE_MAX_MECHANISM_MEMBERS

#define SG_RUNE_CODEC_NO_KEY UINT32_MAX
#define SG_RUNE_CODEC_NO_ACTIVATION_PLAN UINT32_MAX
#define SG_RUNE_CODEC_SEED_WATER UINT16_C(1)
#define SG_RUNE_CODEC_SEED_TOMBSTONE UINT16_C(2)
#define SG_RUNE_CODEC_SEED_FLAG_MASK \
	(SG_RUNE_CODEC_SEED_WATER | SG_RUNE_CODEC_SEED_TOMBSTONE)

typedef enum
{
	SG_RUNE_CODEC_NODE_NONE = 0,
	SG_RUNE_CODEC_NODE_TRIGGER = 1,
	SG_RUNE_CODEC_NODE_BUTTON = 2,
	SG_RUNE_CODEC_NODE_RELAY = 3,
	SG_RUNE_CODEC_NODE_DOOR_MASTER = 4,
	SG_RUNE_CODEC_NODE_DOOR_MEMBER = 5,
	SG_RUNE_CODEC_NODE_AUTO_DOOR_TRIGGER = 6,
	SG_RUNE_CODEC_NODE_PLATFORM = 7,
	SG_RUNE_CODEC_NODE_PLATFORM_TRIGGER = 8,
	SG_RUNE_CODEC_NODE_TRAIN = 9,
	SG_RUNE_CODEC_NODE_PATH_CORNER = 10,
	SG_RUNE_CODEC_NODE_ELEVATOR = 11,
	SG_RUNE_CODEC_NODE_PUSH = 12,
	SG_RUNE_CODEC_NODE_TELEPORTER = 13,
	SG_RUNE_CODEC_NODE_TELEPORT_TRIGGER = 14,
	SG_RUNE_CODEC_NODE_TELEPORT_DEST = 15,
	SG_RUNE_CODEC_NODE_OBJECTIVE = 16,
	SG_RUNE_CODEC_NODE_SECRET_DOOR = 17,
	SG_RUNE_CODEC_NODE_OTHER_TRIGGER = 18,
	SG_RUNE_CODEC_NODE_OTHER_MOVER = 19,
	SG_RUNE_CODEC_NODE_CONTEXTUAL = 20,
	SG_RUNE_CODEC_NODE_TARGET_SPEAKER = 21,
	SG_RUNE_CODEC_NODE_AREAPORTAL = 22,
	SG_RUNE_CODEC_NODE_TOGGLE_WALL = 23,
	SG_RUNE_CODEC_NODE_TRIGGER_HURT = 24,
	SG_RUNE_CODEC_NODE_TARGET_LASER = 25
} sg_rune_codec_node_kind_t;

typedef enum
{
	SG_RUNE_CODEC_NODEF_SYNTHETIC = 1U,
	SG_RUNE_CODEC_NODEF_REPEATABLE = 2U,
	SG_RUNE_CODEC_NODEF_TOUCHABLE = 4U,
	SG_RUNE_CODEC_NODEF_USABLE = 8U,
	SG_RUNE_CODEC_NODEF_MOVER = 16U,
	SG_RUNE_CODEC_NODEF_TEAM_MASTER = 32U,
	SG_RUNE_CODEC_NODEF_TEAM_MEMBER = 64U,
	SG_RUNE_CODEC_NODEF_INVENTORY_ONLY = 128U,
	SG_RUNE_CODEC_NODEF_ONE_SHOT = 256U,
	SG_RUNE_CODEC_NODEF_SHOOTABLE = 512U,
	SG_RUNE_CODEC_NODEF_START_DISABLED = 1024U,
	SG_RUNE_CODEC_NODEF_FRAME_COMPLETE_MOVER = 2048U
} sg_rune_codec_node_flag_t;
#define SG_RUNE_CODEC_NODE_FLAG_MASK UINT16_C(0x0fff)

typedef enum
{
	SG_RUNE_CODEC_CALLBACK_NONE = 0,
	SG_RUNE_CODEC_CALLBACK_TOUCH_MULTI = 1,
	SG_RUNE_CODEC_CALLBACK_TOUCH_DOOR_TRIGGER = 2,
	SG_RUNE_CODEC_CALLBACK_BUTTON_TOUCH = 3,
	SG_RUNE_CODEC_CALLBACK_USE_MULTI = 4,
	SG_RUNE_CODEC_CALLBACK_BUTTON_USE = 5,
	SG_RUNE_CODEC_CALLBACK_THINK_MULTI_WAIT = 6,
	SG_RUNE_CODEC_CALLBACK_THINK_BUTTON_WAIT = 7,
	SG_RUNE_CODEC_CALLBACK_BLOCKED_DOOR = 8,
	SG_RUNE_CODEC_CALLBACK_USE_TRIGGER_RELAY = 9,
	SG_RUNE_CODEC_CALLBACK_USE_DOOR = 10,
	SG_RUNE_CODEC_CALLBACK_THINK_CALC_MOVE_SPEED = 11,
	SG_RUNE_CODEC_CALLBACK_THINK_SPAWN_DOOR_TRIGGER = 12,
	SG_RUNE_CODEC_CALLBACK_TRIGGER_ENABLE = 13,
	SG_RUNE_CODEC_CALLBACK_TOUCH_PLAT_CENTER = 14,
	SG_RUNE_CODEC_CALLBACK_USE_PLAT = 15,
	SG_RUNE_CODEC_CALLBACK_PLAT_GO_DOWN = 16,
	SG_RUNE_CODEC_CALLBACK_BLOCKED_PLAT = 17,
	SG_RUNE_CODEC_CALLBACK_TRAIN_USE = 18,
	SG_RUNE_CODEC_CALLBACK_FUNC_TRAIN_FIND = 19,
	SG_RUNE_CODEC_CALLBACK_TRAIN_NEXT = 20,
	SG_RUNE_CODEC_CALLBACK_TRAIN_WAIT = 21,
	SG_RUNE_CODEC_CALLBACK_BLOCKED_TRAIN = 22,
	SG_RUNE_CODEC_CALLBACK_TRIGGER_ELEVATOR_USE = 23,
	SG_RUNE_CODEC_CALLBACK_TRIGGER_ELEVATOR_INIT = 24,
	SG_RUNE_CODEC_CALLBACK_TRIGGER_PUSH_TOUCH = 25,
	SG_RUNE_CODEC_CALLBACK_TELEPORTER_TOUCH = 26,
	SG_RUNE_CODEC_CALLBACK_PATH_CORNER_TOUCH = 27,
	SG_RUNE_CODEC_CALLBACK_TOUCH_ITEM = 28,
	SG_RUNE_CODEC_CALLBACK_THINK_DELAY = 29,
	SG_RUNE_CODEC_CALLBACK_SECRET_DOOR_USE = 30,
	SG_RUNE_CODEC_CALLBACK_SECRET_DOOR_BLOCKED = 31,
	SG_RUNE_CODEC_CALLBACK_USE_TARGET_SPEAKER = 32,
	SG_RUNE_CODEC_CALLBACK_USE_AREAPORTAL = 33,
	SG_RUNE_CODEC_CALLBACK_USE_FUNC_WALL = 34,
	SG_RUNE_CODEC_CALLBACK_TOUCH_HURT = 35,
	SG_RUNE_CODEC_CALLBACK_USE_HURT = 36,
	SG_RUNE_CODEC_CALLBACK_USE_TARGET_LASER = 37,
	SG_RUNE_CODEC_CALLBACK_THINK_TARGET_LASER = 38,
	SG_RUNE_CODEC_CALLBACK_UNKNOWN = 65535
} sg_rune_codec_callback_id_t;

typedef enum
{
	SG_RUNE_CODEC_EDGE_NONE = 0,
	SG_RUNE_CODEC_EDGE_TARGET = 1,
	SG_RUNE_CODEC_EDGE_KILLTARGET = 2,
	SG_RUNE_CODEC_EDGE_OWNER = 3,
	SG_RUNE_CODEC_EDGE_TEAM = 4,
	SG_RUNE_CODEC_EDGE_PATH_TARGET = 5,
	SG_RUNE_CODEC_EDGE_MOVE_TARGET = 6,
	SG_RUNE_CODEC_EDGE_TARGET_ENT = 7,
	SG_RUNE_CODEC_EDGE_ENEMY = 8,
	SG_RUNE_CODEC_EDGE_ROUTE_TARGET = 9
} sg_rune_codec_edge_kind_t;

typedef sg_mechanism_controller_t sg_rune_codec_controller_kind_t;
#define SG_RUNE_CODEC_CONTROLLER_NONE SG_MECHANISM_CONTROLLER_NONE
#define SG_RUNE_CODEC_CONTROLLER_AUTO_DOOR SG_MECHANISM_CONTROLLER_AUTO_DOOR
#define SG_RUNE_CODEC_CONTROLLER_DIRECT_TRIGGER_DOOR \
	SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR
#define SG_RUNE_CODEC_CONTROLLER_BUTTON_DOOR \
	SG_MECHANISM_CONTROLLER_BUTTON_DOOR
#define SG_RUNE_CODEC_CONTROLLER_RELAY_DOOR SG_MECHANISM_CONTROLLER_RELAY_DOOR
#define SG_RUNE_CODEC_CONTROLLER_PLATFORM SG_MECHANISM_CONTROLLER_PLATFORM
#define SG_RUNE_CODEC_CONTROLLER_TELEPORT SG_MECHANISM_CONTROLLER_TELEPORT
#define SG_RUNE_CODEC_CONTROLLER_PUSH SG_MECHANISM_CONTROLLER_PUSH
#define SG_RUNE_CODEC_CONTROLLER_TRAIN SG_MECHANISM_CONTROLLER_TRAIN
#define SG_RUNE_CODEC_CONTROLLER_TRAIN_SHOOT \
	SG_MECHANISM_CONTROLLER_TRAIN_SHOOT
#define SG_RUNE_CODEC_CONTROLLER_TIMED_VAULT \
	SG_MECHANISM_CONTROLLER_TIMED_VAULT
#define SG_RUNE_CODEC_CONTROLLER_TRAIN_STATION \
	SG_MECHANISM_CONTROLLER_TRAIN_STATION

typedef enum
{
	SG_RUNE_CODEC_PLANF_TOUCH = 1U,
	SG_RUNE_CODEC_PLANF_USE = 2U,
	SG_RUNE_CODEC_PLANF_ATOMIC = 4U,
	SG_RUNE_CODEC_PLANF_REQUIRES_LEASE = 8U,
	SG_RUNE_CODEC_PLANF_SHOOT = 16U
} sg_rune_codec_plan_flag_t;
#define SG_RUNE_CODEC_PLAN_FLAG_MASK UINT16_C(0x001f)

/* Shared RLW_* values remain stable; mechanism-specific failures occupy the
 * codec's reserved extension range. */
typedef enum
{
	RLCODEC_OK = RLW_OK,
	RLCODEC_BAD_MECHANISM_CONTRACT = 128,
	RLCODEC_BAD_ACTIVATION_NODE = 129,
	RLCODEC_BAD_ACTIVATION_EDGE = 130,
	RLCODEC_BAD_ACTIVATION_PLAN = 131,
	RLCODEC_BAD_STRING_POOL = 132,
	RLCODEC_DUPLICATE_NODE_KEY = 133,
	RLCODEC_BAD_MECHANISM_GRAPH = 134,
	RLCODEC_NONZERO_RESERVED = 135
} sg_rune_codec_diagnostic_t;

typedef struct sg_rune_codec_identity_s
{
	char map_name[SG_RUNE_CODEC_MAP_NAME_BYTES];
	uint32_t bsp_checksum;
	uint32_t entity_crc32;
	uint32_t physics_flags;
	float gravity;
	float airaccelerate;
	float maxvelocity;
	uint16_t pmove_substep_ms;
	uint16_t server_frame_ms;
	uint32_t host_physics_id;
} sg_rune_codec_identity_t;

typedef struct sg_rune_codec_seed_s
{
	float origin[3];
	int16_t area_hint;
	int16_t flags;
} sg_rune_codec_seed_t;

typedef struct sg_rune_codec_header_s
{
	/* Byte offsets are stable. Bytes 4..5 are reserved and must be zero. */
	uint32_t magic;
	uint16_t header_bytes;
	uint16_t seed_bytes;
	uint16_t link_bytes;
	uint32_t num_seeds;
	uint32_t num_links;
	uint32_t payload_crc32;
	uint32_t bsp_checksum;
	uint32_t entity_crc32;
	uint32_t action_contract_crc32;
	uint32_t physics_flags;
	float gravity;
	float airaccelerate;
	float maxvelocity;
	uint16_t pmove_substep_ms;
	uint16_t server_frame_ms;
	uint32_t host_physics_id;
	uint32_t header_crc32;
	char map_name[SG_RUNE_CODEC_MAP_NAME_BYTES];

	/* Authenticated mechanism extension, bytes 128..159. */
	uint16_t activation_node_bytes;
	uint16_t activation_edge_bytes;
	uint16_t activation_plan_bytes;
	uint32_t num_activation_nodes;
	uint32_t num_activation_edges;
	uint32_t num_activation_plans;
	uint32_t string_bytes;
	uint32_t mechanism_contract_crc32;
	/* The inventory edge prefix is exhaustive and may be followed by optional
	 * plan-edge copies. */
	uint32_t num_inventory_edges;
} sg_rune_codec_header_t;

typedef struct sg_rune_codec_link_s
{
	/* Stable 44-byte action-graph record. */
	uint32_t source;
	uint32_t destination;
	uint8_t action;
	uint8_t provenance;
	uint8_t min_speed;
	uint8_t heading;
	uint8_t heading_slack;
	uint8_t exit_speed;
	int16_t cost_ms;
	float suffix_anchor[3];
	/* Action-discriminated bytes 28..39; see the generated secondary-control
	 * policy before interpreting these as mechanism authority. */
	float mechanism_anchor[3];
	uint16_t sweep_clear_ms;
	uint8_t mode;
	uint8_t reserved;
	/* Index in the activation-plan array, or UINT32_MAX. */
	uint32_t activation_plan;
} sg_rune_codec_link_t;

typedef struct sg_rune_codec_activation_node_s
{
	uint32_t key;
	uint16_t kind;
	uint16_t flags;
	uint32_t classname_offset;
	uint32_t target_offset;
	uint32_t targetname_offset;
	uint32_t killtarget_offset;
	uint32_t owner_key;
	uint32_t team_master_key;
	uint32_t spawnflags;
	uint16_t touch_callback;
	uint16_t use_callback;
	uint16_t think_callback;
	uint16_t blocked_callback;
	int32_t delay_ms;
	int32_t wait_ms;
	uint32_t speed_q8;
	uint32_t accel_q8;
	uint32_t decel_q8;
	int16_t absmin_q8[3];
	int16_t absmax_q8[3];
	uint32_t path_target_offset;
	float push_velocity[3];
} sg_rune_codec_activation_node_t;

typedef struct sg_rune_codec_activation_edge_s
{
	uint32_t from_key;
	uint32_t to_key;
	uint16_t kind;
	uint16_t ordinal;
	uint32_t delay_ms;
} sg_rune_codec_activation_edge_t;

typedef struct sg_rune_codec_activation_plan_s
{
	uint32_t entry_key;
	uint32_t mover_key;
	uint32_t first_edge;
	uint32_t num_edges;
	uint16_t controller_kind;
	uint16_t flags;
	uint16_t expected_members;
	uint32_t cooldown_ms;
	uint32_t closure_crc32;
} sg_rune_codec_activation_plan_t;

/* All scratch storage is caller-owned. Mechanism arrays support linear-memory
 * key, reference, closure, and cycle checks without allocation or recursion. */
typedef struct sg_rune_codec_workspace_s
{
	uint64_t *graph_link_keys;
	size_t graph_link_key_capacity;
	uint8_t *graph_source_marks;
	size_t graph_source_mark_capacity;

	uint32_t *plan_references;
	size_t plan_reference_capacity;
	uint32_t *node_references;
	size_t node_reference_capacity;
	uint32_t *node_heads;
	size_t node_head_capacity;
	uint32_t *node_indegrees;
	size_t node_indegree_capacity;
	uint32_t *node_generations;
	size_t node_generation_capacity;
	uint32_t *node_touched;
	size_t node_touched_capacity;
	uint32_t *node_queue;
	size_t node_queue_capacity;
	uint32_t *edge_next;
	size_t edge_next_capacity;
	uint8_t *string_marks;
	size_t string_mark_capacity;
} sg_rune_codec_workspace_t;

sg_rune_codec_diagnostic_t SG_RuneCodecFileSize(uint32_t num_seeds,
	uint32_t num_links, uint32_t num_nodes, uint32_t num_edges,
	uint32_t num_plans, uint32_t string_bytes, size_t *size_out);

sg_rune_codec_diagnostic_t SG_RuneCodecHeaderCRC32(
	const unsigned char *encoded, size_t encoded_size, uint32_t *crc_out);
sg_rune_codec_diagnostic_t SG_RuneCodecEncodeHeader(
	const sg_rune_codec_header_t *header, unsigned char *encoded,
	size_t encoded_size);
sg_rune_codec_diagnostic_t SG_RuneCodecDecodeHeader(
	const unsigned char *encoded, size_t encoded_size,
	sg_rune_codec_header_t *header_out);

sg_rune_codec_diagnostic_t SG_RuneCodecEncodeSeed(
	const sg_rune_codec_seed_t *seed, unsigned char *encoded,
	size_t encoded_size);
sg_rune_codec_diagnostic_t SG_RuneCodecDecodeSeed(
	const unsigned char *encoded, size_t encoded_size,
	sg_rune_codec_seed_t *seed_out);
sg_rune_codec_diagnostic_t SG_RuneCodecEncodeLink(
	const sg_rune_codec_link_t *link, unsigned char *encoded,
	size_t encoded_size);
sg_rune_codec_diagnostic_t SG_RuneCodecDecodeLink(
	const unsigned char *encoded, size_t encoded_size,
	sg_rune_codec_link_t *link_out);
sg_rune_codec_diagnostic_t SG_RuneCodecEncodeActivationNode(
	const sg_rune_codec_activation_node_t *node, unsigned char *encoded,
	size_t encoded_size);
sg_rune_codec_diagnostic_t SG_RuneCodecDecodeActivationNode(
	const unsigned char *encoded, size_t encoded_size,
	sg_rune_codec_activation_node_t *node_out);
sg_rune_codec_diagnostic_t SG_RuneCodecEncodeActivationEdge(
	const sg_rune_codec_activation_edge_t *edge, unsigned char *encoded,
	size_t encoded_size);
sg_rune_codec_diagnostic_t SG_RuneCodecDecodeActivationEdge(
	const unsigned char *encoded, size_t encoded_size,
	sg_rune_codec_activation_edge_t *edge_out);
sg_rune_codec_diagnostic_t SG_RuneCodecEncodeActivationPlan(
	const sg_rune_codec_activation_plan_t *plan, unsigned char *encoded,
	size_t encoded_size);
sg_rune_codec_diagnostic_t SG_RuneCodecDecodeActivationPlan(
	const unsigned char *encoded, size_t encoded_size,
	sg_rune_codec_activation_plan_t *plan_out);

sg_rune_codec_diagnostic_t SG_RuneCodecPlanClosureCRC32(
	const sg_rune_codec_activation_edge_t *edges, uint32_t first_edge,
	uint32_t num_edges, uint32_t total_edges, uint32_t *crc_out);
sg_rune_codec_diagnostic_t SG_RuneCodecPushClosureCRC32(uint32_t entry_key,
	const float push_velocity[3], uint32_t *crc_out);

sg_rune_codec_diagnostic_t SG_RuneCodecValidate(
	const sg_rune_codec_seed_t *seeds,
	uint32_t num_seeds,
	const sg_rune_codec_link_t *links, uint32_t num_links,
	const sg_rune_codec_activation_node_t *nodes, uint32_t num_nodes,
	const sg_rune_codec_activation_edge_t *edges, uint32_t num_edges,
	const sg_rune_codec_activation_plan_t *plans, uint32_t num_plans,
	const unsigned char *strings, uint32_t string_bytes,
	sg_rune_codec_workspace_t *workspace);

sg_rune_codec_diagnostic_t SG_RuneCodecMatchIdentity(
	const sg_rune_codec_header_t *header,
	const sg_rune_codec_identity_t *expected_identity);

sg_rune_codec_diagnostic_t SG_RuneCodecEncode(
	const sg_rune_codec_identity_t *identity,
	const sg_rune_codec_seed_t *seeds, uint32_t num_seeds,
	const sg_rune_codec_link_t *links, uint32_t num_links,
	const sg_rune_codec_activation_node_t *nodes, uint32_t num_nodes,
	const sg_rune_codec_activation_edge_t *edges, uint32_t num_edges,
	const sg_rune_codec_activation_plan_t *plans, uint32_t num_plans,
	const unsigned char *strings, uint32_t string_bytes,
	sg_rune_codec_workspace_t *workspace, unsigned char *encoded,
	size_t encoded_capacity, size_t *encoded_size_out);

/* Whole-file decode is transactional only at the publication boundary:
 * header_out is unchanged unless RLCODEC_OK is returned.  The record/string
 * destinations are caller-owned scratch and may be partially overwritten on
 * failure, so callers must publish none of them before success.  encoded,
 * expected_identity, every destination, the workspace object, and every
 * workspace array must be pairwise non-overlapping; detected overlap returns
 * RLW_INVALID_ARGUMENT before any destination record is written. */
sg_rune_codec_diagnostic_t SG_RuneCodecDecode(
	const unsigned char *encoded, size_t encoded_size,
	const sg_rune_codec_identity_t *expected_identity,
	sg_rune_codec_header_t *header_out,
	sg_rune_codec_seed_t *seeds, size_t seed_capacity,
	sg_rune_codec_link_t *links, size_t link_capacity,
	sg_rune_codec_activation_node_t *nodes, size_t node_capacity,
	sg_rune_codec_activation_edge_t *edges, size_t edge_capacity,
	sg_rune_codec_activation_plan_t *plans, size_t plan_capacity,
	unsigned char *strings, size_t string_capacity,
	sg_rune_codec_workspace_t *workspace);

#endif /* SG_RUNE_CODEC_H */
