#include "sg_compound_publication_fixture.h"

int main(void)
{
	SG_CompoundPublicationCoreCasesRun();
	SG_CompoundHookPublicationCasesRun();
	if (failures)
	{
		fprintf(stderr, "sg_compound_publication_test: %d failure(s)\n",
		        failures);
		return 1;
	}
	puts("sg_compound_publication_test: ok");
	return 0;
}
