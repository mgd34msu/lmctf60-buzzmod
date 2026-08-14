/* Boundary tests for the exact formatter used by ReadEntFile/WriteEntFile. */
#include <stdio.h>
#include <string.h>

#include "g_entfile_path.h"
#include "q_shared.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

int main(void)
{
	char mapname[MAX_QPATH];
	char exact_gamedir[56];
	char overlong_gamedir[57];
	char path[MAX_OSPATH];
	char small[16];

	memset(mapname, 'm', sizeof(mapname));
	mapname[sizeof(mapname) - 1] = '\0';
	CHECK(G_EntFilePath(path, sizeof(path), "lmctf-hooktest", mapname));
	CHECK(strlen(path) == 86);
	CHECK(strncmp(path, "lmctf-hooktest/ent/", 19) == 0);
	CHECK(strcmp(path + strlen(path) - 4, ".ent") == 0);

	/* A 55-byte gamedir plus a 63-byte map consumes all 127 characters
	 * available before MAX_OSPATH's terminator; one more byte must reject. */
	memset(exact_gamedir, 'g', sizeof(exact_gamedir));
	exact_gamedir[sizeof(exact_gamedir) - 1] = '\0';
	CHECK(G_EntFilePath(path, sizeof(path), exact_gamedir, mapname));
	CHECK(strlen(path) == MAX_OSPATH - 1);
	memset(overlong_gamedir, 'g', sizeof(overlong_gamedir));
	overlong_gamedir[sizeof(overlong_gamedir) - 1] = '\0';
	memset(path, 0xa5, sizeof(path));
	CHECK(!G_EntFilePath(path, sizeof(path), overlong_gamedir, mapname));
	CHECK(path[0] == '\0');

	memset(small, 0xa5, sizeof(small));
	CHECK(!G_EntFilePath(small, sizeof(small), "lmctf-hooktest", mapname));
	CHECK(small[0] == '\0');
	CHECK(!G_EntFilePath(path, sizeof(path), NULL, mapname));
	CHECK(path[0] == '\0');
	CHECK(!G_EntFilePath(NULL, sizeof(path), "lmctf-hooktest", mapname));
	CHECK(!G_EntFilePath(path, 0, "lmctf-hooktest", mapname));

	if (failures)
	{
		fprintf(stderr, "g_entfile_path_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("g_entfile_path_test: ok");
	return 0;
}
