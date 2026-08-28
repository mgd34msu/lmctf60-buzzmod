/* sg_rune_v2_artifact_loader.c -- fail-closed RUNE v2 publication. */
#include "sg_rune_v2_artifact_loader.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SG_RUNE_V2_LOADER_STATE UINT32_C(0x52564c32)

typedef struct sg_rune_v2_owned_snapshot_s
{
	sg_rune_v2_artifact_snapshot_t published;
	sg_rune_v2_codec_storage_t storage;
} sg_rune_v2_owned_snapshot_t;

static int LoaderReady(const sg_rune_v2_artifact_loader_t *loader)
{
	return loader && loader->state == SG_RUNE_V2_LOADER_STATE &&
		loader->state_inverse == ~SG_RUNE_V2_LOADER_STATE;
}

static int OpsValid(const sg_rune_v2_artifact_loader_ops_t *ops)
{
	return ops && ops->open_read && ops->read && ops->seek && ops->tell &&
		ops->close_file && ops->allocate && ops->deallocate;
}

static sg_rune_v2_artifact_load_result_t Result(
	sg_rune_v2_artifact_loader_diagnostic_t diagnostic,
	sg_rune_v2_artifact_loader_stage_t stage)
{
	sg_rune_v2_artifact_load_result_t result;

	memset(&result, 0, sizeof(result));
	result.diagnostic = diagnostic;
	result.stage = stage;
	result.wire_diagnostic = SG_RUNE_V2_WIRE_OK;
	return result;
}

static void *DefaultOpen(void *context, const char *path, int *error_out)
{
	FILE *file;
	(void)context;
	errno = 0;
	file = fopen(path, "rb");
	if (error_out)
		*error_out = file ? 0 : (errno ? errno : EIO);
	return file;
}

static size_t DefaultRead(void *context, void *file, unsigned char *output,
	size_t output_size, int *error_out)
{
	size_t read_size;
	(void)context;
	errno = 0;
	read_size = fread(output, 1U, output_size, (FILE *)file);
	if (error_out)
		*error_out = (read_size != output_size && ferror((FILE *)file))
			? (errno ? errno : EIO) : 0;
	return read_size;
}

static int DefaultSeek(void *context, void *file,
	sg_rune_v2_loader_seek_origin_t origin, size_t offset, int *error_out)
{
	int result;
	(void)context;
	if (offset > (size_t)LONG_MAX)
	{
		if (error_out)
			*error_out = EOVERFLOW;
		return 0;
	}
	errno = 0;
	result = fseek((FILE *)file, (long)offset,
		origin == SG_RUNE_V2_LOADER_SEEK_END ? SEEK_END : SEEK_SET);
	if (error_out)
		*error_out = result == 0 ? 0 : (errno ? errno : EIO);
	return result == 0;
}

static int DefaultTell(void *context, void *file, size_t *offset_out,
	int *error_out)
{
	long offset;
	(void)context;
	errno = 0;
	offset = ftell((FILE *)file);
	if (offset < 0)
	{
		if (error_out)
			*error_out = errno ? errno : EIO;
		return 0;
	}
	*offset_out = (size_t)offset;
	if (error_out)
		*error_out = 0;
	return 1;
}

static int DefaultClose(void *context, void *file, int *error_out)
{
	int result;
	(void)context;
	errno = 0;
	result = fclose((FILE *)file);
	if (error_out)
		*error_out = result == 0 ? 0 : (errno ? errno : EIO);
	return result == 0;
}

static void *DefaultAllocate(void *context, size_t size)
{
	(void)context;
	return malloc(size);
}

static void DefaultDeallocate(void *context, void *allocation)
{
	(void)context;
	free(allocation);
}

void SG_RuneV2ArtifactLoaderDefaultOps(
	sg_rune_v2_artifact_loader_ops_t *ops_out)
{
	if (!ops_out)
		return;
	memset(ops_out, 0, sizeof(*ops_out));
	ops_out->open_read = DefaultOpen;
	ops_out->read = DefaultRead;
	ops_out->seek = DefaultSeek;
	ops_out->tell = DefaultTell;
	ops_out->close_file = DefaultClose;
	ops_out->allocate = DefaultAllocate;
	ops_out->deallocate = DefaultDeallocate;
}

int SG_RuneV2ArtifactLoaderInit(sg_rune_v2_artifact_loader_t *loader,
	const sg_rune_v2_artifact_loader_ops_t *ops)
{
	sg_rune_v2_artifact_loader_ops_t selected;

	if (!loader)
		return 0;
	/* Reinitialization would orphan the owned publication. */
	if (LoaderReady(loader))
		return 0;
	memset(loader, 0, sizeof(*loader));
	if (ops)
		selected = *ops;
	else
		SG_RuneV2ArtifactLoaderDefaultOps(&selected);
	if (!OpsValid(&selected))
		return 0;
	loader->ops = selected;
	loader->state = SG_RUNE_V2_LOADER_STATE;
	loader->state_inverse = ~SG_RUNE_V2_LOADER_STATE;
	return 1;
}

