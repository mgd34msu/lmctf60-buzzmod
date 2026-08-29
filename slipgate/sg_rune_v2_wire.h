/* sg_rune_v2_wire.h -- canonical RUNE v2 wire contract. */
#ifndef SG_RUNE_V2_WIRE_H
#define SG_RUNE_V2_WIRE_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "sg_rune_model.h"

#define SG_RUNE_V2_MAGIC UINT32_C(0x324e5552) /* RUN2, little endian. */
#define SG_RUNE_V2_VERSION UINT16_C(2)
#define SG_RUNE_V2_ENDIAN_LITTLE UINT16_C(0x0102)
#define SG_RUNE_V2_SCHEMA_REVISION UINT32_C(4)

#define SG_RUNE_V2_CONTENT_ID_BYTES UINT32_C(32)
#define SG_RUNE_V2_HEADER_BYTES UINT16_C(64)
#define SG_RUNE_V2_SECTION_ENTRY_BYTES UINT16_C(32)
#define SG_RUNE_V2_SECTION_ALIGNMENT UINT64_C(8)
#define SG_RUNE_V2_REQUIRED_SECTION_COUNT UINT32_C(13)
#define SG_RUNE_V2_MAX_ARTIFACT_BYTES UINT64_C(4294967296)

#define SG_RUNE_V2_HEADER_MAGIC_OFFSET UINT32_C(0)
#define SG_RUNE_V2_HEADER_VERSION_OFFSET UINT32_C(4)
#define SG_RUNE_V2_HEADER_ENDIAN_OFFSET UINT32_C(6)
#define SG_RUNE_V2_HEADER_BYTES_OFFSET UINT32_C(8)
#define SG_RUNE_V2_HEADER_ENTRY_BYTES_OFFSET UINT32_C(10)
#define SG_RUNE_V2_HEADER_SECTION_COUNT_OFFSET UINT32_C(12)
#define SG_RUNE_V2_HEADER_FLAGS_OFFSET UINT32_C(16)
#define SG_RUNE_V2_HEADER_SCHEMA_REVISION_OFFSET UINT32_C(20)
#define SG_RUNE_V2_HEADER_GENERATION_OFFSET UINT32_C(24)
#define SG_RUNE_V2_HEADER_TOTAL_BYTES_OFFSET UINT32_C(32)
#define SG_RUNE_V2_HEADER_PAYLOAD_CRC_OFFSET UINT32_C(40)
#define SG_RUNE_V2_HEADER_CRC_OFFSET UINT32_C(44)
#define SG_RUNE_V2_HEADER_RESERVED_OFFSET UINT32_C(48)
#define SG_RUNE_V2_HEADER_RESERVED_BYTES UINT32_C(16)

#define SG_RUNE_V2_SECTION_TYPE_OFFSET UINT32_C(0)
#define SG_RUNE_V2_SECTION_FLAGS_OFFSET UINT32_C(2)
#define SG_RUNE_V2_SECTION_ELEMENT_BYTES_OFFSET UINT32_C(4)
#define SG_RUNE_V2_SECTION_COUNT_OFFSET UINT32_C(8)
#define SG_RUNE_V2_SECTION_CRC_OFFSET UINT32_C(12)
#define SG_RUNE_V2_SECTION_OFFSET_OFFSET UINT32_C(16)
#define SG_RUNE_V2_SECTION_BYTES_OFFSET UINT32_C(24)
#define SG_RUNE_V2_SECTION_FLAG_REQUIRED UINT16_C(1)

typedef enum sg_rune_v2_section_type_e
{
	SG_RUNE_V2_SECTION_MODEL = 1,
	SG_RUNE_V2_SECTION_PLANES,
	SG_RUNE_V2_SECTION_PORTAL_VERTICES,
	SG_RUNE_V2_SECTION_PHASES,
	SG_RUNE_V2_SECTION_PHASE_TRANSITIONS,
	SG_RUNE_V2_SECTION_CELLS,
	SG_RUNE_V2_SECTION_PORTALS,
	SG_RUNE_V2_SECTION_SURFACES,
	SG_RUNE_V2_SECTION_AFFORDANCES,
	SG_RUNE_V2_SECTION_KERNELS,
	SG_RUNE_V2_SECTION_LANDMARKS,
	SG_RUNE_V2_SECTION_MECHANISMS,
	SG_RUNE_V2_SECTION_BINDING
} sg_rune_v2_section_type_t;

#define SG_RUNE_V2_MODEL_RECORD_BYTES UINT32_C(256)
#define SG_RUNE_V2_PLANE_RECORD_BYTES UINT32_C(64)
#define SG_RUNE_V2_PORTAL_VERTEX_RECORD_BYTES UINT32_C(12)
#define SG_RUNE_V2_PHASE_RECORD_BYTES UINT32_C(136)
#define SG_RUNE_V2_PHASE_TRANSITION_RECORD_BYTES UINT32_C(160)
#define SG_RUNE_V2_CELL_RECORD_BYTES UINT32_C(164)
#define SG_RUNE_V2_PORTAL_RECORD_BYTES UINT32_C(172)
#define SG_RUNE_V2_SURFACE_RECORD_BYTES UINT32_C(132)
#define SG_RUNE_V2_AFFORDANCE_RECORD_BYTES UINT32_C(104)
#define SG_RUNE_V2_KERNEL_RECORD_BYTES UINT32_C(332)
#define SG_RUNE_V2_LANDMARK_RECORD_BYTES UINT32_C(188)
#define SG_RUNE_V2_MECHANISM_RECORD_BYTES UINT32_C(160)
#define SG_RUNE_V2_BINDING_RECORD_BYTES UINT32_C(64)

