/* sg_rune_artifact_writer.c -- two-pass authenticated RUNE artifact streaming writer. */
#include "../q_shared.h"
#include "sg_rune_artifact_writer.h"

#include "sg_crc32.h"

#include <string.h>

typedef struct artifact_writer_crc_s
{
	uint32_t state;
	sg_rune_artifact_write_stage_t stage;
	uint32_t index;
} artifact_writer_crc_t;

typedef struct artifact_writer_range_s
{
	const void *data;
	size_t bytes;
} artifact_writer_range_t;

static sg_rune_artifact_write_result_t ArtifactWriter_Result(void)
{
	sg_rune_artifact_write_result_t result;

	memset(&result, 0, sizeof(result));
	result.diagnostic = RLCODEC_OK;
	result.stage = SG_RUNE_ARTIFACT_WRITE_STAGE_ARGUMENT;
	result.index = SG_RUNE_ARTIFACT_WRITE_INDEX_NONE;
	return result;
}

static void ArtifactWriter_Fail(sg_rune_artifact_write_result_t *result,
	sg_rune_codec_diagnostic_t diagnostic, sg_rune_artifact_write_stage_t stage,
	uint32_t index)
{
	result->diagnostic = diagnostic;
	result->stage = stage;
	result->index = index;
}

static int ArtifactWriter_RangesOverlap(const artifact_writer_range_t *left,
	const artifact_writer_range_t *right)
{
	uintptr_t left_start;
	uintptr_t right_start;
	uintptr_t left_end;
	uintptr_t right_end;

	if (!left || !right || !left->data || !right->data ||
	    left->bytes == 0U || right->bytes == 0U)
		return 0;
	left_start = (uintptr_t)left->data;
	right_start = (uintptr_t)right->data;
	if (left_start > UINTPTR_MAX - left->bytes ||
	    right_start > UINTPTR_MAX - right->bytes)
		return 1;
	left_end = left_start + left->bytes;
	right_end = right_start + right->bytes;
	return left_start < right_end && right_start < left_end;
}

/* The validator owns and mutates all workspace arrays. Reject aliasing
 * before it runs so a hostile workspace cannot turn a const source array into
 * scratch storage or make two validator roles silently corrupt each other. */
static int ArtifactWriter_WorkspaceDisjoint(
	const sg_rune_codec_identity_t *identity,
	const sg_rune_codec_seed_t *seeds, uint32_t num_seeds,
	const sg_rune_codec_link_t *links, uint32_t num_links,
	const sg_rune_codec_activation_node_t *nodes, uint32_t num_nodes,
	const sg_rune_codec_activation_edge_t *edges, uint32_t num_edges,
	const sg_rune_codec_activation_plan_t *plans, uint32_t num_plans,
	const unsigned char *strings, uint32_t string_bytes,
	sg_rune_codec_workspace_t *workspace)
{
	artifact_writer_range_t immutable[] = {
		{ identity, sizeof(*identity) },
		{ seeds, (size_t)num_seeds * sizeof(*seeds) },
		{ links, (size_t)num_links * sizeof(*links) },
		{ nodes, (size_t)num_nodes * sizeof(*nodes) },
		{ edges, (size_t)num_edges * sizeof(*edges) },
		{ plans, (size_t)num_plans * sizeof(*plans) },
		{ strings, (size_t)string_bytes },
		{ workspace, sizeof(*workspace) }
	};
	artifact_writer_range_t scratch[] = {
		{ workspace->graph_link_keys,
			(size_t)num_links * sizeof(workspace->graph_link_keys[0]) },
		{ workspace->graph_source_marks,
			(size_t)num_seeds * sizeof(workspace->graph_source_marks[0]) },
		{ workspace->plan_references,
			(size_t)num_plans * sizeof(workspace->plan_references[0]) },
		{ workspace->node_references,
			(size_t)num_nodes * sizeof(workspace->node_references[0]) },
		{ workspace->node_heads,
			(size_t)num_nodes * sizeof(workspace->node_heads[0]) },
		{ workspace->node_indegrees,
			(size_t)num_nodes * sizeof(workspace->node_indegrees[0]) },
		{ workspace->node_generations,
			(size_t)num_nodes * sizeof(workspace->node_generations[0]) },
		{ workspace->node_touched,
			(size_t)num_nodes * sizeof(workspace->node_touched[0]) },
		{ workspace->node_queue,
			(size_t)num_nodes * sizeof(workspace->node_queue[0]) },
		{ workspace->edge_next,
			(size_t)num_edges * sizeof(workspace->edge_next[0]) },
		{ workspace->string_marks, (size_t)string_bytes }
	};
	size_t scratch_index;
	size_t other_index;

	for (scratch_index = 0U;
	     scratch_index < sizeof(scratch) / sizeof(scratch[0]);
	     scratch_index++)
	{
		for (other_index = 0U;
		     other_index < sizeof(immutable) / sizeof(immutable[0]);
		     other_index++)
			if (ArtifactWriter_RangesOverlap(&scratch[scratch_index],
			    &immutable[other_index]))
				return 0;
		for (other_index = 0U; other_index < scratch_index;
		     other_index++)
			if (ArtifactWriter_RangesOverlap(&scratch[scratch_index],
			    &scratch[other_index]))
				return 0;
	}
	return 1;
}

