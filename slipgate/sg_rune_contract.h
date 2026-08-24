/* sg_rune_contract.h -- RUNE runtime contract. */
#ifndef SG_RUNE_CONTRACT_H
#define SG_RUNE_CONTRACT_H

#include <stdint.h>

#include "sg_identity.h"

/* RUNE has one byte layout. The authenticated u16 at byte offset four states
 * whether its proved graph closes the route between both objectives. */
#define RUNE_ARTIFACT_MAGIC UINT32_C(0x454e5552)
#define RUNE_ARTIFACT_HEADER_BYTES UINT16_C(160)
#define RUNE_MAP_NAME_BYTES SG_LEVEL_IDENTITY_MAPNAME_BYTES

typedef enum rune_route_contract_e
{
	RUNE_ROUTE_CONTRACT_COMPLETE = 0,
	RUNE_ROUTE_CONTRACT_LOCAL_ONLY = 1
} rune_route_contract_t;

static inline int SG_RuneRouteContractValid(uint16_t route_contract)
{
	return route_contract == RUNE_ROUTE_CONTRACT_COMPLETE ||
	       route_contract == RUNE_ROUTE_CONTRACT_LOCAL_ONLY;
}

#define RUNE_MAX_SEEDS 32768
#define RUNE_MAX_LINKS 262144
#define RUNE_MAX_MECHANISM_NODES 8192
#define RUNE_MAX_MECHANISM_EDGES 262144
#define RUNE_MAX_MECHANISM_PLANS 262144
#define RUNE_MAX_MECHANISM_PLAN_EDGES 65536
#define RUNE_MAX_MECHANISM_STRING_BYTES 1048576
#define RUNE_MAX_MECHANISM_MEMBERS 16
#define RUNE_MIN_COST_MS 1
#define RUNE_MAX_COST_MS 30000

static inline int SG_RuneCarrierDoorSpawnflags(uint32_t spawnflags)
{
	return spawnflags == UINT32_C(4) || spawnflags == UINT32_C(5);
}

static inline int SG_RuneButtonCarrierDoorSpawnflags(uint32_t spawnflags)
{
	return SG_RuneCarrierDoorSpawnflags(spawnflags) ||
	       spawnflags == UINT32_C(32);
}

#endif /* SG_RUNE_CONTRACT_H */
