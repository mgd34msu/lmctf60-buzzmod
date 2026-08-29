#ifndef SG_HUMAN_TRACE_H
#define SG_HUMAN_TRACE_H

#include "../g_local.h"
#include "sg_identity.h"

#define SG_HUMAN_TRACE_SHA256_BYTES 32U
#define SG_HUMAN_TRACE_VERSION_BYTES 64U
#define SG_HUMAN_TRACE_SPOOL_PATH_BYTES 1024U

typedef struct sg_human_trace_completion_s
{
	uint8_t terminal_sha256[SG_HUMAN_TRACE_SHA256_BYTES];
	uint64_t session;
	uint32_t segment;
	uint32_t continuation;
	char mapname[SG_LEVEL_IDENTITY_MAPNAME_BYTES];
	uint32_t bsp_checksum;
	uint32_t entity_crc32;
	uint32_t host_physics_id;
	uint32_t gravity_bits;
	uint32_t airaccelerate_bits;
	uint32_t maxvelocity_bits;
	uint16_t pmove_substep_ms;
	uint16_t server_frame_ms;
	uint32_t physics_flags;
	uint32_t module_revision;
	char module_version[SG_HUMAN_TRACE_VERSION_BYTES];
	uint64_t end_order;
	uint32_t end_frame;
	uint32_t end_level_time_bits;
} sg_human_trace_completion_t;

/* This is the typed, accepted subset of an already-written v3 record.  It is
 * a read-only audit view: the recorder retains ownership and exposes it only
 * after the terminal hash has been committed. */
typedef enum sg_human_trace_v3_event_kind_e
{
	SG_HUMAN_TRACE_V3_EVENT_STEP = 0,
	SG_HUMAN_TRACE_V3_EVENT_HOOK_FIRE,
	SG_HUMAN_TRACE_V3_EVENT_HOOK_ATTACH,
	SG_HUMAN_TRACE_V3_EVENT_HOOK_RELEASE,
	SG_HUMAN_TRACE_V3_EVENT_HOOK_RESET,
	SG_HUMAN_TRACE_V3_EVENT_KIND_COUNT
} sg_human_trace_v3_event_kind_t;

typedef struct sg_human_trace_v3_event_s
{
	sg_human_trace_v3_event_kind_t kind;
	uint64_t order;
	uint64_t command;
	uint64_t hook_event;
	uint64_t after_command;
	uint64_t spawn_generation;
	uint32_t client_id;
	uint32_t frame;
	uint32_t level_time_bits;
	int32_t hook_entity;
	int32_t hook_target;
	int16_t after_origin[3];
	uint16_t command_msec;
	uint32_t origin_bits[3];
	uint32_t hook_origin_bits[3];
	uint8_t grounded;
	uint8_t reserved;
} sg_human_trace_v3_event_t;

/* A completed recorder-owned spool is an immutable durable witness. */
typedef struct sg_human_trace_v3_spool_ref_s
{
	sg_human_trace_completion_t completion;
	uint32_t root_segment;
	char path[SG_HUMAN_TRACE_SPOOL_PATH_BYTES];
} sg_human_trace_v3_spool_ref_t;

/* Physics may change between recorder segments. Consumers receive the exact
 * authenticated header that governed each event instead of treating terminal
 * physics as if it governed the whole root. */
typedef struct sg_human_trace_v3_segment_ref_s
{
	sg_level_identity_t identity;
	uint32_t session;
	uint32_t segment;
	uint32_t continuation;
	uint32_t physics_id;
	uint32_t gravity_bits;
	uint32_t airaccelerate_bits;
	uint32_t maxvelocity_bits;
	uint16_t pmove_substep_ms;
	uint16_t server_frame_ms;
	uint32_t physics_flags;
	uint32_t module_revision;
	char module_version[SG_HUMAN_TRACE_VERSION_BYTES];
	uint64_t start_order;
	uint64_t start_command;
	uint64_t start_hook_event;
	uint8_t previous_sha256[SG_HUMAN_TRACE_SHA256_BYTES];
	uint8_t header_sha256[SG_HUMAN_TRACE_SHA256_BYTES];
} sg_human_trace_v3_segment_ref_t;

/* Minted only for one exact client life in the active authenticated visit. */
typedef struct sg_human_trace_v3_scope_acceptance_s
	sg_human_trace_v3_scope_acceptance_t;

typedef struct sg_human_trace_v3_collection_visitor_s
{
	int (*begin_root)(void *context,
		const sg_human_trace_v3_spool_ref_t *root);
	int (*segment)(void *context,
		const sg_human_trace_v3_segment_ref_t *segment);
	int (*scope)(void *context,
		const sg_human_trace_v3_scope_acceptance_t *scope);
	int (*event)(void *context,
		const sg_human_trace_v3_scope_acceptance_t *scope,
		const sg_human_trace_v3_segment_ref_t *segment,
		const sg_human_trace_v3_event_t *event);
	int (*finish_root)(void *context);
} sg_human_trace_v3_collection_visitor_t;

void SG_HumanTraceNewLevel(void);
void SG_HumanTraceMatchEnd(void);
/* Reads accepted roots only when called. Each root and player life is visited
 * in canonical first-occurrence order; the API has no consume or mutation
 * operation and remains valid after process restart. */
int SG_HumanTraceVisitAcceptedV3Collection(const sg_level_identity_t *identity,
	const sg_human_trace_v3_collection_visitor_t *visitor, void *context);
/* Opens an opaque scope only while its authenticated root is being visited.
 * Saved or caller-forged pointers are rejected, and no receipt is produced. */
int SG_HumanTraceAcceptedV3ScopeView(
	const sg_human_trace_v3_scope_acceptance_t *scope,
	const sg_human_trace_v3_spool_ref_t **root_out,
	uint32_t *client_id_out, uint64_t *spawn_generation_out);
void SG_HumanTracePmove(edict_t *entity,
	const pmove_state_t *before, const pmove_t *after);
void SG_HumanTraceHookFire(edict_t *entity, edict_t *hook);
void SG_HumanTraceHookAttach(edict_t *entity, edict_t *hook,
	edict_t *target);
void SG_HumanTraceHookRelease(edict_t *entity);
void SG_HumanTraceHookReset(edict_t *entity, edict_t *hook);

#ifdef SG_HUMAN_TRACE_TEST
int SG_HumanTraceTestFormatJsonPath(const char *directory,
	const sg_level_identity_t *identity, uint32_t segment,
	char path[SG_HUMAN_TRACE_SPOOL_PATH_BYTES]);
int SG_HumanTraceTestJsonNameSegment(const char *name,
	const sg_level_identity_t *identity, uint32_t *segment_out);
#endif

#endif /* SG_HUMAN_TRACE_H */
