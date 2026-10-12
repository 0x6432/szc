#include "intern.h"

#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

/* FNV-1a */
static uint32_t
hash_bytes(const char *text, uint32_t length)
{
	uint32_t hash = 2166136261u;
	for (uint32_t i = 0; i < length; i++) {
		hash = hash ^ (uint8_t)text[i];
		hash = hash * 16777619u;
	}
	return hash;
}

void
intern_init(Interner *interner, Arena *arena)
{
	interner->arena = arena;
	interner->capacity = 256;
	interner->entries = arena_zalloc(arena, interner->capacity * sizeof(InternEntry), alignof(InternEntry));
	interner->count = 1;
	interner->slot_count = 512;
	interner->slots = arena_zalloc(arena, interner->slot_count * sizeof(uint32_t), alignof(uint32_t));
}

static void
insert_slot(uint32_t *slots, uint32_t slot_count, uint32_t hash, StringId id)
{
	uint32_t mask = slot_count - 1;
	uint32_t index = hash & mask;
	while (slots[index] != 0)
		index = (index + 1) & mask;
	slots[index] = id;
}

static void
grow_slots(Interner *interner)
{
	uint32_t new_count = interner->slot_count * 2;
	uint32_t *new_slots = arena_zalloc(interner->arena, new_count * sizeof(uint32_t), alignof(uint32_t));
	for (StringId id = 1; id < interner->count; id++)
		insert_slot(new_slots, new_count, interner->entries[id].hash, id);
	interner->slots = new_slots;
	interner->slot_count = new_count;
}

StringId
intern(Interner *interner, const char *text, uint32_t length)
{
	uint32_t hash = hash_bytes(text, length);
	uint32_t mask = interner->slot_count - 1;
	for (uint32_t index = hash & mask; interner->slots[index] != 0; index = (index + 1) & mask) {
		InternEntry *entry = &interner->entries[interner->slots[index]];
		if (entry->hash == hash && entry->length == length && memcmp(entry->text, text, length) == 0)
			return interner->slots[index];
	}

	if (interner->count == interner->capacity) {
		uint32_t new_capacity = interner->capacity * 2;
		interner->entries = arena_grow_array(interner->arena, interner->entries, interner->count, new_capacity, sizeof(InternEntry), alignof(InternEntry));
		interner->capacity = new_capacity;
	}
	StringId id = interner->count;
	interner->entries[id].text = arena_strndup(interner->arena, text, length);
	interner->entries[id].length = length;
	interner->entries[id].hash = hash;
	interner->count = interner->count + 1;

	/* Keep the table at most half full. */
	if (interner->count * 2 > interner->slot_count)
		grow_slots(interner);
	else
		insert_slot(interner->slots, interner->slot_count, hash, id);
	return id;
}

StringId
intern_cstr(Interner *interner, const char *text)
{
	return intern(interner, text, (uint32_t)strlen(text));
}

const char *
intern_text(const Interner *interner, StringId id)
{
	if (id == STRING_NONE || id >= interner->count)
		return "";
	return interner->entries[id].text;
}

uint32_t
intern_length(const Interner *interner, StringId id)
{
	if (id == STRING_NONE || id >= interner->count)
		return 0;
	return interner->entries[id].length;
}
