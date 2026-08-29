/* V3 trace shape and ordered evidence validation. */
#include "sg_human_trace_learning.h"

#include <string.h>

static int LearningTraceStringValid(const char *value, uint32_t capacity,
	int map_name)
{
	uint32_t index;

	if (!value || capacity == 0U || value[0] == '\0')
		return 0;
	for (index = 0U; index < capacity; index++)
	{
		unsigned char character = (unsigned char)value[index];

		if (character == '\0')
		{
			uint32_t tail;

			for (tail = index + 1U; tail < capacity; tail++)
				if (value[tail] != '\0')
					return 0;
			return 1;
		}
		if (character < 0x20U || character > 0x7eU ||
			(map_name && !((character >= 'a' && character <= 'z') ||
				(character >= 'A' && character <= 'Z') ||
				(character >= '0' && character <= '9') ||
				character == '_' || character == '-' || character == '.')))
			return 0;
	}
	return 0;
}

int SG_HumanTraceLearningTraceV3AuthValid(
	const sg_human_trace_learning_trace_v3_auth_t *trace)
{
	return trace && SG_HumanTraceLearningTraceIdValid(&trace->terminal_sha256) &&
		trace->session != UINT64_MAX && trace->continuation <= 1U &&
		LearningTraceStringValid(trace->mapname,
			SG_LEVEL_IDENTITY_MAPNAME_BYTES, 1) &&
		trace->host_physics_id == SG_HOST_PHYSICS_EPOCH &&
		trace->pmove_substep_ms == SG_HUMAN_TRACE_LEARNING_PMOVE_SUBSTEP_MS &&
		trace->server_frame_ms == SG_HUMAN_TRACE_LEARNING_SERVER_FRAME_MS &&
		(trace->physics_flags & ~UINT32_C(1)) == 0U &&
		LearningTraceStringValid(trace->module_version,
			SG_HUMAN_TRACE_LEARNING_TRACE_VERSION_BYTES, 0) &&
		trace->end_order != 0U;
}

int SG_HumanTraceLearningTraceV3AuthEqual(
	const sg_human_trace_learning_trace_v3_auth_t *left,
	const sg_human_trace_learning_trace_v3_auth_t *right)
{
	return SG_HumanTraceLearningTraceV3AuthValid(left) &&
		SG_HumanTraceLearningTraceV3AuthValid(right) &&
		memcmp(left->terminal_sha256.bytes, right->terminal_sha256.bytes,
			sizeof(left->terminal_sha256.bytes)) == 0 &&
		left->session == right->session && left->segment == right->segment &&
		left->continuation == right->continuation &&
		memcmp(left->mapname, right->mapname, sizeof(left->mapname)) == 0 &&
		left->bsp_checksum == right->bsp_checksum &&
		left->entity_crc32 == right->entity_crc32 &&
		left->host_physics_id == right->host_physics_id &&
		left->gravity_bits == right->gravity_bits &&
		left->airaccelerate_bits == right->airaccelerate_bits &&
		left->maxvelocity_bits == right->maxvelocity_bits &&
		left->pmove_substep_ms == right->pmove_substep_ms &&
		left->server_frame_ms == right->server_frame_ms &&
		left->physics_flags == right->physics_flags &&
		left->module_revision == right->module_revision &&
		memcmp(left->module_version, right->module_version,
			sizeof(left->module_version)) == 0 &&
		left->end_order == right->end_order &&
		left->end_frame == right->end_frame &&
		left->end_level_time_bits == right->end_level_time_bits;
}

int SG_HumanTraceLearningTraceScopeValid(
	const sg_human_trace_learning_trace_scope_t *scope)
{
	return scope && SG_HumanTraceLearningTraceV3AuthValid(&scope->trace) &&
		scope->client_id != 0U && scope->spawn_generation != 0U;
}

int SG_HumanTraceLearningRecordValid(const sg_human_trace_learning_trace_scope_t *scope,
	const sg_human_trace_learning_record_t *record)
{
	return SG_HumanTraceLearningTraceScopeValid(scope) && record &&
		SG_HumanTraceLearningUpdateValid(&record->update) &&
		!SG_HumanTraceLearningUpdateTouchesGeometry(&record->update) &&
		memcmp(record->update.evidence.trace.bytes,
			scope->trace.terminal_sha256.bytes,
			sizeof(scope->trace.terminal_sha256.bytes)) == 0 &&
		record->first_frame <= record->last_frame &&
		record->last_frame <= scope->trace.end_frame &&
		record->first_order != 0U &&
		record->first_order <= record->last_order &&
		record->last_order < scope->trace.end_order;
}

int SG_HumanTraceLearningBatchValid(const sg_human_trace_learning_batch_t *batch)
{
	uint32_t index;

	if (!batch || !SG_HumanTraceLearningTraceScopeValid(&batch->trace) ||
		batch->record_count == 0U || !batch->records)
		return 0;
	for (index = 0U; index < batch->record_count; index++)
	{
		const sg_human_trace_learning_record_t *record = &batch->records[index];

		if (!SG_HumanTraceLearningRecordValid(&batch->trace, record))
			return 0;
		if (index != 0U)
		{
			const sg_human_trace_learning_record_t *previous =
				&batch->records[index - 1U];

			if (record->first_order <= previous->last_order ||
				record->first_frame < previous->last_frame)
				return 0;
		}
	}
	return 1;
}
