/* Focused tests for the RUNE v2 artifact contract. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_rune_v2_acceptance.h"

#define IMAGE_CAPACITY 2048U

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

#define CHECK_DIAGNOSTIC(expected, expression) do { \
	int actual_ = (int)(expression); \
	if (actual_ != (int)(expected)) { \
		fprintf(stderr, "%s:%d: expected %d, got %d: %s\n", \
			__FILE__, __LINE__, (int)(expected), actual_, #expression); \
		failures++; \
	} \
} while (0)

typedef struct section_definition_s
{
	uint16_t type;
	uint32_t count;
} section_definition_t;

static sg_rune_v2_content_id_t Identity(unsigned int seed)
{
	sg_rune_v2_content_id_t identity = { { 0 } };
	unsigned int index;

	for (index = 0; index < SG_RUNE_V2_CONTENT_ID_BYTES; index++)
		identity.bytes[index] = (uint8_t)(seed + index);
	return identity;
}

static uint64_t Align(uint64_t value)
{
	return (value + SG_RUNE_V2_SECTION_ALIGNMENT - 1U) &
		~(SG_RUNE_V2_SECTION_ALIGNMENT - 1U);
}

static unsigned char *SectionEntry(unsigned char *image, uint32_t index)
{
	return image + SG_RUNE_V2_HEADER_BYTES +
		(size_t)index * SG_RUNE_V2_SECTION_ENTRY_BYTES;
}

static void FixChecksums(unsigned char *image, uint32_t image_size)
{
	uint32_t index;

	for (index = 0; index < SG_RUNE_V2_REQUIRED_SECTION_COUNT; index++)
	{
		unsigned char *entry = SectionEntry(image, index);
		uint64_t offset = SG_RuneV2WireGetU64(entry +
			SG_RUNE_V2_SECTION_OFFSET_OFFSET);
		uint64_t bytes = SG_RuneV2WireGetU64(entry +
			SG_RUNE_V2_SECTION_BYTES_OFFSET);

		SG_RuneV2WirePutU32(entry + SG_RUNE_V2_SECTION_CRC_OFFSET,
			SG_RuneV2WireCRC32(image + (size_t)offset, (size_t)bytes));
	}
	SG_RuneV2WirePutU32(image + SG_RUNE_V2_HEADER_PAYLOAD_CRC_OFFSET,
		SG_RuneV2WireCRC32(image + SG_RUNE_V2_HEADER_BYTES,
			image_size - SG_RUNE_V2_HEADER_BYTES));
	SG_RuneV2WirePutU32(image + SG_RUNE_V2_HEADER_CRC_OFFSET,
		SG_RuneV2WireHeaderCRC32(image));
}

static uint32_t BuildImage(unsigned char image[IMAGE_CAPACITY],
	sg_rune_v2_content_id_t bsp_identity,
	sg_rune_v2_content_id_t schema_identity)
{
	static const section_definition_t definitions[] = {
		{ SG_RUNE_V2_SECTION_MODEL, 1U },
		{ SG_RUNE_V2_SECTION_PLANES, 0U },
		{ SG_RUNE_V2_SECTION_PORTAL_VERTICES, 0U },
		{ SG_RUNE_V2_SECTION_PHASES, 0U },
		{ SG_RUNE_V2_SECTION_PHASE_TRANSITIONS, 0U },
		{ SG_RUNE_V2_SECTION_CELLS, 2U },
		{ SG_RUNE_V2_SECTION_PORTALS, 1U },
		{ SG_RUNE_V2_SECTION_SURFACES, 0U },
		{ SG_RUNE_V2_SECTION_AFFORDANCES, 0U },
		{ SG_RUNE_V2_SECTION_KERNELS, 0U },
		{ SG_RUNE_V2_SECTION_LANDMARKS, 0U },
		{ SG_RUNE_V2_SECTION_MECHANISMS, 0U },
		{ SG_RUNE_V2_SECTION_BINDING, 1U }
	};
	uint64_t offset = SG_RUNE_V2_HEADER_BYTES +
		(uint64_t)SG_RUNE_V2_REQUIRED_SECTION_COUNT *
		SG_RUNE_V2_SECTION_ENTRY_BYTES;
	uint32_t index;
	uint32_t image_size;
	unsigned char *record;

	memset(image, 0, IMAGE_CAPACITY);
	SG_RuneV2WirePutU32(image + SG_RUNE_V2_HEADER_MAGIC_OFFSET,
		SG_RUNE_V2_MAGIC);
	SG_RuneV2WirePutU16(image + SG_RUNE_V2_HEADER_VERSION_OFFSET,
		SG_RUNE_V2_VERSION);
	SG_RuneV2WirePutU16(image + SG_RUNE_V2_HEADER_ENDIAN_OFFSET,
		SG_RUNE_V2_ENDIAN_LITTLE);
	SG_RuneV2WirePutU16(image + SG_RUNE_V2_HEADER_BYTES_OFFSET,
		SG_RUNE_V2_HEADER_BYTES);
	SG_RuneV2WirePutU16(image + SG_RUNE_V2_HEADER_ENTRY_BYTES_OFFSET,
		SG_RUNE_V2_SECTION_ENTRY_BYTES);
	SG_RuneV2WirePutU32(image + SG_RUNE_V2_HEADER_SECTION_COUNT_OFFSET,
		SG_RUNE_V2_REQUIRED_SECTION_COUNT);
	SG_RuneV2WirePutU32(image + SG_RUNE_V2_HEADER_SCHEMA_REVISION_OFFSET,
		SG_RUNE_V2_SCHEMA_REVISION);
	SG_RuneV2WirePutU64(image + SG_RUNE_V2_HEADER_GENERATION_OFFSET, 7U);
	for (index = 0; index < SG_RUNE_V2_REQUIRED_SECTION_COUNT; index++)
	{
		unsigned char *entry = SectionEntry(image, index);
		uint32_t record_bytes = SG_RuneV2WireRecordBytes(definitions[index].type);
		uint64_t bytes = (uint64_t)record_bytes * definitions[index].count;

		offset = Align(offset);
		SG_RuneV2WirePutU16(entry + SG_RUNE_V2_SECTION_TYPE_OFFSET,
			definitions[index].type);
		SG_RuneV2WirePutU16(entry + SG_RUNE_V2_SECTION_FLAGS_OFFSET,
			SG_RUNE_V2_SECTION_FLAG_REQUIRED);
		SG_RuneV2WirePutU32(entry + SG_RUNE_V2_SECTION_ELEMENT_BYTES_OFFSET,
			record_bytes);
		SG_RuneV2WirePutU32(entry + SG_RUNE_V2_SECTION_COUNT_OFFSET,
			definitions[index].count);
		SG_RuneV2WirePutU64(entry + SG_RUNE_V2_SECTION_OFFSET_OFFSET, offset);
		SG_RuneV2WirePutU64(entry + SG_RUNE_V2_SECTION_BYTES_OFFSET, bytes);
		offset += bytes;
	}
	image_size = (uint32_t)Align(offset);
	SG_RuneV2WirePutU64(image + SG_RUNE_V2_HEADER_TOTAL_BYTES_OFFSET,
		image_size);

	record = image + (size_t)SG_RuneV2WireGetU64(SectionEntry(image,
		SG_RUNE_V2_SECTION_MODEL - 1U) +
		SG_RUNE_V2_SECTION_OFFSET_OFFSET);
	SG_RuneV2WirePutU16(record + SG_RUNE_V2_MODEL_VERSION_OFFSET,
		SG_RUNE_MODEL_VERSION);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_MODEL_SCHEMA_TAG_OFFSET,
		SG_RUNE_MODEL_SCHEMA_TAG);

	record = image + (size_t)SG_RuneV2WireGetU64(SectionEntry(image,
		SG_RUNE_V2_SECTION_BINDING - 1U) +
		SG_RUNE_V2_SECTION_OFFSET_OFFSET);
	memcpy(record + SG_RUNE_V2_BINDING_BSP_OFFSET, bsp_identity.bytes,
		SG_RUNE_V2_CONTENT_ID_BYTES);
	memcpy(record + SG_RUNE_V2_BINDING_SCHEMA_OFFSET, schema_identity.bytes,
		SG_RUNE_V2_CONTENT_ID_BYTES);
	FixChecksums(image, image_size);
	return image_size;
}

static void TestWireBoundary(void)
{
	unsigned char image[IMAGE_CAPACITY];
	unsigned char bad[IMAGE_CAPACITY];
	sg_rune_v2_wire_view_t view = { 0 };
	sg_rune_v2_content_id_t bsp = Identity(1U);
	sg_rune_v2_content_id_t schema = Identity(40U);
	uint32_t image_size = BuildImage(image, bsp, schema);
	uint64_t portal_offset;

	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_OK,
		SG_RuneV2WireInspect(image, image_size, &view));
	CHECK(view.header.generation == 7U);
	CHECK(view.section[SG_RUNE_V2_SECTION_CELLS - 1U].count == 2U);
	CHECK(SG_RuneV2ContentIdEqual(&view.binding.bsp_identity, &bsp));
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_TRUNCATED,
		SG_RuneV2WireInspect(image, SG_RUNE_V2_HEADER_BYTES - 1U, &view));
	memcpy(bad, image, SG_RUNE_V2_HEADER_BYTES);
	SG_RuneV2WirePutU64(bad + SG_RUNE_V2_HEADER_TOTAL_BYTES_OFFSET,
		SG_RUNE_V2_HEADER_BYTES);
	SG_RuneV2WirePutU32(bad + SG_RUNE_V2_HEADER_CRC_OFFSET,
		SG_RuneV2WireHeaderCRC32(bad));
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_BAD_SIZE,
		SG_RuneV2WireInspect(bad, SG_RUNE_V2_HEADER_BYTES, &view));

	memcpy(bad, image, image_size);
	SG_RuneV2WirePutU16(bad + SG_RUNE_V2_HEADER_VERSION_OFFSET, 3U);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_BAD_VERSION,
		SG_RuneV2WireInspect(bad, image_size, &view));

	memcpy(bad, image, image_size);
	bad[image_size - 1U] ^= 1U;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_BAD_SECTION_CRC,
		SG_RuneV2WireInspect(bad, image_size, &view));

	memcpy(bad, image, image_size);
	portal_offset = SG_RuneV2WireGetU64(SectionEntry(bad,
		SG_RUNE_V2_SECTION_MODEL - 1U) +
		SG_RUNE_V2_SECTION_OFFSET_OFFSET);
	SG_RuneV2WirePutU16(bad + (size_t)portal_offset +
		SG_RUNE_V2_MODEL_VERSION_OFFSET, 0U);
	FixChecksums(bad, image_size);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_BAD_RECORD,
		SG_RuneV2WireInspect(bad, image_size, &view));

	memcpy(bad, image, image_size);
	SG_RuneV2WirePutU32(SectionEntry(bad, SG_RUNE_V2_SECTION_CELLS - 1U) +
		SG_RUNE_V2_SECTION_COUNT_OFFSET, SG_RUNE_MODEL_MAX_CELLS + 1U);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_HOSTILE_COUNT,
		SG_RuneV2WireInspect(bad, image_size, &view));
}

static void TestExactFileIdentityIsOpaque(void)
{
	unsigned char image[IMAGE_CAPACITY];
	sg_rune_v2_wire_view_t view = { 0 };
	sg_rune_v2_artifact_binding_t binding;
	sg_rune_v2_content_id_t exact_file_identity = Identity(80U);
	sg_rune_v2_content_id_t transformed_stream_identity = Identity(81U);
	uint32_t image_size;

	binding = (sg_rune_v2_artifact_binding_t){ 0 };
	binding.generation = 7U;
	binding.bsp_identity = Identity(1U);
	binding.schema_identity = Identity(40U);
	binding.artifact_identity = exact_file_identity;
	image_size = BuildImage(image, binding.bsp_identity, binding.schema_identity);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_WIRE_OK,
		SG_RuneV2WireInspect(image, image_size, &view));
	CHECK(SG_RuneV2ArtifactBindingAccepts(&view, &binding,
		&exact_file_identity));
	CHECK(!SG_RuneV2ArtifactBindingAccepts(&view, &binding,
		&transformed_stream_identity));
}

static void BuildAcceptance(unsigned char image[IMAGE_CAPACITY],
	uint32_t *image_size, sg_rune_v2_acceptance_evidence_t *evidence,
	sg_rune_v2_artifact_binding_t *artifact,
	sg_rune_v2_completeness_proof_t *proof,
	sg_rune_v2_reader_agreement_t *readers)
{
	uint32_t index;

	*artifact = (sg_rune_v2_artifact_binding_t){ 0 };
	artifact->generation = 7U;
	artifact->bsp_identity = Identity(1U);
	artifact->schema_identity = Identity(40U);
	artifact->artifact_identity = Identity(80U);
	*image_size = BuildImage(image, artifact->bsp_identity,
		artifact->schema_identity);
	*proof = (sg_rune_v2_completeness_proof_t){ 0 };
	proof->generation = 7U;
	proof->expected_cells = proof->represented_cells = 2U;
	proof->expected_portals = proof->represented_portals = 1U;
	proof->flags = SG_RUNE_V2_REQUIRED_PROOF_MASK;
	proof->bsp_identity = artifact->bsp_identity;
	proof->schema_identity = artifact->schema_identity;
	proof->artifact_identity = artifact->artifact_identity;
	proof->proof_identity = Identity(120U);
	*readers = (sg_rune_v2_reader_agreement_t){ 0 };
	readers->count = SG_RUNE_V2_REQUIRED_READER_COUNT;
	for (index = 0; index < readers->count; index++)
	{
		readers->result[index].reader = UINT32_C(1) << index;
		readers->result[index].generation = 7U;
		readers->result[index].artifact_identity = artifact->artifact_identity;
		readers->result[index].proof_identity = proof->proof_identity;
	}
	*evidence = (sg_rune_v2_acceptance_evidence_t){ 0 };
	evidence->artifact = artifact;
	evidence->exact_artifact_identity = &artifact->artifact_identity;
	evidence->proof = proof;
	evidence->exact_proof_identity = &proof->proof_identity;
	evidence->readers = readers;
}

static void TestAcceptanceBindings(void)
{
	unsigned char image[IMAGE_CAPACITY];
	uint32_t image_size;
	sg_rune_v2_acceptance_evidence_t evidence;
	sg_rune_v2_artifact_binding_t artifact;
	sg_rune_v2_completeness_proof_t proof;
	sg_rune_v2_reader_agreement_t readers;
	sg_rune_v2_accepted_artifact_t accepted = { 0 };
	sg_rune_v2_content_id_t wrong_identity = Identity(81U);
	sg_rune_v2_content_id_t exact_sidecar_identity = Identity(160U);
	sg_rune_v2_sidecar_binding_t sidecar = { 0 };

	BuildAcceptance(image, &image_size, &evidence, &artifact, &proof, &readers);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_ACCEPT_OK,
		SG_RuneV2Accept(image, image_size, &evidence, &accepted));
	CHECK(accepted.generation == 7U);
	sidecar.kind = 1U;
	sidecar.generation = 7U;
	sidecar.artifact_identity = artifact.artifact_identity;
	sidecar.sidecar_identity = exact_sidecar_identity;
	evidence.sidecars = &sidecar;
	evidence.exact_sidecar_identities = &exact_sidecar_identity;
	evidence.sidecar_count = 1U;
	evidence.sidecar_set_identity = Identity(200U);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_ACCEPT_OK,
		SG_RuneV2Accept(image, image_size, &evidence, &accepted));
	CHECK(accepted.sidecar_count == 1U);
	CHECK(accepted.sidecars[0].kind == 1U);
	CHECK(SG_RuneV2ContentIdEqual(&accepted.sidecars[0].exact_identity,
		&exact_sidecar_identity));
	{
		sg_rune_v2_sidecar_binding_t bindings[2] = { sidecar, sidecar };
		sg_rune_v2_content_id_t identities[2] = {
			Identity(170U), exact_sidecar_identity
		};

		bindings[0].kind = 3U;
		bindings[0].sidecar_identity = identities[0];
		bindings[1].kind = 1U;
		bindings[1].sidecar_identity = identities[1];
		evidence.sidecars = bindings;
		evidence.exact_sidecar_identities = identities;
		evidence.sidecar_count = 2U;
		CHECK_DIAGNOSTIC(SG_RUNE_V2_ACCEPT_OK,
			SG_RuneV2Accept(image, image_size, &evidence, &accepted));
		CHECK(accepted.sidecar_count == 2U);
		CHECK(accepted.sidecars[0].kind == 1U);
		CHECK(accepted.sidecars[1].kind == 3U);
		CHECK(SG_RuneV2ContentIdEqual(
			&accepted.sidecars[0].exact_identity, &identities[1]));
		CHECK(SG_RuneV2ContentIdEqual(
			&accepted.sidecars[1].exact_identity, &identities[0]));
	}
	evidence.sidecars = &sidecar;
	evidence.exact_sidecar_identities = &exact_sidecar_identity;
	evidence.sidecar_count = 1U;
	sidecar.sidecar_identity = (sg_rune_v2_content_id_t){ { 0 } };
	exact_sidecar_identity = (sg_rune_v2_content_id_t){ { 0 } };
	CHECK_DIAGNOSTIC(SG_RUNE_V2_ACCEPT_SIDECAR,
		SG_RuneV2Accept(image, image_size, &evidence, &accepted));
	exact_sidecar_identity = Identity(160U);
	sidecar.sidecar_identity = exact_sidecar_identity;
	exact_sidecar_identity.bytes[0] ^= 1U;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_ACCEPT_SIDECAR,
		SG_RuneV2Accept(image, image_size, &evidence, &accepted));
	exact_sidecar_identity.bytes[0] ^= 1U;
	memset(exact_sidecar_identity.bytes, 0,
		sizeof(exact_sidecar_identity.bytes));
	sidecar.sidecar_identity = exact_sidecar_identity;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_ACCEPT_SIDECAR,
		SG_RuneV2Accept(image, image_size, &evidence, &accepted));
	exact_sidecar_identity = Identity(160U);
	sidecar.sidecar_identity = exact_sidecar_identity;
	sidecar.kind = 0U;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_ACCEPT_SIDECAR,
		SG_RuneV2Accept(image, image_size, &evidence, &accepted));
	sidecar.kind = 1U;
	evidence.sidecars = NULL;
	evidence.exact_sidecar_identities = NULL;
	evidence.sidecar_count = 0U;
	evidence.sidecar_set_identity = (sg_rune_v2_content_id_t){ { 0 } };
	evidence.exact_artifact_identity = &wrong_identity;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_ACCEPT_ARTIFACT_IDENTITY,
		SG_RuneV2Accept(image, image_size, &evidence, &accepted));
	evidence.exact_artifact_identity = &artifact.artifact_identity;
	proof.omitted_cells = 1U;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_ACCEPT_PROOF,
		SG_RuneV2Accept(image, image_size, &evidence, &accepted));
	proof.omitted_cells = 0U;
	readers.result[0].artifact_identity.bytes[0] ^= 1U;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_ACCEPT_READER_AGREEMENT,
		SG_RuneV2Accept(image, image_size, &evidence, &accepted));
}

static sg_rune_v2_generation_t Generation(uint64_t generation,
	unsigned int identity_seed)
{
	sg_rune_v2_generation_t value = { 0 };

	value.generation = generation;
	value.artifact_identity = Identity(identity_seed);
	return value;
}

static void TestPublicationAndReceipts(void)
{
	unsigned char image[IMAGE_CAPACITY];
	uint32_t image_size;
	sg_rune_v2_acceptance_evidence_t evidence;
	sg_rune_v2_artifact_binding_t artifact;
	sg_rune_v2_completeness_proof_t proof;
	sg_rune_v2_reader_agreement_t readers;
	sg_rune_v2_accepted_artifact_t accepted7 = { 0 };
	sg_rune_v2_accepted_artifact_t accepted8 = { 0 };
	sg_rune_v2_publication_t publication;
	sg_rune_v2_publication_event_t published7 = { 0 };
	sg_rune_v2_publication_event_t rolled_back8 = { 0 };
	sg_rune_v2_generation_t generation7 = Generation(7U, 80U);
	sg_rune_v2_generation_t generation6 = Generation(6U, 79U);
	sg_rune_v2_generation_t generation8 = Generation(8U, 90U);
	sg_rune_v2_receipt_t receipt = { 0 };

	BuildAcceptance(image, &image_size, &evidence, &artifact, &proof, &readers);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_ACCEPT_OK,
		SG_RuneV2Accept(image, image_size, &evidence, &accepted7));
	SG_RuneV2PublicationInit(&publication);
	CHECK(SG_RuneV2PublicationStage(&publication, &generation7));
	CHECK(SG_RuneV2PublicationMarkDurable(&publication));
	CHECK(SG_RuneV2PublicationBeginCommit(&publication));
	CHECK(SG_RuneV2PublicationCommit(&publication, &published7));
	CHECK(SG_RuneV2PublicationEventMatches(&publication, &published7));

	/* A stale generation must not displace or disturb the active one. */
	CHECK(!SG_RuneV2PublicationStage(&publication, &generation6));
	CHECK(publication.state == SG_RUNE_V2_PUBLICATION_PUBLISHED);
	CHECK(publication.active.generation == 7U);
	CHECK(publication.pending.generation == 0U);

	CHECK(SG_RuneV2ReceiptIssue(&accepted7, NULL,
		SG_RUNE_V2_RECEIPT_ACCEPTANCE, &receipt));
	CHECK(SG_RuneV2ReceiptAccepts(&receipt, &accepted7, NULL));
	/* Relabeling acceptance bytes is not publication or rollback evidence. */
	receipt.kind = SG_RUNE_V2_RECEIPT_PUBLICATION;
	CHECK(!SG_RuneV2ReceiptAccepts(&receipt, &accepted7, NULL));
	CHECK(!SG_RuneV2ReceiptAccepts(&receipt, &accepted7, &published7));
	receipt.kind = SG_RUNE_V2_RECEIPT_ROLLBACK;
	CHECK(!SG_RuneV2ReceiptAccepts(&receipt, &accepted7, NULL));

	CHECK(SG_RuneV2ReceiptIssue(&accepted7, &published7,
		SG_RUNE_V2_RECEIPT_PUBLICATION, &receipt));
	CHECK(SG_RuneV2ReceiptAccepts(&receipt, &accepted7, &published7));
	CHECK(SG_RuneV2PublicationStage(&publication, &generation8));
	CHECK(SG_RuneV2PublicationBeginRollback(&publication));
	CHECK(SG_RuneV2PublicationRollback(&publication, &rolled_back8));
	CHECK(SG_RuneV2PublicationEventMatches(&publication, &rolled_back8));
	CHECK(publication.active.generation == 7U);
	accepted8 = accepted7;
	accepted8.generation = 8U;
	accepted8.artifact_identity = generation8.artifact_identity;
	CHECK(SG_RuneV2ReceiptIssue(&accepted8, &rolled_back8,
		SG_RUNE_V2_RECEIPT_ROLLBACK, &receipt));
	CHECK(SG_RuneV2ReceiptAccepts(&receipt, &accepted8, &rolled_back8));
}

