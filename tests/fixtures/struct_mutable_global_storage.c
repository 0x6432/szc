/* szc serializer policy, not C struct ABI or Zig logical undefined semantics. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

uint8_t check_backing(const void *opaque, const uintptr_t *metadata);
uint8_t check_edges(const void *tail, const void *bundle, const void *undefined,
	const void *empty, const void *array, const uintptr_t *metadata);

static bool
check_bytes(const unsigned char *root, unsigned char *used, size_t total,
	size_t offset, const void *expected, size_t size)
{
	if (offset > total || size > total - offset)
		return false;
	if (memcmp(root + offset, expected, size) != 0)
		return false;
	memset(used + offset, 1, size);
	return true;
}

uint8_t
check_backing(const void *opaque, const uintptr_t *m)
{
	const unsigned char *p = opaque;
	size_t total = m[0];
	unsigned char used[4096] = {0};
	if (total != 80 || total > sizeof used || (uintptr_t)p % m[1] != 0)
		return 90;
	uint8_t tag = 11, itag = 12, row0 = 13, row1 = 14, flags = 1;
	uint16_t odd = 257;
	uint32_t inner = 0x12345678, r0 = 101, r1 = 202;
	uint64_t wide = UINT64_C(0x1122334455667788);
#define CHECK(offset, value) do { \
	if (!check_bytes(p, used, total, (offset), &(value), sizeof(value))) return 91; \
} while (0)
	CHECK(m[5], tag);
	CHECK(m[6], wide);
	CHECK(m[7] + m[3], inner);
	CHECK(m[7] + m[4], itag);
	CHECK(m[8] + m[3], r0);
	CHECK(m[8] + m[4], row0);
	CHECK(m[8] + m[2] + m[3], r1);
	CHECK(m[8] + m[2] + m[4], row1);
	const unsigned char *text, *target, *child;
	uintptr_t length;
	memcpy(&text, p + m[9], sizeof text);
	memcpy(&length, p + m[9] + 8, sizeof length);
	if (length != 3 || memcmp(text, "abc\0", 4) != 0)
		return 92;
	CHECK(m[9], text);
	CHECK(m[9] + 8, length);
	memcpy(&target, p + m[10], sizeof target);
	if (*target != 71)
		return 93;
	CHECK(m[10], target);
	memcpy(&child, p + m[11], sizeof child);
	CHECK(m[11], child);
	uint32_t child_word;
	memcpy(&child_word, child + m[3], sizeof child_word);
	if (child_word != 505 || child[m[4]] != 17)
		return 94;
	uint8_t (*callback)(uint8_t);
	memcpy(&callback, p + m[12], sizeof callback);
	if (callback(7) != 8)
		return 95;
	CHECK(m[12], callback);
	const unsigned char sentinel[] = {21, 22, 9};
	if (!check_bytes(p, used, total, m[13], sentinel, sizeof sentinel))
		return 96;
	uint8_t empty_sentinel = 7;
	CHECK(m[14], empty_sentinel);
	CHECK(m[15], flags);
	CHECK(m[16], odd);
#undef CHECK
	if (m[17] > total || m[18] > total)
		return 97;
	for (size_t i = 0; i < total; i++)
		if (!used[i] && p[i] != 0)
			return 98;
	return 0;
}

uint8_t
check_edges(const void *tail, const void *bundle, const void *undefined,
	const void *empty, const void *array, const uintptr_t *m)
{
	const unsigned char *t = tail, *b = bundle, *u = undefined, *e = empty, *a = array;
	if ((uintptr_t)t % m[0] || (uintptr_t)b % m[0]
		|| (uintptr_t)u % m[0] || (uintptr_t)e % m[0])
		return 101;
	uint64_t word;
	memcpy(&word, t + m[3], sizeof word);
	if (m[1] != 16 || word != UINT64_C(0x123456789abcdef0) || t[m[2]] != 23)
		return 102;
	for (uintptr_t i = 0; i < m[1]; i++)
		if (i != m[2] && !(i >= m[3] && i < m[3] + 8) && t[i] != 0)
			return 103;
	/* Physical zero fill only: no comptime undefined-value read is performed. */
	for (uintptr_t i = 0; i < m[1]; i++)
		if (u[i] != 0)
			return 104;
	if (*e != 0)
		return 105;
	const unsigned char *pointer;
	memcpy(&pointer, b + m[4], sizeof pointer);
	if (pointer != t + m[2] || *pointer != 23)
		return 106;
	memcpy(&pointer, b + m[5], sizeof pointer);
	if (pointer != t + m[3])
		return 107;
	memcpy(&pointer, b + m[6], sizeof pointer);
	if (pointer != a + m[7] + m[8])
		return 108;
	uint32_t inner;
	memcpy(&inner, pointer, sizeof inner);
	if (inner != 202)
		return 109;
	for (uintptr_t i = 0; i < m[1]; i++)
		if (b[m[9] + i] != 0)
			return 110;
	return 0;
}

/* Raw byte interfaces deliberately avoid the C ordinary-struct ABI. */
uint8_t check_storage(void *root, void *tail, void *unknown, void *empty,
	const void *bundle, const uintptr_t *rm, const uintptr_t *em);

uint8_t
check_storage(void *root, void *tail, void *unknown, void *empty,
	const void *bundle, const uintptr_t *rm, const uintptr_t *em)
{
	uint8_t result = check_backing(root, rm);
	if (result != 0)
		return result;
	result = check_edges(tail, bundle, unknown, empty,
		(const unsigned char *)root + rm[8], em);
	if (result != 0)
		return result;
	/* The existing checks validated all metadata extents and original bytes. */
	unsigned char *r = root, *t = tail, *u = unknown, *e = empty;
	uint32_t row = 777;
	uint64_t word = 888, defined = 999;
	const unsigned char *pointer = t + em[2];
	r[rm[5]] = 33;
	memcpy(r + rm[8] + rm[2] + rm[3], &row, sizeof row);
	memcpy(r + rm[10], &pointer, sizeof pointer);
	memcpy(t + em[3], &word, sizeof word);
	memcpy(u + em[3], &defined, sizeof defined);
	/* The anchor byte exists physically; it is not a logical Empty field. */
	volatile unsigned char *anchor = e;
	*anchor = 77;
	if (*anchor != 77)
		return 123;
	*anchor = 0;
	return 0;
}
