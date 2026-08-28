/* Emit the canonical synthetic fixture for independent-reader tests. */
#include "tests/support/sg_rune_v2_fixture.h"

#include <stdio.h>

int main(int argc, char **argv)
{
	sg_rune_v2_test_model_fixture_t fixture;
	unsigned char encoded[TEST_IMAGE_CAPACITY];
	size_t encoded_size = 0U;
	FILE *file;

	if (argc != 2)
		return 2;
	SG_RuneV2TestFixtureInit(&fixture);
	if (SG_RuneV2CodecEncode(&fixture.binding, &fixture.model,
		&fixture.evidence, encoded, sizeof(encoded), &encoded_size) !=
		SG_RUNE_V2_WIRE_OK)
		return 1;
	SG_RuneV2TestFixChecksums(encoded, encoded_size);
	file = fopen(argv[1], "wb");
	if (!file)
		return 2;
	if (fwrite(encoded, 1U, encoded_size, file) != encoded_size ||
		fclose(file) != 0)
		return 2;
	return 0;
}
