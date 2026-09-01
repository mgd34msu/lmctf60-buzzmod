/* Shared binary32 arithmetic law for compact transport witnesses. */
#ifndef SG_RUNE_COMPACT_BINARY32_H
#define SG_RUNE_COMPACT_BINARY32_H

#include <stddef.h>
#include <stdint.h>

/*
 * Transform one already-converted local point by an authenticated mover
 * matrix.  Every product and every add is a separate binary32 operation:
 *
 *   p0 = local[0] * axis[0][column]
 *   p1 = local[1] * axis[1][column]
 *   p2 = local[2] * axis[2][column]
 *   s0 = p0 + p1
 *   s1 = s0 + p2
 *   world = s1 + origin[column]
 *
 * Volatile intermediate objects are intentional.  They prevent an optimizer
 * from contracting or reassociating this persisted-witness law under -O3,
 * including when the caller is built with FMA enabled.  The caller owns
 * finite/canonical-bit validation of the inputs and outputs.
 */
static inline int SG_RuneCompactBinary32TransformPoint(
	const float local[3], const float origin[3], const float axis[3][3],
	float world_out[3])
{
	uint32_t column;

	if (local == NULL || origin == NULL || axis == NULL || world_out == NULL)
		return 0;
	for (column = 0U; column < 3U; column++)
	{
		volatile float first = local[0] * axis[0][column];
		volatile float second = local[1] * axis[1][column];
		volatile float third = local[2] * axis[2][column];
		volatile float sum = first + second;
		volatile float rotated = sum + third;
		volatile float world = rotated + origin[column];

		if (world == 0.0f)
			world = 0.0f;
		world_out[column] = world;
	}
	return 1;
}

#endif