static void ArtifactWriter_BuildHeader(sg_rune_codec_header_t *header,
	const sg_rune_codec_identity_t *identity,
	uint32_t num_seeds,
	uint32_t num_links, uint32_t num_nodes, uint32_t num_edges,
	uint32_t num_inventory_edges, uint32_t num_plans,
	uint32_t string_bytes, uint32_t payload_crc32)
{
	memset(header, 0, sizeof(*header));
	header->magic = SG_RUNE_CODEC_MAGIC;
	header->header_bytes = SG_RUNE_CODEC_HEADER_BYTES;
	header->seed_bytes = SG_RUNE_CODEC_SEED_BYTES;
	header->link_bytes = SG_RUNE_CODEC_LINK_BYTES;
	header->num_seeds = num_seeds;
	header->num_links = num_links;
	header->payload_crc32 = payload_crc32;
	header->bsp_checksum = identity->bsp_checksum;
	header->entity_crc32 = identity->entity_crc32;
	header->action_contract_crc32 = SG_RUNE_ACTION_CONTRACT_CRC32;
	header->physics_flags = identity->physics_flags;
	header->gravity = identity->gravity;
	header->airaccelerate = identity->airaccelerate;
	header->maxvelocity = identity->maxvelocity;
	header->pmove_substep_ms = identity->pmove_substep_ms;
	header->server_frame_ms = identity->server_frame_ms;
	header->host_physics_id = identity->host_physics_id;
	memcpy(header->map_name, identity->map_name,
		SG_RUNE_CODEC_MAP_NAME_BYTES);
	header->activation_node_bytes = SG_RUNE_CODEC_ACTIVATION_NODE_BYTES;
	header->activation_edge_bytes = SG_RUNE_CODEC_ACTIVATION_EDGE_BYTES;
	header->activation_plan_bytes = SG_RUNE_CODEC_ACTIVATION_PLAN_BYTES;
	header->num_activation_nodes = num_nodes;
	header->num_activation_edges = num_edges;
	header->num_activation_plans = num_plans;
	header->string_bytes = string_bytes;
	header->mechanism_contract_crc32 = SG_RUNE_MECHANISM_CONTRACT_CRC32;
	header->num_inventory_edges = num_inventory_edges;
}

static int ArtifactWriter_CRCStart(artifact_writer_crc_t *crc,
	sg_rune_artifact_write_stage_t stage)
{
	if (!crc)
		return 0;
	crc->state = SG_CRC32Init();
	crc->stage = stage;
	crc->index = SG_RUNE_ARTIFACT_WRITE_INDEX_NONE;
	return 1;
}

static int ArtifactWriter_CRCAdd(artifact_writer_crc_t *crc, const void *fragment,
	size_t fragment_size, sg_rune_artifact_write_stage_t stage, uint32_t index)
{
	if (!crc)
		return 0;
	crc->stage = stage;
	crc->index = index;
	return SG_CRC32Update(&crc->state, fragment, fragment_size);
}

