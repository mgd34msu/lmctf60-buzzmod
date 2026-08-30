/* Deterministic collision/Pmove authority over an owned Quake II BSP. */
#ifndef SG_HOST_COLLISION_H
#define SG_HOST_COLLISION_H

#include <stddef.h>
#include <stdint.h>

#include "sg_bsp_world.h"
#include "sg_rune_model.h"

#define SG_HOST_COLLISION_MODEL_WORLD UINT32_C(0)
#define SG_HOST_COLLISION_TEXINFO_NONE UINT32_MAX
#define SG_HOST_GROUND_PROBE 0.25f
#define SG_HOST_GROUND_NORMAL_Z 0.7f
#define SG_HOST_STANDING_VIEW_HEIGHT 22.0f
#define SG_HOST_CROUCHING_VIEW_HEIGHT (-2.0f)

typedef uint32_t sg_host_collision_contents_t;
enum
{
	SG_HOST_CONTENTS_SOLID = UINT32_C(0x00000001),
	SG_HOST_CONTENTS_WINDOW = UINT32_C(0x00000002),
	SG_HOST_CONTENTS_LAVA = UINT32_C(0x00000008),
	SG_HOST_CONTENTS_SLIME = UINT32_C(0x00000010),
	SG_HOST_CONTENTS_WATER = UINT32_C(0x00000020),
	SG_HOST_CONTENTS_PLAYER_CLIP = UINT32_C(0x00010000),
	SG_HOST_CONTENTS_CURRENT_0 = UINT32_C(0x00040000),
	SG_HOST_CONTENTS_CURRENT_90 = UINT32_C(0x00080000),
	SG_HOST_CONTENTS_CURRENT_180 = UINT32_C(0x00100000),
	SG_HOST_CONTENTS_CURRENT_270 = UINT32_C(0x00200000),
	SG_HOST_CONTENTS_CURRENT_UP = UINT32_C(0x00400000),
	SG_HOST_CONTENTS_CURRENT_DOWN = UINT32_C(0x00800000),
	SG_HOST_CONTENTS_MONSTER = UINT32_C(0x02000000)
};

#define SG_HOST_MASK_PLAYER_SOLID \
	(SG_HOST_CONTENTS_SOLID | SG_HOST_CONTENTS_PLAYER_CLIP | \
	 SG_HOST_CONTENTS_WINDOW | SG_HOST_CONTENTS_MONSTER)
#define SG_HOST_MASK_WATER \
	(SG_HOST_CONTENTS_WATER | SG_HOST_CONTENTS_LAVA | SG_HOST_CONTENTS_SLIME)

enum
{
	SG_HOST_SURFACE_SLICK = INT32_C(2),
	SG_HOST_SURFACE_SKY = INT32_C(4),
	SG_HOST_SURFACE_LADDER = INT32_C(0x20000000)
};

typedef enum sg_host_collision_error_e
{
	SG_HOST_COLLISION_ERROR_NONE = 0,
	SG_HOST_COLLISION_ERROR_INVALID_ARGUMENT,
	SG_HOST_COLLISION_ERROR_INVALID_WORLD,
	SG_HOST_COLLISION_ERROR_INVALID_IDENTITY,
	SG_HOST_COLLISION_ERROR_INVALID_MODEL,
	SG_HOST_COLLISION_ERROR_INVALID_SCENE
} sg_host_collision_error_t;

typedef struct sg_host_collision_transform_s
{
	float origin[3];
	float angles[3];
} sg_host_collision_transform_t;

typedef struct sg_host_collision_instance_s
{
	/* Nonzero stable identity; scene evaluation is ordered by this value. */
	uint64_t instance_id;
	uint32_t model_index;
	sg_host_collision_transform_t transform;
} sg_host_collision_instance_t;

typedef struct sg_host_collision_scene_s
{
	const sg_host_collision_instance_t *instances;
	size_t instance_count;
} sg_host_collision_scene_t;

typedef struct sg_host_collision_plane_s
{
	float normal[3];
	float distance;
	int32_t type;
} sg_host_collision_plane_t;

typedef struct sg_host_collision_trace_s
{
	int allsolid;
	int startsolid;
	float fraction;
	float end[3];
	sg_host_collision_plane_t plane;
	sg_host_collision_contents_t contents;
	uint32_t texinfo;
	int32_t surface_flags;
	uint32_t model_index;
	uint64_t instance_id;
} sg_host_collision_trace_t;

typedef struct sg_host_collision_authority_s
{
	/* The BSP is borrowed and must remain immutable for this authority's life. */
	const sg_bsp_world_t *world;
	/* Copied from the loader-authenticated world at initialization. */
	sg_bsp_content_identity_t content_identity;
	/* Identity is copied so gravity, movement cvars, and hulls cannot drift. */
	sg_rune_model_identity_t identity;
} sg_host_collision_authority_t;

typedef struct sg_host_collision_pose_s
{
	int valid;
	sg_rune_stance_t stance;
	sg_rune_hull_profile_t hull;
	sg_host_collision_trace_t occupancy;
	sg_host_collision_trace_t support;
	int supported;
	int support_is_mover;
	uint8_t water_level;
	sg_host_collision_contents_t water_type;
	float gravity;
	uint64_t physics_abi_id;
} sg_host_collision_pose_t;

typedef struct sg_host_collision_transition_s
{
	int source_valid;
	int destination_valid;
	int clear;
	sg_host_collision_trace_t sweep;
} sg_host_collision_transition_t;

int SG_HostCollisionInit(sg_host_collision_authority_t *authority,
	const sg_bsp_world_t *world, const sg_rune_model_identity_t *identity,
	sg_host_collision_error_t *error_out);

sg_host_collision_contents_t SG_HostCollisionPointContentsModel(
	const sg_host_collision_authority_t *authority, uint32_t model_index,
	const sg_host_collision_transform_t *transform, const float point[3]);
sg_host_collision_contents_t SG_HostCollisionPointContents(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene, const float point[3]);
sg_rune_contents_mask_t SG_HostCollisionRuneContents(
	sg_host_collision_contents_t contents);

int SG_HostCollisionTraceModel(
	const sg_host_collision_authority_t *authority, uint32_t model_index,
	const sg_host_collision_transform_t *transform, const float start[3],
	const float mins[3], const float maxs[3], const float end[3],
	sg_host_collision_contents_t mask, sg_host_collision_trace_t *trace_out);
int SG_HostCollisionTrace(const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene, const float start[3],
	const float mins[3], const float maxs[3], const float end[3],
	sg_host_collision_contents_t mask, sg_host_collision_trace_t *trace_out);

int SG_HostCollisionClassifyPose(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene, const float origin[3],
	sg_rune_stance_t stance, sg_host_collision_pose_t *pose_out);
int SG_HostCollisionTransition(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene, const float start[3],
	const float end[3], sg_rune_stance_t stance,
	sg_host_collision_transition_t *transition_out);

const char *SG_HostCollisionErrorString(sg_host_collision_error_t error);

#endif
