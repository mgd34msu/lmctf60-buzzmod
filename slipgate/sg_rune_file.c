/* sg_rune_file.c -- RUNE artifact decode and native adaptation. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "../q_shared.h"
#include "sg_rune_file.h"

#include "sg_action.h"
#include "sg_rune_artifact_loader.h"
#include "sg_rune_v2_content_identity.h"
#include "sg_rune_v2_exact_snapshot.h"

#include <errno.h>
#include <limits.h>
#include <string.h>

static void RuneFile_ContentIdentityText(const sg_rune_v2_content_id_t *identity,
	char out[65])
{
	static const char hex[] = "0123456789abcdef";
	size_t index;

	for (index = 0U; index < sizeof(identity->bytes); index++)
	{
		out[index * 2U] = hex[identity->bytes[index] >> 4];
		out[index * 2U + 1U] = hex[identity->bytes[index] & 15U];
	}
	out[64] = '\0';
}

void SG_RuneFileSHA256Buffer(const unsigned char *bytes, size_t length,
	char out[65])
{
	sg_rune_v2_content_id_t identity;

	if (!SG_RuneV2ContentIdentitySHA256(bytes, length, &identity))
	{
		out[0] = '\0';
		return;
	}
	RuneFile_ContentIdentityText(&identity, out);
}

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

static sg_rune_file_load_result_t RuneFile_SnapshotFailure(
	sg_rune_v2_snapshot_diagnostic_t diagnostic, int os_error)
{
	switch (diagnostic)
	{
	case SG_RUNE_V2_SNAPSHOT_OPEN_FAILED:
		return RuneFile_Result(os_error == ENOENT
			? SG_RUNE_FILE_LOAD_MISSING : SG_RUNE_FILE_LOAD_INFRA,
			"snapshot", "artifact snapshot open failure", UINT32_MAX,
			os_error);
	case SG_RUNE_V2_SNAPSHOT_NOT_REGULAR:
		return RuneFile_Result(SG_RUNE_FILE_LOAD_REJECTED, "snapshot",
			"artifact is not a regular file", UINT32_MAX, 0);
	case SG_RUNE_V2_SNAPSHOT_TOO_LARGE:
		return RuneFile_Result(SG_RUNE_FILE_LOAD_REJECTED, "snapshot",
			"artifact exceeds snapshot limit", UINT32_MAX, 0);
	case SG_RUNE_V2_SNAPSHOT_SHORT_READ:
		return RuneFile_Result(SG_RUNE_FILE_LOAD_REJECTED, "snapshot",
			"artifact snapshot is truncated", UINT32_MAX, 0);
	case SG_RUNE_V2_SNAPSHOT_EXTRA_BYTES:
		return RuneFile_Result(SG_RUNE_FILE_LOAD_REJECTED, "snapshot",
			"artifact changed during snapshot read", UINT32_MAX, 0);
	case SG_RUNE_V2_SNAPSHOT_FILE_CHANGED:
		return RuneFile_Result(SG_RUNE_FILE_LOAD_INFRA, "snapshot",
			"artifact changed during snapshot read", UINT32_MAX, os_error);
	default:
		return RuneFile_Result(SG_RUNE_FILE_LOAD_INFRA, "snapshot",
			"artifact snapshot failure", UINT32_MAX, os_error);
	}
}

static sg_rune_file_inspect_status_t RuneFile_SnapshotInspectStatus(
	sg_rune_v2_snapshot_diagnostic_t diagnostic)
{
	switch (diagnostic)
	{
	case SG_RUNE_V2_SNAPSHOT_NOT_REGULAR:
	case SG_RUNE_V2_SNAPSHOT_TOO_LARGE:
	case SG_RUNE_V2_SNAPSHOT_SHORT_READ:
	case SG_RUNE_V2_SNAPSHOT_EXTRA_BYTES:
		return SG_RUNE_FILE_INSPECT_DRIFT;
	default:
		return SG_RUNE_FILE_INSPECT_ERROR;
	}
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
	case RLCODEC_BAD_ROUTE_CONTRACT:
		return "invalid route contract";
	default:
		return "unknown RUNE diagnostic";
	}
}

/* These diagnostics describe the host/loader contract rather than frozen
 * RUNE bytes.  They must never turn a retryable runtime fault into a request
 * to replace an already authenticated candidate. */
