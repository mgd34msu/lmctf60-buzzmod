#include "slipgate/sg_rune_v2_content_identity.h"

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
	static const char digits[] = "0123456789abcdef";
	unsigned char *bytes = NULL;
	size_t size = 0U;
	size_t capacity = 0U;
	sg_rune_v2_content_id_t identity;
	size_t index;

	for (;;)
	{
		int value = fgetc(stdin);
		unsigned char *grown;

		if (value == EOF)
			break;
		if (size == capacity)
		{
			size_t next = capacity == 0U ? 256U : capacity * 2U;

			if (next < capacity)
			{
				free(bytes);
				return 2;
			}
			grown = (unsigned char *)realloc(bytes, next);
			if (!grown)
			{
				free(bytes);
				return 2;
			}
			bytes = grown;
			capacity = next;
		}
		bytes[size++] = (unsigned char)value;
	}
	if (ferror(stdin) ||
		!SG_RuneV2ContentIdentitySHA256(bytes, size, &identity))
	{
		free(bytes);
		return 2;
	}
	for (index = 0U; index < sizeof(identity.bytes); index++)
		printf("%c%c", digits[identity.bytes[index] >> 4],
			digits[identity.bytes[index] & 15U]);
	putchar('\n');
	free(bytes);
	return 0;
}
