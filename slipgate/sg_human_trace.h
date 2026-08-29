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

typedef int (*sg_human_trace_v3_event_visitor_fn)(void *context,
	const sg_human_trace_v3_event_t *event);

/* A completed recorder-owned spool is an immutable durable witness.  The
 * recorder validates its binary chain, terminal v3 identity, and every typed
 * event before exposing it to the host.  Scope-consumption receipts live in
 * separate terminal-bound atomic sidecars, so a torn receipt never corrupts
 * evidence. A path copied by an ordinary caller cannot authorize parameter
 * application; only the host's private capability can do that. */
typedef struct sg_human_trace_v3_spool_ref_s
{
	sg_human_trace_completion_t completion;
	uint32_t root_segment;
	char path[SG_HUMAN_TRACE_SPOOL_PATH_BYTES];
} sg_human_trace_v3_spool_ref_t;

void SG_HumanTraceNewLevel(void);
void SG_HumanTraceMatchEnd(void);
int SG_HumanTraceCompleted(sg_human_trace_completion_t *completion_out);
/* Visits only records committed into the exact current terminal trace. A
 * modified or cross-trace completion cannot select recorder state. */
int SG_HumanTraceVisitAcceptedV3Events(
	const sg_human_trace_completion_t *completion,
	sg_human_trace_v3_event_visitor_fn visitor, void *context);
void SG_HumanTracePmove(edict_t *entity,
	const pmove_state_t *before, const pmove_t *after);
void SG_HumanTraceHookFire(edict_t *entity, edict_t *hook);
void SG_HumanTraceHookAttach(edict_t *entity, edict_t *hook,
	edict_t *target);
void SG_HumanTraceHookRelease(edict_t *entity);
void SG_HumanTraceHookReset(edict_t *entity, edict_t *hook);

#endif /* SG_HUMAN_TRACE_H */
