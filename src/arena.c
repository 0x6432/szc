#include "arena.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__SANITIZE_ADDRESS__)
#define SZC_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SZC_ASAN 1
#endif
#endif

#ifdef SZC_ASAN
#include <sanitizer/asan_interface.h>
#define POISON(address, size)   ASAN_POISON_MEMORY_REGION((address), (size))
#define UNPOISON(address, size) ASAN_UNPOISON_MEMORY_REGION((address), (size))
#define RED_ZONE_SIZE 16
#else
#define POISON(address, size)   ((void)(address), (void)(size))
#define UNPOISON(address, size) ((void)(address), (void)(size))
#define RED_ZONE_SIZE 0
#endif

#define DEFAULT_CHUNK_SIZE ((size_t)1 << 20)

struct ArenaChunk {
	ArenaChunk     *next;
	size_t         capacity;
	size_t         used;
	max_align_t    data[];
};

static void
fatal_out_of_memory(size_t size)
{
	fprintf(stderr, "szc: out of memory (requested %zu bytes)\n", size);
	exit(2);
}

static ArenaChunk *
chunk_new(size_t min_capacity)
{
	size_t capacity = DEFAULT_CHUNK_SIZE;

	while (capacity < min_capacity) {
		if (capacity > SIZE_MAX / 2)
			fatal_out_of_memory(min_capacity);
		capacity = capacity * 2;
	}
	if (capacity > SIZE_MAX - sizeof(ArenaChunk))
		fatal_out_of_memory(min_capacity);

	ArenaChunk *chunk = malloc(sizeof(ArenaChunk) + capacity);
	if (chunk == NULL)
		fatal_out_of_memory(capacity);
	chunk->next = NULL;
	chunk->capacity = capacity;
	chunk->used = 0;
	POISON(chunk->data, capacity);
	return chunk;
}

void
arena_init(Arena *arena)
{
	arena->head = NULL;
	arena->total_allocated = 0;
}

void
arena_free(Arena *arena)
{
	ArenaChunk *chunk = arena->head;

	while (chunk != NULL) {
		ArenaChunk *next = chunk->next;
		UNPOISON(chunk->data, chunk->capacity);
		free(chunk);
		chunk = next;
	}
	arena->head = NULL;
	arena->total_allocated = 0;
}

static bool
is_power_of_two(size_t value)
{
	return value != 0 && (value & (value - 1)) == 0;
}

/* Returns the offset inside the chunk where an allocation of `size` bytes aligned to `align` fits, or SIZE_MAX. */
static size_t
chunk_fit(ArenaChunk *chunk, size_t size, size_t align)
{
	uintptr_t base = (uintptr_t)chunk->data;
	uintptr_t current = base + chunk->used;
	uintptr_t aligned = (current + (align - 1)) & ~((uintptr_t)align - 1);
	size_t offset = (size_t)(aligned - base);

	if (offset > chunk->capacity)
		return SIZE_MAX;
	if (size > chunk->capacity - offset)
		return SIZE_MAX;
	return offset;
}

void *
arena_alloc(Arena *arena, size_t size, size_t align)
{
	if (!is_power_of_two(align)) {
		fprintf(stderr, "szc: internal error: arena alignment %zu is not a power of two\n", align);
		abort();
	}
	if (size == 0)
		size = 1;
	if (size > SIZE_MAX - RED_ZONE_SIZE - align)
		fatal_out_of_memory(size);

	size_t needed = size + RED_ZONE_SIZE;
	size_t offset = SIZE_MAX;

	if (arena->head != NULL)
		offset = chunk_fit(arena->head, needed, align);
	if (offset == SIZE_MAX) {
		ArenaChunk *chunk = chunk_new(needed + align);
		chunk->next = arena->head;
		arena->head = chunk;
		offset = chunk_fit(chunk, needed, align);
		if (offset == SIZE_MAX)
			fatal_out_of_memory(size);
	}

	ArenaChunk *chunk = arena->head;
	unsigned char *result = (unsigned char *)chunk->data + offset;
	chunk->used = offset + needed;
	arena->total_allocated += size;
	UNPOISON(result, size);
	return result;
}

void *
arena_zalloc(Arena *arena, size_t size, size_t align)
{
	void *result = arena_alloc(arena, size, align);
	memset(result, 0, size == 0 ? 1 : size);
	return result;
}

void *
arena_grow_array(Arena *arena, void *old, size_t old_count, size_t new_count, size_t elem_size, size_t align)
{
	if (elem_size != 0 && new_count > SIZE_MAX / elem_size)
		fatal_out_of_memory(SIZE_MAX);

	void *result = arena_alloc(arena, new_count * elem_size, align);
	if (old != NULL && old_count != 0)
		memcpy(result, old, old_count * elem_size);
	return result;
}

char *
arena_strndup(Arena *arena, const char *text, size_t length)
{
	if (length == SIZE_MAX)
		fatal_out_of_memory(length);

	char *result = arena_alloc(arena, length + 1, 1);
	if (length != 0)
		memcpy(result, text, length);
	result[length] = '\0';
	return result;
}

char *
arena_sprintf(Arena *arena, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	int length = vsnprintf(NULL, 0, format, args);
	va_end(args);
	if (length < 0) {
		fprintf(stderr, "szc: internal error: bad format string\n");
		abort();
	}

	char *result = arena_alloc(arena, (size_t)length + 1, 1);
	va_start(args, format);
	vsnprintf(result, (size_t)length + 1, format, args);
	va_end(args);
	return result;
}
