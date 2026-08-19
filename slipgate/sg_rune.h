/*
 * sg_rune.h -- the RUNE: what a map affords, proven and written down.
 *
 * A rune is a link graph over the map's reachable movement states. Every link
 * it was demonstrated by the oracle -- the real physics rolled forward from
 * a concrete entry state -- before it was recorded. Nothing is inferred from
 * geometry heuristics; a link exists because a phantom traversed it.
 *
 * File: maps/<mapname>.rune. The generator installs the RUNE file atomically;
 * the runtime snapshots and validates the exact file before adapting it into
 * the native controller view below.
 */

#pragma once

#include <limits.h>

#include "sg_action.h"
#include "sg_identity.h"
#include "sg_rune_contract.h"

#define RUNE_PROOF_GRAVITY 800
#define RUNE_PROOF_AIRACCELERATE 0.0f
#define RUNE_HOOK_BOLT_SPEED 800.0f
#define RUNE_HOOK_FRAME_DISTANCE 80.0f
#define RUNE_HOOK_MAX_RAY 8192.0f
/* Dry graph hooks retain their proved entry-heading slack. Water-origin hooks
 * use an otherwise-invalid marker because their controller is a different
 * contract: actual-state online reproof, zero-input outbound drift, exact
 * attachment state, air reserve, and a dry destination. */
#define RUNE_HOOK_CONTROL_SLACK 24
#define RUNE_WATER_HOOK_CONTROL_MARKER 253
#define RUNE_HOOK_DRY_SETTLE_MS 1000
#define RUNE_HOOK_WATER_SETTLE_MS 1250
#define RUNE_TELEPORT_SEED_REACH 128.0f
#define RUNE_DROP_RECOVERY_RADIUS 96.0f
#define RUNE_DROP_RECOVERY_Z 72.0f

/* Action and provenance IDs are generated from rune_actions.json through the
 * canonical descriptor layer included above. */

/* Native seed flags are independent of their wire encoding. */
#define RSF_WATER	1       /* the seed is a point INSIDE a water volume, not
                             * a floor point: reached and left by swimming */
#define RSF_TOMBSTONE	2   /* retained geometry owner outside the closed
                             * two-objective route core; has no links and
                             * makes localization fail closed instead of
                             * snapping through it to a farther live seed */

/* Fail-closed native controller markers retained by the runtime adapter. */
#define RUNE_DECLARED_CONTROL_MARKER 254
#define RUNE_DROP_CONTROL_MARKER 254

typedef struct rune_seed_s
{
	vec3_t	origin;         /* on the floor, player-standable, proven --
	                         * or inside water when flags & RSF_WATER */
	short	area_hint;      /* reserved: coarse region id, 0 for now */
	short	flags;
} rune_seed_t;

typedef struct rune_link_s
{
	int		from, to;       /* seed indices */
	byte	action;         /* rune_action_t */
	byte	provenance;     /* rune_provenance_t */
	byte	min_speed;      /* entry envelope: speed/4, 0 = from rest */
	byte	heading;        /* entry heading, 0-255 wrapping the circle */
	byte	heading_slack;  /* +/- tolerance, same units */
	byte	exit_speed;     /* speed/4 the traversal ended with */
	short	cost_ms;        /* real traversal time, milliseconds */
	/*
	 * anchor: three floats whose meaning is the ACTION's, claimed in the order
	 * the actions were added and never shared. A reader that does not know an
	 * action must not read its anchor.
	 *
	 *   RL_HOOK       NOT a point. anchor[PITCH] and anchor[YAW] are the exact
	 *                 SHORT-quantized view angles used by the proof;
	 *                 anchor[ROLL] is the distance from the handed muzzle to the
	 *                 static-world bite along that ray. Together they reproduce
	 *                 both the shot control and expected bite without an inverse
	 *                 floating-point solve.
	 *   RL_DROP       a world point: the lip the mover steps off, found by
	 *                 ProveDrop and walked to before the fall was rolled.
	 *   RL_RUN        zero for a direct proof, otherwise the world-space
	 *                 detour apex the proved controller walked through.
	 *   RL_LIFT       the world-space bottom ride point that owns the plat's
	 *                 center trigger.
	 *   RL_TELEPORT   the world-space teleporter pad/trigger point.
	 *   RL_DOOR       the exact dry, sweep-clear wait point inside one unique
	 *                 validated repeatable trigger. That trigger may own several
	 *                 independent door teams; `from` begins the proved approach
	 *                 and `to` ends the proved open-pose egress.
	 *   RL_JUMP/RL_SWIM  unused, written as zero.
	 *   RL_ROCKETJUMP    registered but unsupported by the native runtime.
	 */
	vec3_t	anchor;
	/* Compound records retain their independent mechanism witness and
	 * temporal boundary in the native graph.  Noncompound records keep these
	 * fields exactly zero with mode RLCM_NONE. */
	vec3_t	mechanism_anchor;
	unsigned short sweep_clear_ms;
	byte	mode;
	/* Index in mechanism_plans, or RUNE_NO_MECHANISM_PLAN. */
	uint32_t mechanism_plan;
} rune_link_t;

#define RUNE_NO_MECHANISM_PLAN UINT32_MAX

/* Authenticated map and movement law. This is the shared identity consumed by
 * generation, runtime publication, sidecars, and future wire adapters. */
typedef struct rune_identity_s
{
	uint32_t bsp_checksum;
	uint32_t entity_crc32;
	uint32_t physics_flags;
	float gravity;
	float airaccelerate;
	float maxvelocity;
	uint16_t pmove_substep_ms;
	uint16_t server_frame_ms;
	uint32_t host_physics_id;
	char map_name[RUNE_MAP_NAME_BYTES];
} rune_identity_t;