/* Reusable scalar layouts within records. */
#define SG_RUNE_V2_STABLE_ID_SOURCE_OFFSET UINT32_C(0)
#define SG_RUNE_V2_STABLE_ID_HIGH_OFFSET UINT32_C(8)
#define SG_RUNE_V2_STABLE_ID_LOW_OFFSET UINT32_C(16)
#define SG_RUNE_V2_STABLE_ID_BYTES UINT32_C(24)
#define SG_RUNE_V2_ORDER_SOURCE_SET_OFFSET UINT32_C(0)
#define SG_RUNE_V2_ORDER_DOMAIN_OFFSET UINT32_C(8)
#define SG_RUNE_V2_ORDER_SOURCE_INDEX_OFFSET UINT32_C(12)
#define SG_RUNE_V2_ORDER_LOCAL_ORDINAL_OFFSET UINT32_C(16)
#define SG_RUNE_V2_ORDER_VARIANT_OFFSET UINT32_C(20)
#define SG_RUNE_V2_ORDER_BYTES UINT32_C(24)
#define SG_RUNE_V2_GEOMETRY_SOURCE_SET_OFFSET UINT32_C(0)
#define SG_RUNE_V2_GEOMETRY_SOURCE_INDEX_OFFSET UINT32_C(8)
#define SG_RUNE_V2_GEOMETRY_SOURCE_ORDINAL_OFFSET UINT32_C(12)
#define SG_RUNE_V2_GEOMETRY_BYTES UINT32_C(16)
#define SG_RUNE_V2_VEC3_X_OFFSET UINT32_C(0)
#define SG_RUNE_V2_VEC3_Y_OFFSET UINT32_C(4)
#define SG_RUNE_V2_VEC3_Z_OFFSET UINT32_C(8)
#define SG_RUNE_V2_VEC3_BYTES UINT32_C(12)
#define SG_RUNE_V2_INTERVAL_MIN_OFFSET UINT32_C(0)
#define SG_RUNE_V2_INTERVAL_MAX_OFFSET UINT32_C(4)
#define SG_RUNE_V2_INTERVAL_BYTES UINT32_C(8)
#define SG_RUNE_V2_SPAN_FIRST_OFFSET UINT32_C(0)
#define SG_RUNE_V2_SPAN_COUNT_OFFSET UINT32_C(4)
#define SG_RUNE_V2_SPAN_BYTES UINT32_C(8)
#define SG_RUNE_V2_ENTITY_INDEX_OFFSET UINT32_C(0)
#define SG_RUNE_V2_ENTITY_SPAWN_ORDINAL_OFFSET UINT32_C(4)
#define SG_RUNE_V2_ENTITY_BYTES UINT32_C(8)

/* Model metadata and independent validation evidence. */
#define SG_RUNE_V2_MODEL_VERSION_OFFSET UINT32_C(0)
#define SG_RUNE_V2_MODEL_RESERVED_OFFSET UINT32_C(2)
#define SG_RUNE_V2_MODEL_SCHEMA_TAG_OFFSET UINT32_C(4)
#define SG_RUNE_V2_MODEL_FLAGS_OFFSET UINT32_C(8)
#define SG_RUNE_V2_MODEL_PADDING_OFFSET UINT32_C(12)
#define SG_RUNE_V2_MODEL_BSP_CONTENT_ID_OFFSET UINT32_C(16)
#define SG_RUNE_V2_MODEL_ENTITY_SEMANTICS_ID_OFFSET UINT32_C(24)
#define SG_RUNE_V2_MODEL_PHYSICS_ABI_ID_OFFSET UINT32_C(32)
#define SG_RUNE_V2_MODEL_SOURCE_SET_ID_OFFSET UINT32_C(40)
#define SG_RUNE_V2_MODEL_SCHEMA_ID_OFFSET UINT32_C(48)
#define SG_RUNE_V2_MODEL_PRODUCER_ID_OFFSET UINT32_C(56)
#define SG_RUNE_V2_MODEL_STANDING_HULL_MINS_OFFSET UINT32_C(64)
#define SG_RUNE_V2_MODEL_STANDING_HULL_MAXS_OFFSET UINT32_C(76)
#define SG_RUNE_V2_MODEL_CROUCHING_HULL_MINS_OFFSET UINT32_C(88)
#define SG_RUNE_V2_MODEL_CROUCHING_HULL_MAXS_OFFSET UINT32_C(100)
#define SG_RUNE_V2_MODEL_GRAVITY_OFFSET UINT32_C(112)
#define SG_RUNE_V2_MODEL_GROUND_ACCELERATION_OFFSET UINT32_C(116)
#define SG_RUNE_V2_MODEL_AIR_ACCELERATION_OFFSET UINT32_C(120)
#define SG_RUNE_V2_MODEL_WATER_ACCELERATION_OFFSET UINT32_C(124)
#define SG_RUNE_V2_MODEL_HOOK_ACCELERATION_OFFSET UINT32_C(128)
#define SG_RUNE_V2_MODEL_EXTERNAL_ACCELERATION_OFFSET UINT32_C(132)
#define SG_RUNE_V2_MODEL_WATER_DRAG_OFFSET UINT32_C(136)
#define SG_RUNE_V2_MODEL_MAX_VELOCITY_OFFSET UINT32_C(140)
#define SG_RUNE_V2_MODEL_FRAME_MS_OFFSET UINT32_C(144)
#define SG_RUNE_V2_MODEL_SUBSTEP_MS_OFFSET UINT32_C(148)
#define SG_RUNE_V2_MODEL_COMPLETENESS_STATE_OFFSET UINT32_C(152)
#define SG_RUNE_V2_MODEL_COMPLETENESS_REASON_OFFSET UINT32_C(156)
#define SG_RUNE_V2_MODEL_EXPECTED_CELLS_OFFSET UINT32_C(160)
#define SG_RUNE_V2_MODEL_EXPECTED_PORTALS_OFFSET UINT32_C(164)
#define SG_RUNE_V2_MODEL_COVERED_CELLS_OFFSET UINT32_C(168)
#define SG_RUNE_V2_MODEL_COVERED_PORTALS_OFFSET UINT32_C(172)
#define SG_RUNE_V2_MODEL_FAILURE_RECORD_OFFSET UINT32_C(176)
#define SG_RUNE_V2_MODEL_EVIDENCE_VERSION_OFFSET UINT32_C(180)
#define SG_RUNE_V2_MODEL_EVIDENCE_RESERVED_OFFSET UINT32_C(184)
#define SG_RUNE_V2_MODEL_EVIDENCE_PADDING_OFFSET UINT32_C(188)
#define SG_RUNE_V2_MODEL_VERIFIER_ID_OFFSET UINT32_C(192)
#define SG_RUNE_V2_MODEL_EVIDENCE_BSP_ID_OFFSET UINT32_C(200)
#define SG_RUNE_V2_MODEL_EVIDENCE_SOURCE_SET_ID_OFFSET UINT32_C(208)
#define SG_RUNE_V2_MODEL_FIXED_POINT_ID_OFFSET UINT32_C(216)
#define SG_RUNE_V2_MODEL_FIXED_POINT_ROUNDS_OFFSET UINT32_C(224)
#define SG_RUNE_V2_MODEL_PROVED_CELLS_OFFSET UINT32_C(228)
#define SG_RUNE_V2_MODEL_PROVED_PORTALS_OFFSET UINT32_C(232)
#define SG_RUNE_V2_MODEL_OMITTED_CELLS_OFFSET UINT32_C(236)
#define SG_RUNE_V2_MODEL_OMITTED_PORTALS_OFFSET UINT32_C(240)
#define SG_RUNE_V2_MODEL_INVENTED_PORTALS_OFFSET UINT32_C(244)
#define SG_RUNE_V2_MODEL_PENDING_WORK_OFFSET UINT32_C(248)
#define SG_RUNE_V2_MODEL_TAIL_RESERVED_OFFSET UINT32_C(252)

