#include "sg_rune_v2_artifact_publication_internal.h"

#include <string.h>

static const unsigned char manifest_magic[8] = {
	'R', 'U', 'N', 'E', 'P', 'U', 'B', '2'
};

static uint32_t ManifestCRC32(const unsigned char *bytes, size_t size)
{
	uint32_t crc = UINT32_MAX;
	size_t index;

	for (index = 0U; index < size; index++)
	{
		uint32_t value = index >= 196U && index < 200U ? 0U : bytes[index];
		unsigned int bit;

		crc ^= value;
		for (bit = 0U; bit < 8U; bit++)
			crc = (crc >> 1) ^ ((crc & 1U) != 0U
				? UINT32_C(0xedb88320) : 0U);
	}
	return ~crc;
}

static int ContentIdsValid(const sg_rune_v2_accepted_artifact_t *accepted)
{
	return SG_RuneV2ContentIdValid(&accepted->bsp_identity) &&
		SG_RuneV2ContentIdValid(&accepted->schema_identity) &&
		SG_RuneV2ContentIdValid(&accepted->artifact_identity) &&
		SG_RuneV2ContentIdValid(&accepted->proof_identity);
}

static const sg_rune_v2_publication_sidecar_file_t *SidecarByKind(
	const sg_rune_v2_publication_candidate_t *candidate, uint32_t kind);

int SG_RuneV2PublicationCandidateValid(
	const sg_rune_v2_publication_candidate_t *candidate)
{
	const sg_rune_v2_accepted_artifact_t *accepted;
	uint32_t mask = 0U;
	uint32_t index;

	if (!candidate || !candidate->accepted || !candidate->artifact_bytes ||
		candidate->artifact_size == 0U || !candidate->verify_staged_file ||
		candidate->sidecar_count > SG_RUNE_V2_MAX_SIDECARS ||
		(candidate->sidecar_count != 0U && !candidate->sidecars))
		return 0;
	accepted = candidate->accepted;
	if (accepted->generation == 0U ||
		accepted->reader_mask != SG_RUNE_V2_REQUIRED_READER_MASK ||
		!ContentIdsValid(accepted) ||
		(accepted->sidecar_mask & UINT32_C(0x80000000)) != 0U ||
		accepted->sidecar_count != candidate->sidecar_count ||
		(candidate->sidecar_count == 0U) != (accepted->sidecar_mask == 0U) ||
		(candidate->sidecar_count == 0U) !=
			!SG_RuneV2ContentIdValid(&accepted->sidecar_set_identity))
		return 0;
	for (index = 0U; index < candidate->sidecar_count; index++)
	{
		const sg_rune_v2_publication_sidecar_file_t *sidecar =
			&candidate->sidecars[index];
		uint32_t bit;

		if (sidecar->kind == 0U || sidecar->kind > SG_RUNE_V2_MAX_SIDECARS ||
			(sidecar->size != 0U && !sidecar->bytes) ||
			!SG_RuneV2ContentIdValid(&sidecar->exact_identity))
			return 0;
		bit = UINT32_C(1) << (sidecar->kind - 1U);
		if ((mask & bit) != 0U)
			return 0;
		mask |= bit;
	}
	if (mask != accepted->sidecar_mask)
		return 0;
	mask = 0U;
	for (index = 0U; index < accepted->sidecar_count; index++)
	{
		const sg_rune_v2_accepted_sidecar_t *accepted_sidecar =
			&accepted->sidecars[index];
		const sg_rune_v2_publication_sidecar_file_t *candidate_sidecar;
		uint32_t bit;

		if (accepted_sidecar->kind == 0U ||
			accepted_sidecar->kind > SG_RUNE_V2_MAX_SIDECARS ||
			(index != 0U && accepted_sidecar->kind <=
				accepted->sidecars[index - 1U].kind) ||
			!SG_RuneV2ContentIdValid(&accepted_sidecar->exact_identity))
			return 0;
		candidate_sidecar = SidecarByKind(candidate, accepted_sidecar->kind);
		if (!candidate_sidecar || !SG_RuneV2ContentIdEqual(
			&candidate_sidecar->exact_identity,
			&accepted_sidecar->exact_identity))
			return 0;
		bit = UINT32_C(1) << (accepted_sidecar->kind - 1U);
		mask |= bit;
	}
	for (; index < SG_RUNE_V2_MAX_SIDECARS; index++)
		if (accepted->sidecars[index].kind != 0U ||
			SG_RuneV2ContentIdValid(&accepted->sidecars[index].exact_identity))
			return 0;
	return mask == accepted->sidecar_mask;
}

static const sg_rune_v2_publication_sidecar_file_t *SidecarByKind(
	const sg_rune_v2_publication_candidate_t *candidate, uint32_t kind)
{
	uint32_t index;

	for (index = 0U; index < candidate->sidecar_count; index++)
		if (candidate->sidecars[index].kind == kind)
			return &candidate->sidecars[index];
	return NULL;
}