static sg_rune_codec_diagnostic_t ArtifactWriter_HashPayload(
	const sg_rune_codec_seed_t *seeds, uint32_t num_seeds,
	const sg_rune_codec_link_t *links, uint32_t num_links,
	const sg_rune_codec_activation_node_t *nodes, uint32_t num_nodes,
	const sg_rune_codec_activation_edge_t *edges, uint32_t num_edges,
	const sg_rune_codec_activation_plan_t *plans, uint32_t num_plans,
	const unsigned char *strings, uint32_t string_bytes,
	sg_rune_artifact_write_stage_t seed_stage,
	sg_rune_artifact_write_stage_t link_stage,
	sg_rune_artifact_write_stage_t node_stage,
	sg_rune_artifact_write_stage_t edge_stage,
	sg_rune_artifact_write_stage_t plan_stage,
	sg_rune_artifact_write_stage_t string_stage,
	uint32_t *crc_out, sg_rune_artifact_write_stage_t *stage_out,
	uint32_t *index_out)
{
	unsigned char fragment[SG_RUNE_CODEC_ACTIVATION_NODE_BYTES];
	artifact_writer_crc_t crc;
	uint32_t index;
	sg_rune_codec_diagnostic_t diagnostic;

	if (!crc_out || !stage_out || !index_out ||
	    !ArtifactWriter_CRCStart(&crc, string_stage))
		return (sg_rune_codec_diagnostic_t)RLW_INVALID_ARGUMENT;
	*crc_out = 0U;
	*stage_out = string_stage;
	*index_out = SG_RUNE_ARTIFACT_WRITE_INDEX_NONE;
	for (index = 0U; index < num_seeds; index++)
	{
		diagnostic = SG_RuneCodecEncodeSeed(&seeds[index], fragment,
			SG_RUNE_CODEC_SEED_BYTES);
		if (diagnostic != RLCODEC_OK)
		{
			*stage_out = seed_stage;
			*index_out = index;
			return diagnostic;
		}
		if (!ArtifactWriter_CRCAdd(&crc, fragment, SG_RUNE_CODEC_SEED_BYTES,
		    seed_stage, index))
			goto crc_error;
	}
	for (index = 0U; index < num_links; index++)
	{
		diagnostic = SG_RuneCodecEncodeLink(&links[index], fragment,
			SG_RUNE_CODEC_LINK_BYTES);
		if (diagnostic != RLCODEC_OK)
		{
			*stage_out = link_stage;
			*index_out = index;
			return diagnostic;
		}
		if (!ArtifactWriter_CRCAdd(&crc, fragment, SG_RUNE_CODEC_LINK_BYTES,
		    link_stage, index))
			goto crc_error;
	}
	for (index = 0U; index < num_nodes; index++)
	{
		diagnostic = SG_RuneCodecEncodeActivationNode(&nodes[index], fragment,
			SG_RUNE_CODEC_ACTIVATION_NODE_BYTES);
		if (diagnostic != RLCODEC_OK)
		{
			*stage_out = node_stage;
			*index_out = index;
			return diagnostic;
		}
		if (!ArtifactWriter_CRCAdd(&crc, fragment,
		    SG_RUNE_CODEC_ACTIVATION_NODE_BYTES, node_stage, index))
			goto crc_error;
	}
	for (index = 0U; index < num_edges; index++)
	{
		diagnostic = SG_RuneCodecEncodeActivationEdge(&edges[index], fragment,
			SG_RUNE_CODEC_ACTIVATION_EDGE_BYTES);
		if (diagnostic != RLCODEC_OK)
		{
			*stage_out = edge_stage;
			*index_out = index;
			return diagnostic;
		}
		if (!ArtifactWriter_CRCAdd(&crc, fragment,
		    SG_RUNE_CODEC_ACTIVATION_EDGE_BYTES, edge_stage, index))
			goto crc_error;
	}
	for (index = 0U; index < num_plans; index++)
	{
		diagnostic = SG_RuneCodecEncodeActivationPlan(&plans[index], fragment,
			SG_RUNE_CODEC_ACTIVATION_PLAN_BYTES);
		if (diagnostic != RLCODEC_OK)
		{
			*stage_out = plan_stage;
			*index_out = index;
			return diagnostic;
		}
		if (!ArtifactWriter_CRCAdd(&crc, fragment,
		    SG_RUNE_CODEC_ACTIVATION_PLAN_BYTES, plan_stage, index))
			goto crc_error;
	}
	if (!ArtifactWriter_CRCAdd(&crc, strings, string_bytes, string_stage, 0U))
		goto crc_error;
	*crc_out = SG_CRC32Final(crc.state);
	return RLCODEC_OK;

crc_error:
	*stage_out = crc.stage;
	*index_out = crc.index;
	return (sg_rune_codec_diagnostic_t)RLW_INVALID_ARGUMENT;
}