static void FreeStorage(const sg_rune_v2_artifact_loader_ops_t *ops,
	sg_rune_v2_codec_storage_t *storage)
{
#define FREE(member, capacity) do { \
	if (storage->member) \
		ops->deallocate(ops->context, storage->member); \
	storage->member = NULL; \
	storage->capacity = 0U; \
} while (0)
	FREE(planes, plane_capacity);
	FREE(portal_vertices, portal_vertex_capacity);
	FREE(phases, phase_capacity);
	FREE(phase_transitions, phase_transition_capacity);
	FREE(cells, cell_capacity);
	FREE(portals, portal_capacity);
	FREE(surfaces, surface_capacity);
	FREE(affordances, affordance_capacity);
	FREE(kernels, kernel_capacity);
	FREE(landmarks, landmark_capacity);
	FREE(mechanisms, mechanism_capacity);
#undef FREE
}

static void FreeOwned(const sg_rune_v2_artifact_loader_ops_t *ops,
	sg_rune_v2_owned_snapshot_t *owned)
{
	if (!owned)
		return;
	FreeStorage(ops, &owned->storage);
	ops->deallocate(ops->context, owned);
}

void SG_RuneV2ArtifactLoaderReset(sg_rune_v2_artifact_loader_t *loader)
{
	sg_rune_v2_owned_snapshot_t *old;
	if (!LoaderReady(loader))
		return;
	old = (sg_rune_v2_owned_snapshot_t *)loader->owned_active;
	loader->owned_active = NULL;
	FreeOwned(&loader->ops, old);
}

void SG_RuneV2ArtifactLoaderDestroy(sg_rune_v2_artifact_loader_t *loader)
{
	if (!loader)
		return;
	SG_RuneV2ArtifactLoaderReset(loader);
	memset(loader, 0, sizeof(*loader));
}

const sg_rune_v2_artifact_snapshot_t *SG_RuneV2ArtifactLoaderSnapshot(
	const sg_rune_v2_artifact_loader_t *loader)
{
	const sg_rune_v2_owned_snapshot_t *owned;
	if (!LoaderReady(loader))
		return NULL;
	owned = (const sg_rune_v2_owned_snapshot_t *)loader->owned_active;
	return owned ? &owned->published : NULL;
}

static int AllocateArray(const sg_rune_v2_artifact_loader_ops_t *ops,
	void **output, size_t element_size, uint32_t count)
{
	if (count == 0U)
	{
		*output = NULL;
		return 1;
	}
	if ((size_t)count > SIZE_MAX / element_size)
		return 0;
	*output = ops->allocate(ops->context, (size_t)count * element_size);
	return *output != NULL;
}

static int AllocateStorage(const sg_rune_v2_artifact_loader_ops_t *ops,
	const sg_rune_v2_wire_view_t *view, sg_rune_v2_codec_storage_t *storage)
{
#define ALLOC(member, capacity, type, section_type) do { \
	uint32_t count_ = view->section[(section_type) - 1U].count; \
	if (!AllocateArray(ops, (void **)&storage->member, sizeof(type), count_)) \
		return 0; \
	storage->capacity = count_; \
} while (0)
	memset(storage, 0, sizeof(*storage));
	ALLOC(planes, plane_capacity, sg_rune_plane_t, SG_RUNE_V2_SECTION_PLANES);
	ALLOC(portal_vertices, portal_vertex_capacity, sg_rune_vec3_t,
		SG_RUNE_V2_SECTION_PORTAL_VERTICES);
	ALLOC(phases, phase_capacity, sg_rune_phase_basis_t, SG_RUNE_V2_SECTION_PHASES);
	ALLOC(phase_transitions, phase_transition_capacity,
		sg_rune_phase_transition_t,
		SG_RUNE_V2_SECTION_PHASE_TRANSITIONS);
	ALLOC(cells, cell_capacity, sg_rune_cell_t, SG_RUNE_V2_SECTION_CELLS);
	ALLOC(portals, portal_capacity, sg_rune_portal_t, SG_RUNE_V2_SECTION_PORTALS);
	ALLOC(surfaces, surface_capacity, sg_rune_surface_t,
		SG_RUNE_V2_SECTION_SURFACES);
	ALLOC(affordances, affordance_capacity, sg_rune_affordance_t,
		SG_RUNE_V2_SECTION_AFFORDANCES);
	ALLOC(kernels, kernel_capacity, sg_rune_capability_kernel_t,
		SG_RUNE_V2_SECTION_KERNELS);
	ALLOC(landmarks, landmark_capacity, sg_rune_landmark_t,
		SG_RUNE_V2_SECTION_LANDMARKS);
	ALLOC(mechanisms, mechanism_capacity, sg_rune_mechanism_t,
		SG_RUNE_V2_SECTION_MECHANISMS);
