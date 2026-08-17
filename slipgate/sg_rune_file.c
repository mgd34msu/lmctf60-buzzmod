/* sg_rune_file.c -- RUNE artifact decode and native adaptation. */
#include "../q_shared.h"
#include "sg_rune_file.h"

#include "sg_action.h"
#include "sg_rune_artifact_loader.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef struct rune_decode_storage_s
{
	sg_rune_codec_seed_t *seeds;
	sg_rune_codec_link_t *links;
	sg_rune_codec_activation_node_t *nodes;
	sg_rune_codec_activation_edge_t *edges;
	sg_rune_codec_activation_plan_t *plans;
	unsigned char *strings;
	uint64_t *link_keys;
	uint8_t *source_marks;
	uint32_t *plan_references;
	uint32_t *node_references;
	uint32_t *node_heads;
	uint32_t *node_indegrees;
	uint32_t *node_generations;
	uint32_t *node_touched;
	uint32_t *node_queue;
	uint32_t *edge_next;
	uint8_t *string_marks;
} rune_decode_storage_t;

static sg_rune_file_load_result_t RuneFile_Result(
	sg_rune_file_load_status_t status, const char *stage,
	const char *reason, uint32_t index, int os_error)
{
	sg_rune_file_load_result_t result;

	result.status = status;
	result.stage = stage;
	result.reason = reason;
	result.index = index;
	result.os_error = os_error;
	return result;
}

static const char *RuneFile_DiagnosticText(sg_rune_codec_diagnostic_t diagnostic)
{
	switch ((int)diagnostic)
	{
#define RUNE_DIAGNOSTIC_CASE(symbol, id, message) \
	case symbol: return #symbol ": " message;
	SG_RUNE_WIRE_DIAGNOSTIC_ROWS(RUNE_DIAGNOSTIC_CASE)
#undef RUNE_DIAGNOSTIC_CASE
	case RLCODEC_BAD_MECHANISM_CONTRACT:
		return "mechanism contract mismatch";
	case RLCODEC_BAD_ACTIVATION_NODE:
		return "invalid mechanism node";
	case RLCODEC_BAD_ACTIVATION_EDGE:
		return "invalid mechanism edge";
	case RLCODEC_BAD_ACTIVATION_PLAN:
		return "invalid mechanism plan";
	case RLCODEC_BAD_STRING_POOL:
		return "invalid mechanism string pool";
	case RLCODEC_DUPLICATE_NODE_KEY:
		return "duplicate mechanism node key";
	case RLCODEC_BAD_MECHANISM_GRAPH:
		return "invalid mechanism graph";
	case RLCODEC_NONZERO_RESERVED:
		return "nonzero reserved header field";
	default:
		return "unknown RUNE diagnostic";
	}
}

static void *RuneFile_AllocArray(sg_rune_alloc_fn allocate,
	size_t count, size_t item_size)
{
	if (!allocate || count == 0U)
		return NULL;
	if (item_size == 0U || count > (size_t)INT_MAX / item_size)
		return NULL;
	return allocate((int)(count * item_size));
}

static void RuneFile_StorageFree(rune_decode_storage_t *storage,
	sg_rune_free_fn release)
{
#define RUNE_FREE(member_) \
	do { if (storage->member_) release(storage->member_); } while (0)
	if (!storage || !release)
		return;
	RUNE_FREE(string_marks);
	RUNE_FREE(edge_next);
	RUNE_FREE(node_queue);
	RUNE_FREE(node_touched);
	RUNE_FREE(node_generations);
	RUNE_FREE(node_indegrees);
	RUNE_FREE(node_heads);
	RUNE_FREE(node_references);
	RUNE_FREE(plan_references);
	RUNE_FREE(source_marks);
	RUNE_FREE(link_keys);
	RUNE_FREE(strings);
	RUNE_FREE(plans);
	RUNE_FREE(edges);
	RUNE_FREE(nodes);
	RUNE_FREE(links);
	RUNE_FREE(seeds);
	memset(storage, 0, sizeof(*storage));
#undef RUNE_FREE
}

