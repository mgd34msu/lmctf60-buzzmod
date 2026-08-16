/* sg_rune_loader.h -- pure RUNE v3 snapshot inspection and native adapter. */
#ifndef SG_RUNE_LOADER_H
#define SG_RUNE_LOADER_H

#include <stddef.h>
#include <stdint.h>

#include "sg_rune.h"
#include "sg_rune_wire.h"

#define SG_RUNE_LOAD_INDEX_NONE UINT32_MAX

typedef enum sg_rune_snapshot_kind_e
{
	SG_RUNE_SNAPSHOT_UNKNOWN = 0,
	SG_RUNE_SNAPSHOT_V2,
	SG_RUNE_SNAPSHOT_V3
} sg_rune_snapshot_kind_t;

typedef enum sg_rune_load_stage_e
{
	SG_RUNE_LOAD_STAGE_ARGUMENT = 0,
	SG_RUNE_LOAD_STAGE_HEADER,
	SG_RUNE_LOAD_STAGE_FILE_SIZE,
	SG_RUNE_LOAD_STAGE_PAYLOAD_CRC,
	SG_RUNE_LOAD_STAGE_IDENTITY,
	SG_RUNE_LOAD_STAGE_CAPACITY,
	SG_RUNE_LOAD_STAGE_DECODE,
	SG_RUNE_LOAD_STAGE_SEED,
	SG_RUNE_LOAD_STAGE_LINK,
	SG_RUNE_LOAD_STAGE_ACTION,
	SG_RUNE_LOAD_STAGE_CONTROL,
	SG_RUNE_LOAD_STAGE_DONE
} sg_rune_load_stage_t;

typedef struct sg_rune_load_result_s
{
	rune_wire_diagnostic_t diagnostic;
	rune_reject_reason_t reason;
	sg_rune_load_stage_t stage;
	/* Zero based within the record class named by stage, or NONE when the
	 * failure belongs to the whole snapshot rather than one record. */
	uint32_t index;
	sg_rune_snapshot_kind_t snapshot_kind;
	/* Canonical size derived from a valid bounded header, else zero. */
	size_t file_size;
} sg_rune_load_result_t;

/* SG_RuneV3Decode needs explicit v3 records before they can be mapped into the
 * smaller native runtime records.  Every buffer remains caller-owned. */
typedef struct sg_rune_v3_loader_workspace_s
{
	sg_rune_v3_seed_t *wire_seeds;
	size_t wire_seed_capacity;
	sg_rune_v3_link_t *wire_links;
	size_t wire_link_capacity;
	sg_rune_v3_workspace_t graph;
} sg_rune_v3_loader_workspace_t;

/* Prefix-only format classification for actionable migration diagnostics.
 * UNKNOWN includes malformed, unsupported, and insufficient prefixes. */
sg_rune_snapshot_kind_t SG_RuneV3Probe(const unsigned char *snapshot,
	size_t snapshot_size);

/* Inspect exactly the first 128 bytes before allocating or reading the full
 * file.  This authenticates header semantics and identity, computes the exact
 * bounded file_size in the result, and leaves header_out untouched on failure.
 * Payload CRC and record validation necessarily wait for the exact snapshot. */
sg_rune_load_result_t SG_RuneV3InspectHeader(
	const unsigned char *encoded_header, size_t encoded_header_size,
	const sg_rune_v3_identity_t *expected,
	sg_rune_v3_header_t *header_out);

/* Authenticate an immutable exact file snapshot without allocating.  The full
 * payload CRC is checked, but records are intentionally not decoded: callers
 * use the returned bounded counts to allocate the Load buffers.  expected and
 * header_out are mandatory.  header_out is unchanged unless stage is DONE. */
sg_rune_load_result_t SG_RuneV3Inspect(const unsigned char *snapshot,
	size_t snapshot_size, const sg_rune_v3_identity_t *expected,
	sg_rune_v3_header_t *header_out);

/* Complete executor-facing law for the frozen native literal record.  This is
 * intentionally stricter than structural wire validation.  It requires
 * runtime support, validates every compound field before native adaptation,
 * and rejects a noncompound record with any compound-tail state. */
rune_reject_reason_t SG_RuneV3ValidateLiteralLink(
	const sg_rune_v3_seed_t *seeds, uint32_t num_seeds,
	const sg_rune_v3_link_t *link);

/* Decode and adapt an authenticated immutable snapshot without allocation.
 * Native arrays, v3 scratch arrays, graph workspace, and header_out must be
 * distinct caller-owned storage with capacities at least the inspected counts.
 * No native output or header is changed unless the complete operation reaches
 * DONE.  Link order, seed order, and every mapped scalar bit are preserved.
 * This core does not construct outbound indexes or validate live objective
 * topology; those world-dependent checks belong to the publication layer. */
sg_rune_load_result_t SG_RuneV3Load(const unsigned char *snapshot,
	size_t snapshot_size, const sg_rune_v3_identity_t *expected,
	sg_rune_v3_header_t *header_out,
	rune_seed_t *native_seeds, size_t native_seed_capacity,
	rune_link_t *native_links, size_t native_link_capacity,
	sg_rune_v3_loader_workspace_t *workspace);

#endif /* SG_RUNE_LOADER_H */
