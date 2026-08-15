/* sg_rune_wire.h -- allocation-free, explicit little-endian RUNE v3 codec. */
#ifndef SG_RUNE_WIRE_H
#define SG_RUNE_WIRE_H

#include <stddef.h>
#include <stdint.h>

#include "sg_action_contract.generated.h"

#define SG_RUNE_V3_SEED_WATER     UINT16_C(1)
#define SG_RUNE_V3_SEED_TOMBSTONE UINT16_C(2)
#define SG_RUNE_V3_SEED_FLAG_MASK \
	(SG_RUNE_V3_SEED_WATER | SG_RUNE_V3_SEED_TOMBSTONE)

typedef struct sg_rune_v3_identity_s
{
	char map_name[SG_RUNE_V3_MAP_NAME_BYTES];
	uint32_t bsp_checksum;
	uint32_t entity_crc32;
	uint32_t physics_flags;
	float gravity;
	float airaccelerate;
	float maxvelocity;
	uint16_t pmove_substep_ms;
	uint16_t server_frame_ms;
	uint32_t host_physics_id;
} sg_rune_v3_identity_t;

typedef struct sg_rune_v3_header_s
{
	uint32_t magic;
	uint16_t version;
	uint16_t header_bytes;
	uint16_t seed_bytes;
	uint16_t link_bytes;
	uint32_t num_seeds;
	uint32_t num_links;
	uint32_t payload_crc32;
	uint32_t bsp_checksum;
	uint32_t entity_crc32;
	uint32_t action_contract_crc32;
	uint32_t physics_flags;
	float gravity;
	float airaccelerate;
	float maxvelocity;
	uint16_t pmove_substep_ms;
	uint16_t server_frame_ms;
	uint32_t host_physics_id;
	uint32_t header_crc32;
	char map_name[SG_RUNE_V3_MAP_NAME_BYTES];
} sg_rune_v3_header_t;

typedef struct sg_rune_v3_seed_s
{
	float origin[3];
	int16_t area_hint;
	int16_t flags;
} sg_rune_v3_seed_t;

typedef struct sg_rune_v3_link_s
{
	uint32_t source;
	uint32_t destination;
	uint8_t action;
	uint8_t provenance;
	uint8_t min_speed;
	uint8_t heading;
	uint8_t heading_slack;
	uint8_t exit_speed;
	int16_t cost_ms;
	float suffix_anchor[3];
	float mechanism_anchor[3];
	uint16_t sweep_clear_ms;
	uint8_t mode;
	uint8_t reserved;
} sg_rune_v3_link_t;

/* Validation needs one key per link and one mark per seed.  Keeping both in a
 * caller-owned workspace makes duplicate and ownership checks bounded without
 * allocating or changing graph order. */
typedef struct sg_rune_v3_workspace_s
{
	uint64_t *link_keys;
	size_t link_key_capacity;
	uint8_t *source_marks;
	size_t source_mark_capacity;
} sg_rune_v3_workspace_t;

/* Exact encoded size after bounded count validation. */
rune_wire_diagnostic_t SG_RuneV3FileSize(uint32_t num_seeds,
	uint32_t num_links, size_t *size_out);

/* Primitive explicit-LE codecs.  These never read or write native structs as
 * wire images.  Header encoding computes and stores its canonical CRC. */
rune_wire_diagnostic_t SG_RuneV3EncodeHeader(
	const sg_rune_v3_header_t *header, unsigned char *encoded,
	size_t encoded_size);
rune_wire_diagnostic_t SG_RuneV3DecodeHeader(const unsigned char *encoded,
	size_t encoded_size, sg_rune_v3_header_t *header_out);
rune_wire_diagnostic_t SG_RuneV3EncodeSeed(const sg_rune_v3_seed_t *seed,
	unsigned char *encoded, size_t encoded_size);
rune_wire_diagnostic_t SG_RuneV3DecodeSeed(const unsigned char *encoded,
	size_t encoded_size, sg_rune_v3_seed_t *seed_out);
rune_wire_diagnostic_t SG_RuneV3EncodeLink(const sg_rune_v3_link_t *link,
	unsigned char *encoded, size_t encoded_size);
rune_wire_diagnostic_t SG_RuneV3DecodeLink(const unsigned char *encoded,
	size_t encoded_size, sg_rune_v3_link_t *link_out);

/* Header CRC canonicalizes bytes 60..63 to zero.  Payload helpers intentionally
 * accept fragments so generation and loading can hash records in wire order
 * without assembling a second payload. */
rune_wire_diagnostic_t SG_RuneV3HeaderCRC32(const unsigned char *encoded,
	size_t encoded_size, uint32_t *crc_out);
rune_wire_diagnostic_t SG_RuneV3PayloadCRCInit(uint32_t *state_out);
rune_wire_diagnostic_t SG_RuneV3PayloadCRCUpdate(uint32_t *state,
	const void *fragment, size_t fragment_size);
rune_wire_diagnostic_t SG_RuneV3PayloadCRCFinish(uint32_t state,
	uint32_t *crc_out);

/* Structural validation admits every v3 wire-known action, including a known
 * action whose live controller is not yet enabled.  Execution authorization is
 * a separate query and must never be inferred from successful decoding. */
int SG_RuneV3ActionWireKnown(uint8_t action);
int SG_RuneV3ActionRuntimeSupported(uint8_t action);
rune_wire_diagnostic_t SG_RuneV3ValidateGraph(
	const sg_rune_v3_seed_t *seeds, uint32_t num_seeds,
	const sg_rune_v3_link_t *links, uint32_t num_links,
	sg_rune_v3_workspace_t *workspace);

/* Exact, case-sensitive identity comparison.  A NULL expected identity means
 * structural inspection only and does not authenticate the active level. */
rune_wire_diagnostic_t SG_RuneV3MatchIdentity(
	const sg_rune_v3_header_t *header,
	const sg_rune_v3_identity_t *expected_identity);

/* Whole-file convenience codecs.  All storage and validation workspace remain
 * caller-owned; no function in this module allocates memory.  Decode outputs
 * are publishable only when the returned diagnostic is RLW_OK. */
rune_wire_diagnostic_t SG_RuneV3Encode(
	const sg_rune_v3_identity_t *identity,
	const sg_rune_v3_seed_t *seeds, uint32_t num_seeds,
	const sg_rune_v3_link_t *links, uint32_t num_links,
	sg_rune_v3_workspace_t *workspace, unsigned char *encoded,
	size_t encoded_capacity, size_t *encoded_size_out);
rune_wire_diagnostic_t SG_RuneV3Decode(
	const unsigned char *encoded, size_t encoded_size,
	const sg_rune_v3_identity_t *expected_identity,
	sg_rune_v3_header_t *header_out,
	sg_rune_v3_seed_t *seeds, size_t seed_capacity,
	sg_rune_v3_link_t *links, size_t link_capacity,
	sg_rune_v3_workspace_t *workspace);

#endif /* SG_RUNE_WIRE_H */