/* Description of the authenticated artifact actually published. It is a
 * checked native value, never a serialized structure. */
typedef struct rune_artifact_s
{
	uint32_t magic;
	uint32_t payload_crc32;
	uint32_t header_crc32;
	uint32_t action_contract_crc32;
	uint32_t mechanism_contract_crc32;
	uint32_t num_seeds;
	uint32_t num_links;
	uint32_t num_mechanism_nodes;
	uint32_t num_mechanism_edges;
	uint32_t num_inventory_edges;
	uint32_t num_mechanism_plans;
	uint32_t string_bytes;
	rune_identity_t identity;
} rune_artifact_t;

/* Public interpretation of the stable activation-node flags. */
#define RUNE_NODEF_FRAME_COMPLETE_MOVER UINT16_C(2048)
#define RUNE_NODE_FLAG_MASK UINT16_C(0x0fff)

typedef struct rune_mechanism_node_s
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
} rune_mechanism_node_t;

typedef struct rune_mechanism_edge_s
{
	uint32_t from_key;
	uint32_t to_key;
	uint16_t kind;
	uint16_t ordinal;
	uint32_t delay_ms;
} rune_mechanism_edge_t;

typedef struct rune_mechanism_plan_s
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
} rune_mechanism_plan_t;

typedef struct rune_header_s
{
	/* Checked convenience metadata for existing graph consumers.  This is not
	 * and must never again become a serialized header. */
	int		magic;
	int		num_seeds;
	int		num_links;
	char	mapname[64];
} rune_header_t;

struct sg_compound_publication_s;

/* The explicit wire codec owns byte order and binary32 requirements.  These
 * assertions are only the capacity contract of the native runtime adapter. */
_Static_assert(INT_MAX >= (long long)RUNE_ARTIFACT_MAGIC,
	"native rune header cannot represent RUNE magic");
_Static_assert(INT_MAX >= RUNE_MAX_LINKS,
	"native rune indices cannot represent RUNE limits");
_Static_assert(SHRT_MAX >= RUNE_MAX_COST_MS,
	"native rune cost cannot represent RUNE limits");

/* in-memory form */
typedef struct rune_s
{
	/* artifact is the authority for runtime identity and proof law. hdr remains
	 * a checked convenience view for existing graph consumers. Neither is a
	 * wire image. */
	rune_artifact_t	artifact;
	rune_header_t	hdr;
	rune_seed_t		*seeds;
	rune_link_t		*links;
	rune_mechanism_node_t *mechanism_nodes;
	rune_mechanism_edge_t *mechanism_edges;
	rune_mechanism_plan_t *mechanism_plans;
	unsigned char *mechanism_strings;
	/* first-link index per seed, built at load; -1 = none */
	int				*first_link;
	int				*next_link;
	byte			*linked_seed; /* owns at least one outgoing link; incoming-only
	                             * dead ends and true orphans are not routes */
	/* Sparse loader-replayed state for D_SWIM only.  The table is built while
	 * this rune is still an unpublished candidate and is destroyed with it. */
	struct sg_compound_publication_s *compound_publication;
} rune_t;

/* One immutable snapshot of the current level identity and movement law.
 * Generation and loading use the same capture boundary so neither can bless
 * a graph under a weaker or differently interpreted authority. */
typedef struct sg_rune_authority_s
{
	sg_level_identity_t level;
	rune_identity_t identity;
	sg_identity_status_t identity_status;
} sg_rune_authority_t;

/*
 * sg_oracle.c -- the rocket-jump force and the ceiling it implies.
 *
 * These three live here rather than beside the other oracle calls in
 * sg_local.h for a mechanical reason: sg_local.h includes THIS header first
 * and only then defines sg_phantom_t, so a declaration there cannot be seen
 * by a reader of the RUNE graph contract who needs RL_ROCKETJUMP. The
 * struct tag is all a pointer declaration needs.
 */
struct sg_phantom_s;
qboolean	SG_OracleRocketJumpAim(vec3_t origin, vec3_t aim,
                                   vec3_t boom_out, float *flight_ms);
int			SG_OracleRocketJumpStep(struct sg_phantom_s *ph, vec3_t boom);
float		SG_OracleRocketJumpCeiling(void);

/* sg_rune.c -- generation and IO */
qboolean	Rune_Generate(const char *mapname);     /* seeds + proves + writes */
rune_t		*Rune_Load(const char *mapname);
void		Rune_Free(rune_t *rune);
void		Rune_DumpVisual(const rune_t *rune, const char *path);  /* html */
qboolean	SG_RuneAuthorityCapture(const char *mapname,
						 sg_rune_authority_t *authority);
qboolean	SG_RuneAuthorityMatchesArtifact(
						 const sg_rune_authority_t *authority,
						 const rune_artifact_t *artifact);
qboolean	SG_RunePhysicsCompatible(const rune_t *rune);
qboolean	SG_RunePublishedShapeValid(const rune_t *rune);
int		SG_RuneArtifactsEqual(const rune_artifact_t *left,
					 const rune_artifact_t *right);
const rune_artifact_t *SG_RuneArtifact(const rune_t *rune);
const rune_mechanism_node_t *SG_RuneMechanismNodeByKey(
						 const rune_t *rune, uint32_t key);
const rune_mechanism_plan_t *SG_RuneMechanismPlanForLink(
						 const rune_t *rune, uint32_t link_index);
const char *SG_RuneMechanismStringAt(const rune_t *rune, uint32_t offset);
