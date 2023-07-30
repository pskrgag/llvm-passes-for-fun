#pragma clang diagnostic push
#pragma ide diagnostic ignored "bugprone-reserved-identifier"
/// The code in this file is just a skeleton. You are allowed (and encouraged!)
/// to change if it doesn't fit your needs or ideas.

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>

#define SHADOW_OFFSET (1UL << 44)
#define SHADOW_OFFSET_PTR ((unsigned char*)SHADOW_OFFSET)
#define SHADOW_SIZE	0xfffffffffff

#define UL(x) ((unsigned long)(x))
#define GENMASK(h, l) \
	(((~UL(0)) - (UL(1) << (l)) + 1) & (~UL(0) >> (32 - 1 - (h))))

#define __round_mask(x, y) ((__typeof__(x))((y)-1))
#define round_up(x, y) ((((x)-1) | __round_mask(x, y)) + 1)

#define POISON_VAL	0xffffffff

static unsigned char* shadow_base;

void __runtime_init()
{
	struct rlimit rlim;
	rlim_t max_mem = 1UL << 47;
	rlim.rlim_cur = max_mem;

	setrlimit(RLIMIT_AS, &rlim);

	void* shadow_base = mmap((void*)SHADOW_OFFSET, SHADOW_SIZE, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

	assert(shadow_base != MAP_FAILED);
}

void __runtime_poison(const void* ptr, size_t size)
{
	unsigned* base = ((unsigned*)(SHADOW_OFFSET_PTR + (UL(ptr) >> 3))) - 1;
	unsigned i;
	unsigned last_byte = 0;

	assert((UL(ptr) & 7) == 0);

	/* printf("size = %zu\n", size); */

	/* poison upper redzone */
	*(base++) = POISON_VAL;

	/* every 32 byte gives zero */
	for (i = size; size >= 32; size -= 32)
		*(base++) = 0x0;

	/* handle upper unaligned */
	if (size) {
		last_byte = ((size % 8) << ((size / 8) * 8));
		last_byte |= GENMASK(31, ((size / 8) * 8) + 8);
		*(base++) = last_byte;
	}


	/* poison upper redzone */
	*base = POISON_VAL;
}

void __runtime_unpoison(const void* ptr)
{
	unsigned* base = ((unsigned*)(SHADOW_OFFSET_PTR + (UL(ptr) >> 3)));

	while (*base != POISON_VAL) {
		*base = POISON_VAL;
		++base;
	}
}

void __runtime_cleanup()
{
	munmap(SHADOW_OFFSET_PTR, SHADOW_SIZE);
}

static void report_error(const void *ptr, size_t size, int store)
{
	fprintf(stderr, "Illegal memory access at %p [%s]\n", ptr, store ? "store": "load");
	exit(-1);
}

void __runtime_check_addr(const void* ptr, size_t size, int store)
{
	unsigned char* shadow = SHADOW_OFFSET_PTR + (UL(ptr) >> 3);
	signed char k = *shadow;

	printf("k = %hhx ptr = %p size = %zu\n", k, ptr, size);

	if (size != 8) {
		/* Casting to signed char to make sign check work */
		if (k && ((signed char)(UL(ptr) & 7) + (signed char)size > k))
			report_error(ptr, size, store);
	} else {
		if (k)
			report_error(ptr, size, store);
	}
}

void* __runtime_malloc(size_t size)
{
	void *ret;
	int r = posix_memalign(&ret, 32, round_up(size + 32 * 2, 32));
	if (r)
		return NULL;

	__runtime_poison(ret + 32, size);

	return ret + 32;
}

void __runtime_free(void* ptr)
{
	__runtime_unpoison(ptr);
}

#pragma clang diagnostic pop
