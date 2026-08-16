/* sg_rune_artifact_writer.h -- allocation-free authenticated RUNE artifact streamer. */
#ifndef SG_RUNE_ARTIFACT_WRITER_H
#define SG_RUNE_ARTIFACT_WRITER_H

#include <stddef.h>
#include <stdint.h>

#include "sg_rune_codec.h"

#define SG_RUNE_ARTIFACT_WRITE_INDEX_NONE UINT32_MAX

typedef enum sg_rune_artifact_write_stage_e
{
	SG_RUNE_ARTIFACT_WRITE_STAGE_ARGUMENT = 0,
	SG_RUNE_ARTIFACT_WRITE_STAGE_PREFLIGHT_HEADER,
	SG_RUNE_ARTIFACT_WRITE_STAGE_PREFLIGHT_SEED,
	SG_RUNE_ARTIFACT_WRITE_STAGE_PREFLIGHT_LINK,
	SG_RUNE_ARTIFACT_WRITE_STAGE_PREFLIGHT_NODE,
	SG_RUNE_ARTIFACT_WRITE_STAGE_PREFLIGHT_EDGE,
	SG_RUNE_ARTIFACT_WRITE_STAGE_PREFLIGHT_PLAN,
	SG_RUNE_ARTIFACT_WRITE_STAGE_PREFLIGHT_STRING_POOL,
	SG_RUNE_ARTIFACT_WRITE_STAGE_VALIDATE,
	SG_RUNE_ARTIFACT_WRITE_STAGE_EMIT_HEADER,
	SG_RUNE_ARTIFACT_WRITE_STAGE_EMIT_SEED,
	SG_RUNE_ARTIFACT_WRITE_STAGE_EMIT_LINK,
	SG_RUNE_ARTIFACT_WRITE_STAGE_EMIT_NODE,
	SG_RUNE_ARTIFACT_WRITE_STAGE_EMIT_EDGE,
	SG_RUNE_ARTIFACT_WRITE_STAGE_EMIT_PLAN,
	SG_RUNE_ARTIFACT_WRITE_STAGE_EMIT_STRING_POOL,
	SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY_HEADER,
	SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY_SEED,
	SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY_LINK,
	SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY_NODE,
	SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY_EDGE,
	SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY_PLAN,
	SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY_STRING_POOL,
	SG_RUNE_ARTIFACT_WRITE_STAGE_VERIFY,
	SG_RUNE_ARTIFACT_WRITE_STAGE_DONE
} sg_rune_artifact_write_stage_t;

/* Return zero only after accepting the complete fragment.  A nonzero return
 * stops the writer immediately.  The writer never retries or subdivides a
 * fragment, and fragment storage is valid only for the duration of the call. */
typedef int (*sg_rune_artifact_write_sink_fn)(void *context,
	const unsigned char *fragment, size_t fragment_size);

typedef struct sg_rune_artifact_write_result_s
{
	sg_rune_codec_diagnostic_t diagnostic;
	sg_rune_artifact_write_stage_t stage;
	uint32_t index;
	size_t bytes_written;
	size_t file_size;
	uint32_t payload_crc32;
} sg_rune_artifact_write_result_t;

/* Validate and stream one already-native RUNE artifact file without allocation,
 * filesystem access, input mutation, reordering, sorting, or culling.
 * workspace is caller-owned validation scratch and is intentionally mutable;
 * its used ranges must be pairwise disjoint and must not overlap any input.
 *
 * Before the first sink call, pass one validates the identity, computes the
 * exact bounded file size, encodes every record in canonical section order,
 * computes the payload CRC, validates the whole graph/string contract, and
 * encodes the authenticated 160-byte header.  Pass two emits exactly one
 * header fragment, one complete fragment per record, and one complete string
 * pool fragment.  It re-encodes and re-hashes the payload; a final re-hash also
 * catches mutation performed by a sink after accepting a fragment.  Success
 * therefore proves that the emitted bytes still match the preflight header.
 *
 * The identity, arrays, strings, counts, and workspace configuration must stay
 * stable for the duration of the call.  Structural acceptance of action 12
 * does not grant it live runtime authority. */
sg_rune_artifact_write_result_t SG_RuneArtifactWrite(
	const sg_rune_codec_identity_t *identity,
	const sg_rune_codec_seed_t *seeds, uint32_t num_seeds,
	const sg_rune_codec_link_t *links, uint32_t num_links,
	const sg_rune_codec_activation_node_t *nodes, uint32_t num_nodes,
	const sg_rune_codec_activation_edge_t *edges, uint32_t num_edges,
	const sg_rune_codec_activation_plan_t *plans, uint32_t num_plans,
	const unsigned char *strings, uint32_t string_bytes,
	sg_rune_codec_workspace_t *workspace,
	sg_rune_artifact_write_sink_fn sink, void *sink_context);

#endif /* SG_RUNE_ARTIFACT_WRITER_H */
