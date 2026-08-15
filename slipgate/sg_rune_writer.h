/* sg_rune_writer.h -- pure native-graph adapter and streaming RUNE v3 writer. */
#ifndef SG_RUNE_WRITER_H
#define SG_RUNE_WRITER_H

#include <stddef.h>
#include <stdint.h>

#include "sg_rune.h"
#include "sg_rune_wire.h"

#define SG_RUNE_WRITE_INDEX_NONE UINT32_MAX

typedef enum sg_rune_write_stage_e
{
	SG_RUNE_WRITE_STAGE_ARGUMENT = 0,
	SG_RUNE_WRITE_STAGE_PREFLIGHT,
	SG_RUNE_WRITE_STAGE_ADAPT_SEED,
	SG_RUNE_WRITE_STAGE_ADAPT_LINK,
	SG_RUNE_WRITE_STAGE_HEADER,
	SG_RUNE_WRITE_STAGE_EMIT_HEADER,
	SG_RUNE_WRITE_STAGE_EMIT_SEED,
	SG_RUNE_WRITE_STAGE_EMIT_LINK,
	SG_RUNE_WRITE_STAGE_DONE,
	/* Pass-two records changed after their pass-one CRC was fixed.  No record
	 * index can be inferred from a whole-payload CRC mismatch. */
	SG_RUNE_WRITE_STAGE_VERIFY
} sg_rune_write_stage_t;

/* Return zero only after accepting the complete fragment.  A nonzero return
 * aborts immediately with RLW_IO_ERROR; the writer never retries or splits a
 * fragment.  Fragment storage is valid only for the duration of the call. */
typedef int (*sg_rune_write_sink_fn)(void *context,
	const unsigned char *fragment, size_t fragment_size);

typedef struct sg_rune_write_result_s
{
	rune_wire_diagnostic_t diagnostic;
	rune_reject_reason_t reason;
	sg_rune_write_stage_t stage;
	uint32_t index;
	size_t bytes_written;
	size_t file_size;
	uint32_t payload_crc32;
} sg_rune_write_result_t;

/* Adapt and emit one native generator graph without allocation, filesystem
 * access, input mutation, sorting, deduplication, or culling.  The workspace
 * remains caller-owned and needs link_keys[num_links] plus
 * source_marks[num_seeds], exactly as SG_RuneV3ValidateGraph documents.
 *
 * Pass one adapts and validates every input record, validates duplicate and
 * source-ownership laws, and computes the encoded payload CRC.  The sink is
 * not called unless that complete preflight and final header encoding pass.
 * Pass two emits exactly one 128-byte header, each 16-byte seed in input
 * order, then each 44-byte link in input order.  Native action 7 and every
 * action above 8 reject because the native 28-byte record cannot carry an
 * executable v3 contract for them. */
sg_rune_write_result_t SG_RuneV3Write(
	const sg_rune_v3_identity_t *identity,
	const rune_seed_t *seeds, uint32_t num_seeds,
	const rune_link_t *links, uint32_t num_links,
	sg_rune_v3_workspace_t *workspace,
	sg_rune_write_sink_fn sink, void *sink_context);

#endif /* SG_RUNE_WRITER_H */