static void RuneFile_NativeFree(rune_t *rune,
	sg_rune_free_fn release)
{
	if (!rune || !release)
		return;
	if (rune->mechanism_strings)
		release(rune->mechanism_strings);
	if (rune->mechanism_plans)
		release(rune->mechanism_plans);
	if (rune->mechanism_edges)
		release(rune->mechanism_edges);
	if (rune->mechanism_nodes)
		release(rune->mechanism_nodes);
	if (rune->links)
		release(rune->links);
	if (rune->seeds)
		release(rune->seeds);
	release(rune);
}

static void RuneFile_WireIdentity(const rune_identity_t *source,
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

static void RuneFile_ArtifactFromWire(const sg_rune_codec_header_t *source,
	rune_artifact_t *destination)
{
	memset(destination, 0, sizeof(*destination));
	destination->magic = source->magic;
	destination->payload_crc32 = source->payload_crc32;
	destination->header_crc32 = source->header_crc32;
	destination->action_contract_crc32 = source->action_contract_crc32;
	destination->mechanism_contract_crc32 =
		source->mechanism_contract_crc32;
	destination->num_seeds = source->num_seeds;
	destination->num_links = source->num_links;
	destination->num_mechanism_nodes = source->num_activation_nodes;
	destination->num_mechanism_edges = source->num_activation_edges;
	destination->num_inventory_edges = source->num_inventory_edges;
	destination->num_mechanism_plans = source->num_activation_plans;
	destination->string_bytes = source->string_bytes;
	destination->identity.bsp_checksum = source->bsp_checksum;
	destination->identity.entity_crc32 = source->entity_crc32;
	destination->identity.physics_flags = source->physics_flags;
	destination->identity.gravity = source->gravity;
	destination->identity.airaccelerate = source->airaccelerate;
	destination->identity.maxvelocity = source->maxvelocity;
	destination->identity.pmove_substep_ms = source->pmove_substep_ms;
	destination->identity.server_frame_ms = source->server_frame_ms;
	destination->identity.host_physics_id = source->host_physics_id;
	memcpy(destination->identity.map_name, source->map_name,
		sizeof(destination->identity.map_name));
}

static const char *RuneFile_AdaptDecoded(rune_t *rune,
	const sg_rune_codec_header_t *header,
	const rune_decode_storage_t *storage, uint32_t *index_out)
{
	uint32_t index;

	if (index_out)
		*index_out = UINT32_MAX;
	for (index = 0U; index < header->num_seeds; index++)
	{
		memcpy(rune->seeds[index].origin, storage->seeds[index].origin,
			sizeof(rune->seeds[index].origin));
		rune->seeds[index].area_hint =
			(short)storage->seeds[index].area_hint;
		rune->seeds[index].flags = (short)storage->seeds[index].flags;
	}
	for (index = 0U; index < header->num_links; index++)
	{
		const sg_rune_codec_link_t *source = &storage->links[index];
		rune_link_t *destination = &rune->links[index];
		int has_plan = source->activation_plan !=
			SG_RUNE_CODEC_NO_ACTIVATION_PLAN;

		if (!SG_ActionRuntimeSupported((int)source->action) ||
		    !SG_ActionMechanismAdmitted((int)source->action))
		{
			if (index_out)
				*index_out = index;
			return "action has no production controller";
		}
		if (SG_ActionMechanismPlanRequired((int)source->action) != has_plan)
		{
			if (index_out)
				*index_out = index;
			return has_plan ? "planless action claims a mechanism plan"
				: "mechanism action has no admitted execution plan";
		}
		if (has_plan && source->activation_plan >=
		    header->num_activation_plans)
		{
			if (index_out)
				*index_out = index;
			return "mechanism plan index is out of bounds";
		}
		if (has_plan)
		{
			const sg_rune_codec_activation_plan_t *plan =
				&storage->plans[source->activation_plan];

			if (!SG_ActionMechanismPlanAllowed((int)source->action,
			        plan->controller_kind))
			{
				if (index_out)
					*index_out = index;
				return "mechanism action/controller pair is not admitted";
			}
		}
		destination->from = (int)source->source;
		destination->to = (int)source->destination;
		destination->action = source->action;
		destination->provenance = source->provenance;
		destination->min_speed = source->min_speed;
		destination->heading = source->heading;
		destination->heading_slack = source->heading_slack;
		destination->exit_speed = source->exit_speed;
		destination->cost_ms = (short)source->cost_ms;
		memcpy(destination->anchor, source->suffix_anchor,
			sizeof(destination->anchor));
		memcpy(destination->mechanism_anchor, source->mechanism_anchor,
			sizeof(destination->mechanism_anchor));
		destination->sweep_clear_ms = source->sweep_clear_ms;
		destination->mode = source->mode;
		destination->mechanism_plan = source->activation_plan;
	}
	for (index = 0U; index < header->num_activation_nodes; index++)
	{
		const sg_rune_codec_activation_node_t *source = &storage->nodes[index];
		rune_mechanism_node_t *destination = &rune->mechanism_nodes[index];

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
	}
	for (index = 0U; index < header->num_activation_edges; index++)
	{
		const sg_rune_codec_activation_edge_t *source = &storage->edges[index];
		rune_mechanism_edge_t *destination = &rune->mechanism_edges[index];

		destination->from_key = source->from_key;
		destination->to_key = source->to_key;
		destination->kind = source->kind;
		destination->ordinal = source->ordinal;
		destination->delay_ms = source->delay_ms;
	}
	for (index = 0U; index < header->num_activation_plans; index++)
	{
		const sg_rune_codec_activation_plan_t *source = &storage->plans[index];
		rune_mechanism_plan_t *destination = &rune->mechanism_plans[index];

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
	memcpy(rune->mechanism_strings, storage->strings,
		header->string_bytes);
	RuneFile_ArtifactFromWire(header, &rune->artifact);
	rune->hdr.magic = (int)rune->artifact.magic;
	rune->hdr.num_seeds = (int)rune->artifact.num_seeds;
	rune->hdr.num_links = (int)rune->artifact.num_links;
	memcpy(rune->hdr.mapname, rune->artifact.identity.map_name,
		sizeof(rune->hdr.mapname));
	return NULL;
}

sg_rune_file_load_result_t SG_RuneFileLoad(const char *path,
	const rune_identity_t *expected_identity,
	sg_rune_alloc_fn allocate, sg_rune_free_fn release,
	rune_t **rune_out)
{
	unsigned char encoded_header[SG_RUNE_CODEC_HEADER_BYTES];
	rune_decode_storage_t storage;
	sg_rune_codec_workspace_t workspace;
	sg_rune_artifact_backing_t backing;
	sg_rune_artifact_loader_t loader;
	sg_rune_codec_header_t header;
	sg_rune_codec_identity_t wire_identity;
	sg_rune_codec_diagnostic_t diagnostic = RLCODEC_OK;
	sg_rune_file_load_result_t result;
	unsigned char *snapshot = NULL;
	rune_t *rune = NULL;
	FILE *file = NULL;
	size_t file_size = 0U;
	size_t read_size;
	long file_length;
	uint32_t failure_index = UINT32_MAX;
	const char *failure;

	if (rune_out)
		*rune_out = NULL;
	result = RuneFile_Result(SG_RUNE_FILE_LOAD_REJECTED, "argument",
		"invalid RUNE loader argument", UINT32_MAX, EINVAL);
	if (!path || !path[0] || !expected_identity || !allocate || !release ||
	    !rune_out)
		return result;
	memset(&storage, 0, sizeof(storage));
	memset(&workspace, 0, sizeof(workspace));
	memset(&backing, 0, sizeof(backing));
	memset(&header, 0, sizeof(header));
	SG_RuneArtifactLoaderReset(&loader);
	RuneFile_WireIdentity(expected_identity, &wire_identity);

	errno = 0;
	file = fopen(path, "rb");
	if (!file)
	{
		if (errno == ENOENT)
			return RuneFile_Result(SG_RUNE_FILE_LOAD_MISSING, "open",
				"artifact is missing", UINT32_MAX, ENOENT);
		return RuneFile_Result(SG_RUNE_FILE_LOAD_REJECTED, "open",
			"artifact open failure", UINT32_MAX, errno ? errno : EIO);
	}
	read_size = fread(encoded_header, 1, sizeof(encoded_header), file);
	if (read_size != sizeof(encoded_header) || ferror(file))
	{
		result = RuneFile_Result(SG_RUNE_FILE_LOAD_REJECTED, "header-read",
			"RUNE header is incomplete", UINT32_MAX,
			ferror(file) ? (errno ? errno : EIO) : 0);
		goto cleanup;
	}
	diagnostic = SG_RuneCodecDecodeHeader(encoded_header,
		sizeof(encoded_header), &header);
	if (diagnostic != RLCODEC_OK)
	{
		result = RuneFile_Result(SG_RUNE_FILE_LOAD_REJECTED, "header",
			RuneFile_DiagnosticText(diagnostic), UINT32_MAX, 0);
		goto cleanup;
	}
	if (SG_RuneCodecMatchIdentity(&header, &wire_identity) != RLCODEC_OK)
	{
		result = RuneFile_Result(SG_RUNE_FILE_LOAD_REJECTED, "identity",
			"artifact identity differs from current level", UINT32_MAX, 0);
		goto cleanup;
	}
	diagnostic = SG_RuneCodecFileSize(header.num_seeds, header.num_links,
		header.num_activation_nodes, header.num_activation_edges,
		header.num_activation_plans, header.string_bytes, &file_size);
	if (diagnostic != RLCODEC_OK || file_size > (size_t)INT_MAX)
	{
		result = RuneFile_Result(SG_RUNE_FILE_LOAD_REJECTED, "file-size",
			diagnostic == RLCODEC_OK ? "artifact exceeds allocator range"
			: RuneFile_DiagnosticText(diagnostic), UINT32_MAX, 0);
		goto cleanup;
	}
	if (fseek(file, 0, SEEK_END) != 0 ||
	    (file_length = ftell(file)) < 0 ||
	    (size_t)file_length != file_size || fseek(file, 0, SEEK_SET) != 0)
	{
		result = RuneFile_Result(SG_RUNE_FILE_LOAD_REJECTED, "file-size",
			"artifact size differs from authenticated header", UINT32_MAX,
			errno);
		goto cleanup;
	}
	snapshot = allocate((int)file_size);
	if (!snapshot)
	{
		result = RuneFile_Result(SG_RUNE_FILE_LOAD_REJECTED, "allocation",
			"artifact snapshot allocation failure", UINT32_MAX, 0);
		goto cleanup;
	}
	if (fread(snapshot, 1, file_size, file) != file_size ||
	    fgetc(file) != EOF || ferror(file))
	{
		result = RuneFile_Result(SG_RUNE_FILE_LOAD_REJECTED, "read",
			"short, trailing, or failed artifact read", UINT32_MAX,
			errno ? errno : EIO);
		goto cleanup;
	}
	if (fclose(file) != 0)
	{
		file = NULL;
		result = RuneFile_Result(SG_RUNE_FILE_LOAD_REJECTED, "close",
			"artifact close failure", UINT32_MAX, errno ? errno : EIO);
		goto cleanup;
	}
	file = NULL;

	rune = allocate((int)sizeof(*rune));
	if (rune)
		memset(rune, 0, sizeof(*rune));
	if (!rune)
	{
		result = RuneFile_Result(SG_RUNE_FILE_LOAD_REJECTED, "allocation",
			"native rune allocation failure", UINT32_MAX, 0);
		goto cleanup;
	}
	rune->seeds = RuneFile_AllocArray(allocate, header.num_seeds,
		sizeof(*rune->seeds));
	rune->links = RuneFile_AllocArray(allocate, header.num_links,
		sizeof(*rune->links));
	rune->mechanism_nodes = RuneFile_AllocArray(allocate,
		header.num_activation_nodes, sizeof(*rune->mechanism_nodes));
	rune->mechanism_edges = RuneFile_AllocArray(allocate,
		header.num_activation_edges, sizeof(*rune->mechanism_edges));
	rune->mechanism_plans = RuneFile_AllocArray(allocate,
		header.num_activation_plans, sizeof(*rune->mechanism_plans));
	rune->mechanism_strings = RuneFile_AllocArray(allocate,
		header.string_bytes, 1U);

	storage.seeds = RuneFile_AllocArray(allocate, header.num_seeds,
		sizeof(*storage.seeds));
	storage.links = RuneFile_AllocArray(allocate, header.num_links,
		sizeof(*storage.links));
	storage.nodes = RuneFile_AllocArray(allocate, header.num_activation_nodes,
		sizeof(*storage.nodes));
	storage.edges = RuneFile_AllocArray(allocate, header.num_activation_edges,
		sizeof(*storage.edges));
	storage.plans = RuneFile_AllocArray(allocate, header.num_activation_plans,
		sizeof(*storage.plans));
	storage.strings = RuneFile_AllocArray(allocate, header.string_bytes, 1U);
	storage.link_keys = RuneFile_AllocArray(allocate, header.num_links,
		sizeof(*storage.link_keys));
	storage.source_marks = RuneFile_AllocArray(allocate, header.num_seeds,
		sizeof(*storage.source_marks));
	storage.plan_references = RuneFile_AllocArray(allocate,
		header.num_activation_plans, sizeof(*storage.plan_references));
	storage.node_references = RuneFile_AllocArray(allocate,
		header.num_activation_nodes, sizeof(*storage.node_references));
	storage.node_heads = RuneFile_AllocArray(allocate,
		header.num_activation_nodes, sizeof(*storage.node_heads));
	storage.node_indegrees = RuneFile_AllocArray(allocate,
		header.num_activation_nodes, sizeof(*storage.node_indegrees));
	storage.node_generations = RuneFile_AllocArray(allocate,
		header.num_activation_nodes, sizeof(*storage.node_generations));
	storage.node_touched = RuneFile_AllocArray(allocate,
		header.num_activation_nodes, sizeof(*storage.node_touched));
	storage.node_queue = RuneFile_AllocArray(allocate,
		header.num_activation_nodes, sizeof(*storage.node_queue));
	storage.edge_next = RuneFile_AllocArray(allocate,
		header.num_activation_edges, sizeof(*storage.edge_next));
	storage.string_marks = RuneFile_AllocArray(allocate,
		header.string_bytes, 1U);

	if (!rune->seeds || (header.num_links != 0U && !rune->links) ||
	    (header.num_activation_nodes != 0U && !rune->mechanism_nodes) ||
	    (header.num_activation_edges != 0U && !rune->mechanism_edges) ||
	    (header.num_activation_plans != 0U && !rune->mechanism_plans) ||
	    !rune->mechanism_strings || !storage.seeds ||
	    (header.num_links != 0U &&
	     (!storage.links || !storage.link_keys)) ||
	    (header.num_activation_nodes != 0U &&
	     (!storage.nodes || !storage.node_references ||
	      !storage.node_heads || !storage.node_indegrees ||
	      !storage.node_generations || !storage.node_touched ||
	      !storage.node_queue)) ||
	    (header.num_activation_edges != 0U &&
	     (!storage.edges || !storage.edge_next)) ||
	    (header.num_activation_plans != 0U &&
	     (!storage.plans || !storage.plan_references)) ||
	    !storage.source_marks || !storage.strings || !storage.string_marks)
	{
		result = RuneFile_Result(SG_RUNE_FILE_LOAD_REJECTED, "allocation",
			"RUNE decode workspace allocation failure", UINT32_MAX, 0);
		goto cleanup;
	}

	backing.seeds = storage.seeds;
	backing.seed_capacity = header.num_seeds;
	backing.links = storage.links;
	backing.link_capacity = header.num_links;
	backing.nodes = storage.nodes;
	backing.node_capacity = header.num_activation_nodes;
	backing.edges = storage.edges;
	backing.edge_capacity = header.num_activation_edges;
	backing.plans = storage.plans;
	backing.plan_capacity = header.num_activation_plans;
	backing.strings = storage.strings;
	backing.string_capacity = header.string_bytes;
	workspace.graph_link_keys = storage.link_keys;
	workspace.graph_link_key_capacity = header.num_links;
	workspace.graph_source_marks = storage.source_marks;
	workspace.graph_source_mark_capacity = header.num_seeds;
	workspace.plan_references = storage.plan_references;
	workspace.plan_reference_capacity = header.num_activation_plans;
	workspace.node_references = storage.node_references;
	workspace.node_reference_capacity = header.num_activation_nodes;
	workspace.node_heads = storage.node_heads;
	workspace.node_head_capacity = header.num_activation_nodes;
	workspace.node_indegrees = storage.node_indegrees;
	workspace.node_indegree_capacity = header.num_activation_nodes;
	workspace.node_generations = storage.node_generations;
	workspace.node_generation_capacity = header.num_activation_nodes;
	workspace.node_touched = storage.node_touched;
	workspace.node_touched_capacity = header.num_activation_nodes;
	workspace.node_queue = storage.node_queue;
	workspace.node_queue_capacity = header.num_activation_nodes;
	workspace.edge_next = storage.edge_next;
	workspace.edge_next_capacity = header.num_activation_edges;
	workspace.string_marks = storage.string_marks;
	workspace.string_mark_capacity = header.string_bytes;

	diagnostic = SG_RuneArtifactLoaderLoad(&loader, snapshot, file_size,
		&wire_identity, &backing, &workspace);
	if (diagnostic != RLCODEC_OK || !SG_RuneArtifactLoaderIsPublished(&loader))
	{
		result = RuneFile_Result(SG_RUNE_FILE_LOAD_REJECTED, "decode",
			RuneFile_DiagnosticText(diagnostic), UINT32_MAX, 0);
		goto cleanup;
	}
	failure = RuneFile_AdaptDecoded(rune, &header, &storage, &failure_index);
	if (failure)
	{
		result = RuneFile_Result(SG_RUNE_FILE_LOAD_REJECTED,
			"runtime-adapter", failure, failure_index, 0);
		goto cleanup;
	}
	*rune_out = rune;
	rune = NULL;
	result = RuneFile_Result(SG_RUNE_FILE_LOAD_READY, "ready", NULL,
		UINT32_MAX, 0);

cleanup:
	if (file)
		(void)fclose(file);
	RuneFile_StorageFree(&storage, release);
	if (snapshot)
		release(snapshot);
	RuneFile_NativeFree(rune, release);
	return result;
}

sg_rune_file_inspect_status_t SG_RuneFileInspect(const char *path,
	const rune_identity_t *expected_identity, rune_artifact_t *artifact_out,
	int *os_error_out)
{
	unsigned char header_bytes[SG_RUNE_CODEC_HEADER_BYTES];
	sg_rune_codec_identity_t wire_identity;
	sg_rune_codec_header_t wire_header;
	FILE *file = NULL;
	long file_length;
	size_t expected_file_size;
	size_t read_size;
	int close_status;

	if (artifact_out)
		memset(artifact_out, 0, sizeof(*artifact_out));
	if (os_error_out)
		*os_error_out = 0;
	if (!path || !path[0] || !expected_identity || !artifact_out)
		return SG_RUNE_FILE_INSPECT_DRIFT;
	RuneFile_WireIdentity(expected_identity, &wire_identity);
	errno = 0;
	file = fopen(path, "rb");
	if (!file)
	{
		if (os_error_out)
			*os_error_out = errno ? errno : EIO;
		return SG_RUNE_FILE_INSPECT_ERROR;
	}
	read_size = fread(header_bytes, 1, sizeof(header_bytes), file);
	if (read_size != sizeof(header_bytes) || ferror(file) ||
	    fseek(file, 0, SEEK_END) != 0 ||
	    (file_length = ftell(file)) < 0)
	{
		int saved_error = errno ? errno : EIO;

		(void)fclose(file);
		if (os_error_out)
			*os_error_out = saved_error;
		return SG_RUNE_FILE_INSPECT_ERROR;
	}
	errno = 0;
	close_status = fclose(file);
	if (close_status != 0)
	{
		if (os_error_out)
			*os_error_out = errno ? errno : EIO;
		return SG_RUNE_FILE_INSPECT_ERROR;
	}
	if (SG_RuneCodecDecodeHeader(header_bytes, sizeof(header_bytes),
	        &wire_header) != RLCODEC_OK ||
	    SG_RuneCodecMatchIdentity(&wire_header, &wire_identity) != RLCODEC_OK ||
	    SG_RuneCodecFileSize(wire_header.num_seeds, wire_header.num_links,
	        wire_header.num_activation_nodes,
	        wire_header.num_activation_edges,
	        wire_header.num_activation_plans, wire_header.string_bytes,
	        &expected_file_size) != RLCODEC_OK ||
	    expected_file_size != (size_t)file_length)
		return SG_RUNE_FILE_INSPECT_DRIFT;
	RuneFile_ArtifactFromWire(&wire_header, artifact_out);
	return SG_RUNE_FILE_INSPECT_MATCH;
}