size_t SG_RuneV2PublicationManifestEncode(
	const sg_rune_v2_publication_candidate_t *candidate,
	unsigned char output[SG_RUNE_V2_PUBLICATION_MANIFEST_MAX_BYTES])
{
	const sg_rune_v2_accepted_artifact_t *accepted = candidate->accepted;
	size_t total = SG_RUNE_V2_PUBLICATION_MANIFEST_HEADER_BYTES +
		(size_t)candidate->sidecar_count *
		SG_RUNE_V2_PUBLICATION_MANIFEST_ENTRY_BYTES;
	size_t offset = SG_RUNE_V2_PUBLICATION_MANIFEST_HEADER_BYTES;
	uint32_t kind;

	memset(output, 0, total);
	memcpy(output, manifest_magic, sizeof(manifest_magic));
	SG_RuneV2WirePutU32(output + 8U, 1U);
	SG_RuneV2WirePutU32(output + 12U, (uint32_t)total);
	SG_RuneV2WirePutU64(output + 16U, accepted->generation);
	SG_RuneV2WirePutU32(output + 24U, accepted->reader_mask);
	SG_RuneV2WirePutU32(output + 28U, accepted->sidecar_mask);
	memcpy(output + 32U, accepted->bsp_identity.bytes,
		SG_RUNE_V2_CONTENT_ID_BYTES);
	memcpy(output + 64U, accepted->schema_identity.bytes,
		SG_RUNE_V2_CONTENT_ID_BYTES);
	memcpy(output + 96U, accepted->artifact_identity.bytes,
		SG_RUNE_V2_CONTENT_ID_BYTES);
	memcpy(output + 128U, accepted->proof_identity.bytes,
		SG_RUNE_V2_CONTENT_ID_BYTES);
	memcpy(output + 160U, accepted->sidecar_set_identity.bytes,
		SG_RUNE_V2_CONTENT_ID_BYTES);
	SG_RuneV2WirePutU32(output + 192U, candidate->sidecar_count);
	for (kind = 0U; kind < accepted->sidecar_count; kind++)
	{
		const sg_rune_v2_accepted_sidecar_t *sidecar =
			&accepted->sidecars[kind];

		SG_RuneV2WirePutU32(output + offset, sidecar->kind);
		memcpy(output + offset + 8U, sidecar->exact_identity.bytes,
			SG_RUNE_V2_CONTENT_ID_BYTES);
		offset += SG_RUNE_V2_PUBLICATION_MANIFEST_ENTRY_BYTES;
	}
	SG_RuneV2WirePutU32(output + 196U, ManifestCRC32(output, total));
	return total;
}

int SG_RuneV2PublicationManifestDecode(const unsigned char *bytes, size_t size,
	sg_rune_v2_active_generation_t *active)
{
	uint32_t count;
	uint32_t mask = 0U;
	uint32_t index;

	if (size < SG_RUNE_V2_PUBLICATION_MANIFEST_HEADER_BYTES ||
		memcmp(bytes, manifest_magic, sizeof(manifest_magic)) != 0 ||
		SG_RuneV2WireGetU32(bytes + 8U) != 1U ||
		SG_RuneV2WireGetU32(bytes + 12U) != size ||
		SG_RuneV2WireGetU32(bytes + 196U) != ManifestCRC32(bytes, size))
		return 0;
	count = SG_RuneV2WireGetU32(bytes + 192U);
	if (count > SG_RUNE_V2_MAX_SIDECARS ||
		size != SG_RUNE_V2_PUBLICATION_MANIFEST_HEADER_BYTES +
			(size_t)count * SG_RUNE_V2_PUBLICATION_MANIFEST_ENTRY_BYTES)
		return 0;
	memset(active, 0, sizeof(*active));
	active->accepted.generation = SG_RuneV2WireGetU64(bytes + 16U);
	active->accepted.reader_mask = SG_RuneV2WireGetU32(bytes + 24U);
	active->accepted.sidecar_mask = SG_RuneV2WireGetU32(bytes + 28U);
	memcpy(active->accepted.bsp_identity.bytes, bytes + 32U,
		SG_RUNE_V2_CONTENT_ID_BYTES);
	memcpy(active->accepted.schema_identity.bytes, bytes + 64U,
		SG_RUNE_V2_CONTENT_ID_BYTES);
	memcpy(active->accepted.artifact_identity.bytes, bytes + 96U,
		SG_RUNE_V2_CONTENT_ID_BYTES);
	memcpy(active->accepted.proof_identity.bytes, bytes + 128U,
		SG_RUNE_V2_CONTENT_ID_BYTES);
	memcpy(active->accepted.sidecar_set_identity.bytes, bytes + 160U,
		SG_RUNE_V2_CONTENT_ID_BYTES);
	active->accepted.sidecar_count = count;
	if (active->accepted.generation == 0U ||
		active->accepted.reader_mask != SG_RUNE_V2_REQUIRED_READER_MASK ||
		!ContentIdsValid(&active->accepted) ||
		(count == 0U) != (active->accepted.sidecar_mask == 0U) ||
		(count == 0U) !=
			!SG_RuneV2ContentIdValid(&active->accepted.sidecar_set_identity))
		return 0;
	for (index = 0U; index < count; index++)
	{
		size_t offset = SG_RUNE_V2_PUBLICATION_MANIFEST_HEADER_BYTES +
			(size_t)index * SG_RUNE_V2_PUBLICATION_MANIFEST_ENTRY_BYTES;
		uint32_t kind = SG_RuneV2WireGetU32(bytes + offset);
		uint32_t bit;

		if (kind == 0U || kind > SG_RUNE_V2_MAX_SIDECARS ||
			SG_RuneV2WireGetU32(bytes + offset + 4U) != 0U)
			return 0;
		bit = UINT32_C(1) << (kind - 1U);
		if ((mask & bit) != 0U || (index != 0U &&
			kind <= active->accepted.sidecars[index - 1U].kind))
			return 0;
		active->accepted.sidecars[index].kind = kind;
		memcpy(active->accepted.sidecars[index].exact_identity.bytes,
			bytes + offset + 8U, SG_RUNE_V2_CONTENT_ID_BYTES);
		if (!SG_RuneV2ContentIdValid(
			&active->accepted.sidecars[index].exact_identity))
			return 0;
		mask |= bit;
	}
	return mask == active->accepted.sidecar_mask;
}
