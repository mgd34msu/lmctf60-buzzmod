/* sg_rune_stream.c -- RUNE byte conversion behind a neutral API. */
#include "../q_shared.h"
#include "sg_rune.h"
#include "sg_rune_stream.h"
#include "sg_rune_artifact_writer.h"

#include <limits.h>
#include <string.h>

struct sg_rune_stream_s
{
	sg_rune_stream_free_fn release;
	void *allocation_context;
	sg_rune_codec_identity_t identity;
	sg_rune_codec_seed_t *seeds;
	uint32_t num_seeds;
	sg_rune_codec_link_t *links;
	uint32_t num_links;
	sg_rune_codec_activation_node_t *nodes;
	uint32_t num_nodes;
	sg_rune_codec_activation_edge_t *edges;
	uint32_t num_edges;
	sg_rune_codec_activation_plan_t *plans;
	uint32_t num_plans;
	unsigned char *strings;
	uint32_t string_bytes;
	sg_rune_codec_workspace_t workspace;
};

static sg_rune_stream_result_t Stream_Result(int diagnostic)
{
	sg_rune_stream_result_t result;

	memset(&result, 0, sizeof(result));
	result.diagnostic = diagnostic;
	result.stage = SG_RUNE_STREAM_STAGE_ARGUMENT;
	result.index = SG_RUNE_STREAM_INDEX_NONE;
	return result;
}

static void *Stream_Allocate(sg_rune_stream_alloc_fn allocate,
	void *context, size_t count, size_t element_size)
{
	if (count == 0U)
		return NULL;
	if (!allocate || element_size == 0U || count > SIZE_MAX / element_size)
		return NULL;
	return allocate(context, count * element_size);
}

static void Stream_Release(sg_rune_stream_t *stream, void *allocation)
{
	if (stream && stream->release && allocation)
		stream->release(stream->allocation_context, allocation);
}

void SG_RuneStreamDestroy(sg_rune_stream_t *stream)
{
	if (!stream)
		return;
	Stream_Release(stream, stream->seeds);
	Stream_Release(stream, stream->links);
	Stream_Release(stream, stream->nodes);
	Stream_Release(stream, stream->edges);
	Stream_Release(stream, stream->plans);
	Stream_Release(stream, stream->strings);
	Stream_Release(stream, stream->workspace.graph_link_keys);
	Stream_Release(stream, stream->workspace.graph_source_marks);
	Stream_Release(stream, stream->workspace.plan_references);
	Stream_Release(stream, stream->workspace.node_references);
	Stream_Release(stream, stream->workspace.node_heads);
	Stream_Release(stream, stream->workspace.node_indegrees);
	Stream_Release(stream, stream->workspace.node_generations);
	Stream_Release(stream, stream->workspace.node_touched);
	Stream_Release(stream, stream->workspace.node_queue);
	Stream_Release(stream, stream->workspace.edge_next);
	Stream_Release(stream, stream->workspace.string_marks);
	stream->release(stream->allocation_context, stream);
}

static void Stream_Identity(const rune_identity_t *source,
	sg_rune_codec_identity_t *destination)
{
	memset(destination, 0, sizeof(*destination));
	destination->bsp_checksum = source->bsp_checksum;
	destination->entity_crc32 = source->entity_crc32;
	destination->physics_flags = source->physics_flags;
	destination->gravity = source->gravity;
	destination->airaccelerate = source->airaccelerate;
	destination->maxvelocity = source->maxvelocity;
	destination->pmove_substep_ms = source->pmove_substep_ms;
	destination->server_frame_ms = source->server_frame_ms;
	destination->host_physics_id = source->host_physics_id;
	memcpy(destination->map_name, source->map_name,
		sizeof(destination->map_name));
}

static void Stream_Seed(const rune_seed_t *source,
	sg_rune_codec_seed_t *destination)
{
	memcpy(destination->origin, source->origin, sizeof(destination->origin));
	destination->area_hint = source->area_hint;
	destination->flags = source->flags;
}

