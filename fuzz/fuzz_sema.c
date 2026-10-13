/*
 * sema fuzz target (libFuzzer entry point). Inputs that parse and lower to IR
 * are analysed as an executable and the typed IR is dumped; this must not
 * crash, leak or loop forever.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "arena.h"
#include "diag.h"
#include "intern.h"
#include "ir.h"
#include "parser.h"
#include "sema.h"
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
	Interner interner;
	intern_init(&interner, &arena);

	FileId file = source_add_memory(&sources, "fuzz.zig", (const char *)data, (uint32_t)size);
	AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
	if (!diag_has_errors(&diag)) {
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		if (!diag_has_errors(&diag)) {
			Sema *sema = sema_create(&arena, &diag, &interner, &sources);
			sema_analyze_executable(sema, file, ir);
			FILE *null_out = fopen("/dev/null", "w");
			if (null_out != NULL) {
				sema_dump(null_out, sema);
				fclose(null_out);
			}
		}
	}
	arena_free(&arena);
	return 0;
}
