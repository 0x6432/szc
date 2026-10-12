/*
 * Lexer fuzz target (libFuzzer entry point). Checks the properties that
 * tokenizer.zig's "fuzzable properties upheld" test checks:
 *   - tokens are in order, do not overlap, and stay inside the input
 *   - only EOF may be empty, and EOF is at the end of the input
 *   - EOF is always reached, and the lexer always makes progress
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"

static void
require(bool condition)
{
	if (!condition)
		abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size > 1u << 20)
		return 0;

	char *source = malloc(size + 1);
	require(source != NULL);
	if (size != 0)
		memcpy(source, data, size);
	source[size] = '\0';

	Lexer lexer;
	lexer_init(&lexer, source, (uint32_t)size);
	uint32_t previous_end = lexer.index;
	size_t steps = 0;

	for (;;) {
		Token token = lexer_next(&lexer);
		require(token.kind < TK_COUNT);
		require(token.start <= token.end);
		require(token.end <= size);
		require(token.start >= previous_end);
		if (token.kind == TK_EOF) {
			require(token.start == size);
			require(token.end == size);
			break;
		}
		require(token.end > token.start);
		previous_end = token.end;
		steps = steps + 1;
		require(steps <= size + 1);
	}

	free(source);
	return 0;
}
