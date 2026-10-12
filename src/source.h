#ifndef SZC_SOURCE_H
#define SZC_SOURCE_H

#include <stdbool.h>
#include <stdint.h>

#include "arena.h"

/* A loaded source file. `text` is always NUL-terminated at text[length]. */
typedef struct SourceFile {
	const char *path;
	const char *text;
	uint32_t   length;
	uint32_t   *line_starts;	/* byte offset of the start of each line */
	uint32_t   line_count;
} SourceFile;

typedef uint32_t FileId;

typedef struct SourceTable {
	Arena      *arena;
	SourceFile *files;
	uint32_t   count;
	uint32_t   capacity;
} SourceTable;

typedef struct LineColumn {
	uint32_t line;
	uint32_t column;
} LineColumn;

void        source_table_init(SourceTable *table, Arena *arena);
bool        source_load_file(SourceTable *table, const char *path, FileId *out_id);
FileId      source_add_memory(SourceTable *table, const char *path, const char *bytes, uint32_t length);
SourceFile *source_get(SourceTable *table, FileId id);
LineColumn  source_line_column(const SourceFile *file, uint32_t offset);

#endif