#undef ALLOC
	return 1;
}

sg_rune_v2_artifact_load_result_t SG_RuneV2ArtifactLoaderLoadBytes(
	sg_rune_v2_artifact_loader_t *loader,
	const unsigned char *encoded, size_t encoded_size,
	const sg_rune_v2_artifact_binding_t *expected_binding,
	const sg_rune_v2_content_id_t *exact_file_identity)
{
	sg_rune_v2_artifact_load_result_t result;
	sg_rune_v2_wire_view_t view;
	sg_rune_v2_codec_storage_t scratch;
	sg_rune_v2_owned_snapshot_t *candidate = NULL;
	sg_rune_v2_owned_snapshot_t *old;
	sg_rune_v2_wire_binding_t decoded_binding;
	sg_rune_v2_wire_diagnostic_t wire;

	if (!loader || !encoded || !expected_binding || !exact_file_identity)
		return Result(SG_RUNE_V2_LOADER_INVALID_ARGUMENT,
			SG_RUNE_V2_LOADER_STAGE_ARGUMENT);
	if (!LoaderReady(loader))
		return Result(SG_RUNE_V2_LOADER_NOT_INITIALIZED,
			SG_RUNE_V2_LOADER_STAGE_ARGUMENT);
	wire = SG_RuneV2WireInspect(encoded, encoded_size, &view);
	if (wire != SG_RUNE_V2_WIRE_OK)
	{
		result = Result(SG_RUNE_V2_LOADER_WIRE_REJECTED,
			SG_RUNE_V2_LOADER_STAGE_INSPECT);
		result.wire_diagnostic = wire;
		return result;
	}
	if (!SG_RuneV2ArtifactBindingAccepts(&view, expected_binding,
		exact_file_identity))
		return Result(SG_RUNE_V2_LOADER_BINDING_MISMATCH,
			SG_RUNE_V2_LOADER_STAGE_BINDING);

	memset(&scratch, 0, sizeof(scratch));
	candidate = loader->ops.allocate(loader->ops.context, sizeof(*candidate));
	if (!candidate)
		return Result(SG_RUNE_V2_LOADER_ALLOCATION_FAILED,
			SG_RUNE_V2_LOADER_STAGE_MODEL_ALLOCATION);
	memset(candidate, 0, sizeof(*candidate));
	if (!AllocateStorage(&loader->ops, &view, &scratch) ||
		!AllocateStorage(&loader->ops, &view, &candidate->storage))
	{
		FreeStorage(&loader->ops, &scratch);
		FreeOwned(&loader->ops, candidate);
		return Result(SG_RUNE_V2_LOADER_ALLOCATION_FAILED,
			SG_RUNE_V2_LOADER_STAGE_MODEL_ALLOCATION);
	}
	wire = SG_RuneV2CodecDecode(encoded, encoded_size, &scratch,
		&candidate->storage, &decoded_binding,
		&candidate->published.model, &candidate->published.evidence);
	FreeStorage(&loader->ops, &scratch);
	if (wire != SG_RUNE_V2_WIRE_OK)
	{
		FreeOwned(&loader->ops, candidate);
		result = Result(SG_RUNE_V2_LOADER_WIRE_REJECTED,
			SG_RUNE_V2_LOADER_STAGE_DECODE);
		result.wire_diagnostic = wire;
		return result;
	}
	candidate->published.binding = *expected_binding;
	/* The candidate owns every referenced byte. This pointer replacement is
	 * the sole publication operation. */
	old = (sg_rune_v2_owned_snapshot_t *)loader->owned_active;
	loader->owned_active = candidate;
	FreeOwned(&loader->ops, old);
	return Result(SG_RUNE_V2_LOADER_OK,
		SG_RUNE_V2_LOADER_STAGE_PUBLICATION);
}

static void CloseAfterFailure(const sg_rune_v2_artifact_loader_ops_t *ops,
	void *file, sg_rune_v2_artifact_load_result_t *result)
{
	int close_error = 0;
	if (!ops->close_file(ops->context, file, &close_error))
		result->close_error = close_error ? close_error : EIO;
}

