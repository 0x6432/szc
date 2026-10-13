#include "diag.h"

#include <stdarg.h>
#include <stdlib.h>

void
diag_init(Diag *diag, Arena *arena, SourceTable *sources)
{
	diag->arena = arena;
	diag->sources = sources;
	diag->items = NULL;
	diag->count = 0;
	diag->capacity = 0;
	diag->error_count = 0;
}

static void
append_diagnostic(Diag *diag, DiagLevel level, FileId file, uint32_t offset, const char *message, size_t length)
{
	if (diag->count == diag->capacity) {
		uint32_t new_capacity = diag->capacity == 0 ? 16 : diag->capacity * 2;
		diag->items = arena_grow_array(diag->arena, diag->items, diag->count, new_capacity, sizeof(Diagnostic), alignof(Diagnostic));
		diag->capacity = new_capacity;
	}

	Diagnostic *item = &diag->items[diag->count];
	item->level = level;
	item->file = file;
	item->offset = offset;
	item->message = message;
	item->message_length = length;
	diag->count = diag->count + 1;
	if (level == DIAG_ERROR)
		diag->error_count = diag->error_count + 1;
}

void
diag_report_bytes(Diag *diag, DiagLevel level, FileId file, uint32_t offset, const char *message, size_t length)
{
	append_diagnostic(diag, level, file, offset, arena_strndup(diag->arena, message, length), length);
}

void
diag_report(Diag *diag, DiagLevel level, FileId file, uint32_t offset, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	int length = vsnprintf(NULL, 0, format, args);
	va_end(args);
	if (length < 0)
		length = 0;

	char *message = arena_alloc(diag->arena, (size_t)length + 1, 1);
	va_start(args, format);
	vsnprintf(message, (size_t)length + 1, format, args);
	va_end(args);

	append_diagnostic(diag, level, file, offset, message, (size_t)length);
}

bool
diag_has_errors(const Diag *diag)
{
	return diag->error_count != 0;
}

static const char *
level_name(DiagLevel level)
{
	switch (level) {
	case DIAG_ERROR:
		return "error";
	case DIAG_NOTE:
		return "note";
	}
	return "unknown";
}

/* Prints the source line containing `offset` followed by a caret under the column. */
static void
print_source_line(const SourceFile *file, uint32_t offset, FILE *out)
{
	if (offset > file->length)
		offset = file->length;

	uint32_t line_start = offset;
	while (line_start > 0 && file->text[line_start - 1] != '\n')
		line_start = line_start - 1;

	uint32_t line_end = offset;
	while (line_end < file->length && file->text[line_end] != '\n')
		line_end = line_end + 1;

	fprintf(out, "%.*s\n", (int)(line_end - line_start), file->text + line_start);
	for (uint32_t i = line_start; i < offset; i++) {
		if (file->text[i] == '\t')
			fputc('\t', out);
		else
			fputc(' ', out);
	}
	fputs("^\n", out);
}

void
diag_print(const Diag *diag, FILE *out)
{
	for (uint32_t i = 0; i < diag->count; i++) {
		const Diagnostic *item = &diag->items[i];
		SourceFile *file = source_get(diag->sources, item->file);
		LineColumn position = source_line_column(file, item->offset);

		fprintf(out, "%s:%u:%u: %s: ", file->path, position.line, position.column, level_name(item->level));
		fwrite(item->message, 1, item->message_length, out);
		fputc('\n', out);
		print_source_line(file, item->offset, out);
	}
}