/* Array record field offsets. */
#define SG_RUNE_V2_RECORD_ID_OFFSET UINT32_C(0)
#define SG_RUNE_V2_RECORD_ORDER_OFFSET UINT32_C(24)
#define SG_RUNE_V2_PLANE_NORMAL_OFFSET UINT32_C(48)
#define SG_RUNE_V2_PLANE_DISTANCE_OFFSET UINT32_C(60)
#define SG_RUNE_V2_PHASE_STANCE_OFFSET UINT32_C(48)
#define SG_RUNE_V2_PHASE_MOTION_OFFSET UINT32_C(52)
#define SG_RUNE_V2_PHASE_SUPPORT_OFFSET UINT32_C(56)
#define SG_RUNE_V2_PHASE_MEDIUM_OFFSET UINT32_C(60)
#define SG_RUNE_V2_PHASE_VOID_RELATION_OFFSET UINT32_C(64)
#define SG_RUNE_V2_PHASE_REFERENCE_FRAME_OFFSET UINT32_C(68)
#define SG_RUNE_V2_PHASE_MOVER_OFFSET UINT32_C(72)
#define SG_RUNE_V2_PHASE_VELOCITY_OFFSET UINT32_C(96)
#define SG_RUNE_V2_PHASE_ELAPSED_MS_OFFSET UINT32_C(120)
#define SG_RUNE_V2_PHASE_TIME_QUANTUM_OFFSET UINT32_C(128)
#define SG_RUNE_V2_PHASE_TIME_HORIZON_OFFSET UINT32_C(132)
#define SG_RUNE_V2_TRANSITION_CELL_OFFSET UINT32_C(48)
#define SG_RUNE_V2_TRANSITION_SOURCE_PHASE_OFFSET UINT32_C(72)
#define SG_RUNE_V2_TRANSITION_DESTINATION_PHASE_OFFSET UINT32_C(96)
#define SG_RUNE_V2_TRANSITION_KIND_OFFSET UINT32_C(120)
#define SG_RUNE_V2_TRANSITION_DURATION_OFFSET UINT32_C(124)
#define SG_RUNE_V2_TRANSITION_FLAGS_OFFSET UINT32_C(132)
#define SG_RUNE_V2_TRANSITION_DESTINATION_CELL_OFFSET UINT32_C(136)
#define SG_RUNE_V2_CELL_GEOMETRY_OFFSET UINT32_C(48)
#define SG_RUNE_V2_CELL_BOUNDS_MINS_OFFSET UINT32_C(64)
#define SG_RUNE_V2_CELL_BOUNDS_MAXS_OFFSET UINT32_C(76)
#define SG_RUNE_V2_CELL_BOUNDARY_PLANES_OFFSET UINT32_C(88)
#define SG_RUNE_V2_CELL_PHASES_OFFSET UINT32_C(96)
#define SG_RUNE_V2_CELL_SURFACES_OFFSET UINT32_C(104)
#define SG_RUNE_V2_CELL_AFFORDANCES_OFFSET UINT32_C(112)
#define SG_RUNE_V2_CELL_KERNELS_OFFSET UINT32_C(120)
#define SG_RUNE_V2_CELL_LANDMARKS_OFFSET UINT32_C(128)
#define SG_RUNE_V2_CELL_MECHANISMS_OFFSET UINT32_C(136)
#define SG_RUNE_V2_CELL_BSP_LEAF_OFFSET UINT32_C(144)
#define SG_RUNE_V2_CELL_BSP_AREA_OFFSET UINT32_C(148)
#define SG_RUNE_V2_CELL_BSP_CLUSTER_OFFSET UINT32_C(152)
#define SG_RUNE_V2_CELL_CONTENTS_OFFSET UINT32_C(156)
#define SG_RUNE_V2_CELL_SEMANTICS_OFFSET UINT32_C(160)
#define SG_RUNE_V2_PORTAL_GEOMETRY_OFFSET UINT32_C(48)
#define SG_RUNE_V2_PORTAL_FROM_CELL_OFFSET UINT32_C(64)
#define SG_RUNE_V2_PORTAL_TO_CELL_OFFSET UINT32_C(88)
#define SG_RUNE_V2_PORTAL_BOUNDARY_PLANE_OFFSET UINT32_C(112)
#define SG_RUNE_V2_PORTAL_BOUNDARY_VERTICES_OFFSET UINT32_C(136)
#define SG_RUNE_V2_PORTAL_PHASES_OFFSET UINT32_C(144)
#define SG_RUNE_V2_PORTAL_DIRECTION_OFFSET UINT32_C(152)
#define SG_RUNE_V2_PORTAL_CLEARANCE_OFFSET UINT32_C(156)
#define SG_RUNE_V2_PORTAL_CONTENTS_FROM_OFFSET UINT32_C(160)
#define SG_RUNE_V2_PORTAL_CONTENTS_TO_OFFSET UINT32_C(164)
#define SG_RUNE_V2_PORTAL_FLAGS_OFFSET UINT32_C(168)
#define SG_RUNE_V2_SURFACE_GEOMETRY_OFFSET UINT32_C(48)
#define SG_RUNE_V2_SURFACE_OWNER_CELL_OFFSET UINT32_C(64)
#define SG_RUNE_V2_SURFACE_PLANE_OFFSET UINT32_C(88)
#define SG_RUNE_V2_SURFACE_NORMAL_OFFSET UINT32_C(112)
#define SG_RUNE_V2_SURFACE_CONTENTS_OFFSET UINT32_C(124)
#define SG_RUNE_V2_SURFACE_SEMANTICS_OFFSET UINT32_C(128)
#define SG_RUNE_V2_AFFORDANCE_OWNER_CELL_OFFSET UINT32_C(48)
#define SG_RUNE_V2_AFFORDANCE_SURFACES_OFFSET UINT32_C(72)
#define SG_RUNE_V2_AFFORDANCE_PHASES_OFFSET UINT32_C(80)
#define SG_RUNE_V2_AFFORDANCE_KIND_OFFSET UINT32_C(88)
#define SG_RUNE_V2_AFFORDANCE_RANGE_OFFSET UINT32_C(92)
#define SG_RUNE_V2_AFFORDANCE_FLAGS_OFFSET UINT32_C(100)
#define SG_RUNE_V2_KERNEL_SOURCE_CELL_OFFSET UINT32_C(48)
#define SG_RUNE_V2_KERNEL_DESTINATION_CELL_OFFSET UINT32_C(72)
#define SG_RUNE_V2_KERNEL_BOUNDARY_OFFSET UINT32_C(96)
#define SG_RUNE_V2_KERNEL_AFFORDANCE_OFFSET UINT32_C(120)
#define SG_RUNE_V2_KERNEL_MECHANISM_OFFSET UINT32_C(144)
#define SG_RUNE_V2_KERNEL_SOURCE_PHASE_OFFSET UINT32_C(168)
#define SG_RUNE_V2_KERNEL_DESTINATION_PHASE_OFFSET UINT32_C(192)
#define SG_RUNE_V2_KERNEL_TRANSITION_OFFSET UINT32_C(216)
#define SG_RUNE_V2_KERNEL_FAMILY_OFFSET UINT32_C(240)
#define SG_RUNE_V2_KERNEL_COST_LAW_OFFSET UINT32_C(244)
#define SG_RUNE_V2_KERNEL_DISPLACEMENT_OFFSET UINT32_C(248)
#define SG_RUNE_V2_KERNEL_DURATION_OFFSET UINT32_C(272)
#define SG_RUNE_V2_KERNEL_SPEED_OFFSET UINT32_C(280)
#define SG_RUNE_V2_KERNEL_ACCELERATION_OFFSET UINT32_C(288)
#define SG_RUNE_V2_KERNEL_VERTICAL_ACCELERATION_OFFSET UINT32_C(296)
#define SG_RUNE_V2_KERNEL_GRAVITY_OFFSET UINT32_C(304)
#define SG_RUNE_V2_KERNEL_DRAG_OFFSET UINT32_C(308)
#define SG_RUNE_V2_KERNEL_PHYSICS_ABI_OFFSET UINT32_C(312)
#define SG_RUNE_V2_KERNEL_FIXED_LATENCY_OFFSET UINT32_C(320)
#define SG_RUNE_V2_KERNEL_DWELL_OFFSET UINT32_C(324)
#define SG_RUNE_V2_KERNEL_FLAGS_OFFSET UINT32_C(328)
#define SG_RUNE_V2_LANDMARK_GEOMETRY_OFFSET UINT32_C(48)
#define SG_RUNE_V2_LANDMARK_CELL_OFFSET UINT32_C(64)
#define SG_RUNE_V2_LANDMARK_ENTITY_OFFSET UINT32_C(88)
#define SG_RUNE_V2_LANDMARK_KIND_OFFSET UINT32_C(96)
#define SG_RUNE_V2_LANDMARK_ORIGIN_OFFSET UINT32_C(100)
#define SG_RUNE_V2_LANDMARK_BOUNDS_MINS_OFFSET UINT32_C(112)
#define SG_RUNE_V2_LANDMARK_BOUNDS_MAXS_OFFSET UINT32_C(124)
#define SG_RUNE_V2_LANDMARK_MECHANISM_OFFSET UINT32_C(136)
#define SG_RUNE_V2_LANDMARK_SURFACE_OFFSET UINT32_C(160)
#define SG_RUNE_V2_LANDMARK_SEMANTICS_OFFSET UINT32_C(184)
#define SG_RUNE_V2_MECHANISM_KIND_OFFSET UINT32_C(48)
#define SG_RUNE_V2_MECHANISM_ENTRY_CELL_OFFSET UINT32_C(52)
#define SG_RUNE_V2_MECHANISM_EXIT_CELL_OFFSET UINT32_C(76)
#define SG_RUNE_V2_MECHANISM_ACTIVATION_LANDMARK_OFFSET UINT32_C(100)
#define SG_RUNE_V2_MECHANISM_ENTITY_OFFSET UINT32_C(124)
#define SG_RUNE_V2_MECHANISM_DWELL_OFFSET UINT32_C(132)
#define SG_RUNE_V2_MECHANISM_TRAVEL_OFFSET UINT32_C(140)
#define SG_RUNE_V2_MECHANISM_TOPOLOGY_OFFSET UINT32_C(148)
#define SG_RUNE_V2_MECHANISM_FLAGS_OFFSET UINT32_C(156)
#define SG_RUNE_V2_BINDING_BSP_OFFSET UINT32_C(0)
#define SG_RUNE_V2_BINDING_SCHEMA_OFFSET UINT32_C(32)

