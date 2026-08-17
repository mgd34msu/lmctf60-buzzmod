/* sg_rune_stream.h -- RUNE stream adapter. */
#ifndef SG_RUNE_STREAM_H
#define SG_RUNE_STREAM_H

#include <stddef.h>
#include <stdint.h>

struct rune_identity_s;
struct rune_seed_s;
struct rune_link_s;
struct rune_mechanism_node_s;
struct rune_mechanism_edge_s;
struct rune_mechanism_plan_s;
typedef struct sg_rune_stream_s sg_rune_stream_t;

typedef enum sg_rune_stream_stage_e
{
	SG_RUNE_STREAM_STAGE_ARGUMENT = 0,
	SG_RUNE_STREAM_STAGE_PREFLIGHT_HEADER,
	SG_RUNE_STREAM_STAGE_PREFLIGHT_SEED,
	SG_RUNE_STREAM_STAGE_PREFLIGHT_LINK,
	SG_RUNE_STREAM_STAGE_PREFLIGHT_NODE,
	SG_RUNE_STREAM_STAGE_PREFLIGHT_EDGE,
	SG_RUNE_STREAM_STAGE_PREFLIGHT_PLAN,
	SG_RUNE_STREAM_STAGE_PREFLIGHT_STRING_POOL,
	SG_RUNE_STREAM_STAGE_VALIDATE,
	SG_RUNE_STREAM_STAGE_EMIT_HEADER,
	SG_RUNE_STREAM_STAGE_EMIT_SEED,
	SG_RUNE_STREAM_STAGE_EMIT_LINK,
	SG_RUNE_STREAM_STAGE_EMIT_NODE,
	SG_RUNE_STREAM_STAGE_EMIT_EDGE,
	SG_RUNE_STREAM_STAGE_EMIT_PLAN,
	SG_RUNE_STREAM_STAGE_EMIT_STRING_POOL,
	SG_RUNE_STREAM_STAGE_VERIFY_HEADER,
	SG_RUNE_STREAM_STAGE_VERIFY_SEED,
	SG_RUNE_STREAM_STAGE_VERIFY_LINK,
	SG_RUNE_STREAM_STAGE_VERIFY_NODE,
	SG_RUNE_STREAM_STAGE_VERIFY_EDGE,
	SG_RUNE_STREAM_STAGE_VERIFY_PLAN,
	SG_RUNE_STREAM_STAGE_VERIFY_STRING_POOL,
	SG_RUNE_STREAM_STAGE_VERIFY,
	SG_RUNE_STREAM_STAGE_DONE
} sg_rune_stream_stage_t;

#define SG_RUNE_STREAM_INDEX_NONE UINT32_MAX

typedef struct sg_rune_stream_result_s
{
	int diagnostic;
	sg_rune_stream_stage_t stage;
	uint32_t index;
	size_t bytes_written;
	size_t file_size;
	uint32_t payload_crc32;
} sg_rune_stream_result_t;

typedef int (*sg_rune_stream_sink_fn)(void *context,
	const unsigned char *fragment, size_t fragment_size);
typedef void *(*sg_rune_stream_alloc_fn)(void *context, size_t bytes);
typedef void (*sg_rune_stream_free_fn)(void *context, void *allocation);

typedef struct sg_rune_stream_source_s
{
	const struct rune_identity_s *identity;
	const struct rune_seed_s *seeds;
	uint32_t num_seeds;
	const struct rune_link_s *links;
	uint32_t num_links;
	const struct rune_mechanism_node_s *nodes;
	uint32_t num_nodes;
	const struct rune_mechanism_edge_s *edges;
	uint32_t num_edges;
	const struct rune_mechanism_plan_s *plans;
	uint32_t num_plans;
	const unsigned char *strings;
	uint32_t string_bytes;
} sg_rune_stream_source_t;

/* Creation snapshots every native record and owns all codec conversion
 * and validation workspace. The source may be released only after Create
 * returns; strings are copied as well. */
sg_rune_stream_t *SG_RuneStreamCreate(
	const sg_rune_stream_source_t *source,
	sg_rune_stream_alloc_fn allocate, sg_rune_stream_free_fn release,
	void *allocation_context, sg_rune_stream_result_t *failure_out);
void SG_RuneStreamDestroy(sg_rune_stream_t *stream);
sg_rune_stream_result_t SG_RuneStreamWrite(void *stream,
	sg_rune_stream_sink_fn sink, void *sink_context);

int SG_RuneStreamResultSucceeded(const sg_rune_stream_result_t *result);
void SG_RuneStreamResultMarkIOFailure(sg_rune_stream_result_t *result,
	int payload_mismatch);

#endif /* SG_RUNE_STREAM_H */
