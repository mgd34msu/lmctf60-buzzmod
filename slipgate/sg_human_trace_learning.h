/* Authenticated v3 human evidence for runtime parameters only. */
#ifndef SG_HUMAN_TRACE_LEARNING_H
#define SG_HUMAN_TRACE_LEARNING_H

#include <stdint.h>

#include "sg_identity.h"
#include "sg_human_trace_learning_contract.h"

#define SG_HUMAN_TRACE_LEARNING_TRACE_V3_FORMAT "lmctf-human-trace-v3"
#define SG_HUMAN_TRACE_LEARNING_TRACE_VERSION_BYTES 64U
#define SG_HUMAN_TRACE_LEARNING_PMOVE_SUBSTEP_MS 25U
#define SG_HUMAN_TRACE_LEARNING_SERVER_FRAME_MS 100U

typedef struct sg_human_trace_learning_trace_v3_auth_s
{
	sg_human_trace_learning_trace_id_t terminal_sha256;
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
	char module_version[SG_HUMAN_TRACE_LEARNING_TRACE_VERSION_BYTES];
	uint64_t end_order;
	uint32_t end_frame;
	uint32_t end_level_time_bits;
} sg_human_trace_learning_trace_v3_auth_t;

typedef struct sg_human_trace_learning_trace_scope_s
{
	sg_human_trace_learning_trace_v3_auth_t trace;
	uint32_t client_id;
	uint64_t spawn_generation;
} sg_human_trace_learning_trace_scope_t;

typedef struct sg_human_trace_learning_record_s
{
	sg_human_trace_learning_update_t update;
	uint32_t first_frame;
	uint32_t last_frame;
	uint64_t first_order;
	uint64_t last_order;
} sg_human_trace_learning_record_t;

typedef struct sg_human_trace_learning_batch_s
{
	sg_human_trace_learning_trace_scope_t trace;
	const sg_human_trace_learning_record_t *records;
	uint32_t record_count;
} sg_human_trace_learning_batch_t;

SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningTraceV3AuthValid(
	const sg_human_trace_learning_trace_v3_auth_t *trace);
SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningTraceV3AuthEqual(
	const sg_human_trace_learning_trace_v3_auth_t *left,
	const sg_human_trace_learning_trace_v3_auth_t *right);
SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningTraceScopeValid(
	const sg_human_trace_learning_trace_scope_t *scope);
SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningRecordValid(const sg_human_trace_learning_trace_scope_t *scope,
	const sg_human_trace_learning_record_t *record);
SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningBatchValid(const sg_human_trace_learning_batch_t *batch);

#endif /* SG_HUMAN_TRACE_LEARNING_H */