typedef enum sg_rune_v2_wire_diagnostic_e
{
	SG_RUNE_V2_WIRE_OK = 0,
	SG_RUNE_V2_WIRE_INVALID_ARGUMENT,
	SG_RUNE_V2_WIRE_TRUNCATED,
	SG_RUNE_V2_WIRE_BAD_HEADER,
	SG_RUNE_V2_WIRE_BAD_VERSION,
	SG_RUNE_V2_WIRE_BAD_ENDIAN,
	SG_RUNE_V2_WIRE_BAD_SIZE,
	SG_RUNE_V2_WIRE_BAD_HEADER_CRC,
	SG_RUNE_V2_WIRE_BAD_PAYLOAD_CRC,
	SG_RUNE_V2_WIRE_BAD_SECTION,
	SG_RUNE_V2_WIRE_BAD_SECTION_CRC,
	SG_RUNE_V2_WIRE_HOSTILE_COUNT,
	SG_RUNE_V2_WIRE_BAD_RECORD,
	SG_RUNE_V2_WIRE_BAD_REFERENCE,
	SG_RUNE_V2_WIRE_BAD_BINDING
} sg_rune_v2_wire_diagnostic_t;

typedef struct sg_rune_v2_content_id_s
{
	uint8_t bytes[SG_RUNE_V2_CONTENT_ID_BYTES];
} sg_rune_v2_content_id_t;

