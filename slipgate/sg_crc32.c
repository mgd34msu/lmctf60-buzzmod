/* sg_crc32.c -- standard reflected IEEE CRC32 (polynomial 0xedb88320). */
#include "slipgate/sg_crc32.h"

uint32_t SG_CRC32Init(void)
{
	return UINT32_C(0xffffffff);
}

int SG_CRC32Update(uint32_t *state, const void *block, size_t size)
{
	const unsigned char *p = (const unsigned char *)block;
	size_t i;
	int bit;

	if (!state)
		return 0;
	/* A zero-length fragment is useful when streaming an empty record class and
	 * does not require a non-NULL pointer. */
	if (size == 0)
		return 1;
	if (!p)
	{
		*state = 0;
		return 0;
	}

	for (i = 0; i < size; i++)
	{
		*state ^= (uint32_t)p[i];
		for (bit = 0; bit < 8; bit++)
			*state = (*state & UINT32_C(1))
			    ? (*state >> 1) ^ UINT32_C(0xedb88320)
			    : *state >> 1;
	}
	return 1;
}

uint32_t SG_CRC32Final(uint32_t state)
{
	return state ^ UINT32_C(0xffffffff);
}

int SG_CRC32Buffer(const void *block, size_t size, uint32_t *out)
{
	uint32_t state;

	if (!out)
		return 0;
	*out = 0;
	state = SG_CRC32Init();
	if (!SG_CRC32Update(&state, block, size))
		return 0;
	*out = SG_CRC32Final(state);
	return 1;
}