static void Stream_Link(const rune_link_t *source,
	sg_rune_codec_link_t *destination, uint32_t num_plans)
{
	memset(destination, 0, sizeof(*destination));
	destination->source = (uint32_t)source->from;
	destination->destination = (uint32_t)source->to;
	destination->action = source->action;
	destination->provenance = source->provenance;
	destination->min_speed = source->min_speed;
	destination->heading = source->heading;
	destination->heading_slack = source->heading_slack;
	destination->exit_speed = source->exit_speed;
	destination->cost_ms = source->cost_ms;
	memcpy(destination->suffix_anchor, source->anchor,
		sizeof(destination->suffix_anchor));
	/* The codec policy, not this mechanical adapter, decides whether bytes
	 * 28..39 are a mechanism witness or a secondary control. */
	memcpy(destination->mechanism_anchor, source->mechanism_anchor,
		sizeof(destination->mechanism_anchor));
	destination->sweep_clear_ms = source->sweep_clear_ms;
	destination->mode = source->mode;
	destination->activation_plan = num_plans != 0U
		? source->mechanism_plan : SG_RUNE_CODEC_NO_ACTIVATION_PLAN;
}

static void Stream_Node(const rune_mechanism_node_t *source,
	sg_rune_codec_activation_node_t *destination)
{
	memset(destination, 0, sizeof(*destination));
	destination->key = source->key;
	destination->kind = source->kind;
	destination->flags = source->flags;
	destination->classname_offset = source->classname_offset;
	destination->target_offset = source->target_offset;
	destination->targetname_offset = source->targetname_offset;
	destination->killtarget_offset = source->killtarget_offset;
	destination->owner_key = source->owner_key;
	destination->team_master_key = source->team_master_key;
	destination->spawnflags = source->spawnflags;
	destination->touch_callback = source->touch_callback;
	destination->use_callback = source->use_callback;
	destination->think_callback = source->think_callback;
	destination->blocked_callback = source->blocked_callback;
	destination->delay_ms = source->delay_ms;
	destination->wait_ms = source->wait_ms;
	destination->speed_q8 = source->speed_q8;
	destination->accel_q8 = source->accel_q8;
	destination->decel_q8 = source->decel_q8;
	memcpy(destination->absmin_q8, source->absmin_q8,
		sizeof(destination->absmin_q8));
	memcpy(destination->absmax_q8, source->absmax_q8,
		sizeof(destination->absmax_q8));
	destination->path_target_offset = source->path_target_offset;
	memcpy(destination->push_velocity, source->push_velocity,
		sizeof(destination->push_velocity));
}

static void Stream_Edge(const rune_mechanism_edge_t *source,
	sg_rune_codec_activation_edge_t *destination)
{
	destination->from_key = source->from_key;
	destination->to_key = source->to_key;
	destination->kind = source->kind;
	destination->ordinal = source->ordinal;
	destination->delay_ms = source->delay_ms;
}

static void Stream_Plan(const rune_mechanism_plan_t *source,
	sg_rune_codec_activation_plan_t *destination)
{
	destination->entry_key = source->entry_key;
	destination->mover_key = source->mover_key;
	destination->first_edge = source->first_edge;
	destination->num_edges = source->num_edges;
	destination->controller_kind = source->controller_kind;
	destination->flags = source->flags;
	destination->expected_members = source->expected_members;
	destination->cooldown_ms = source->cooldown_ms;
	destination->closure_crc32 = source->closure_crc32;
}

#define STREAM_ALLOC(member_, count_, type_) do { \
	stream->member_ = Stream_Allocate(allocate, allocation_context, \
		(count_), sizeof(type_)); \
	if ((count_) != 0U && !stream->member_) goto allocation_failed; \
} while (0)