static int RuneFile_DiagnosticIsInfrastructure(
	sg_rune_codec_diagnostic_t diagnostic)
{
	switch ((int)diagnostic)
	{
	case RLW_INVALID_ARGUMENT:
	case RLW_IO_ERROR:
	case RLW_IDENTITY_UNAVAILABLE:
	case RLW_ALLOCATION_FAILED:
		return 1;
	default:
		return 0;
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
	destination->route_contract = source->route_contract;
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
		memcpy(destination->push_velocity, source->push_velocity,
			sizeof(destination->push_velocity));
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
	rune_decode_storage_t storage;
	sg_rune_codec_workspace_t workspace;
	sg_rune_artifact_backing_t backing;
	sg_rune_artifact_loader_t loader;
	sg_rune_codec_header_t header;
	sg_rune_codec_identity_t wire_identity;
	sg_rune_codec_diagnostic_t diagnostic = RLCODEC_OK;
	sg_rune_v2_snapshot_diagnostic_t snapshot_diagnostic;
	sg_rune_v2_exact_snapshot_t *exact_snapshot = NULL;
	const sg_rune_v2_snapshot_view_t *snapshot_view = NULL;
	sg_rune_file_load_result_t result;
	rune_t *rune = NULL;
	size_t file_size = 0U;
	uint32_t failure_index = UINT32_MAX;
	const char *failure;

	if (rune_out)
		*rune_out = NULL;
	result = RuneFile_Result(SG_RUNE_FILE_LOAD_INFRA, "argument",
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
	snapshot_diagnostic = SG_RuneV2ExactSnapshotAcquireFile(path,
		SG_RUNE_V2_SNAPSHOT_ARTIFACT, &exact_snapshot);
	if (snapshot_diagnostic != SG_RUNE_V2_SNAPSHOT_OK)
	{
		result = RuneFile_SnapshotFailure(snapshot_diagnostic,
			errno ? errno : EIO);
		goto cleanup;
	}
	if (SG_RuneV2ExactSnapshotInspect(exact_snapshot, &snapshot_view) !=
	    SG_RUNE_V2_SNAPSHOT_OK || !snapshot_view)
	{
		result = RuneFile_Result(SG_RUNE_FILE_LOAD_INFRA, "snapshot",
			"artifact snapshot inspection failed", UINT32_MAX, EIO);
		goto cleanup;
	}
	if (snapshot_view->size < SG_RUNE_CODEC_HEADER_BYTES)
	{
		result = RuneFile_Result(SG_RUNE_FILE_LOAD_REJECTED, "header-read",
			"RUNE header is incomplete", UINT32_MAX, 0);
		goto cleanup;
	}
	diagnostic = SG_RuneCodecDecodeHeader(snapshot_view->bytes,
		SG_RUNE_CODEC_HEADER_BYTES, &header);
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
	if (snapshot_view->size != file_size)
	{
		result = RuneFile_Result(SG_RUNE_FILE_LOAD_REJECTED, "file-size",
			"artifact size differs from authenticated header", UINT32_MAX, 0);
		goto cleanup;
	}

	rune = allocate((int)sizeof(*rune));
	if (rune)
		memset(rune, 0, sizeof(*rune));
	if (!rune)
	{
		result = RuneFile_Result(SG_RUNE_FILE_LOAD_INFRA, "allocation",
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
		result = RuneFile_Result(SG_RUNE_FILE_LOAD_INFRA, "allocation",
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

	diagnostic = SG_RuneArtifactLoaderLoad(&loader, snapshot_view->bytes,
		file_size,
		&wire_identity, &backing, &workspace);
	if (diagnostic != RLCODEC_OK)
	{
		result = RuneFile_Result(RuneFile_DiagnosticIsInfrastructure(diagnostic)
			? SG_RUNE_FILE_LOAD_INFRA : SG_RUNE_FILE_LOAD_REJECTED, "decode",
			RuneFile_DiagnosticText(diagnostic), UINT32_MAX, 0);
		goto cleanup;
	}
	if (!SG_RuneArtifactLoaderIsPublished(&loader))
	{
		result = RuneFile_Result(SG_RUNE_FILE_LOAD_INFRA, "decode",
			"artifact loader publication unavailable", UINT32_MAX, 0);
		goto cleanup;
	}
	failure = RuneFile_AdaptDecoded(rune, &header, &storage, &failure_index);
	if (failure)
	{
		result = RuneFile_Result(SG_RUNE_FILE_LOAD_REJECTED,
			"runtime-adapter", failure, failure_index, 0);
		goto cleanup;
	}
	RuneFile_ContentIdentityText(&snapshot_view->content_identity,
		rune->encoded_sha256);
	*rune_out = rune;
	rune = NULL;
	result = RuneFile_Result(SG_RUNE_FILE_LOAD_READY, "ready", NULL,
		UINT32_MAX, 0);

cleanup:
	RuneFile_StorageFree(&storage, release);
	SG_RuneV2ExactSnapshotDestroy(exact_snapshot);
	RuneFile_NativeFree(rune, release);
	return result;
}

static sg_rune_file_inspect_status_t RuneFile_AcquireSnapshot(const char *path,
	sg_rune_v2_exact_snapshot_t **snapshot_out,
	const sg_rune_v2_snapshot_view_t **view_out, int *os_error_out)
{
	sg_rune_v2_snapshot_diagnostic_t diagnostic;

	*snapshot_out = NULL;
	*view_out = NULL;
	errno = 0;
	diagnostic = SG_RuneV2ExactSnapshotAcquireFile(path,
		SG_RUNE_V2_SNAPSHOT_ARTIFACT, snapshot_out);
	if (diagnostic != SG_RUNE_V2_SNAPSHOT_OK)
	{
		sg_rune_file_inspect_status_t status =
			RuneFile_SnapshotInspectStatus(diagnostic);

		if (status == SG_RUNE_FILE_INSPECT_ERROR && os_error_out)
			*os_error_out = errno ? errno : EIO;
		return status;
	}
	if (SG_RuneV2ExactSnapshotInspect(*snapshot_out, view_out) !=
	    SG_RUNE_V2_SNAPSHOT_OK || !*view_out)
	{
		SG_RuneV2ExactSnapshotDestroy(*snapshot_out);
		*snapshot_out = NULL;
		if (os_error_out)
			*os_error_out = EIO;
		return SG_RUNE_FILE_INSPECT_ERROR;
	}
	return SG_RUNE_FILE_INSPECT_MATCH;
}

static int RuneFile_SnapshotHeader(const sg_rune_v2_snapshot_view_t *view,
	const rune_identity_t *expected_identity, sg_rune_codec_header_t *header_out)
{
	sg_rune_codec_identity_t wire_identity;
	size_t expected_file_size;

	if (view->size < SG_RUNE_CODEC_HEADER_BYTES)
		return 0;
	RuneFile_WireIdentity(expected_identity, &wire_identity);
	if (SG_RuneCodecDecodeHeader(view->bytes, SG_RUNE_CODEC_HEADER_BYTES,
	        header_out) != RLCODEC_OK ||
	    SG_RuneCodecMatchIdentity(header_out, &wire_identity) != RLCODEC_OK ||
	    SG_RuneCodecFileSize(header_out->num_seeds, header_out->num_links,
	        header_out->num_activation_nodes,
	        header_out->num_activation_edges,
	        header_out->num_activation_plans, header_out->string_bytes,
	        &expected_file_size) != RLCODEC_OK ||
	    expected_file_size != view->size)
		return 0;
	return 1;
}

sg_rune_file_inspect_status_t SG_RuneFileInspect(const char *path,
	const rune_identity_t *expected_identity, rune_artifact_t *artifact_out,
	int *os_error_out)
{
	sg_rune_v2_exact_snapshot_t *snapshot = NULL;
	const sg_rune_v2_snapshot_view_t *view = NULL;
	sg_rune_codec_header_t header;
	sg_rune_file_inspect_status_t status;

	if (artifact_out)
		memset(artifact_out, 0, sizeof(*artifact_out));
	if (os_error_out)
		*os_error_out = 0;
	if (!path || !path[0] || !expected_identity || !artifact_out)
		return SG_RUNE_FILE_INSPECT_DRIFT;
	status = RuneFile_AcquireSnapshot(path, &snapshot, &view, os_error_out);
	if (status != SG_RUNE_FILE_INSPECT_MATCH)
		return status;
	if (!RuneFile_SnapshotHeader(view, expected_identity, &header))
		status = SG_RUNE_FILE_INSPECT_DRIFT;
	else
	{
		RuneFile_ArtifactFromWire(&header, artifact_out);
		status = SG_RUNE_FILE_INSPECT_MATCH;
	}
	SG_RuneV2ExactSnapshotDestroy(snapshot);
	return status;
}

static int RuneFile_SHA256TextValid(const char *text)
{
	size_t index;

	if (!text || strlen(text) != 64U)
		return 0;
	for (index = 0U; index < 64U; index++)
		if (!((text[index] >= '0' && text[index] <= '9') ||
		      (text[index] >= 'a' && text[index] <= 'f')))
			return 0;
	return 1;
}

sg_rune_file_inspect_status_t SG_RuneFileInspectExact(const char *path,
	const rune_identity_t *expected_identity, const char *expected_sha256,
	rune_artifact_t *artifact_out, int *os_error_out)
{
	sg_rune_v2_exact_snapshot_t *snapshot = NULL;
	const sg_rune_v2_snapshot_view_t *view = NULL;
	sg_rune_codec_header_t header;
	char actual_sha256[65];
	sg_rune_file_inspect_status_t status;

	if (artifact_out)
		memset(artifact_out, 0, sizeof(*artifact_out));
	if (os_error_out)
		*os_error_out = 0;
	if (!path || !path[0] || !expected_identity || !artifact_out ||
	    !RuneFile_SHA256TextValid(expected_sha256))
		return SG_RUNE_FILE_INSPECT_DRIFT;
	status = RuneFile_AcquireSnapshot(path, &snapshot, &view, os_error_out);
	if (status != SG_RUNE_FILE_INSPECT_MATCH)
		return status;
	if (!RuneFile_SnapshotHeader(view, expected_identity, &header))
		status = SG_RUNE_FILE_INSPECT_DRIFT;
	else
	{
		RuneFile_ContentIdentityText(&view->content_identity, actual_sha256);
		if (strcmp(actual_sha256, expected_sha256) != 0)
			status = SG_RUNE_FILE_INSPECT_DRIFT;
		else
		{
			RuneFile_ArtifactFromWire(&header, artifact_out);
			status = SG_RUNE_FILE_INSPECT_MATCH;
		}
	}
	SG_RuneV2ExactSnapshotDestroy(snapshot);
	return status;
}