static void TestInvalidationClosure(void)
{
	uint32_t sidecar = SG_RuneV2InvalidationMask(SG_RUNE_V2_CHANGE_SIDECAR);
	uint32_t source = SG_RuneV2InvalidationMask(SG_RUNE_V2_CHANGE_SOURCE);

	CHECK((sidecar & SG_RUNE_V2_INVALIDATE_SIDECARS) != 0U);
	CHECK((sidecar & SG_RUNE_V2_INVALIDATE_BUNDLE) != 0U);
	CHECK((sidecar & SG_RUNE_V2_INVALIDATE_INSTALLATION) != 0U);
	CHECK((sidecar & SG_RUNE_V2_INVALIDATE_COLD_LOAD) != 0U);
	CHECK((sidecar & SG_RUNE_V2_INVALIDATE_MATCH_EVIDENCE) != 0U);
	CHECK((sidecar & SG_RUNE_V2_INVALIDATE_ROLLBACK) != 0U);
	CHECK((sidecar & SG_RUNE_V2_INVALIDATE_RECEIPTS) != 0U);
	CHECK(SG_RuneV2InvalidationClosure(sidecar) == sidecar);
	CHECK((source & SG_RUNE_V2_INVALIDATE_ARTIFACT) != 0U);
	CHECK((source & SG_RUNE_V2_INVALIDATE_PROOF) != 0U);
	CHECK(SG_RuneV2InvalidationMask(SG_RUNE_V2_CHANGE_OS_FILE_IDENTITY) == 0U);
}

int main(void)
{
	TestWireBoundary();
	TestExactFileIdentityIsOpaque();
	TestAcceptanceBindings();
	TestPublicationAndReceipts();
	TestInvalidationClosure();
	if (failures)
		return 1;
	puts("sg_rune_v2_artifact_contract_test: ok");
	return 0;
}