sg_rune_stream_t *SG_RuneStreamCreate(
	const sg_rune_stream_source_t *source,
	sg_rune_stream_alloc_fn allocate, sg_rune_stream_free_fn release,
	void *allocation_context, sg_rune_stream_result_t *failure_out)
{
	sg_rune_stream_t *stream;
	uint32_t i;

	if (failure_out)
		*failure_out = Stream_Result(RLW_INVALID_ARGUMENT);
	if (!source || !source->identity || !source->seeds ||
	    source->num_seeds == 0U || !source->strings ||
	    source->string_bytes == 0U || !allocate || !release ||
	    (source->num_links != 0U && !source->links) ||
	    (source->num_nodes != 0U && !source->nodes) ||
	    (source->num_edges != 0U && !source->edges) ||
	    (source->num_plans != 0U && !source->plans))
		return NULL;
	stream = allocate(allocation_context, sizeof(*stream));
	if (!stream)
		goto plain_allocation_failed;
	memset(stream, 0, sizeof(*stream));
	stream->release = release;
	stream->allocation_context = allocation_context;
	stream->num_seeds = source->num_seeds;
	stream->num_links = source->num_links;
	stream->num_nodes = source->num_nodes;
	stream->num_edges = source->num_edges;
	stream->num_plans = source->num_plans;
	stream->string_bytes = source->string_bytes;
	STREAM_ALLOC(seeds, source->num_seeds, sg_rune_codec_seed_t);
	STREAM_ALLOC(links, source->num_links, sg_rune_codec_link_t);
	STREAM_ALLOC(nodes, source->num_nodes, sg_rune_codec_activation_node_t);
	STREAM_ALLOC(edges, source->num_edges, sg_rune_codec_activation_edge_t);
	STREAM_ALLOC(plans, source->num_plans, sg_rune_codec_activation_plan_t);
	STREAM_ALLOC(strings, source->string_bytes, unsigned char);
	STREAM_ALLOC(workspace.graph_link_keys, source->num_links, uint64_t);
	STREAM_ALLOC(workspace.graph_source_marks, source->num_seeds, uint8_t);
	STREAM_ALLOC(workspace.plan_references, source->num_plans, uint32_t);
	STREAM_ALLOC(workspace.node_references, source->num_nodes, uint32_t);
	STREAM_ALLOC(workspace.node_heads, source->num_nodes, uint32_t);
	STREAM_ALLOC(workspace.node_indegrees, source->num_nodes, uint32_t);
	STREAM_ALLOC(workspace.node_generations, source->num_nodes, uint32_t);
	STREAM_ALLOC(workspace.node_touched, source->num_nodes, uint32_t);
	STREAM_ALLOC(workspace.node_queue, source->num_nodes, uint32_t);
	STREAM_ALLOC(workspace.edge_next, source->num_edges, uint32_t);
	STREAM_ALLOC(workspace.string_marks, source->string_bytes, uint8_t);
	Stream_Identity(source->identity, &stream->identity);
	for (i = 0U; i < source->num_seeds; i++)
		Stream_Seed(&source->seeds[i], &stream->seeds[i]);
	for (i = 0U; i < source->num_links; i++)
		Stream_Link(&source->links[i], &stream->links[i], source->num_plans);
	for (i = 0U; i < source->num_nodes; i++)
		Stream_Node(&source->nodes[i], &stream->nodes[i]);
	for (i = 0U; i < source->num_edges; i++)
		Stream_Edge(&source->edges[i], &stream->edges[i]);
	for (i = 0U; i < source->num_plans; i++)
		Stream_Plan(&source->plans[i], &stream->plans[i]);
	memcpy(stream->strings, source->strings, source->string_bytes);
	stream->workspace.graph_link_key_capacity = source->num_links;
	stream->workspace.graph_source_mark_capacity = source->num_seeds;
	stream->workspace.plan_reference_capacity = source->num_plans;
	stream->workspace.node_reference_capacity = source->num_nodes;
	stream->workspace.node_head_capacity = source->num_nodes;
	stream->workspace.node_indegree_capacity = source->num_nodes;
	stream->workspace.node_generation_capacity = source->num_nodes;
	stream->workspace.node_touched_capacity = source->num_nodes;
	stream->workspace.node_queue_capacity = source->num_nodes;
	stream->workspace.edge_next_capacity = source->num_edges;
	stream->workspace.string_mark_capacity = source->string_bytes;
	if (failure_out)
		*failure_out = Stream_Result(RLW_OK);
	return stream;

allocation_failed:
	SG_RuneStreamDestroy(stream);
plain_allocation_failed:
	if (failure_out)
		*failure_out = Stream_Result(RLW_ALLOCATION_FAILED);
	return NULL;
}

