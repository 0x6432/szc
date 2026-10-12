#ifndef SZC_INTERN_H
#define SZC_INTERN_H

#include <stdbool.h>
#include <stdint.h>

#include "arena.h"

/*
 * String interning: equal byte strings get the same `StringId`, so later stages
 * compare names by id. Id 0 is reserved for "no string".
 */

typedef uint32_t StringId;

#define STRING_NONE ((StringId)0)

typedef struct InternEntry {
	const char *text;	/* NUL-terminated copy owned by the arena */
	uint32_t   length;
	uint32_t   hash;
} InternEntry;

typedef struct Interner {
	Arena       *arena;
	InternEntry *entries;	/* indexed by StringId; entries[0] is unused */
	uint32_t    count;
	uint32_t    capacity;
	uint32_t    *slots;	/* open-addressing table of ids; 0 = empty */
	uint32_t    slot_count;	/* power of two */
} Interner;

void        intern_init(Interner *interner, Arena *arena);
StringId    intern(Interner *interner, const char *text, uint32_t length);
StringId    intern_cstr(Interner *interner, const char *text);
const char *intern_text(const Interner *interner, StringId id);
uint32_t    intern_length(const Interner *interner, StringId id);

#endif
