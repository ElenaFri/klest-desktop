#include "klest.h"

#include <assert.h>
#include <string.h>

int main(void)
{
	const char *version = klest_version();

	assert(version != NULL);
	assert(strlen(version) > 0);

	return 0;
}