#undef STREAM_ALLOC

static sg_rune_stream_stage_t Stream_Stage(sg_rune_artifact_write_stage_t stage)
{
	/* The adapter deliberately translates the private codec enum instead
	 * of publishing it through generator/install headers. */
	if ((unsigned int)stage > (unsigned int)SG_RUNE_ARTIFACT_WRITE_STAGE_DONE)
		return SG_RUNE_STREAM_STAGE_ARGUMENT;
	return (sg_rune_stream_stage_t)(unsigned int)stage;
}

static sg_rune_stream_result_t Stream_Translate(
	const sg_rune_artifact_write_result_t *wire)
{
	sg_rune_stream_result_t result = Stream_Result(RLW_INVALID_ARGUMENT);

	if (!wire)
		return result;
	result.diagnostic = (int)wire->diagnostic;
	result.stage = Stream_Stage(wire->stage);
	result.index = wire->index;
	result.bytes_written = wire->bytes_written;
	result.file_size = wire->file_size;
	result.payload_crc32 = wire->payload_crc32;
	return result;
}

typedef struct stream_sink_bridge_s
{
	sg_rune_stream_sink_fn sink;
	void *context;
} stream_sink_bridge_t;

static int Stream_Sink(void *context, const unsigned char *fragment,
	size_t fragment_size)
{
	stream_sink_bridge_t *bridge = context;

	return !bridge || !bridge->sink
		? 1 : bridge->sink(bridge->context, fragment, fragment_size);
}

sg_rune_stream_result_t SG_RuneStreamWrite(void *opaque,
	sg_rune_stream_sink_fn sink, void *sink_context)
{
	sg_rune_stream_t *stream = opaque;
	stream_sink_bridge_t bridge;
	sg_rune_artifact_write_result_t result;

	if (!stream || !sink)
		return Stream_Result(RLW_INVALID_ARGUMENT);
	bridge.sink = sink;
	bridge.context = sink_context;
	result = SG_RuneArtifactWrite(&stream->identity, stream->seeds,
		stream->num_seeds, stream->links,
		stream->num_links,
		stream->nodes, stream->num_nodes, stream->edges, stream->num_edges,
		stream->plans, stream->num_plans, stream->strings,
		stream->string_bytes, &stream->workspace, Stream_Sink, &bridge);
	return Stream_Translate(&result);
}

int SG_RuneStreamResultSucceeded(const sg_rune_stream_result_t *result)
{
	return result && result->diagnostic == RLW_OK &&
	       result->stage == SG_RUNE_STREAM_STAGE_DONE &&
	       result->index == SG_RUNE_STREAM_INDEX_NONE &&
	       result->bytes_written == result->file_size;
}

void SG_RuneStreamResultMarkIOFailure(sg_rune_stream_result_t *result,
	int payload_mismatch)
{
	if (!result)
		return;
	result->diagnostic = payload_mismatch ? RLW_BAD_PAYLOAD_CRC : RLW_IO_ERROR;
	if (payload_mismatch)
	{
		result->stage = SG_RUNE_STREAM_STAGE_VERIFY;
		result->index = SG_RUNE_STREAM_INDEX_NONE;
	}
}
