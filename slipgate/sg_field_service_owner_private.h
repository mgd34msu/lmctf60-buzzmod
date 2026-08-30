/* Private adoption seam for the complete dynamics-model owner. */
#ifndef SG_FIELD_SERVICE_OWNER_PRIVATE_H
#define SG_FIELD_SERVICE_OWNER_PRIVATE_H

#ifndef SG_FIELD_SERVICE_OWNER_PRIVATE
#error "only the complete dynamics-model owner may include this header"
#endif

#include "sg_rune_dynamics_model.h"

sg_field_status_t SG_FieldModelSourceAdoptOwnerPrivate(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_rune_dynamics_model_t *dynamics_model, uint64_t owner_identity,
	sg_field_model_source_t **source_out);
void SG_FieldModelSourceDestroyOwnerPrivate(
	sg_field_model_source_t **source_io);

#ifdef SG_FIELD_SERVICE_TESTING
void SG_FieldServiceTestExhaustIdentities(void);
size_t SG_FieldServiceTestCacheCount(const sg_field_service_t *service);
size_t SG_FieldServiceTestLeaseCount(const sg_field_service_t *service);
uint64_t SG_FieldServiceTestCleanSolveCount(const sg_field_service_t *service);
uint64_t SG_FieldServiceTestIncrementalReuseCount(
	const sg_field_service_t *service);
#endif

#endif