typedef struct sg_rune_v2_wire_binding_s
{
	uint64_t generation;
	sg_rune_v2_content_id_t bsp_identity;
	sg_rune_v2_content_id_t schema_identity;
} sg_rune_v2_wire_binding_t;

/* artifact_identity is supplied after encoding by the exact-file identity
 * boundary. The codec never computes or serializes it. */
typedef struct sg_rune_v2_artifact_binding_s
{
	uint64_t generation;
	sg_rune_v2_content_id_t bsp_identity;
	sg_rune_v2_content_id_t schema_identity;
	sg_rune_v2_content_id_t artifact_identity;
} sg_rune_v2_artifact_binding_t;

typedef struct sg_rune_v2_wire_header_s
{
	uint64_t generation;
	uint64_t total_bytes;
	uint32_t payload_crc32;
} sg_rune_v2_wire_header_t;

typedef struct sg_rune_v2_wire_section_s
{
	uint16_t type;
	uint32_t element_bytes;
	uint32_t count;
	uint64_t offset;
	uint64_t bytes;
} sg_rune_v2_wire_section_t;

typedef struct sg_rune_v2_wire_view_s
{
	sg_rune_v2_wire_header_t header;
	sg_rune_v2_wire_section_t section[SG_RUNE_V2_REQUIRED_SECTION_COUNT];
	sg_rune_v2_wire_binding_t binding;
} sg_rune_v2_wire_view_t;

