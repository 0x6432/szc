#include "source.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SOURCE_SIZE ((uint32_t)0x7fffffff)

void
source_table_init(SourceTable *table, Arena *arena)
{
	table->arena = arena;
	table->files = NULL;
	table->count = 0;
	table->capacity = 0;
}

static FileId
add_file(SourceTable *table, const char *path, const char *text, uint32_t length)
{
	if (table->count == table->capacity) {
		uint32_t new_capacity = table->capacity == 0 ? 8 : table->capacity * 2;
		table->files = arena_grow_array(table->arena, table->files, table->count, new_capacity, sizeof(SourceFile), alignof(SourceFile));
		table->capacity = new_capacity;
	}

	FileId id = table->count;
	table->files[id].path = path;
	table->files[id].text = text;
	table->files[id].length = length;
	table->count = table->count + 1;
	return id;
}

FileId
source_add_memory(SourceTable *table, const char *path, const char *bytes, uint32_t length)
{
	char *text = arena_strndup(table->arena, bytes, length);
	char *path_copy = arena_strndup(table->arena, path, strlen(path));
	return add_file(table, path_copy, text, length);
}

bool
source_load_file(SourceTable *table, const char *path, FileId *out_id)
{
	FILE *file = fopen(path, "rb");
	if (file == NULL) {
		fprintf(stderr, "szc: cannot open '%s': %s\n", path, strerror(errno));
		return false;
	}

	Arena *arena = table->arena;
	size_t capacity = 4096;
	size_t length = 0;
	char *buffer = arena_alloc(arena, capacity, 1);

	for (;;) {
		if (length == capacity) {
			if (capacity >= MAX_SOURCE_SIZE) {
				fprintf(stderr, "szc: '%s' is too large\n", path);
				fclose(file);
				return false;
			}
			buffer = arena_grow_array(arena, buffer, length, capacity * 2, 1, 1);
			capacity = capacity * 2;
		}
		size_t got = fread(buffer + length, 1, capacity - length, file);
		length = length + got;
		if (got == 0)
			break;
	}

	bool failed = ferror(file) != 0;
	fclose(file);
	if (failed) {
		fprintf(stderr, "szc: error reading '%s'\n", path);
		return false;
	}
	if (length > MAX_SOURCE_SIZE) {
		fprintf(stderr, "szc: '%s' is too large\n", path);
		return false;
	}

	char *text = arena_strndup(arena, buffer, length);
	char *path_copy = arena_strndup(arena, path, strlen(path));
	*out_id = add_file(table, path_copy, text, (uint32_t)length);
	return true;
}

SourceFile *
source_get(SourceTable *table, FileId id)
{
	if (id >= table->count) {
		fprintf(stderr, "szc: internal error: bad file id %u\n", id);
		abort();
	}
	return &table->files[id];
}

LineColumn
source_line_column(const SourceFile *file, uint32_t offset)
{
	LineColumn result = {1, 1};

	if (offset > file->length)
		offset = file->length;
	for (uint32_t i = 0; i < offset; i++) {
		if (file->text[i] == '\n') {
			result.line = result.line + 1;
			result.column = 1;
		} else {
			result.column = result.column + 1;
		}
	}
	return result;
}
