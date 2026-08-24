#include <stdio.h>
#include <string.h>

#include "q_shared.h"
#include "slipgate/sg_rune_update_source.h"

static int failures;
static int load_calls;
static int free_calls;
static rune_t *published_rune;
static rune_t resident_rune;
static rune_t transient_rune;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

rune_t *SG_Rune(void)
{
	return published_rune;
}

rune_t *Rune_Load(const char *mapname)
{
	load_calls++;
	CHECK(mapname && strcmp(mapname, "lmctf01") == 0);
	return &transient_rune;
}

void Rune_Free(rune_t *rune)
{
	free_calls++;
	CHECK(rune == &transient_rune);
}

static void Reset(void)
{
	load_calls = 0;
	free_calls = 0;
	published_rune = NULL;
}

static void TestNoBotAcquiresAndReleasesTransientRune(void)
{
	sg_rune_update_source_t source = { NULL, false };

	Reset();
	CHECK(SG_RuneUpdateSourceAcquire("lmctf01", &source));
	CHECK(source.rune == &transient_rune);
	CHECK(source.transient);
	CHECK(load_calls == 1);
	SG_RuneUpdateSourceRelease(&source);
	CHECK(free_calls == 1);
	CHECK(source.rune == NULL);
	CHECK(!source.transient);
	SG_RuneUpdateSourceRelease(&source);
	CHECK(free_calls == 1);
}

static void TestResidentRuneIsBorrowedAndNeverFreed(void)
{
	sg_rune_update_source_t source = { NULL, false };

	Reset();
	published_rune = &resident_rune;
	CHECK(SG_RuneUpdateSourceAcquire("lmctf01", &source));
	CHECK(source.rune == &resident_rune);
	CHECK(!source.transient);
	CHECK(load_calls == 0);
	SG_RuneUpdateSourceRelease(&source);
	CHECK(free_calls == 0);
	CHECK(source.rune == NULL);
}

int main(void)
{
	TestNoBotAcquiresAndReleasesTransientRune();
	TestResidentRuneIsBorrowedAndNeverFreed();
	if (failures)
	{
		fprintf(stderr, "sg_rune_update_source_test: %d failure(s)\n",
			failures);
		return 1;
	}
	puts("sg_rune_update_source_test: ok");
	return 0;
}
