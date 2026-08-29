#include "sg_authority_entropy.h"

#if defined(_WIN32)

#include <limits.h>

#if defined(_MSC_VER)
__declspec(dllimport) unsigned char __stdcall SystemFunction036(
	void *buffer, unsigned long size);
#pragma comment(lib, "advapi32.lib")
#elif defined(__MINGW32__)
__attribute__((dllimport)) unsigned char __attribute__((stdcall))
SystemFunction036(void *buffer, unsigned long size);
#else
extern unsigned char SystemFunction036(void *buffer, unsigned long size);
#endif

int SG_AuthorityEntropyFill(void *buffer, size_t size)
{
	unsigned char *bytes = buffer;
	size_t offset = 0U;

	if (!buffer && size != 0U)
		return 0;
	while (offset < size)
	{
		size_t remaining = size - offset;
		unsigned long chunk = remaining > (size_t)ULONG_MAX ? ULONG_MAX :
			(unsigned long)remaining;

		if (!SystemFunction036(bytes + offset, chunk))
			return 0;
		offset += (size_t)chunk;
	}
	return 1;
}

#elif defined(__linux__)

#include <errno.h>
#include <sys/random.h>

int SG_AuthorityEntropyFill(void *buffer, size_t size)
{
	unsigned char *bytes = buffer;
	size_t offset = 0U;

	if (!buffer && size != 0U)
		return 0;
	while (offset < size)
	{
		ssize_t count = getrandom(bytes + offset, size - offset, 0U);

		if (count > 0)
		{
			offset += (size_t)count;
			continue;
		}
		if (count < 0 && errno == EINTR)
			continue;
		return 0;
	}
	return 1;
}

#else

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int SG_AuthorityEntropyFill(void *buffer, size_t size)
{
	unsigned char *bytes = buffer;
	size_t offset = 0U;
	int descriptor;
	int result = 1;

	if (!buffer && size != 0U)
		return 0;
	if (size == 0U)
		return 1;
#if defined(O_CLOEXEC)
	descriptor = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
#else
	descriptor = open("/dev/urandom", O_RDONLY);
#endif
	if (descriptor < 0)
		return 0;
	while (offset < size)
	{
		ssize_t count = read(descriptor, bytes + offset, size - offset);

		if (count > 0)
		{
			offset += (size_t)count;
			continue;
		}
		if (count < 0 && errno == EINTR)
			continue;
		result = 0;
		break;
	}
	if (close(descriptor) != 0)
		result = 0;
	return result;
}

#endif
