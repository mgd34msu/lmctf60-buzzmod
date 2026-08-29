/* Atomic file-backed ownership of learned parameters and scope receipts. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "sg_human_trace_learning_store.h"
#include "sg_rune_v2_content_identity.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#define LEARNING_STORE_VERSION UINT16_C(1)
#define LEARNING_STORE_HEADER_BYTES 104U
#define LEARNING_STORE_RECEIPT_BYTES 52U
#define LEARNING_STORE_DIGEST_OFFSET 72U

static void LearningStorePutU16(unsigned char *bytes, uint16_t value)
{
	bytes[0] = (unsigned char)(value & UINT16_C(0xff));
	bytes[1] = (unsigned char)((value >> 8) & UINT16_C(0xff));
}

static void LearningStorePutU32(unsigned char *bytes, uint32_t value)
{
	uint32_t index;

	for (index = 0U; index < 4U; index++)
		bytes[index] = (unsigned char)((value >> (index * 8U)) &
			UINT32_C(0xff));
}

static void LearningStorePutU64(unsigned char *bytes, uint64_t value)
{
	uint32_t index;

	for (index = 0U; index < 8U; index++)
		bytes[index] = (unsigned char)((value >> (index * 8U)) &
			UINT64_C(0xff));
}

static uint16_t LearningStoreGetU16(const unsigned char *bytes)
{
	return (uint16_t)((uint16_t)bytes[0] |
		(uint16_t)((uint16_t)bytes[1] << 8));
}

static uint32_t LearningStoreGetU32(const unsigned char *bytes)
{
	uint32_t value = 0U;
	uint32_t index;

	for (index = 0U; index < 4U; index++)
		value |= (uint32_t)bytes[index] << (index * 8U);
	return value;
}

static uint64_t LearningStoreGetU64(const unsigned char *bytes)
{
	uint64_t value = 0U;
	uint32_t index;

	for (index = 0U; index < 8U; index++)
		value |= (uint64_t)bytes[index] << (index * 8U);
	return value;
}

static void LearningStorePutIdentity(unsigned char *bytes,
	const sg_human_trace_learning_identity_t *identity)
{
	LearningStorePutU64(bytes, identity->rune_identity);
	LearningStorePutU64(bytes + 8U, identity->topology_revision);
	LearningStorePutU64(bytes + 16U, identity->bsp_identity);
	LearningStorePutU64(bytes + 24U, identity->physics_identity);
}

static void LearningStoreGetIdentity(const unsigned char *bytes,
	sg_human_trace_learning_identity_t *identity)
{
	identity->rune_identity = LearningStoreGetU64(bytes);
	identity->topology_revision = LearningStoreGetU64(bytes + 8U);
	identity->bsp_identity = LearningStoreGetU64(bytes + 16U);
	identity->physics_identity = LearningStoreGetU64(bytes + 24U);
}

static uint64_t LearningStoreScopeHash(
	const sg_human_trace_learning_trace_scope_t *scope)
{
	uint64_t hash = UINT64_C(1469598103934665603);
	size_t index;

	for (index = 0U; index < sizeof(scope->trace.terminal_sha256.bytes); index++)
	{
		hash ^= scope->trace.terminal_sha256.bytes[index];
		hash *= UINT64_C(1099511628211);
	}
	hash ^= scope->client_id;
	hash *= UINT64_C(1099511628211);
	hash ^= scope->spawn_generation;
	hash *= UINT64_C(1099511628211);
	return hash;
}

static int LearningStoreReceiptMatches(
	const sg_human_trace_learning_scope_receipt_t *receipt,
	const sg_human_trace_learning_trace_scope_t *scope)
{
	return receipt && scope && receipt->reserved == 0U &&
		receipt->client_id == scope->client_id &&
		receipt->spawn_generation == scope->spawn_generation &&
		memcmp(receipt->terminal_sha256.bytes,
			scope->trace.terminal_sha256.bytes,
			sizeof(receipt->terminal_sha256.bytes)) == 0;
}

static int LearningStoreReceiptValid(
	const sg_human_trace_learning_scope_receipt_t *receipt,
	uint64_t state_generation)
{
	return receipt && receipt->reserved == 0U && receipt->client_id != 0U &&
		receipt->spawn_generation != 0U && receipt->state_generation != 0U &&
		receipt->state_generation <= state_generation &&
		SG_HumanTraceLearningTraceIdValid(&receipt->terminal_sha256);
}

static int LearningStoreReceiptIndexBuild(
	sg_human_trace_learning_store_t *store)
{
	size_t capacity = 8U;
	size_t index;

	if (!store)
		return 0;
	if (store->receipt_count > SIZE_MAX / 2U)
		return 0;
	while (capacity < store->receipt_count * 2U)
	{
		if (capacity > SIZE_MAX / 2U)
			return 0;
		capacity *= 2U;
	}
	free(store->receipt_slots);
	store->receipt_slots = calloc(capacity, sizeof(*store->receipt_slots));
	if (!store->receipt_slots)
	{
		store->receipt_slot_capacity = 0U;
		return 0;
	}
	store->receipt_slot_capacity = capacity;
	for (index = 0U; index < store->receipt_count; index++)
	{
		sg_human_trace_learning_trace_scope_t scope;
		size_t slot;

		if (!LearningStoreReceiptValid(&store->receipts[index],
			store->generation))
			return 0;
		memset(&scope, 0, sizeof(scope));
		scope.trace.terminal_sha256 = store->receipts[index].terminal_sha256;
		scope.client_id = store->receipts[index].client_id;
		scope.spawn_generation = store->receipts[index].spawn_generation;
		slot = (size_t)LearningStoreScopeHash(&scope) & (capacity - 1U);
		while (store->receipt_slots[slot] != 0U)
		{
			size_t existing = store->receipt_slots[slot] - 1U;

			if (existing >= index || LearningStoreReceiptMatches(
				&store->receipts[existing], &scope))
				return 0;
			slot = (slot + 1U) & (capacity - 1U);
		}
		store->receipt_slots[slot] = index + 1U;
	}
	return 1;
}

static int LearningStoreDigest(unsigned char *bytes, size_t size,
	uint8_t digest_out[SG_HUMAN_TRACE_LEARNING_TRACE_SHA256_BYTES])
{
	sg_rune_v2_content_id_t digest;

	if (!bytes || size < LEARNING_STORE_HEADER_BYTES || !digest_out)
		return 0;
	memset(bytes + LEARNING_STORE_DIGEST_OFFSET, 0,
		SG_HUMAN_TRACE_LEARNING_TRACE_SHA256_BYTES);
	if (!SG_RuneV2ContentIdentitySHA256(bytes, size, &digest))
		return 0;
	memcpy(digest_out, digest.bytes, sizeof(digest.bytes));
	return 1;
}

static int LearningStoreSerialize(
	const sg_human_trace_learning_store_t *store,
	const sg_human_trace_learning_parameters_t *parameters,
	uint64_t next_transaction_id,
	const sg_human_trace_learning_scope_receipt_t *added,
	unsigned char **bytes_out, size_t *size_out)
{
	unsigned char *bytes;
	uint8_t digest[SG_HUMAN_TRACE_LEARNING_TRACE_SHA256_BYTES];
	size_t costs_bytes, receipt_count, receipts_bytes, size, index, offset;

	if (bytes_out)
		*bytes_out = NULL;
	if (size_out)
		*size_out = 0U;
	if (!store || !parameters || !added || !bytes_out || !size_out ||
		!store->initialized || !SG_HumanTraceLearningParametersValid(parameters) ||
		!SG_HumanTraceLearningIdentityEqual(&store->identity,
			&parameters->domain.identity) ||
		parameters->domain.kernel_key_count != store->effective_cost_count ||
		(store->receipt_count != 0U && !store->receipts) ||
		next_transaction_id == 0U || store->receipt_count == SIZE_MAX)
		return 0;
	receipt_count = store->receipt_count + 1U;
	if (store->effective_cost_count > SIZE_MAX / 8U ||
		receipt_count > SIZE_MAX / LEARNING_STORE_RECEIPT_BYTES)
		return 0;
	costs_bytes = store->effective_cost_count * 8U;
	receipts_bytes = receipt_count * LEARNING_STORE_RECEIPT_BYTES;
	if (LEARNING_STORE_HEADER_BYTES > SIZE_MAX - costs_bytes ||
		LEARNING_STORE_HEADER_BYTES + costs_bytes > SIZE_MAX - receipts_bytes)
		return 0;
	size = LEARNING_STORE_HEADER_BYTES + costs_bytes + receipts_bytes;
	bytes = calloc(1U, size);
	if (!bytes)
		return 0;
	memcpy(bytes, "SGLS", 4U);
	LearningStorePutU16(bytes + 4U, LEARNING_STORE_VERSION);
	LearningStorePutU16(bytes + 6U, LEARNING_STORE_HEADER_BYTES);
	LearningStorePutIdentity(bytes + 8U, &store->identity);
	LearningStorePutU64(bytes + 40U, parameters->generation);
	LearningStorePutU64(bytes + 48U, next_transaction_id);
	LearningStorePutU64(bytes + 56U, (uint64_t)store->effective_cost_count);
	LearningStorePutU64(bytes + 64U, (uint64_t)receipt_count);
	offset = LEARNING_STORE_HEADER_BYTES;
	for (index = 0U; index < store->effective_cost_count; index++)
	{
		LearningStorePutU64(bytes + offset, parameters->effective_cost_us[index]);
		offset += 8U;
	}
	for (index = 0U; index < receipt_count; index++)
	{
		const sg_human_trace_learning_scope_receipt_t *receipt =
			index < store->receipt_count ? &store->receipts[index] : added;

		memcpy(bytes + offset, receipt->terminal_sha256.bytes,
			SG_HUMAN_TRACE_LEARNING_TRACE_SHA256_BYTES);
		LearningStorePutU32(bytes + offset + 32U, receipt->client_id);
		LearningStorePutU64(bytes + offset + 36U,
			receipt->spawn_generation);
		LearningStorePutU64(bytes + offset + 44U,
			receipt->state_generation);
		offset += LEARNING_STORE_RECEIPT_BYTES;
	}
	if (offset != size)
	{
		free(bytes);
		return 0;
	}
	if (!LearningStoreDigest(bytes, size, digest))
	{
		free(bytes);
		return 0;
	}
	memcpy(bytes + LEARNING_STORE_DIGEST_OFFSET, digest, sizeof(digest));
	*bytes_out = bytes;
	*size_out = size;
	return 1;
}

static int LearningStoreSyncParent(const char *path)
{
#ifdef _WIN32
	(void)path;
	return 1;
#else
	char directory[SG_HUMAN_TRACE_LEARNING_STATE_PATH_BYTES];
	const char *separator;
	int descriptor;
	size_t bytes;
	int status;

	if (!path || !(separator = strrchr(path, '/')))
		return 0;
	bytes = (size_t)(separator - path);
	if (bytes == 0U || bytes >= sizeof(directory))
		return 0;
	memcpy(directory, path, bytes);
	directory[bytes] = '\0';
	descriptor = open(directory, O_RDONLY);
	if (descriptor < 0)
		return 0;
	status = fsync(descriptor) == 0 && close(descriptor) == 0;
	return status != 0;
#endif
}

static int LearningStorePublish(sg_human_trace_learning_store_t *store,
	const unsigned char *bytes, size_t size)
{
	char temporary[SG_HUMAN_TRACE_LEARNING_STATE_PATH_BYTES];
	FILE *file;
	int written;
	int durable;

	if (!store || !bytes || size == 0U ||
		store->temporary_nonce == UINT64_MAX)
		return 0;
	for (;;)
	{
		store->temporary_nonce++;
		written = snprintf(temporary, sizeof(temporary), "%s.tmp-%" PRIu64,
			store->path, store->temporary_nonce);
		if (written < 0 || (size_t)written >= sizeof(temporary))
			return 0;
		file = fopen(temporary, "wbx");
		if (file)
			break;
		if (errno != EEXIST || store->temporary_nonce == UINT64_MAX)
			return 0;
	}
	durable = fwrite(bytes, 1U, size, file) == size && fflush(file) == 0;
#ifdef _WIN32
	if (durable)
		durable = _commit(_fileno(file)) == 0;
#else
	if (durable)
		durable = fsync(fileno(file)) == 0;
#endif
	if (fclose(file) != 0)
		durable = 0;
	if (!durable)
	{
		remove(temporary);
		return 0;
	}
#ifdef _WIN32
	if (!MoveFileExA(temporary, store->path,
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
#else
	if (rename(temporary, store->path) != 0)
#endif
	{
		remove(temporary);
		return 0;
	}
	return LearningStoreSyncParent(store->path);
}

static int LearningStoreLoad(sg_human_trace_learning_store_t *store,
	const sg_human_trace_learning_parameters_t *parameters, int *found_out)
{
	sg_human_trace_learning_identity_t identity;
	unsigned char *bytes = NULL;
	uint8_t digest[SG_HUMAN_TRACE_LEARNING_TRACE_SHA256_BYTES];
	uint8_t stored_digest[SG_HUMAN_TRACE_LEARNING_TRACE_SHA256_BYTES];
	FILE *file;
	long length;
	uint64_t generation, next_transaction_id, effective_cost_count, receipt_count;
	size_t expected, costs_bytes, receipts_bytes, index, offset;
	int result = 0;

	*found_out = 0;
	file = fopen(store->path, "rb");
	if (!file)
		return errno == ENOENT;
	*found_out = 1;
	if (fseek(file, 0L, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
		fseek(file, 0L, SEEK_SET) != 0 ||
		(size_t)length < LEARNING_STORE_HEADER_BYTES)
		goto finish;
	bytes = malloc((size_t)length);
	if (!bytes || fread(bytes, 1U, (size_t)length, file) != (size_t)length ||
		ferror(file))
		goto finish;
	LearningStoreGetIdentity(bytes + 8U, &identity);
	generation = LearningStoreGetU64(bytes + 40U);
	next_transaction_id = LearningStoreGetU64(bytes + 48U);
	effective_cost_count = LearningStoreGetU64(bytes + 56U);
	receipt_count = LearningStoreGetU64(bytes + 64U);
	if (memcmp(bytes, "SGLS", 4U) != 0 ||
		LearningStoreGetU16(bytes + 4U) != LEARNING_STORE_VERSION ||
		LearningStoreGetU16(bytes + 6U) != LEARNING_STORE_HEADER_BYTES ||
		!SG_HumanTraceLearningIdentityEqual(&identity,
			&parameters->domain.identity) || generation == 0U ||
		next_transaction_id == 0U ||
		effective_cost_count != (uint64_t)parameters->domain.kernel_key_count ||
		effective_cost_count > (uint64_t)(SIZE_MAX / 8U) ||
		receipt_count > (uint64_t)(SIZE_MAX / LEARNING_STORE_RECEIPT_BYTES) ||
		receipt_count > (uint64_t)(SIZE_MAX / sizeof(*store->receipts)))
		goto finish;
	costs_bytes = (size_t)effective_cost_count * 8U;
	receipts_bytes = (size_t)receipt_count * LEARNING_STORE_RECEIPT_BYTES;
	if (LEARNING_STORE_HEADER_BYTES > SIZE_MAX - costs_bytes ||
		LEARNING_STORE_HEADER_BYTES + costs_bytes > SIZE_MAX - receipts_bytes)
		goto finish;
	expected = LEARNING_STORE_HEADER_BYTES + costs_bytes + receipts_bytes;
	if ((size_t)length != expected)
		goto finish;
	memcpy(stored_digest, bytes + LEARNING_STORE_DIGEST_OFFSET,
		sizeof(stored_digest));
	if (!LearningStoreDigest(bytes, expected, digest) ||
		memcmp(stored_digest, digest, sizeof(digest)) != 0)
		goto finish;
	store->effective_cost_us = malloc((size_t)effective_cost_count ?
		(size_t)effective_cost_count * sizeof(*store->effective_cost_us) : 1U);
	store->receipts = malloc((size_t)receipt_count ?
		(size_t)receipt_count * sizeof(*store->receipts) : 1U);
	if (!store->effective_cost_us || !store->receipts)
		goto finish;
	offset = LEARNING_STORE_HEADER_BYTES;
	for (index = 0U; index < (size_t)effective_cost_count; index++)
	{
		store->effective_cost_us[index] = LearningStoreGetU64(bytes + offset);
		offset += 8U;
	}
	for (index = 0U; index < (size_t)receipt_count; index++)
	{
		sg_human_trace_learning_scope_receipt_t *receipt =
			&store->receipts[index];

		memset(receipt, 0, sizeof(*receipt));
		memcpy(receipt->terminal_sha256.bytes, bytes + offset,
			SG_HUMAN_TRACE_LEARNING_TRACE_SHA256_BYTES);
		receipt->client_id = LearningStoreGetU32(bytes + offset + 32U);
		receipt->spawn_generation = LearningStoreGetU64(bytes + offset + 36U);
		receipt->state_generation = LearningStoreGetU64(bytes + offset + 44U);
		offset += LEARNING_STORE_RECEIPT_BYTES;
	}
	if (offset != expected)
		goto finish;
	store->effective_cost_count = (size_t)effective_cost_count;
	store->receipt_count = (size_t)receipt_count;
	store->receipt_capacity = store->receipt_count;
	store->identity = identity;
	store->generation = generation;
	store->next_transaction_id = next_transaction_id;
	store->initialized = 1U;
	if (!LearningStoreReceiptIndexBuild(store))
		goto finish;
	result = 1;
finish:
	free(bytes);
	if (fclose(file) != 0)
		result = 0;
	return result;
}

int SG_HumanTraceLearningStoreOpen(sg_human_trace_learning_store_t *store,
	const char *directory, const sg_level_identity_t *level_identity,
	const sg_human_trace_learning_parameters_t *parameters,
	uint64_t next_transaction_id)
{
	int found;
	int written;
	size_t costs_bytes;

	if (!store || !directory || !level_identity ||
		!SG_HumanTraceLearningParametersValid(parameters) ||
		parameters->domain.kernel_key_count > SIZE_MAX / sizeof(uint64_t) ||
		next_transaction_id == 0U)
		return 0;
	memset(store, 0, sizeof(*store));
	written = snprintf(store->path, sizeof(store->path),
		"%s/humantrace-learning-%s-%08" PRIx32 "-%08" PRIx32
		"-%08" PRIx32 "-%016" PRIx64 "-%016" PRIx64
		"-%016" PRIx64 "-%016" PRIx64 ".state",
		directory, level_identity->mapname, level_identity->bsp_checksum,
		level_identity->entity_crc32, level_identity->host_physics_id,
		parameters->domain.identity.rune_identity,
		parameters->domain.identity.topology_revision,
		parameters->domain.identity.bsp_identity,
		parameters->domain.identity.physics_identity);
	if (written < 0 || (size_t)written >= sizeof(store->path) ||
		!LearningStoreLoad(store, parameters, &found))
	{
		SG_HumanTraceLearningStoreClose(store);
		return 0;
	}
	if (found)
		return 1;
	store->identity = parameters->domain.identity;
	store->generation = parameters->generation;
	store->next_transaction_id = next_transaction_id;
	store->effective_cost_count = parameters->domain.kernel_key_count;
	costs_bytes = store->effective_cost_count * sizeof(uint64_t);
	store->effective_cost_us = malloc(costs_bytes ? costs_bytes : 1U);
	if (!store->effective_cost_us || !LearningStoreReceiptIndexBuild(store))
	{
		SG_HumanTraceLearningStoreClose(store);
		return 0;
	}
	if (costs_bytes)
		memcpy(store->effective_cost_us, parameters->effective_cost_us,
			costs_bytes);
	store->initialized = 1U;
	return 1;
}

void SG_HumanTraceLearningStoreClose(sg_human_trace_learning_store_t *store)
{
	if (!store)
		return;
	free(store->effective_cost_us);
	free(store->receipts);
	free(store->receipt_slots);
	memset(store, 0, sizeof(*store));
}

int SG_HumanTraceLearningStoreRestore(
	const sg_human_trace_learning_store_t *store,
	sg_human_trace_learning_parameters_t *parameters,
	uint64_t *next_transaction_id_out)
{
	sg_human_trace_learning_parameters_t durable;

	if (!store || !parameters || !next_transaction_id_out ||
		!store->initialized || store->generation == 0U ||
		store->next_transaction_id == 0U ||
		store->effective_cost_count != parameters->domain.kernel_key_count)
		return 0;
	durable = *parameters;
	durable.generation = store->generation;
	durable.effective_cost_us = store->effective_cost_us;
	durable.effective_cost_capacity = store->effective_cost_count;
	if (!SG_HumanTraceLearningParametersReplace(parameters, &durable))
		return 0;
	*next_transaction_id_out = store->next_transaction_id;
	return 1;
}

int SG_HumanTraceLearningStoreScopeConsumed(
	const sg_human_trace_learning_store_t *store,
	const sg_human_trace_learning_trace_scope_t *scope)
{
	size_t slot;

	if (!store || !scope || !store->initialized ||
		!SG_HumanTraceLearningTraceScopeValid(scope) ||
		store->receipt_slot_capacity == 0U)
		return 0;
	slot = (size_t)LearningStoreScopeHash(scope) &
		(store->receipt_slot_capacity - 1U);
	while (store->receipt_slots[slot] != 0U)
	{
		size_t index = store->receipt_slots[slot] - 1U;

		if (index >= store->receipt_count)
			return 0;
		if (LearningStoreReceiptMatches(&store->receipts[index], scope))
			return store->receipts[index].state_generation <= store->generation;
		slot = (slot + 1U) & (store->receipt_slot_capacity - 1U);
	}
	return 0;
}

int SG_HumanTraceLearningStoreCommitScope(
	sg_human_trace_learning_store_t *store,
	const sg_human_trace_learning_parameters_t *candidate,
	uint64_t next_transaction_id, uint64_t committed_transaction_count,
	const sg_human_trace_learning_trace_scope_t *scope)
{
	sg_human_trace_learning_scope_receipt_t receipt;
	unsigned char *bytes;
	size_t size;
	size_t costs_bytes;
	sg_human_trace_learning_store_t next;

	if (!store || !candidate || !scope || !store->initialized ||
		SG_HumanTraceLearningStoreScopeConsumed(store, scope) ||
		!SG_HumanTraceLearningParametersValid(candidate) ||
		!SG_HumanTraceLearningTraceScopeValid(scope) ||
		committed_transaction_count > UINT64_MAX - store->generation ||
		committed_transaction_count > UINT64_MAX - store->next_transaction_id ||
		candidate->generation !=
			store->generation + committed_transaction_count ||
		next_transaction_id !=
			store->next_transaction_id + committed_transaction_count)
		return 0;
	memset(&receipt, 0, sizeof(receipt));
	receipt.terminal_sha256 = scope->trace.terminal_sha256;
	receipt.client_id = scope->client_id;
	receipt.spawn_generation = scope->spawn_generation;
	receipt.state_generation = candidate->generation;
	if (!LearningStoreSerialize(store, candidate, next_transaction_id, &receipt,
		&bytes, &size))
		return 0;
	next = *store;
	next.receipt_count = store->receipt_count + 1U;
	next.receipt_capacity = next.receipt_count;
	if (next.receipt_count > SIZE_MAX / sizeof(*next.receipts))
	{
		free(bytes);
		return 0;
	}
	next.receipts = malloc(next.receipt_count * sizeof(*next.receipts));
	next.receipt_slots = NULL;
	next.receipt_slot_capacity = 0U;
	next.generation = candidate->generation;
	if (!next.receipts)
	{
		free(bytes);
		return 0;
	}
	if (store->receipt_count)
		memcpy(next.receipts, store->receipts,
			store->receipt_count * sizeof(*next.receipts));
	next.receipts[store->receipt_count] = receipt;
	if (!LearningStoreReceiptIndexBuild(&next))
	{
		free(next.receipt_slots);
		free(next.receipts);
		free(bytes);
		return 0;
	}
	if (!LearningStorePublish(store, bytes, size))
	{
		free(next.receipt_slots);
		free(next.receipts);
		free(bytes);
		return 0;
	}
	free(bytes);
	free(store->receipts);
	free(store->receipt_slots);
	store->receipts = next.receipts;
	store->receipt_count = next.receipt_count;
	store->receipt_capacity = next.receipt_capacity;
	store->receipt_slots = next.receipt_slots;
	store->receipt_slot_capacity = next.receipt_slot_capacity;
	store->generation = candidate->generation;
	store->next_transaction_id = next_transaction_id;
	costs_bytes = store->effective_cost_count * sizeof(uint64_t);
	memcpy(store->effective_cost_us, candidate->effective_cost_us, costs_bytes);
	return 1;
}
