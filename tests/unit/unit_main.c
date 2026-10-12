/* Unit tests for support code and the lexer. Built with ASan+UBSan by `make unit-test`. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "arena.h"
#include "lexer.h"

static uint32_t failures = 0;
static uint32_t checks = 0;

#define CHECK(condition) check_impl((condition), #condition, __FILE__, __LINE__)

static void
check_impl(bool condition, const char *text, const char *file, int line)
{
	checks = checks + 1;
	if (condition)
		return;
	failures = failures + 1;
	fprintf(stderr, "%s:%d: check failed: %s\n", file, line, text);
}

static void
test_arena_alignment(void)
{
	Arena arena;
	arena_init(&arena);
	for (size_t align = 1; align <= 4096; align = align * 2) {
		unsigned char *a = arena_alloc(&arena, 3, 1);
		unsigned char *b = arena_alloc(&arena, 17, align);
		CHECK(((uintptr_t)b % align) == 0);
		a[0] = 1;
		a[2] = 2;
		memset(b, 0xAB, 17);
	}
	arena_free(&arena);
}

static void
test_arena_large_and_many(void)
{
	Arena arena;
	arena_init(&arena);
	unsigned char *big = arena_alloc(&arena, (size_t)5 << 20, 16);
	memset(big, 1, (size_t)5 << 20);
	CHECK(big[((size_t)5 << 20) - 1] == 1);
	for (uint32_t i = 0; i < 100000; i++) {
		uint32_t *value = arena_alloc(&arena, sizeof(uint32_t), alignof(uint32_t));
		*value = i;
	}
	unsigned char *zeroed = arena_zalloc(&arena, 64, 8);
	bool all_zero = true;
	for (uint32_t i = 0; i < 64; i++) {
		if (zeroed[i] != 0)
			all_zero = false;
	}
	CHECK(all_zero);
	arena_free(&arena);
	arena_free(&arena);
}

static void
test_arena_helpers(void)
{
	Arena arena;
	arena_init(&arena);
	uint32_t *items = NULL;
	uint32_t count = 0;
	uint32_t capacity = 0;
	for (uint32_t i = 0; i < 1000; i++) {
		if (count == capacity) {
			uint32_t new_capacity = capacity == 0 ? 4 : capacity * 2;
			items = arena_grow_array(&arena, items, count, new_capacity, sizeof(uint32_t), alignof(uint32_t));
			capacity = new_capacity;
		}
		items[count] = i;
		count = count + 1;
	}
	bool ordered = true;
	for (uint32_t i = 0; i < count; i++) {
		if (items[i] != i)
			ordered = false;
	}
	CHECK(ordered);

	char *copy = arena_strndup(&arena, "hello world", 5);
	CHECK(strcmp(copy, "hello") == 0);
	char *formatted = arena_sprintf(&arena, "%s-%u", "x", 42u);
	CHECK(strcmp(formatted, "x-42") == 0);
	char *empty = arena_strndup(&arena, "", 0);
	CHECK(empty[0] == '\0');
	arena_free(&arena);
}

static Token
lex_one(const char *text, Token *second)
{
	Lexer lexer;
	lexer_init(&lexer, text, (uint32_t)strlen(text));
	Token first = lexer_next(&lexer);
	*second = lexer_next(&lexer);
	return first;
}

/* Every token with a fixed lexeme must lex back to itself, followed by EOF. */
static void
test_lexeme_round_trip(void)
{
	for (uint32_t i = 0; i < TK_COUNT; i++) {
		TokenKind kind = (TokenKind)i;
		const char *lexeme = token_kind_lexeme(kind);
		if (lexeme == NULL)
			continue;
		Token second;
		Token first = lex_one(lexeme, &second);
		if (first.kind != kind)
			fprintf(stderr, "lexeme '%s' gave %s\n", lexeme, token_kind_name(first.kind));
		CHECK(first.kind == kind);
		CHECK(first.start == 0);
		CHECK(first.end == strlen(lexeme));
		CHECK(second.kind == TK_EOF);
	}
}

static void
test_keyword_lookup(void)
{
	uint32_t keyword_count = 0;
	for (uint32_t i = 0; i < TK_COUNT; i++) {
		TokenKind kind = (TokenKind)i;
		if (!token_kind_is_keyword(kind))
			continue;
		const char *text = token_kind_lexeme(kind);
		CHECK(lexer_keyword_lookup(text, (uint32_t)strlen(text)) == kind);
		keyword_count = keyword_count + 1;
	}
	CHECK(keyword_count == 46);
	CHECK(lexer_keyword_lookup("pubx", 4) == TK_IDENTIFIER);
	CHECK(lexer_keyword_lookup("pu", 2) == TK_IDENTIFIER);
	CHECK(lexer_keyword_lookup("async", 5) == TK_IDENTIFIER);
	CHECK(lexer_keyword_lookup("await", 5) == TK_IDENTIFIER);
	CHECK(lexer_keyword_lookup("usingnamespace", 14) == TK_IDENTIFIER);
}

static void
test_eof_is_sticky(void)
{
	Lexer lexer;
	lexer_init(&lexer, "x", 1);
	CHECK(lexer_next(&lexer).kind == TK_IDENTIFIER);
	for (uint32_t i = 0; i < 4; i++) {
		Token token = lexer_next(&lexer);
		CHECK(token.kind == TK_EOF);
		CHECK(token.start == 1 && token.end == 1);
	}
}

static void
test_bom_and_embedded_nul(void)
{
	const char with_bom[] = "\xEF\xBB\xBF" "const";
	Lexer lexer;
	lexer_init(&lexer, with_bom, 8);
	Token token = lexer_next(&lexer);
	CHECK(token.kind == TK_CONST);
	CHECK(token.start == 3);

	const char with_nul[] = "a\0b\nc";
	lexer_init(&lexer, with_nul, 5);
	CHECK(lexer_next(&lexer).kind == TK_IDENTIFIER);
	CHECK(lexer_next(&lexer).kind == TK_INVALID);
	CHECK(lexer_next(&lexer).kind == TK_IDENTIFIER);
	CHECK(lexer_next(&lexer).kind == TK_EOF);
}

int
main(void)
{
	test_arena_alignment();
	test_arena_large_and_many();
	test_arena_helpers();
	test_lexeme_round_trip();
	test_keyword_lookup();
	test_eof_is_sticky();
	test_bom_and_embedded_nul();

	printf("unit: %u checks, %u failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
