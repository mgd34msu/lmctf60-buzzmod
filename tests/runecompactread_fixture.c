#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define RESERVED_OFFSET 44U

static int fixture_read(const char *path, unsigned char **bytes_out,
	size_t *size_out)
{
	FILE *stream = fopen(path, "rb");
	long length;
	unsigned char *bytes;
	size_t size;
	int read_ok;

	if (stream == NULL || fseek(stream, 0L, SEEK_END) != 0 ||
		(length = ftell(stream)) < 0 ||
		(uintmax_t)length > (uintmax_t)SIZE_MAX ||
		fseek(stream, 0L, SEEK_SET) != 0)
	{
		if (stream != NULL)
			(void)fclose(stream);
		return 0;
	}
	size = (size_t)length;
	bytes = malloc(size == 0U ? 1U : size);
	if (bytes == NULL)
	{
		(void)fclose(stream);
		return 0;
	}
	read_ok = size == 0U || fread(bytes, 1U, size, stream) == size;
	if (fclose(stream) != 0)
		read_ok = 0;
	if (!read_ok)
	{
		free(bytes);
		return 0;
	}
	*bytes_out = bytes;
	*size_out = size;
	return 1;
}

static int fixture_write(const char *path, const unsigned char *bytes,
	size_t size, int append_zero)
{
	FILE *stream = fopen(path, "wb");
	const unsigned char zero = 0U;
	int ok;

	if (stream == NULL)
		return 0;
	ok = fwrite(bytes, 1U, size, stream) == size &&
		(!append_zero || fwrite(&zero, 1U, 1U, stream) == 1U);
	if (fclose(stream) != 0)
		ok = 0;
	return ok;
}

int main(int argc, char **argv)
{
	unsigned char *image = NULL;
	size_t image_size = 0U;
	int ok;

	if (argc != 5 || !fixture_read(argv[1], &image, &image_size) ||
		image_size <= RESERVED_OFFSET || image[RESERVED_OFFSET] != 0U)
	{
		(void)fprintf(stderr,
			"usage: runecompactread_fixture SOURCE TRUNCATED TRAILING NONCANONICAL\n");
		free(image);
		return 1;
	}
	ok = fixture_write(argv[2], image, image_size - 1U, 0) &&
		fixture_write(argv[3], image, image_size, 1);
	image[RESERVED_OFFSET] = 1U;
	ok = ok && fixture_write(argv[4], image, image_size, 0);
	free(image);
	return ok ? 0 : 1;
}
