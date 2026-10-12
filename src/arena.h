#ifndef SZC_ARENA_H
#define SZC_ARENA_H

#include <stdalign.h>
#include <stddef.h>

/*
 * Arena allocator.
 *
 * Memory is taken from large chunks and never freed one allocation at a time.
 * arena_free() releases every chunk at once. Running out of memory is fatal.
 * Under AddressSanitizer the unused part of each chunk and a red zone after every
 * allocation are poisoned, so overflows are still reported.
 */

typedef struct ArenaChunk ArenaChunk;

typedef struct Arena {
	ArenaChunk *head;
	size_t     total_allocated;
} Arena;

void  arena_init(Arena *arena);
void  arena_free(Arena *arena);
void *arena_alloc(Arena *arena, size_t size, size_t align);
void *arena_zalloc(Arena *arena, size_t size, size_t align);
void *arena_grow_array(Arena *arena, void *old, size_t old_count, size_t new_count, size_t elem_size, size_t align);
char *arena_strndup(Arena *arena, const char *text, size_t length);
char *arena_sprintf(Arena *arena, const char *format, ...);

#define ARENA_NEW(arena, Type) ((Type *)arena_zalloc((arena), sizeof(Type), alignof(Type)))
#define ARENA_ARRAY(arena, Type, count) ((Type *)arena_zalloc((arena), sizeof(Type) * (count), alignof(Type)))

#endif
