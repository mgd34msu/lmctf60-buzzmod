#ifndef SG_CONFIGURATION_LATTICE_H
#define SG_CONFIGURATION_LATTICE_H

#include <stdint.h>

typedef struct sg_configuration_lattice_halfspace_s
{
	float normal[3];
	float distance;
	int open;
} sg_configuration_lattice_halfspace_t;

typedef struct sg_configuration_lattice_stats_s
{
	uint64_t solve_calls;
	uint64_t constraints;
	uint32_t maximum_binary_shift;
} sg_configuration_lattice_stats_t;

/* Offline generator/auditor API.  This module depends on ISL/GMP and must not
 * be linked into the runtime game module or frozen-model loader.
 *
 * Finds an exact signed-q8 point. If objective is non-NULL, the returned point
 * maximizes its exact binary32 dot product before selecting a deterministic
 * feasible optimizer. Returns 1 for feasible, 0 for empty, and -1 on solver
 * failure. */
int SG_ConfigurationLatticeFind(
	const sg_configuration_lattice_halfspace_t *halfspaces,
	uint32_t halfspace_count, const float objective[3], int32_t point_out[3],
	sg_configuration_lattice_stats_t *stats);

/* Finds the exact signed-q8 extrema of one coordinate without introducing an
 * objective dimension into the integer set. */
int SG_ConfigurationLatticeCoordinateBounds(
	const sg_configuration_lattice_halfspace_t *halfspaces,
	uint32_t halfspace_count, uint32_t axis, int32_t *minimum_out,
	int32_t *maximum_out, sg_configuration_lattice_stats_t *stats);

/* Maximizes an integer q8 clearance variable for the selected constraints.
 * The optional exact binary32 dot product is optimized second. */
int SG_ConfigurationLatticeFindMaxClearance(
	const sg_configuration_lattice_halfspace_t *halfspaces,
	const uint8_t *clearance_constraints, uint32_t halfspace_count,
	const float objective[3], int32_t point_out[3], int *positive_margin_out,
	sg_configuration_lattice_stats_t *stats);

#endif
