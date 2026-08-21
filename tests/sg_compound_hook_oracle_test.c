#include <stdio.h>

#include "sg_compound_hook_oracle_fixture.h"

int main(void)
{
	int failures = SG_CompoundHookOracleFixtureRun();

	if (failures)
	{
		fprintf(stderr, "sg_compound_hook_oracle_test: %d failure(s)\n",
		        failures);
		return 1;
	}
	puts("sg_compound_hook_oracle_test: ok");
	return 0;
}
