#include "sg_mechanism_capability_internal.h"

#include <stddef.h>

#define SG_MECHANISM_CAPABILITY_SEAL_KEY UINT64_C(0x8f2c6a4d9137be25)

static int AllocationFits(size_t count, size_t element_size)
{
	return element_size != 0U && count <= SIZE_MAX / element_size;
}

static uint64_t DigestBytes(uint64_t digest, const void *data, size_t size)
{
	const unsigned char *bytes = data;
	size_t index;

	for (index = 0U; index < size; index++)
		digest = (digest ^ (uint64_t)bytes[index]) * UINT64_C(1099511628211);
	return digest;
}

uint64_t SG_MechanismCapabilitySetDigest(
	const sg_mechanism_capability_set_t *capabilities)
{
	uint64_t digest = UINT64_C(1469598103934665603);

	if (!capabilities ||
		(capabilities->fact_count != 0U && !capabilities->facts) ||
		(capabilities->topology_edge_count != 0U &&
			!capabilities->topology_edges) ||
		(capabilities->topology_relation_count != 0U &&
			!capabilities->topology_relations) ||
		(capabilities->mechanism_offset_count != 0U &&
			!capabilities->mechanism_offsets) ||
		(capabilities->fact_count != 0U && !capabilities->facts_by_trace) ||
		!AllocationFits((size_t)capabilities->fact_count,
			sizeof(*capabilities->facts)) ||
		!AllocationFits((size_t)capabilities->fact_count,
			sizeof(*capabilities->facts_by_trace)) ||
		!AllocationFits((size_t)capabilities->topology_edge_count,
			sizeof(*capabilities->topology_edges)) ||
		!AllocationFits((size_t)capabilities->topology_relation_count,
			sizeof(*capabilities->topology_relations)) ||
		!AllocationFits((size_t)capabilities->mechanism_offset_count,
			sizeof(*capabilities->mechanism_offsets)))
		return 0U;
	digest = DigestBytes(digest, &capabilities->identity,
		sizeof(capabilities->identity));
	digest = DigestBytes(digest, &capabilities->candidate_verifier_identity,
		sizeof(capabilities->candidate_verifier_identity));
	digest = DigestBytes(digest, &capabilities->trace_verifier_identity,
		sizeof(capabilities->trace_verifier_identity));
	digest = DigestBytes(digest, &capabilities->fact_count,
		sizeof(capabilities->fact_count));
	digest = DigestBytes(digest, &capabilities->topology_edge_count,
		sizeof(capabilities->topology_edge_count));
	digest = DigestBytes(digest, &capabilities->topology_relation_count,
		sizeof(capabilities->topology_relation_count));
	digest = DigestBytes(digest, &capabilities->mechanism_offset_count,
		sizeof(capabilities->mechanism_offset_count));
	digest = DigestBytes(digest, &capabilities->topology_edge_visits,
		sizeof(capabilities->topology_edge_visits));
	if (capabilities->fact_count != 0U)
	{
		digest = DigestBytes(digest, capabilities->facts,
			(size_t)capabilities->fact_count * sizeof(*capabilities->facts));
		digest = DigestBytes(digest, capabilities->facts_by_trace,
			(size_t)capabilities->fact_count *
				sizeof(*capabilities->facts_by_trace));
	}
	if (capabilities->topology_edge_count != 0U)
		digest = DigestBytes(digest, capabilities->topology_edges,
			(size_t)capabilities->topology_edge_count *
				sizeof(*capabilities->topology_edges));
	if (capabilities->topology_relation_count != 0U)
		digest = DigestBytes(digest, capabilities->topology_relations,
			(size_t)capabilities->topology_relation_count *
				sizeof(*capabilities->topology_relations));
	if (capabilities->mechanism_offset_count != 0U)
		digest = DigestBytes(digest, capabilities->mechanism_offsets,
			(size_t)capabilities->mechanism_offset_count *
				sizeof(*capabilities->mechanism_offsets));
	return digest == 0U ? UINT64_C(1) : digest;
}

static uint64_t SealDigest(const sg_mechanism_capability_set_t *capabilities)
{
	uint64_t digest = SG_MechanismCapabilitySetDigest(capabilities);
	const uint64_t key = SG_MECHANISM_CAPABILITY_SEAL_KEY;

	if (digest == 0U)
		return 0U;
	/* Bind the acceptance stamp to the producer-owned allocation itself.  A
	 * caller may copy the public record, but cannot make that copy accepted by
	 * retargeting its self pointer without also knowing this keyed digest. */
	digest = DigestBytes(digest, &capabilities->self,
		sizeof(capabilities->self));
	return DigestBytes(digest, &key, sizeof(key));
}

void SG_MechanismCapabilitySetStamp(
	sg_mechanism_capability_set_t *capabilities)
{
	if (!capabilities)
		return;
	capabilities->seal_magic = SG_MECHANISM_CAPABILITY_SEAL_MAGIC;
	capabilities->seal_magic_inverse = ~SG_MECHANISM_CAPABILITY_SEAL_MAGIC;
	capabilities->self = capabilities;
	capabilities->seal_digest = SealDigest(capabilities);
}

int SG_MechanismCapabilitySetAccepted(
	const sg_mechanism_capability_set_t *capabilities)
{
	return capabilities &&
		capabilities->seal_magic == SG_MECHANISM_CAPABILITY_SEAL_MAGIC &&
		capabilities->seal_magic_inverse ==
			~SG_MECHANISM_CAPABILITY_SEAL_MAGIC &&
		capabilities->self == capabilities && capabilities->seal_digest != 0U &&
		capabilities->seal_digest == SealDigest(capabilities);
}
