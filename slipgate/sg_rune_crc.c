#include "sg_rune_crc.h"

static uint32_t sg_crc_table[256];
static int sg_crc_ready;

static void Build(void)
{
	uint32_t n;

	for (n = 0U; n < 256U; n++)
	{
		uint32_t c = n;
		int k;

		for (k = 0; k < 8; k++)
			c = (c & 1U) ? (UINT32_C(0xEDB88320) ^ (c >> 1)) : (c >> 1);
		sg_crc_table[n] = c;
	}
	sg_crc_ready = 1;
}

uint32_t SG_RuneCrc32Continue(uint32_t crc, const uint8_t *bytes, size_t count)
{
	size_t i;

	if (!sg_crc_ready)
		Build();
	crc = ~crc;
	for (i = 0U; i < count; i++)
		crc = sg_crc_table[(crc ^ bytes[i]) & 0xFFU] ^ (crc >> 8);
	return ~crc;
}

uint32_t SG_RuneCrc32(const uint8_t *bytes, size_t count)
{
	return SG_RuneCrc32Continue(0U, bytes, count);
}
