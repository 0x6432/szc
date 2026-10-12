/*
 * Parser fuzz target (libFuzzer entry point). Any input must parse without
 * crashing, leaking or looping forever. Errors are expected and fine.
 */
#include <stdint.h>
#include <stdlib.h>

#include "arena.h"
#include "diag.h"
#include "parser.h"
#include "source.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size > 1u << 20)
		return 0;

	Arena arena;
	arena_init(&arena);
	SourceTable sources;
	source_table_init(&sources, &arena);
	Diag diag;
	diag_init(&diag, &arena, &sources);

	FileId file = source_add_memory(&sources, "fuzz.zig", (const char *)data, (uint32_t)size);
	AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
	if (root == NULL)
		abort();
	arena_free(&arena);
	return 0;
}
