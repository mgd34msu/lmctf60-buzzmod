#include <stdio.h>

#include "sg_compound_oracle_fixture.h"

int main(void)
{
	SG_CompoundSwimPreopenCasesRun();
	SG_CompoundSwimRecoveryCasesRun();
	SG_CompoundDeclaredOracleCasesRun();
	if (failures)
	{
		fprintf(stderr, "sg_compound_swim_oracle_test: %d failure(s)\n",
		        failures);
		return 1;
	}
	puts("sg_compound_swim_oracle_test: ok");
	return 0;
}