static inline uint16_t SG_RuneV2WireGetU16(const unsigned char *bytes)
{
	return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static inline uint32_t SG_RuneV2WireGetU32(const unsigned char *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
		((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static inline uint64_t SG_RuneV2WireGetU64(const unsigned char *bytes)
{
	uint64_t value = 0U;
	unsigned int index;

	for (index = 0U; index < 8U; index++)
		value |= (uint64_t)bytes[index] << (index * 8U);
	return value;
}

static inline void SG_RuneV2WirePutU16(unsigned char *bytes, uint16_t value)
{
	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
}

static inline void SG_RuneV2WirePutU32(unsigned char *bytes, uint32_t value)
{
	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
	bytes[2] = (unsigned char)(value >> 16);
	bytes[3] = (unsigned char)(value >> 24);
}

static inline void SG_RuneV2WirePutU64(unsigned char *bytes, uint64_t value)
{
	unsigned int index;

	for (index = 0U; index < 8U; index++)
		bytes[index] = (unsigned char)(value >> (index * 8U));
}

static inline int SG_RuneV2ContentIdEqual(const sg_rune_v2_content_id_t *left,
	const sg_rune_v2_content_id_t *right)
{
	size_t index;

	if (!left || !right)
		return 0;
	for (index = 0U; index < SG_RUNE_V2_CONTENT_ID_BYTES; index++)
		if (left->bytes[index] != right->bytes[index])
			return 0;
	return 1;
}

static inline int SG_RuneV2ContentIdValid(const sg_rune_v2_content_id_t *id)
{
	size_t index;

	if (!id)
		return 0;
	for (index = 0U; index < SG_RUNE_V2_CONTENT_ID_BYTES; index++)
		if (id->bytes[index] != 0U)
			return 1;
	return 0;
}

static inline uint32_t SG_RuneV2WireCRC32(const unsigned char *bytes,
	size_t size)
{
	uint32_t crc = UINT32_MAX;
	size_t index;

	if (!bytes && size != 0U)
		return 0U;
	for (index = 0U; index < size; index++)
	{
		unsigned int bit;

		crc ^= bytes[index];
		for (bit = 0U; bit < 8U; bit++)
			crc = (crc >> 1) ^ ((crc & 1U) ? UINT32_C(0xedb88320) : 0U);
	}
	return ~crc;
}

static inline uint32_t SG_RuneV2WireHeaderCRC32(const unsigned char *header)
{
	unsigned char copy[SG_RUNE_V2_HEADER_BYTES];
	size_t index;

	if (!header)
		return 0U;
	for (index = 0U; index < SG_RUNE_V2_HEADER_BYTES; index++)
		copy[index] = header[index];
	SG_RuneV2WirePutU32(copy + SG_RUNE_V2_HEADER_CRC_OFFSET, 0U);
	return SG_RuneV2WireCRC32(copy, sizeof(copy));
}

static inline int SG_RuneV2WireCheckedAdd(uint64_t left, uint64_t right,
	uint64_t *result)
{
	if (!result || right > UINT64_MAX - left)
		return 0;
	*result = left + right;
	return 1;
}

static inline int SG_RuneV2WireCheckedMul(uint64_t left, uint64_t right,
	uint64_t *result)
{
	if (!result || (left != 0U && right > UINT64_MAX / left))
		return 0;
	*result = left * right;
	return 1;
}

static inline uint32_t SG_RuneV2WireRecordBytes(uint16_t type)
{
	switch (type)
	{
	case SG_RUNE_V2_SECTION_MODEL: return SG_RUNE_V2_MODEL_RECORD_BYTES;
	case SG_RUNE_V2_SECTION_PLANES: return SG_RUNE_V2_PLANE_RECORD_BYTES;
	case SG_RUNE_V2_SECTION_PORTAL_VERTICES:
		return SG_RUNE_V2_PORTAL_VERTEX_RECORD_BYTES;
	case SG_RUNE_V2_SECTION_PHASES: return SG_RUNE_V2_PHASE_RECORD_BYTES;
	case SG_RUNE_V2_SECTION_PHASE_TRANSITIONS:
		return SG_RUNE_V2_PHASE_TRANSITION_RECORD_BYTES;
	case SG_RUNE_V2_SECTION_CELLS: return SG_RUNE_V2_CELL_RECORD_BYTES;
	case SG_RUNE_V2_SECTION_PORTALS: return SG_RUNE_V2_PORTAL_RECORD_BYTES;
	case SG_RUNE_V2_SECTION_SURFACES: return SG_RUNE_V2_SURFACE_RECORD_BYTES;
	case SG_RUNE_V2_SECTION_AFFORDANCES:
		return SG_RUNE_V2_AFFORDANCE_RECORD_BYTES;
	case SG_RUNE_V2_SECTION_KERNELS: return SG_RUNE_V2_KERNEL_RECORD_BYTES;
	case SG_RUNE_V2_SECTION_LANDMARKS: return SG_RUNE_V2_LANDMARK_RECORD_BYTES;
	case SG_RUNE_V2_SECTION_MECHANISMS:
		return SG_RUNE_V2_MECHANISM_RECORD_BYTES;
	case SG_RUNE_V2_SECTION_BINDING: return SG_RUNE_V2_BINDING_RECORD_BYTES;
	default: return 0U;
	}
}

static inline uint32_t SG_RuneV2WireMaxCount(uint16_t type)
{
	switch (type)
	{
	case SG_RUNE_V2_SECTION_MODEL: return 1U;
	case SG_RUNE_V2_SECTION_PLANES: return SG_RUNE_MODEL_MAX_PLANES;
	case SG_RUNE_V2_SECTION_PORTAL_VERTICES:
		return SG_RUNE_MODEL_MAX_PORTAL_VERTICES;
	case SG_RUNE_V2_SECTION_PHASES: return SG_RUNE_MODEL_MAX_PHASES;
	case SG_RUNE_V2_SECTION_PHASE_TRANSITIONS:
		return SG_RUNE_MODEL_MAX_PHASE_TRANSITIONS;
	case SG_RUNE_V2_SECTION_CELLS: return SG_RUNE_MODEL_MAX_CELLS;
	case SG_RUNE_V2_SECTION_PORTALS: return SG_RUNE_MODEL_MAX_PORTALS;
	case SG_RUNE_V2_SECTION_SURFACES: return SG_RUNE_MODEL_MAX_SURFACES;
	case SG_RUNE_V2_SECTION_AFFORDANCES: return SG_RUNE_MODEL_MAX_AFFORDANCES;
	case SG_RUNE_V2_SECTION_KERNELS: return SG_RUNE_MODEL_MAX_KERNELS;
	case SG_RUNE_V2_SECTION_LANDMARKS: return SG_RUNE_MODEL_MAX_LANDMARKS;
	case SG_RUNE_V2_SECTION_MECHANISMS: return SG_RUNE_MODEL_MAX_MECHANISMS;
	case SG_RUNE_V2_SECTION_BINDING: return 1U;
	default: return 0U;
	}
}

static inline const unsigned char *SG_RuneV2WireSectionData(
	const unsigned char *encoded, const sg_rune_v2_wire_section_t *section)
{
	return encoded + (size_t)section->offset;
}

static inline sg_rune_v2_wire_diagnostic_t SG_RuneV2WireInspect(
	const unsigned char *encoded, size_t encoded_size,
	sg_rune_v2_wire_view_t *view_out)
{
	sg_rune_v2_wire_view_t view = { 0 };
	uint64_t directory_end;
	uint64_t previous_end;
	uint32_t index;

	if (!encoded || !view_out)
		return SG_RUNE_V2_WIRE_INVALID_ARGUMENT;
	if (encoded_size < SG_RUNE_V2_HEADER_BYTES)
		return SG_RUNE_V2_WIRE_TRUNCATED;
	if (SG_RuneV2WireGetU32(encoded + SG_RUNE_V2_HEADER_MAGIC_OFFSET) !=
			SG_RUNE_V2_MAGIC ||
		SG_RuneV2WireGetU16(encoded + SG_RUNE_V2_HEADER_BYTES_OFFSET) !=
			SG_RUNE_V2_HEADER_BYTES ||
		SG_RuneV2WireGetU16(encoded + SG_RUNE_V2_HEADER_ENTRY_BYTES_OFFSET) !=
			SG_RUNE_V2_SECTION_ENTRY_BYTES ||
		SG_RuneV2WireGetU32(encoded + SG_RUNE_V2_HEADER_SECTION_COUNT_OFFSET) !=
			SG_RUNE_V2_REQUIRED_SECTION_COUNT ||
		SG_RuneV2WireGetU32(encoded + SG_RUNE_V2_HEADER_FLAGS_OFFSET) != 0U ||
		SG_RuneV2WireGetU32(encoded +
			SG_RUNE_V2_HEADER_SCHEMA_REVISION_OFFSET) !=
			SG_RUNE_V2_SCHEMA_REVISION)
		return SG_RUNE_V2_WIRE_BAD_HEADER;
	if (SG_RuneV2WireGetU16(encoded + SG_RUNE_V2_HEADER_VERSION_OFFSET) !=
		SG_RUNE_V2_VERSION)
		return SG_RUNE_V2_WIRE_BAD_VERSION;
	if (SG_RuneV2WireGetU16(encoded + SG_RUNE_V2_HEADER_ENDIAN_OFFSET) !=
		SG_RUNE_V2_ENDIAN_LITTLE)
		return SG_RUNE_V2_WIRE_BAD_ENDIAN;
	for (index = 0U; index < SG_RUNE_V2_HEADER_RESERVED_BYTES; index++)
		if (encoded[SG_RUNE_V2_HEADER_RESERVED_OFFSET + index] != 0U)
			return SG_RUNE_V2_WIRE_BAD_HEADER;
	view.header.generation = SG_RuneV2WireGetU64(encoded +
		SG_RUNE_V2_HEADER_GENERATION_OFFSET);
	view.header.total_bytes = SG_RuneV2WireGetU64(encoded +
		SG_RUNE_V2_HEADER_TOTAL_BYTES_OFFSET);
	view.header.payload_crc32 = SG_RuneV2WireGetU32(encoded +
		SG_RUNE_V2_HEADER_PAYLOAD_CRC_OFFSET);
	if (view.header.generation == 0U || view.header.total_bytes != encoded_size ||
		view.header.total_bytes > SG_RUNE_V2_MAX_ARTIFACT_BYTES)
		return SG_RUNE_V2_WIRE_BAD_SIZE;
	if (SG_RuneV2WireHeaderCRC32(encoded) != SG_RuneV2WireGetU32(encoded +
		SG_RUNE_V2_HEADER_CRC_OFFSET))
		return SG_RUNE_V2_WIRE_BAD_HEADER_CRC;
	directory_end = SG_RUNE_V2_HEADER_BYTES +
		(uint64_t)SG_RUNE_V2_REQUIRED_SECTION_COUNT *
		SG_RUNE_V2_SECTION_ENTRY_BYTES;
	if (directory_end > view.header.total_bytes)
		return SG_RUNE_V2_WIRE_BAD_SIZE;
	previous_end = directory_end;
	for (index = 0U; index < SG_RUNE_V2_REQUIRED_SECTION_COUNT; index++)
	{
		const unsigned char *entry = encoded + SG_RUNE_V2_HEADER_BYTES +
			(size_t)index * SG_RUNE_V2_SECTION_ENTRY_BYTES;
		sg_rune_v2_wire_section_t *section = &view.section[index];
		uint64_t expected_bytes;
		uint64_t expected_offset;
		uint64_t end;

		if (!SG_RuneV2WireCheckedAdd(previous_end,
			SG_RUNE_V2_SECTION_ALIGNMENT - 1U, &expected_offset))
			return SG_RUNE_V2_WIRE_BAD_SECTION;
		expected_offset &= ~(SG_RUNE_V2_SECTION_ALIGNMENT - 1U);

		section->type = SG_RuneV2WireGetU16(entry +
			SG_RUNE_V2_SECTION_TYPE_OFFSET);
		section->element_bytes = SG_RuneV2WireGetU32(entry +
			SG_RUNE_V2_SECTION_ELEMENT_BYTES_OFFSET);
		section->count = SG_RuneV2WireGetU32(entry +
			SG_RUNE_V2_SECTION_COUNT_OFFSET);
		section->offset = SG_RuneV2WireGetU64(entry +
			SG_RUNE_V2_SECTION_OFFSET_OFFSET);
		section->bytes = SG_RuneV2WireGetU64(entry +
			SG_RUNE_V2_SECTION_BYTES_OFFSET);
		if (section->type != index + 1U ||
			SG_RuneV2WireGetU16(entry + SG_RUNE_V2_SECTION_FLAGS_OFFSET) !=
				SG_RUNE_V2_SECTION_FLAG_REQUIRED ||
			section->element_bytes != SG_RuneV2WireRecordBytes(section->type) ||
			section->count > SG_RuneV2WireMaxCount(section->type) ||
			((section->type == SG_RUNE_V2_SECTION_MODEL ||
			  section->type == SG_RUNE_V2_SECTION_BINDING) &&
			 section->count != 1U))
			return section->count > SG_RuneV2WireMaxCount(section->type)
				? SG_RUNE_V2_WIRE_HOSTILE_COUNT : SG_RUNE_V2_WIRE_BAD_SECTION;
		if (!SG_RuneV2WireCheckedMul(section->element_bytes, section->count,
			&expected_bytes) || expected_bytes != section->bytes ||
			section->offset != expected_offset ||
			(section->offset & (SG_RUNE_V2_SECTION_ALIGNMENT - 1U)) != 0U ||
			!SG_RuneV2WireCheckedAdd(section->offset, section->bytes, &end) ||
			end > view.header.total_bytes)
			return SG_RUNE_V2_WIRE_BAD_SECTION;
		while (previous_end < section->offset)
			if (encoded[(size_t)previous_end++] != 0U)
				return SG_RUNE_V2_WIRE_BAD_SECTION;
		if (SG_RuneV2WireCRC32(encoded + (size_t)section->offset,
			(size_t)section->bytes) != SG_RuneV2WireGetU32(entry +
			SG_RUNE_V2_SECTION_CRC_OFFSET))
			return SG_RUNE_V2_WIRE_BAD_SECTION_CRC;
		previous_end = end;
	}
	{
		uint64_t expected_total;

		if (!SG_RuneV2WireCheckedAdd(previous_end,
			SG_RUNE_V2_SECTION_ALIGNMENT - 1U, &expected_total))
			return SG_RUNE_V2_WIRE_BAD_SIZE;
		expected_total &= ~(SG_RUNE_V2_SECTION_ALIGNMENT - 1U);
		if (view.header.total_bytes != expected_total)
			return SG_RUNE_V2_WIRE_BAD_SIZE;
	}
	while (previous_end < view.header.total_bytes)
		if (encoded[(size_t)previous_end++] != 0U)
			return SG_RUNE_V2_WIRE_BAD_SIZE;
	if (SG_RuneV2WireCRC32(encoded + SG_RUNE_V2_HEADER_BYTES,
		encoded_size - SG_RUNE_V2_HEADER_BYTES) != view.header.payload_crc32)
		return SG_RUNE_V2_WIRE_BAD_PAYLOAD_CRC;
	{
		const unsigned char *model = SG_RuneV2WireSectionData(encoded,
			&view.section[SG_RUNE_V2_SECTION_MODEL - 1U]);
		const unsigned char *binding = SG_RuneV2WireSectionData(encoded,
			&view.section[SG_RUNE_V2_SECTION_BINDING - 1U]);

		if (SG_RuneV2WireGetU16(model + SG_RUNE_V2_MODEL_VERSION_OFFSET) !=
				SG_RUNE_MODEL_VERSION ||
			SG_RuneV2WireGetU16(model + SG_RUNE_V2_MODEL_RESERVED_OFFSET) != 0U ||
			SG_RuneV2WireGetU32(model + SG_RUNE_V2_MODEL_SCHEMA_TAG_OFFSET) !=
				SG_RUNE_MODEL_SCHEMA_TAG ||
			SG_RuneV2WireGetU32(model + SG_RUNE_V2_MODEL_PADDING_OFFSET) != 0U ||
			SG_RuneV2WireGetU32(model +
				SG_RUNE_V2_MODEL_EVIDENCE_PADDING_OFFSET) != 0U ||
			SG_RuneV2WireGetU32(model +
				SG_RUNE_V2_MODEL_TAIL_RESERVED_OFFSET) != 0U)
			return SG_RUNE_V2_WIRE_BAD_RECORD;
		view.binding.generation = view.header.generation;
		for (index = 0U; index < SG_RUNE_V2_CONTENT_ID_BYTES; index++)
		{
			view.binding.bsp_identity.bytes[index] =
				binding[SG_RUNE_V2_BINDING_BSP_OFFSET + index];
			view.binding.schema_identity.bytes[index] =
				binding[SG_RUNE_V2_BINDING_SCHEMA_OFFSET + index];
		}
	}
	if (!SG_RuneV2ContentIdValid(&view.binding.bsp_identity) ||
		!SG_RuneV2ContentIdValid(&view.binding.schema_identity))
		return SG_RUNE_V2_WIRE_BAD_BINDING;
	*view_out = view;
	return SG_RUNE_V2_WIRE_OK;
}

static inline int SG_RuneV2ArtifactBindingAccepts(
	const sg_rune_v2_wire_view_t *wire,
	const sg_rune_v2_artifact_binding_t *binding,
	const sg_rune_v2_content_id_t *exact_file_identity)
{
	return wire && binding &&
		wire->binding.generation == binding->generation &&
		binding->generation != 0U && SG_RuneV2ContentIdValid(exact_file_identity) &&
		SG_RuneV2ContentIdEqual(&wire->binding.bsp_identity,
			&binding->bsp_identity) &&
		SG_RuneV2ContentIdEqual(&wire->binding.schema_identity,
			&binding->schema_identity) &&
		SG_RuneV2ContentIdEqual(&binding->artifact_identity,
			exact_file_identity);
}

#endif /* SG_RUNE_V2_WIRE_H */