static int ArtifactWriter_Emit(sg_rune_artifact_write_result_t *result,
	sg_rune_artifact_write_sink_fn sink, void *sink_context,
	const unsigned char *fragment, size_t fragment_size,
	sg_rune_artifact_write_stage_t stage, uint32_t index)
{
	if (sink(sink_context, fragment, fragment_size) != 0)
	{
		ArtifactWriter_Fail(result, (sg_rune_codec_diagnostic_t)RLW_IO_ERROR,
			stage, index);
		return 0;
	}
	result->bytes_written += fragment_size;
	return 1;
}

sg_rune_artifact_write_result_t SG_RuneArtifactWrite(
	const sg_rune_codec_identity_t *identity,
	const sg_rune_codec_seed_t *seeds, uint32_t num_seeds,
	const sg_rune_codec_link_t *links, uint32_t num_links,
	const sg_rune_codec_activation_node_t *nodes, uint32_t num_nodes,
	const sg_rune_codec_activation_edge_t *edges, uint32_t num_edges,
	const sg_rune_codec_activation_plan_t *plans, uint32_t num_plans,
	const unsigned char *strings, uint32_t string_bytes,
	sg_rune_codec_workspace_t *workspace,
	sg_rune_artifact_write_sink_fn sink, void *sink_context)
{
	sg_rune_artifact_write_result_t result = ArtifactWriter_Result();
	sg_rune_codec_header_t header;
	unsigned char header_bytes[SG_RUNE_CODEC_HEADER_BYTES];
	unsigned char current_header[SG_RUNE_CODEC_HEADER_BYTES];
	unsigned char fragment[SG_RUNE_CODEC_ACTIVATION_NODE_BYTES];
	uint32_t emitted_crc_state;
	uint32_t emitted_crc;
	uint32_t final_crc;
	uint32_t index;
	sg_rune_artifact_write_stage_t failure_stage;
	uint32_t failure_index;
	sg_rune_codec_diagnostic_t diagnostic;
	uint32_t num_inventory_edges;

	diagnostic = SG_RuneCodecFileSize(num_seeds, num_links, num_nodes,
		num_edges, num_plans, string_bytes, &result.file_size);
	if (diagnostic != RLCODEC_OK)
	{
		result.diagnostic = diagnostic;
		return result;
	}
	if (!identity || !seeds || (num_links != 0U && !links) ||
	    (num_nodes != 0U && !nodes) || (num_edges != 0U && !edges) ||
	    (num_plans != 0U && !plans) ||
	    !strings || !workspace || !sink)
	{
		result.diagnostic = (sg_rune_codec_diagnostic_t)RLW_INVALID_ARGUMENT;
		return result;
	}
	num_inventory_edges = num_plans != 0U
		? plans[0].first_edge : num_edges;
	if (!ArtifactWriter_WorkspaceDisjoint(identity, seeds, num_seeds, links,
	    num_links, nodes, num_nodes, edges, num_edges, plans, num_plans,
	    strings, string_bytes, workspace))
	{
		ArtifactWriter_Fail(&result,
			(sg_rune_codec_diagnostic_t)RLW_INVALID_ARGUMENT,
			SG_RUNE_ARTIFACT_WRITE_STAGE_VALIDATE,
			SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
		return result;
	}

	/* Encode a zero-payload header first so identity and header semantics fail
	 * before any potentially expensive whole-graph validation. */
	ArtifactWriter_BuildHeader(&header, identity, num_seeds,
		num_links, num_nodes, num_edges, num_inventory_edges, num_plans,
		string_bytes, 0U);
	diagnostic = SG_RuneCodecEncodeHeader(&header, current_header,
		SG_RUNE_CODEC_HEADER_BYTES);
	if (diagnostic != RLCODEC_OK)
	{
		ArtifactWriter_Fail(&result, diagnostic,
			SG_RUNE_ARTIFACT_WRITE_STAGE_PREFLIGHT_HEADER,
			SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
		return result;
	}

	/* Pass one visits every payload fragment, even though the whole validator
	 * will subsequently re-check the cross-record graph laws. */
	diagnostic = ArtifactWriter_HashPayload(seeds, num_seeds, links, num_links,
		nodes, num_nodes, edges, num_edges, plans, num_plans, strings,
		string_bytes, SG_RUNE_ARTIFACT_WRITE_STAGE_PREFLIGHT_SEED,
		SG_RUNE_ARTIFACT_WRITE_STAGE_PREFLIGHT_LINK,
		SG_RUNE_ARTIFACT_WRITE_STAGE_PREFLIGHT_NODE,
		SG_RUNE_ARTIFACT_WRITE_STAGE_PREFLIGHT_EDGE,
		SG_RUNE_ARTIFACT_WRITE_STAGE_PREFLIGHT_PLAN,
		SG_RUNE_ARTIFACT_WRITE_STAGE_PREFLIGHT_STRING_POOL,
		&result.payload_crc32, &failure_stage, &failure_index);
	if (diagnostic != RLCODEC_OK)
	{
		ArtifactWriter_Fail(&result, diagnostic, failure_stage, failure_index);
		return result;
	}
	diagnostic = SG_RuneCodecValidate(seeds, num_seeds, links,
		num_links,
		nodes, num_nodes, edges, num_edges, plans, num_plans, strings,
		string_bytes, workspace);
	if (diagnostic != RLCODEC_OK)
	{
		ArtifactWriter_Fail(&result, diagnostic,
			SG_RUNE_ARTIFACT_WRITE_STAGE_VALIDATE,
			SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
		return result;
	}
	ArtifactWriter_BuildHeader(&header, identity, num_seeds,
		num_links, num_nodes, num_edges, num_inventory_edges, num_plans,
		string_bytes, result.payload_crc32);
	diagnostic = SG_RuneCodecEncodeHeader(&header, header_bytes,
		SG_RUNE_CODEC_HEADER_BYTES);
	if (diagnostic != RLCODEC_OK)
	{
		ArtifactWriter_Fail(&result, diagnostic,
			SG_RUNE_ARTIFACT_WRITE_STAGE_PREFLIGHT_HEADER,
			SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
		return result;
	}

	/* Pass two begins by independently rebuilding the header from the current
	 * identity.  This makes a pre-emission identity change fail closed. */
	ArtifactWriter_BuildHeader(&header, identity, num_seeds,
		num_links, num_nodes, num_edges, num_inventory_edges, num_plans,
		string_bytes, result.payload_crc32);
	diagnostic = SG_RuneCodecEncodeHeader(&header, current_header,
		SG_RUNE_CODEC_HEADER_BYTES);
	if (diagnostic != RLCODEC_OK ||
	    memcmp(current_header, header_bytes, sizeof(header_bytes)) != 0)
	{
		ArtifactWriter_Fail(&result, diagnostic != RLCODEC_OK ? diagnostic :
			(sg_rune_codec_diagnostic_t)RLW_BAD_HEADER_CRC,
			SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY_HEADER,
			SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
		return result;
	}
	if (!ArtifactWriter_Emit(&result, sink, sink_context, current_header,
	    sizeof(current_header), SG_RUNE_ARTIFACT_WRITE_STAGE_EMIT_HEADER,
	    SG_RUNE_ARTIFACT_WRITE_INDEX_NONE))
		return result;
	emitted_crc_state = SG_CRC32Init();

#define ARTIFACT_WRITER_ENCODE_EMIT(array_, count_, encode_, bytes_, verify_stage_, \
		emit_stage_) do { \
	for (index = 0U; index < (count_); index++) { \
		diagnostic = (encode_)(&(array_)[index], fragment, (bytes_)); \
		if (diagnostic != RLCODEC_OK || \
		    !SG_CRC32Update(&emitted_crc_state, fragment, (bytes_))) { \
			ArtifactWriter_Fail(&result, diagnostic != RLCODEC_OK ? diagnostic : \
				(sg_rune_codec_diagnostic_t)RLW_INVALID_ARGUMENT, \
				(verify_stage_), index); \
			return result; \
		} \
		if (!ArtifactWriter_Emit(&result, sink, sink_context, fragment, (bytes_), \
		    (emit_stage_), index)) \
			return result; \
	} \
} while (0)

	ARTIFACT_WRITER_ENCODE_EMIT(seeds, num_seeds, SG_RuneCodecEncodeSeed,
		SG_RUNE_CODEC_SEED_BYTES, SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY_SEED,
		SG_RUNE_ARTIFACT_WRITE_STAGE_EMIT_SEED);
	ARTIFACT_WRITER_ENCODE_EMIT(links, num_links, SG_RuneCodecEncodeLink,
		SG_RUNE_CODEC_LINK_BYTES, SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY_LINK,
		SG_RUNE_ARTIFACT_WRITE_STAGE_EMIT_LINK);
	ARTIFACT_WRITER_ENCODE_EMIT(nodes, num_nodes, SG_RuneCodecEncodeActivationNode,
		SG_RUNE_CODEC_ACTIVATION_NODE_BYTES,
		SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY_NODE,
		SG_RUNE_ARTIFACT_WRITE_STAGE_EMIT_NODE);
	ARTIFACT_WRITER_ENCODE_EMIT(edges, num_edges, SG_RuneCodecEncodeActivationEdge,
		SG_RUNE_CODEC_ACTIVATION_EDGE_BYTES,
		SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY_EDGE,
		SG_RUNE_ARTIFACT_WRITE_STAGE_EMIT_EDGE);
	ARTIFACT_WRITER_ENCODE_EMIT(plans, num_plans, SG_RuneCodecEncodeActivationPlan,
		SG_RUNE_CODEC_ACTIVATION_PLAN_BYTES,
		SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY_PLAN,
		SG_RUNE_ARTIFACT_WRITE_STAGE_EMIT_PLAN);

#undef ARTIFACT_WRITER_ENCODE_EMIT

	if (!SG_CRC32Update(&emitted_crc_state, strings, string_bytes))
	{
		ArtifactWriter_Fail(&result,
			(sg_rune_codec_diagnostic_t)RLW_INVALID_ARGUMENT,
			SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY_STRING_POOL, 0U);
		return result;
	}
	if (!ArtifactWriter_Emit(&result, sink, sink_context, strings, string_bytes,
	    SG_RUNE_ARTIFACT_WRITE_STAGE_EMIT_STRING_POOL, 0U))
		return result;
	emitted_crc = SG_CRC32Final(emitted_crc_state);
	if (emitted_crc != result.payload_crc32)
	{
		ArtifactWriter_Fail(&result,
			(sg_rune_codec_diagnostic_t)RLW_BAD_PAYLOAD_CRC,
			SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY,
			SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
		return result;
	}

	/* A sink can mutate a fragment after the pass-two CRC consumed it.  Re-hash
	 * the current inputs once more so such mutation cannot produce success. */
	diagnostic = ArtifactWriter_HashPayload(seeds, num_seeds, links, num_links,
		nodes, num_nodes, edges, num_edges, plans, num_plans, strings,
		string_bytes, SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY_SEED,
		SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY_LINK,
		SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY_NODE,
		SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY_EDGE,
		SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY_PLAN,
		SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY_STRING_POOL,
		&final_crc, &failure_stage, &failure_index);
	if (diagnostic != RLCODEC_OK)
	{
		ArtifactWriter_Fail(&result, diagnostic, failure_stage, failure_index);
		return result;
	}
	if (final_crc != result.payload_crc32)
	{
		ArtifactWriter_Fail(&result,
			(sg_rune_codec_diagnostic_t)RLW_BAD_PAYLOAD_CRC,
			SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY,
			SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
		return result;
	}
	ArtifactWriter_BuildHeader(&header, identity, num_seeds,
		num_links, num_nodes, num_edges, num_inventory_edges, num_plans,
		string_bytes, final_crc);
	diagnostic = SG_RuneCodecEncodeHeader(&header, current_header,
		SG_RUNE_CODEC_HEADER_BYTES);
	if (diagnostic != RLCODEC_OK ||
	    memcmp(current_header, header_bytes, sizeof(header_bytes)) != 0)
	{
		ArtifactWriter_Fail(&result, diagnostic != RLCODEC_OK ? diagnostic :
			(sg_rune_codec_diagnostic_t)RLW_BAD_HEADER_CRC,
			SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY_HEADER,
			SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
		return result;
	}
	if (result.bytes_written != result.file_size)
	{
		ArtifactWriter_Fail(&result,
			(sg_rune_codec_diagnostic_t)RLW_BAD_FILE_SIZE,
			SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY,
			SG_RUNE_ARTIFACT_WRITE_INDEX_NONE);
		return result;
	}
	result.stage = SG_RUNE_ARTIFACT_WRITE_STAGE_DONE;
	result.index = SG_RUNE_ARTIFACT_WRITE_INDEX_NONE;
	return result;
}