sg_rune_v2_artifact_load_result_t SG_RuneV2ArtifactLoaderLoadFile(
	sg_rune_v2_artifact_loader_t *loader, const char *path,
	const sg_rune_v2_artifact_binding_t *expected_binding,
	const sg_rune_v2_content_id_t *exact_file_identity)
{
	sg_rune_v2_artifact_load_result_t result;
	void *file;
	unsigned char *bytes = NULL;
	size_t file_size = 0U;
	size_t total = 0U;
	int error = 0;

	if (!loader || !path || !expected_binding || !exact_file_identity)
		return Result(SG_RUNE_V2_LOADER_INVALID_ARGUMENT,
			SG_RUNE_V2_LOADER_STAGE_ARGUMENT);
	if (!LoaderReady(loader))
		return Result(SG_RUNE_V2_LOADER_NOT_INITIALIZED,
			SG_RUNE_V2_LOADER_STAGE_ARGUMENT);
	file = loader->ops.open_read(loader->ops.context, path, &error);
	if (!file)
	{
		result = Result(SG_RUNE_V2_LOADER_IO_ERROR,
			SG_RUNE_V2_LOADER_STAGE_OPEN);
		result.os_error = error ? error : EIO;
		return result;
	}
	if (!loader->ops.seek(loader->ops.context, file,
			SG_RUNE_V2_LOADER_SEEK_END, 0U, &error) ||
		!loader->ops.tell(loader->ops.context, file, &file_size, &error))
	{
		result = Result(SG_RUNE_V2_LOADER_IO_ERROR,
			SG_RUNE_V2_LOADER_STAGE_FILE_SIZE);
		result.os_error = error ? error : EIO;
		CloseAfterFailure(&loader->ops, file, &result);
		return result;
	}
	result = Result(SG_RUNE_V2_LOADER_BAD_FILE_SIZE,
		SG_RUNE_V2_LOADER_STAGE_FILE_SIZE);
	result.observed_file_size = file_size;
	if (file_size < SG_RUNE_V2_HEADER_BYTES ||
		(uint64_t)file_size > SG_RUNE_V2_MAX_ARTIFACT_BYTES)
	{
		CloseAfterFailure(&loader->ops, file, &result);
		return result;
	}
	if (!loader->ops.seek(loader->ops.context, file,
		SG_RUNE_V2_LOADER_SEEK_BEGIN, 0U, &error))
	{
		result.diagnostic = SG_RUNE_V2_LOADER_IO_ERROR;
		result.os_error = error ? error : EIO;
		CloseAfterFailure(&loader->ops, file, &result);
		return result;
	}
	bytes = loader->ops.allocate(loader->ops.context, file_size);
	if (!bytes)
	{
		result = Result(SG_RUNE_V2_LOADER_ALLOCATION_FAILED,
			SG_RUNE_V2_LOADER_STAGE_FILE_ALLOCATION);
		result.observed_file_size = file_size;
		CloseAfterFailure(&loader->ops, file, &result);
		return result;
	}
	while (total < file_size)
	{
		size_t got = loader->ops.read(loader->ops.context, file,
			bytes + total, file_size - total, &error);
		if (got > file_size - total)
		{
			error = EIO;
			break;
		}
		total += got;
		if (got == 0U || error != 0)
			break;
	}
	if (total == file_size && error == 0)
	{
		unsigned char extra;
		size_t got = loader->ops.read(loader->ops.context, file, &extra, 1U,
			&error);
		if (got != 0U)
			error = EFBIG;
	}
	if (total != file_size || error != 0)
	{
		result = Result(total == file_size && error == EFBIG
			? SG_RUNE_V2_LOADER_BAD_FILE_SIZE : SG_RUNE_V2_LOADER_IO_ERROR,
			SG_RUNE_V2_LOADER_STAGE_FILE_READ);
		result.os_error = error;
		result.observed_file_size = file_size;
		result.bytes_read = total;
		CloseAfterFailure(&loader->ops, file, &result);
		loader->ops.deallocate(loader->ops.context, bytes);
		return result;
	}
	if (!loader->ops.close_file(loader->ops.context, file, &error))
	{
		result = Result(SG_RUNE_V2_LOADER_IO_ERROR,
			SG_RUNE_V2_LOADER_STAGE_CLOSE);
		result.os_error = error ? error : EIO;
		result.observed_file_size = file_size;
		result.bytes_read = total;
		loader->ops.deallocate(loader->ops.context, bytes);
		return result;
	}
	result = SG_RuneV2ArtifactLoaderLoadBytes(loader, bytes, file_size,
		expected_binding, exact_file_identity);
	result.observed_file_size = file_size;
	result.bytes_read = total;
	loader->ops.deallocate(loader->ops.context, bytes);
	return result;
}
