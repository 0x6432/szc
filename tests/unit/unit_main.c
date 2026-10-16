/* Unit tests for support code and the lexer. Built with ASan+UBSan by `make unit-test`. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "arena.h"
#include "comptime.h"
#include "diag.h"
#include "lexer.h"
#include "parser.h"
#include "ir.h"
#include "sema.h"

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

/* ---------- Big integers, checked against 128-bit arithmetic ---------- */

__extension__ typedef __int128 Int128;

static BigInt
bigint_from_i64(Arena *arena, int64_t value)
{
	uint64_t magnitude = value < 0 ? (uint64_t)0 - (uint64_t)value : (uint64_t)value;
	BigInt result = bigint_from_u64(arena, magnitude);
	if (value < 0)
		result = bigint_negate(arena, &result);
	return result;
}

/* Converts a big integer that fits in 127 bits. */
static Int128
bigint_to_i128(const BigInt *a)
{
	Int128 magnitude = 0;
	for (uint32_t i = a->limb_count; i > 0; i--)
		magnitude = (magnitude << 32) | a->limbs[i - 1];
	return a->negative ? -magnitude : magnitude;
}

static uint64_t
next_random(uint64_t *state)
{
	*state = *state * 6364136223846793005u + 1442695040888963407u;
	return *state >> 11;
}

static void
test_bigint_random(void)
{
	Arena arena;
	arena_init(&arena);
	uint64_t state = 12345;
	uint32_t mismatches = 0;
	for (uint32_t round = 0; round < 2000; round++) {
		int64_t x = (int64_t)(next_random(&state) >> (next_random(&state) % 52)) - (int64_t)(next_random(&state) >> 20);
		int64_t y = (int64_t)(next_random(&state) >> (next_random(&state) % 52)) - (int64_t)(next_random(&state) >> 20);
		BigInt a = bigint_from_i64(&arena, x);
		BigInt b = bigint_from_i64(&arena, y);
		BigInt r = bigint_add(&arena, &a, &b);
		mismatches += bigint_to_i128(&r) != (Int128)x + y;
		r = bigint_sub(&arena, &a, &b);
		mismatches += bigint_to_i128(&r) != (Int128)x - y;
		r = bigint_abs(&arena, &a);
		mismatches += bigint_to_i128(&r) != (x < 0 ? -(Int128)x : (Int128)x);
		r = bigint_mul(&arena, &a, &b);
		mismatches += bigint_to_i128(&r) != (Int128)x * y;
		BigInt q;
		BigInt m;
		if (y != 0 && bigint_divmod_trunc(&arena, &a, &b, &q, &m)) {
			mismatches += bigint_to_i128(&q) != (Int128)x / y;
			mismatches += bigint_to_i128(&m) != (Int128)x % y;
			Int128 quotient = (Int128)x / y;
			Int128 remainder = (Int128)x % y;
			if (remainder != 0 && (x < 0) != (y < 0)) {
				quotient -= 1;
				remainder += y;
			}
			mismatches += !bigint_divmod(&arena, &a, &b, BIGINT_DIV_FLOOR, &q, &m);
			mismatches += bigint_to_i128(&q) != quotient;
			mismatches += bigint_to_i128(&m) != remainder;
			BigInt alias_a = a;
			BigInt alias_b = b;
			mismatches += !bigint_divmod(&arena, &alias_a, &alias_b, BIGINT_DIV_FLOOR, &alias_a, &alias_b);
			mismatches += bigint_to_i128(&alias_a) != quotient;
			mismatches += bigint_to_i128(&alias_b) != remainder;
			quotient = (Int128)x / y;
			remainder = (Int128)x % y;
			if (remainder != 0 && (x < 0) == (y < 0)) {
				quotient += 1;
				remainder -= y;
			}
			mismatches += !bigint_divmod(&arena, &a, &b, BIGINT_DIV_CEIL, &q, &m);
			mismatches += bigint_to_i128(&q) != quotient;
			mismatches += bigint_to_i128(&m) != remainder;
		}
		uint32_t shift = (uint32_t)(next_random(&state) % 60);
		r = bigint_shl(&arena, &a, shift);
		mismatches += bigint_to_i128(&r) != (Int128)x * ((Int128)1 << shift);
		r = bigint_shr(&arena, &a, shift);
		mismatches += bigint_to_i128(&r) != (Int128)(x >> shift);
		r = bigint_bitop(&arena, &a, &b, BIGINT_AND);
		mismatches += bigint_to_i128(&r) != (Int128)(x & y);
		r = bigint_bitop(&arena, &a, &b, BIGINT_OR);
		mismatches += bigint_to_i128(&r) != (Int128)(x | y);
		r = bigint_bitop(&arena, &a, &b, BIGINT_XOR);
		mismatches += bigint_to_i128(&r) != (Int128)(x ^ y);
		r = bigint_not(&arena, &a);
		mismatches += bigint_to_i128(&r) != (Int128)~x;
		r = bigint_wrap(&arena, &a, true, 16);
		mismatches += bigint_to_i128(&r) != (Int128)(int16_t)(uint16_t)(uint64_t)x;
		r = bigint_wrap(&arena, &a, false, 32);
		mismatches += bigint_to_i128(&r) != (Int128)(uint32_t)(uint64_t)x;
		mismatches += bigint_fits(&a, true, 32) != (x >= INT32_MIN && x <= INT32_MAX);
		mismatches += bigint_fits(&a, false, 16) != (x >= 0 && x <= UINT16_MAX);
		char expected[32];
		snprintf(expected, sizeof(expected), "%lld", (long long)x);
		mismatches += strcmp(bigint_to_string(&arena, &a), expected) != 0;
		mismatches += bigint_compare(&a, &b) != (x < y ? -1 : x > y ? 1 : 0);
	}
	CHECK(mismatches == 0);
	arena_free(&arena);
}

static void
test_bigint_bit_counts(void)
{
	Arena arena;
	arena_init(&arena);
	uint64_t state = 998811;
	uint32_t mismatches = 0;
	for (uint32_t width = 0; width <= 64; width++) {
		for (uint32_t round = 0; round < 16; round++) {
			uint64_t raw = next_random(&state);
			if (width < 64)
				raw &= (UINT64_C(1) << width) - 1;
			uint32_t leading = 0;
			uint32_t trailing = 0;
			uint32_t population = 0;
			for (uint32_t i = 0; i < width; i++)
				population += (uint32_t)(raw >> i & 1);
			while (leading < width && (raw >> (width - leading - 1) & 1) == 0)
				leading++;
			while (trailing < width && (raw >> trailing & 1) == 0)
				trailing++;
			BigInt value = bigint_from_u64(&arena, raw);
			mismatches += bigint_bit_count(&arena, &value, width, BIGINT_CLZ) != leading;
			mismatches += bigint_bit_count(&arena, &value, width, BIGINT_CTZ) != trailing;
			mismatches += bigint_bit_count(&arena, &value, width, BIGINT_POP_COUNT) != population;
		}
	}
	CHECK(mismatches == 0);
	BigInt one = bigint_from_u64(&arena, 1);
	BigInt negative = bigint_negate(&arena, &one);
	BigInt zero = bigint_from_u64(&arena, 0);
	CHECK(bigint_bit_count(&arena, &negative, 257, BIGINT_CLZ) == 0);
	CHECK(bigint_bit_count(&arena, &negative, 257, BIGINT_CTZ) == 0);
	CHECK(bigint_bit_count(&arena, &negative, 257, BIGINT_POP_COUNT) == 257);
	CHECK(bigint_bit_count(&arena, &zero, 65535, BIGINT_CLZ) == 65535);
	CHECK(bigint_bit_count(&arena, &zero, 65535, BIGINT_CTZ) == 65535);
	CHECK(bigint_bit_count(&arena, &zero, 65535, BIGINT_POP_COUNT) == 0);
	BigInt high = bigint_shl(&arena, &one, 256);
	CHECK(bigint_bit_count(&arena, &high, 257, BIGINT_CLZ) == 0);
	CHECK(bigint_bit_count(&arena, &high, 257, BIGINT_CTZ) == 256);
	CHECK(bigint_bit_count(&arena, &high, 257, BIGINT_POP_COUNT) == 1);
	arena_free(&arena);
}

static void
test_bigint_transforms(void)
{
	Arena arena;
	arena_init(&arena);
	uint64_t state = 885522;
	uint32_t mismatches = 0;
	for (uint32_t width = 0; width <= 64; width++) {
		for (uint32_t round = 0; round < 16; round++) {
			uint64_t raw = next_random(&state);
			if (width < 64)
				raw &= (UINT64_C(1) << width) - 1;
			uint64_t expected = 0;
			for (uint32_t i = 0; i < width; i++)
				expected |= (raw >> i & 1) << (width - i - 1);
			BigInt value = bigint_from_u64(&arena, raw);
			BigInt actual = bigint_transform(&arena, &value, false, width, BIGINT_BIT_REVERSE);
			mismatches += bigint_to_i128(&actual) != (Int128)expected;
			BigInt restored = bigint_transform(&arena, &actual, false, width, BIGINT_BIT_REVERSE);
			mismatches += bigint_compare(&restored, &value) != 0;
			if (width % 8 == 0) {
				expected = 0;
				for (uint32_t i = 0; i < width; i += 8)
					expected |= (raw >> i & 255) << (width - i - 8);
				actual = bigint_transform(&arena, &value, false, width, BIGINT_BYTE_SWAP);
				mismatches += bigint_to_i128(&actual) != (Int128)expected;
				restored = bigint_transform(&arena, &actual, false, width, BIGINT_BYTE_SWAP);
				mismatches += bigint_compare(&restored, &value) != 0;
			}
		}
	}
	CHECK(mismatches == 0);
	BigInt one = bigint_from_u64(&arena, 1);
	BigInt negative = bigint_negate(&arena, &one);
	BigInt reversed = bigint_transform(&arena, &negative, true, 257, BIGINT_BIT_REVERSE);
	CHECK(bigint_compare(&reversed, &negative) == 0);
	BigInt high = bigint_shl(&arena, &one, 256);
	reversed = bigint_transform(&arena, &one, false, 257, BIGINT_BIT_REVERSE);
	CHECK(bigint_compare(&reversed, &high) == 0);
	BigInt swapped = bigint_transform(&arena, &negative, true, 512, BIGINT_BYTE_SWAP);
	CHECK(bigint_compare(&swapped, &negative) == 0);
	high = bigint_shl(&arena, &one, 504);
	swapped = bigint_transform(&arena, &one, false, 512, BIGINT_BYTE_SWAP);
	CHECK(bigint_compare(&swapped, &high) == 0);
	arena_free(&arena);
}

static void
test_bigint_div_zero(void)
{
	Arena arena;
	arena_init(&arena);
	BigInt one = bigint_from_u64(&arena, 1);
	BigInt zero = bigint_from_u64(&arena, 0);
	BigInt quotient = bigint_from_u64(&arena, 99);
	BigInt remainder = bigint_from_u64(&arena, 98);
	CHECK(!bigint_divmod(&arena, &one, &zero, BIGINT_DIV_CEIL, &quotient, &remainder));
	CHECK(bigint_to_i128(&quotient) == 99);
	CHECK(bigint_to_i128(&remainder) == 98);
	arena_free(&arena);
}


static void
test_byte_values(void)
{
	Arena arena;
	arena_init(&arena);
	ValueTable table;
	value_table_init(&table, &arena);
	uint8_t data[] = {'a', 0, 'b'};
	ValueId id = value_bytes(&table, 1, data, 3);
	CHECK(id == value_bytes(&table, 1, data, 3));
	CHECK(id != value_bytes(&table, 2, data, 3));
	CHECK(id != value_bytes(&table, 1, data, 1));
	data[2] = 'c';
	CHECK(value_get(&table, id)->as.bytes.data[2] == 'b');
	CHECK(value_get(&table, id)->as.bytes.data[3] == 0);
	CHECK(value_get(&table, id)->as.bytes.length == 3);
	ValueId empty = value_bytes(&table, 1, NULL, 0);
	CHECK(value_get(&table, empty)->as.bytes.data[0] == 0);
	CHECK(empty == value_bytes(&table, 1, data, 0));
	ValueId ids[512];
	for (uint32_t i = 0; i < 512; i++) {
		uint8_t bytes[] = {(uint8_t)i, (uint8_t)(i >> 8), 0, 255};
		ids[i] = value_bytes(&table, 1, bytes, 4);
	}
	bool preserved = true;
	for (uint32_t i = 0; i < 512; i++) {
		uint8_t bytes[] = {(uint8_t)i, (uint8_t)(i >> 8), 0, 255};
		if (value_bytes(&table, 1, bytes, 4) != ids[i])
			preserved = false;
	}
	CHECK(preserved);
	CHECK(table.capacity >= 1024 && table.slot_count >= 2048);
	arena_free(&arena);
}

static void
test_constant_references(void)
{
	Arena arena;
	arena_init(&arena);
	ValueTable table;
	value_table_init(&table, &arena);
	ValueId function = value_function(&table, 5, 1);
	CHECK(function == value_function(&table, 5, 1));
	CHECK(function != value_function(&table, 6, 1));
	CHECK(function != value_function(&table, 5, 2));
	CHECK(value_get(&table, function)->as.function == 1);
	ValueId function_pointer = value_function(&table, 6, 1);
	ValueId function_object = value_ref(&table, 7, function_pointer, 0, 0);
	CHECK(table.objects[value_get(&table, function_object)->as.object].value == function_pointer);
	ValueId n = value_int_u64(&table, 1, 42);
	ValueId ref = value_ref(&table, 2, n, 0, 0);
	CHECK(ref == value_ref(&table, 2, n, 0, 0));
	CHECK(ref != value_ref(&table, 3, n, 0, 0));
	CHECK(value_get(&table, ref)->as.object == value_get(&table, value_ref(&table, 3, n, 0, 0))->as.object);
	ValueId named = value_ref(&table, 2, n, 1, 0);
	CHECK(named != ref);
	CHECK(named != value_ref(&table, 2, n, 2, 0));
	CHECK(table.objects[value_get(&table, named)->as.object].value == n);
	ValueId aligned = value_ref(&table, 2, n, 0, 64);
	CHECK(aligned == value_ref(&table, 2, n, 0, 64));
	CHECK(aligned != ref);
	CHECK(aligned != value_ref(&table, 2, n, 0, 128));
	CHECK(table.objects[value_get(&table, aligned)->as.object].alignment == 64);
	ValueId refs[512];
	for (uint32_t i = 0; i < 512; i++)
		refs[i] = value_ref(&table, 2, value_int_u64(&table, 1, i), i + 3, 1u << (i % 13));
	bool preserved = true;
	for (uint32_t i = 0; i < 512; i++)
		if (refs[i] != value_ref(&table, 2, value_int_u64(&table, 1, i), i + 3, 1u << (i % 13)))
			preserved = false;
	CHECK(preserved);
	CHECK(table.object_capacity >= 1024 && table.object_slot_count >= 2048);
	ValueId outer = value_ref(&table, 4, ref, 0, 0);
	CHECK(table.objects[value_get(&table, outer)->as.object].value == ref);
	arena_free(&arena);
}

static void
test_global_references(void)
{
	Arena arena;
	arena_init(&arena);
	ValueTable table;
	value_table_init(&table, &arena);
	ValueId address = value_global(&table, 2, 1);
	CHECK(address == value_global(&table, 2, 1));
	CHECK(address != value_global(&table, 3, 1));
	CHECK(address != value_global(&table, 2, 2));
	CHECK(value_get(&table, address)->as.global == 1);
	ValueId ids[512];
	for (uint32_t i = 0; i < 512; i++)
		ids[i] = value_global(&table, 2, i + 3);
	bool preserved = true;
	for (uint32_t i = 0; i < 512; i++)
		if (ids[i] != value_global(&table, 2, i + 3))
			preserved = false;
	CHECK(preserved);
	CHECK(table.capacity >= 1024 && table.slot_count >= 2048);
	ValueId outer = value_ref(&table, 4, address, 0, 0);
	CHECK(table.objects[value_get(&table, outer)->as.object].value == address);
	arena_free(&arena);
}

static void
test_element_pointers(void)
{
	Arena arena;
	arena_init(&arena);
	ValueTable table;
	value_table_init(&table, &arena);
	ValueId n = value_int_u64(&table, 1, 42);
	ValueId root = value_ref(&table, 2, n, 1, 16);
	ValueId first = value_element_pointer(&table, 3, root, 1, 8, false);
	CHECK(first == value_element_pointer(&table, 3, root, 1, 8, false));
	CHECK(first != value_element_pointer(&table, 4, root, 1, 8, false));
	CHECK(first != value_element_pointer(&table, 3, root, 2, 8, false));
	CHECK(first != value_element_pointer(&table, 3, root, 1, 9, false));
	CHECK(first != value_element_pointer(&table, 3, root, 1, 8, true));
	ValueId nested = value_element_pointer(&table, 4, first, 2, 10, false);
	PointerAddress address = value_pointer_address(&table, nested);
	CHECK(address.root == root && address.offset == 10 && !address.logical);
	ValueId cast = value_pointer_cast(&table, 5, nested);
	address = value_pointer_address(&table, cast);
	CHECK(address.root == root && address.offset == 10 && !address.logical);
	CHECK(cast == value_pointer_cast(&table, 5, nested));
	CHECK(cast != value_pointer_cast(&table, 6, nested));
	address = value_pointer_address(&table, root);
	CHECK(address.root == root && address.offset == 0 && !address.logical);
	ValueId view = value_pointer_reinterpret(&table, 5, nested);
	CHECK(view == value_pointer_reinterpret(&table, 5, nested));
	CHECK(view != value_pointer_cast(&table, 5, nested));
	CHECK(value_get(&table, view)->kind == VALUE_REINTERPRET_PTR);
	address = value_pointer_address(&table, view);
	CHECK(address.root == root && address.offset == 10 && !address.logical);
	ValueId qualified_view = value_pointer_cast(&table, 6, view);
	CHECK(value_get(&table, qualified_view)->as.pointer == view);
	address = value_pointer_address(&table, qualified_view);
	CHECK(address.root == root && address.offset == 10 && !address.logical);
	ValueId selected_view = value_element_pointer(&table, 7, qualified_view, 1, 14, false);
	address = value_pointer_address(&table, selected_view);
	CHECK(address.root == root && address.offset == 14 && !address.logical);
	ValueId numeric = value_pointer_reinterpret(&table, 5, value_int_pointer(&table, 4, 17));
	CHECK(value_get(&table, numeric)->kind == VALUE_INT_PTR);
	CHECK(value_get(&table, numeric)->as.address == 17);
	ValueId function_view = value_pointer_reinterpret(&table, 5, value_function(&table, 4, 2));
	address = value_pointer_address(&table, function_view);
	CHECK(value_get(&table, address.root)->kind == VALUE_FUNCTION && address.offset == 0);
	ValueId logical = value_element_pointer(&table, 4, root, 1, 1, true);
	address = value_pointer_address(&table, logical);
	CHECK(address.root == root && address.offset == 1 && address.logical);
	ValueId field = value_slice_field_pointer(&table, 6, first, 16, true);
	CHECK(field == value_slice_field_pointer(&table, 6, first, 16, true));
	CHECK(field != value_slice_field_pointer(&table, 6, first, 16, false));
	CHECK(field != value_slice_field_pointer(&table, 7, first, 16, true));
	CHECK(field != value_slice_field_pointer(&table, 6, first, 17, true));
	CHECK(field != value_slice_field_pointer(&table, 6, root, 16, true));
	address = value_pointer_address(&table, field);
	CHECK(address.root == root && address.offset == 16 && !address.logical);
	ValueId field_cast = value_pointer_cast(&table, 7, field);
	address = value_pointer_address(&table, field_cast);
	CHECK(address.root == root && address.offset == 16 && !address.logical);
	ValueId field_element = value_element_pointer(&table, 8, field_cast, 1, 24, false);
	address = value_pointer_address(&table, field_element);
	CHECK(address.root == root && address.offset == 24 && !address.logical);
	ValueId ids[512];
	for (uint32_t i = 0; i < 512; i++)
		ids[i] = value_element_pointer(&table, 3, root, i, (uint64_t)i << 33, false);
	bool preserved = true;
	for (uint32_t i = 0; i < 512; i++)
		if (ids[i] != value_element_pointer(&table, 3, root, i, (uint64_t)i << 33, false))
			preserved = false;
	CHECK(preserved);
	CHECK(table.objects[value_get(&table, root)->as.object].value == n);
	arena_free(&arena);
}

static void
test_array_values(void)
{
	Arena arena;
	arena_init(&arena);
	ValueTable table;
	value_table_init(&table, &arena);
	ValueId first = value_int_u64(&table, 10, 7);
	ValueId second = value_int_u64(&table, 10, 9);
	ValueId items[] = {first, second};
	ValueId array = value_array(&table, 20, items, 2);
	CHECK(value_get(&table, array)->kind == VALUE_ARRAY);
	CHECK(value_get(&table, array)->as.array.count == 2);
	CHECK(value_get(&table, array)->as.array.items != items);
	items[0] = second;
	CHECK(value_get(&table, array)->as.array.items[0] == first);
	CHECK(value_array(&table, 20, items, 2) != array);
	items[0] = first;
	CHECK(value_array(&table, 20, items, 2) == array);
	CHECK(value_array(&table, 21, items, 2) != array);
	CHECK(value_array(&table, 20, items, 1) != array);
	ValueId empty = value_array(&table, 22, NULL, 0);
	CHECK(value_array(&table, 22, NULL, 0) == empty);
	CHECK(value_get(&table, empty)->as.array.count == 0);
	ValueId nested_items[] = {array, empty};
	ValueId nested = value_array(&table, 23, nested_items, 2);
	CHECK(value_get(&table, nested)->as.array.items[0] == array);
	ValueId missing_items[] = {value_undefined(&table, 10), first};
	ValueId missing = value_array(&table, 20, missing_items, 2);
	CHECK(value_get(&table, value_get(&table, missing)->as.array.items[0])->kind == VALUE_UNDEFINED);
	for (uint32_t i = 0; i < 2048; i++)
		value_int_u64(&table, 10, i);
	CHECK(value_array(&table, 20, items, 2) == array);
	CHECK(value_array(&table, 23, nested_items, 2) == nested);
	arena_free(&arena);
}

static void
test_slice_values(void)
{
	Arena arena;
	arena_init(&arena);
	ValueTable table;
	value_table_init(&table, &arena);
	ValueId pointer = value_undefined(&table, 10);
	ValueId length = value_int_u64(&table, 11, 3);
	ValueId slice = value_slice(&table, 12, pointer, length);
	CHECK(value_get(&table, slice)->kind == VALUE_SLICE);
	CHECK(value_get(&table, slice)->as.slice.pointer == pointer);
	CHECK(value_get(&table, slice)->as.slice.length == length);
	CHECK(value_slice(&table, 12, pointer, length) == slice);
	CHECK(value_slice(&table, 13, pointer, length) != slice);
	CHECK(value_slice(&table, 12, pointer, value_int_u64(&table, 11, 4)) != slice);
	for (uint32_t i = 0; i < 2048; i++)
		value_int_u64(&table, 11, i);
	CHECK(value_slice(&table, 12, pointer, length) == slice);
	arena_free(&arena);
}

static void
test_array_layout(void)
{
	Arena arena;
	arena_init(&arena);
	SourceTable sources;
	source_table_init(&sources, &arena);
	Diag diag;
	diag_init(&diag, &arena, &sources);
	Interner interner;
	intern_init(&interner, &arena);
	static const char source[] =
		"fn consume(a: *const [18446744073709551615]u0, b: *const [18446744073709551615]u8, "
		"c: *const [9223372036854775808]u16, d: *const [2][9223372036854775808]u16, "
		"e: *const [2][3]i33, f: *const [0]u64) void { "
		"_ = a.len; _ = b.len; _ = c.len; _ = d.len; _ = e.len; _ = f.len; } "
		"pub fn main() void { consume(undefined, undefined, undefined, undefined, undefined, undefined); }";
	FileId file = source_add_memory(&sources, "layout.zig", source, sizeof(source) - 1);
	AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
	CHECK(!diag_has_errors(&diag));
	IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
	CHECK(!diag_has_errors(&diag));
	Sema *sema = sema_create(&arena, &diag, &interner, &sources);
	CHECK(sema_analyze_executable(sema, file, ir));
	CHECK(sema_func_count(sema) == 3);
	TypeFn fn = sema_type(sema, sema_func(sema, 2)->type)->as.fn;
	CHECK(fn.param_count == 6);
	static const bool valid[] = {true, true, false, false, true, true};
	static const uint64_t sizes[] = {0, UINT64_MAX, 17, 17, 48, 0};
	static const uint32_t alignments[] = {1, 1, 2, 2, 8, 8};
	for (uint32_t i = 0; i < 6; i++) {
		TypeId array = sema_type(sema, fn.params[i])->as.pointer.child;
		uint64_t size = 17;
		CHECK(sema_type_size(sema, array, &size) == valid[i]);
		CHECK(size == sizes[i]);
		CHECK(sema_type_align(sema, array) == alignments[i]);
	}
	uint64_t size;
	CHECK(sema_type_size(sema, IR_REF_TYPE_U8, &size) && size == 1);
	CHECK(sema_type_size(sema, IR_REF_TYPE_I32, &size) && size == 4);
	CHECK(sema_type_size(sema, IR_REF_TYPE_U64, &size) && size == 8);
	CHECK(sema_type_size(sema, IR_REF_TYPE_BOOL, &size) && size == 1);
	arena_free(&arena);
}

static void
test_wide_integer_layout(void)
{
	Arena arena;
	arena_init(&arena);
	SourceTable sources;
	source_table_init(&sources, &arena);
	Diag diag;
	diag_init(&diag, &arena, &sources);
	Interner interner;
	intern_init(&interner, &arena);
	static const char source[] =
		"fn consume(a: *const u129, b: *const u257, c: *const u513, d: *const u65535) void { "
		"_ = a; _ = b; _ = c; _ = d; } "
		"pub fn main() void { consume(undefined,undefined,undefined,undefined); }";
	FileId file = source_add_memory(&sources, "wide_layout.zig", source, sizeof(source) - 1);
	AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
	CHECK(!diag_has_errors(&diag));
	IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
	CHECK(!diag_has_errors(&diag));
	Sema *sema = sema_create(&arena, &diag, &interner, &sources);
	CHECK(sema_analyze_executable(sema, file, ir));
	CHECK(sema_func_count(sema) == 3);
	TypeFn fn = sema_type(sema, sema_func(sema, 2)->type)->as.fn;
	CHECK(fn.param_count == 4);
	static const uint64_t sizes[] = {32, 48, 80, 8192};
	for (uint32_t i = 0; i < 4; i++) {
		TypeId type = sema_type(sema, fn.params[i])->as.pointer.child;
		uint64_t size;
		CHECK(sema_type_size(sema, type, &size) && size == sizes[i]);
		CHECK(sema_type_align(sema, type) == 16);
	}
	arena_free(&arena);
}

static void
test_sentinel_pointer_types(void)
{
	Arena arena;
	arena_init(&arena);
	SourceTable sources;
	source_table_init(&sources, &arena);
	Diag diag;
	diag_init(&diag, &arena, &sources);
	Interner interner;
	intern_init(&interner, &arena);
	static const char source[] =
		"fn consume(a: [*:0]const u8, b: [*:(1 - 1)]const u8, c: [*:1]const u8, "
		"d: [*]const u8, e: [*:false]bool, f: [*:true]bool) void { "
		"_ = a; _ = b; _ = c; _ = d; _ = e; _ = f; } "
		"pub fn main() void { consume(undefined,undefined,undefined,undefined,undefined,undefined); }";
	FileId file = source_add_memory(&sources, "sentinel_types.zig", source, sizeof(source) - 1);
	AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
	CHECK(!diag_has_errors(&diag));
	IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
	CHECK(!diag_has_errors(&diag));
	Sema *sema = sema_create(&arena, &diag, &interner, &sources);
	CHECK(sema_analyze_executable(sema, file, ir));
	CHECK(sema_func_count(sema) == 3);
	TypeFn fn = sema_type(sema, sema_func(sema, 2)->type)->as.fn;
	CHECK(fn.param_count == 6);
	CHECK(fn.params[0] == fn.params[1]);
	CHECK(fn.params[0] != fn.params[2] && fn.params[0] != fn.params[3]);
	CHECK(fn.params[4] != fn.params[5]);
	for (uint32_t i = 0; i < 6; i++) {
		uint64_t size;
		CHECK(sema_type_size(sema, fn.params[i], &size) && size == 8);
		CHECK(sema_type_align(sema, fn.params[i]) == 8);
		ValueId sentinel = sema_type(sema, fn.params[i])->as.pointer.sentinel;
		CHECK((sentinel == VALUE_NONE) == (i == 3));
	}
	arena_free(&arena);
}

static void
test_sentinel_slice_types(void)
{
	Arena arena;
	arena_init(&arena);
	SourceTable sources;
	source_table_init(&sources, &arena);
	Diag diag;
	diag_init(&diag, &arena, &sources);
	Interner interner;
	intern_init(&interner, &arena);
	static const char source[] =
		"fn consume(a: [:0]const u8, b: [:(1 - 1)]const u8, c: [:1]const u8, "
		"d: []const u8, e: [:false]bool, f: [:true]bool) void { "
		"_ = a; _ = b; _ = c; _ = d; _ = e; _ = f; } "
		"pub fn main() void { consume(undefined,undefined,undefined,undefined,undefined,undefined); }";
	FileId file = source_add_memory(&sources, "sentinel_slice_types.zig", source, sizeof(source) - 1);
	AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
	CHECK(!diag_has_errors(&diag));
	IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
	CHECK(!diag_has_errors(&diag));
	Sema *sema = sema_create(&arena, &diag, &interner, &sources);
	CHECK(sema_analyze_executable(sema, file, ir));
	CHECK(sema_func_count(sema) == 3);
	TypeFn fn = sema_type(sema, sema_func(sema, 2)->type)->as.fn;
	CHECK(fn.param_count == 6);
	CHECK(fn.params[0] == fn.params[1]);
	CHECK(fn.params[0] != fn.params[2] && fn.params[0] != fn.params[3]);
	CHECK(fn.params[4] != fn.params[5]);
	for (uint32_t i = 0; i < 6; i++) {
		uint64_t size;
		CHECK(sema_type_size(sema, fn.params[i], &size) && size == 16);
		CHECK(sema_type_align(sema, fn.params[i]) == 8);
		ValueId sentinel = sema_type(sema, fn.params[i])->as.pointer.sentinel;
		CHECK((sentinel == VALUE_NONE) == (i == 3));
	}
	arena_free(&arena);
}

static void
test_qualifier_cast_types(void)
{
	Arena arena;
	arena_init(&arena);
	SourceTable sources;
	source_table_init(&sources, &arena);
	Diag diag;
	diag_init(&diag, &arena, &sources);
	Interner interner;
	intern_init(&interner, &arena);
	static const char source[] =
		"fn clear_const(p: *const volatile u32) *volatile u32 { return @constCast(p); } "
		"fn clear_volatile(p: *const volatile u32) *const u32 { return @volatileCast(p); } "
		"fn clear_slice(p: [:17]const u16) [:17]u16 { return @constCast(p); } "
		"pub fn main() void { _ = clear_const(undefined); _ = clear_volatile(undefined); _ = clear_slice(undefined); }";
	FileId file = source_add_memory(&sources, "qualifier_cast_types.zig", source, sizeof(source) - 1);
	AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
	CHECK(!diag_has_errors(&diag));
	IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
	CHECK(!diag_has_errors(&diag));
	Sema *sema = sema_create(&arena, &diag, &interner, &sources);
	CHECK(sema_analyze_executable(sema, file, ir));
	CHECK(sema_func_count(sema) == 5);
	for (FuncId i = 2; i < 5; i++) {
		const SemaFunc *func = sema_func(sema, i);
		TypeFn fn = sema_type(sema, func->type)->as.fn;
		CHECK(fn.param_count == 1);
		TypePointer before = sema_type(sema, fn.params[0])->as.pointer;
		TypePointer after = sema_type(sema, fn.ret)->as.pointer;
		CHECK(before.child == after.child && before.size == after.size);
		CHECK(before.sentinel == after.sentinel && before.alignment == after.alignment);
		CHECK(before.is_allowzero == after.is_allowzero);
		CHECK(after.is_const == (i == 3));
		CHECK(after.is_volatile == (i == 2));
		uint64_t size;
		CHECK(sema_type_size(sema, fn.ret, &size) && size == (i == 4 ? 16u : 8u));
		bool volatile_load = false;
		for (TirRef j = 0; j < func->tir.inst_count; j++)
			volatile_load |= func->tir.insts[j].op == TIR_LOAD_VOLATILE;
		CHECK(!volatile_load);
	}
	arena_free(&arena);
}

static void
test_integer_pointer_values(void)
{
	Arena arena;
	arena_init(&arena);
	ValueTable values;
	value_table_init(&values, &arena);
	ValueId pointer = value_int_pointer(&values, 100, 4096);
	CHECK(pointer == value_int_pointer(&values, 100, 4096));
	CHECK(pointer != value_int_pointer(&values, 100, 8192));
	CHECK(pointer != value_int_pointer(&values, 101, 4096));
	CHECK(value_get(&values, pointer)->kind == VALUE_INT_PTR);
	CHECK(value_get(&values, pointer)->as.address == 4096);
	ValueId cast = value_pointer_cast(&values, 101, pointer);
	CHECK(value_get(&values, cast)->kind == VALUE_INT_PTR);
	CHECK(value_get(&values, cast)->type == 101);
	CHECK(value_get(&values, cast)->as.address == 4096);
	ValueId element = value_element_pointer(&values, 102, cast, 3, 4102, false);
	CHECK(value_get(&values, element)->kind == VALUE_INT_PTR);
	PointerAddress address = value_pointer_address(&values, element);
	CHECK(address.root == element && address.offset == 4102 && !address.logical);
	ValueId field = value_slice_field_pointer(&values, 103, pointer, 4104, true);
	CHECK(value_get(&values, field)->kind == VALUE_INT_PTR);
	CHECK(value_get(&values, field)->as.address == 4104);
	CHECK(values.object_count == 0);
	arena_free(&arena);
}

static void
test_sentinel_assertion_order(void)
{
	Arena arena;
	arena_init(&arena);
	SourceTable sources;
	source_table_init(&sources, &arena);
	Diag diag;
	diag_init(&diag, &arena, &sources);
	Interner interner;
	intern_init(&interner, &arena);
	static const char source[] =
		"fn consume(p: []volatile u16, first: usize, last: usize) void { _ = p[first..last:17]; } "
		"pub fn main() void { consume(undefined,0,1); }";
	FileId file = source_add_memory(&sources, "sentinel_assertion_order.zig", source, sizeof(source) - 1);
	AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
	CHECK(!diag_has_errors(&diag));
	IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
	CHECK(!diag_has_errors(&diag));
	Sema *sema = sema_create(&arena, &diag, &interner, &sources);
	CHECK(sema_analyze_executable(sema, file, ir));
	CHECK(sema_func_count(sema) == 3);
	const TirFunc *tir = &sema_func(sema, 2)->tir;
	TirRef order = TIR_REF_NONE;
	TirRef bounds = TIR_REF_NONE;
	TirRef load = TIR_REF_NONE;
	TirRef assertion = TIR_REF_NONE;
	uint32_t loads = 0;
	uint32_t assertions = 0;
	for (TirRef i = 0; i < tir->inst_count; i++) {
		TirOp op = tir->insts[i].op;
		if (op == TIR_SLICE_START_CHECK)
			order = i;
		if (op == TIR_SLICE_END_CHECK)
			bounds = i;
		if (op == TIR_LOAD_VOLATILE) {
			load = i;
			loads++;
		}
		if (op == TIR_SENTINEL_CHECK) {
			assertion = i;
			assertions++;
		}
	}
	CHECK(order != TIR_REF_NONE && bounds != TIR_REF_NONE);
	CHECK(order < bounds && bounds < load && load < assertion);
	CHECK(loads == 1 && assertions == 1);
	if (assertion != TIR_REF_NONE)
		CHECK(tir->insts[assertion].data.bin.lhs == load);
	arena_free(&arena);
}

static void
test_sentinel_array_types(void)
{
	Arena arena;
	arena_init(&arena);
	SourceTable sources;
	source_table_init(&sources, &arena);
	Diag diag;
	diag_init(&diag, &arena, &sources);
	Interner interner;
	intern_init(&interner, &arena);
	static const char source[] =
		"fn consume(a: *const [2:0]u8, b: *const [2:(1 - 1)]u8, c: *const [2:1]u8, "
		"d: *const [2]u8, e: *const [0:false]bool, f: *const [0:true]bool) void { "
		"_ = a; _ = b; _ = c; _ = d; _ = e; _ = f; } "
		"pub fn main() void { consume(undefined,undefined,undefined,undefined,undefined,undefined); }";
	FileId file = source_add_memory(&sources, "sentinel_array_types.zig", source, sizeof(source) - 1);
	AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
	CHECK(!diag_has_errors(&diag));
	IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
	CHECK(!diag_has_errors(&diag));
	Sema *sema = sema_create(&arena, &diag, &interner, &sources);
	CHECK(sema_analyze_executable(sema, file, ir));
	CHECK(sema_func_count(sema) == 3);
	TypeFn fn = sema_type(sema, sema_func(sema, 2)->type)->as.fn;
	CHECK(fn.param_count == 6);
	CHECK(fn.params[0] == fn.params[1]);
	CHECK(fn.params[0] != fn.params[2] && fn.params[0] != fn.params[3]);
	CHECK(fn.params[4] != fn.params[5]);
	for (uint32_t i = 0; i < 6; i++) {
		uint64_t size;
		TypeId type = sema_type(sema, fn.params[i])->as.pointer.child;
		uint64_t expected = i < 3 ? 3 : i == 3 ? 2 : 1;
		CHECK(sema_type_size(sema, type, &size) && size == expected);
		CHECK(sema_type_align(sema, type) == 1);
		ValueId sentinel = sema_type(sema, type)->as.array.sentinel;
		CHECK((sentinel == VALUE_NONE) == (i == 3));
	}
	arena_free(&arena);
}

static void
test_alignment_cast_types(void)
{
	Arena arena;
	arena_init(&arena);
	SourceTable sources;
	source_table_init(&sources, &arena);
	Diag diag;
	diag_init(&diag, &arena, &sources);
	Interner interner;
	intern_init(&interner, &arena);
	static const char source[] =
		"fn thin(p: *align(1) const volatile u16) *align(16) u16 { return @volatileCast(@alignCast(@constCast(p))); } "
		"fn slice(p: [:17]align(1) u16) [:17]align(16) u16 { return @alignCast(p); } "
		"fn array(p: *align(1) [3:17]u16) [:17]align(16) u16 { return @alignCast(p); } "
		"pub fn main() void { _ = thin(undefined); _ = slice(undefined); _ = array(undefined); }";
	FileId file = source_add_memory(&sources, "alignment_cast_types.zig", source, sizeof(source) - 1);
	AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
	CHECK(!diag_has_errors(&diag));
	IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
	CHECK(!diag_has_errors(&diag));
	uint32_t casts = 0;
	bool combined = false;
	for (uint32_t i = 0; i < ir->inst_count; i++)
		if (ir->insts[i].op == IR_ALIGN_CAST) {
			IrPointerCast cast;
			memcpy(&cast, &ir->extra[ir->insts[i].data.payload], sizeof(cast));
			combined |= cast.flags == (IR_CAST_ALIGN | IR_CAST_CONST | IR_CAST_VOLATILE);
			casts++;
		}
	CHECK(casts == 3 && combined);
	Sema *sema = sema_create(&arena, &diag, &interner, &sources);
	CHECK(sema_analyze_executable(sema, file, ir));
	CHECK(sema_func_count(sema) == 5);
	for (FuncId i = 2; i < 5; i++) {
		const SemaFunc *func = sema_func(sema, i);
		TypeFn fn = sema_type(sema, func->type)->as.fn;
		TypePointer result = sema_type(sema, fn.ret)->as.pointer;
		CHECK(result.alignment == 16);
		CHECK(!result.is_const && !result.is_volatile && !result.is_allowzero);
		CHECK((result.sentinel != VALUE_NONE) == (i != 2));
		uint32_t guards = 0;
		bool volatile_access = false;
		for (TirRef j = 0; j < func->tir.inst_count; j++) {
			TirOp op = func->tir.insts[j].op;
			guards += op == TIR_ALIGN_CHECK;
			volatile_access |= op == TIR_LOAD_VOLATILE || op == TIR_STORE_VOLATILE;
		}
		CHECK(guards == 1 && !volatile_access);
		uint64_t size;
		CHECK(sema_type_size(sema, fn.ret, &size) && size == (i == 2 ? 8u : 16u));
	}
	arena_free(&arena);
}

static void
test_qualifier_cast_chains(void)
{
	const struct {
		const char *source;
		const char *error;
	} cases[] = {
		{"fn f(p: *const u8) void { _ = @constCast(@constCast(p)); }", "redundant @constCast"},
		{"fn f(p: *volatile u8) void { _ = @volatileCast(@volatileCast(p)); }", "redundant @volatileCast"},
		{"fn f(p: *const volatile u8) void { _ = @constCast((@volatileCast(@constCast(p)))); }", "redundant @constCast"},
		{"fn f(p: *const volatile u8) void { _ = @volatileCast((@constCast(@volatileCast(p)))); }", "redundant @volatileCast"},
		{"fn f(p: *const volatile u8) void { _ = (@constCast((@volatileCast(p)))); }", NULL},
		{"fn f(p: *const volatile u8) void { _ = (@volatileCast((@constCast(p)))); }", NULL},
		{"fn f(p: *const u8) void { _ = @constCast(@as(*const u8, @constCast(p))); }", NULL},
		{"fn f(p: *const u8) void { const q: *const u8 = @constCast(p); _ = @constCast(q); }", NULL},
		{"fn f(p: *const u8) void { _ = @constCast(@constCast(p, p)); }", "expected 1 argument, found 2"},
		{"fn f() void { _ = @volatileCast(@volatileCast()); }", "expected 1 argument, found 0"},
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		Arena arena;
		arena_init(&arena);
		SourceTable sources;
		source_table_init(&sources, &arena);
		Diag diag;
		diag_init(&diag, &arena, &sources);
		Interner interner;
		intern_init(&interner, &arena);
		FileId file = source_add_memory(&sources, "cast_chain.zig", cases[i].source, strlen(cases[i].source));
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		CHECK(!diag_has_errors(&diag));
		(void)irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		if (cases[i].error == NULL)
			CHECK(!diag_has_errors(&diag));
		else {
			CHECK(diag.error_count == 1);
			CHECK(diag.count != 0 && strstr(diag.items[0].message, cases[i].error) != NULL);
		}
		arena_free(&arena);
	}
}

static void
test_scalar_bit_casts(void)
{
	const struct {
		const char *body;
		const char *error;
	} cases[] = {
		{"const n: i5 = @bitCast(@as(u5,31)); const x: u8 = if(n == -1) 0 else 256; _ = x;", NULL},
		{"const n: u5 = @bitCast(@as(i5,-1)); const x: u8 = if(n == 31) 0 else 256; _ = x;", NULL},
		{"const n: i1 = @bitCast(true); const x: u8 = if(n == -1) 0 else 256; _ = x;", NULL},
		{"const n: bool = @bitCast(@as(i1,-1)); const x: u8 = if(n) 0 else 256; _ = x;", NULL},
		{"const n: bool = @bitCast(@as(u1,0)); const x: u8 = if(!n) 0 else 256; _ = x;", NULL},
		{"const n: u0 = @bitCast(@as(void,{})); _ = n;", NULL},
		{"const n: void = @bitCast(@as(u0,0)); _ = n;", NULL},
		{"const n: u8 = @bitCast(@as(i8,undefined)); _ = n;", NULL},
		{"const n: i128 = @bitCast(@as(u128,340282366920938463463374607431768211455)); const x: u8 = if(n == -1) 0 else 256; _ = x;", NULL},
		{"const n: u8 = @bitCast(@as(u16,1)); _ = n;", "@bitCast size mismatch"},
		{"const n: u8 = @bitCast(1); _ = n;", "cannot @bitCast from 'comptime_int'"},
		{"const n: *u8 = @bitCast(@as(usize,16)); _ = n;", "cannot @bitCast to '*u8'"},
	};
	for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		Arena arena;
		arena_init(&arena);
		SourceTable sources;
		source_table_init(&sources, &arena);
		Diag diag;
		diag_init(&diag, &arena, &sources);
		Interner interner;
		intern_init(&interner, &arena);
		char source[4096];
		int length = snprintf(source, sizeof(source), "pub fn main() void { %s }", cases[i].body);
		CHECK(length > 0 && (size_t)length < sizeof(source));
		FileId file = source_add_memory(&sources, "bit_casts.zig", source, (uint32_t)length);
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		CHECK(!diag_has_errors(&diag));
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		CHECK(!diag_has_errors(&diag));
		Sema *sema = sema_create(&arena, &diag, &interner, &sources);
		bool success = sema_analyze_executable(sema, file, ir);
		CHECK(success == (cases[i].error == NULL));
		if (cases[i].error != NULL)
			CHECK(diag.count != 0 && strstr(diag.items[0].message, cases[i].error) != NULL);
		arena_free(&arena);
	}
}

static void
test_character_literals(void)
{
	const struct {
		const char *body;
		const char *error;
	} cases[] = {
		{"const n = 'a'; const check:u8=if(n==97) 0 else 256; _=check;", NULL},
		{"const n = '\\n'; const check:u8=if(n==10) 0 else 256; _=check;", NULL},
		{"const n = '\\x00'; const check:u8=if(n==0) 0 else 256; _=check;", NULL},
		{"const n = '\\xff'; const check:u8=if(n==255) 0 else 256; _=check;", NULL},
		{"const n = 'ä'; const check:u8=if(n==228) 0 else 256; _=check;", NULL},
		{"const n = 'ぁ'; const check:u8=if(n==12353) 0 else 256; _=check;", NULL},
		{"const n = '😀'; const check:u8=if(n==128512) 0 else 256; _=check;", NULL},
		{"const n = '\\u{7ff}'; const check:u8=if(n==2047) 0 else 256; _=check;", NULL},
		{"const n = '\\u{10ffff}'; const check:u8=if(n==1114111) 0 else 256; _=check;", NULL},
		{"const n = '\\u{d800}'; const check:u8=if(n==55296) 0 else 256; _=check;", NULL},
		{"const n = '\\u{0000000041}'; const check:u8=if(n==65) 0 else 256; _=check;", NULL},
	};
	for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		Arena arena;
		arena_init(&arena);
		SourceTable sources;
		source_table_init(&sources, &arena);
		Diag diag;
		diag_init(&diag, &arena, &sources);
		Interner interner;
		intern_init(&interner, &arena);
		char source[4096];
		int length = snprintf(source, sizeof(source), "pub fn main() void { %s }", cases[i].body);
		CHECK(length > 0 && (size_t)length < sizeof(source));
		FileId file = source_add_memory(&sources, "character_literals.zig", source, (uint32_t)length);
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		CHECK(!diag_has_errors(&diag));
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		CHECK(!diag_has_errors(&diag));
		Sema *sema = sema_create(&arena, &diag, &interner, &sources);
		bool success = sema_analyze_executable(sema, file, ir);
		CHECK(success == (cases[i].error == NULL));
		if (cases[i].error != NULL)
			CHECK(diag.count != 0 && strstr(diag.items[0].message, cases[i].error) != NULL);
		arena_free(&arena);
	}
}

static void
test_constant_aggregate_bit_casts(void)
{
	const struct {
		const char *body;
		const char *error;
	} cases[] = {
		{"const n:u10=@bitCast(@as([2]u5,.{1,2})); const x:u8=if(n==65)0 else 256; _=x;", NULL},
		{"const n:[2]i5=@bitCast(@as(u10,63)); const x:u8=if(n[0]==-1 and n[1]==1)0 else 256; _=x;", NULL},
		{"const n:u3=@bitCast(@as([3]bool,.{true,false,true})); const x:u8=if(n==5)0 else 256; _=x;", NULL},
		{"const n:u20=@bitCast(@as([2][2]u5,.{.{1,2},.{3,4}})); const x:u8=if(n==134209)0 else 256; _=x;", NULL},
		{"const n:[2:17]u8=@bitCast(@as(u24,0xff0201)); const x:u8=if(n[0]==1 and n[1]==2 and n[2]==17)0 else 256; _=x;", NULL},
		{"const n:u24=@bitCast(@as([2:17]u8,.{1,2})); const x:u8=if(n==0x110201)0 else 256; _=x;", NULL},
		{"const n:u24=@bitCast(@as([2:17]u8,.{undefined,undefined})); const x:u8=if(n==0x110000)0 else 256; _=x;", NULL},
		{"const n:u48=@bitCast(@as([2][2:17]u8,.{.{undefined,undefined},.{1,2}})); const x:u8=if(n==0x110201110000)0 else 256; _=x;", NULL},
		{"const n:u16=@bitCast(@as([2]u8,.{undefined,undefined})); const x:u8=if(n==0)0 else 256; _=x;", "use of undefined"},
		{"const n:u16=@bitCast(@as([2]u8,.{1,undefined})); const x:u8=if(n==1)0 else 256; _=x;", NULL},
		{"const n:[2]u0=@bitCast(@as([2]u0,.{undefined,undefined})); const x:u8=if(n[0]==0 and n[1]==0)0 else 256; _=x;", NULL},
		{"const n:[2]u0=@bitCast(@as(u0,undefined)); const x:u8=if(n[0]==0 and n[1]==0)0 else 256; _=x;", NULL},
		{"const n:[2]u0=@bitCast(@as([2]u0,undefined)); const x:u8=if(n[0]==0 and n[1]==0)0 else 256; _=x;", NULL},
		{"const n:[2]u0=@bitCast(@as(void,{})); const x:u8=if(n[0]==0 and n[1]==0)0 else 256; _=x;", NULL},
		{"const n:[0]u8=@bitCast(@as([0][5000000000]u8,.{})); _=n;", NULL},
		{"const n:[2]u8=@bitCast(@as(u16,undefined)); _=n;", NULL},
		{"const n:[0]*u8=@bitCast(@as([0]u8,.{})); _=n;", "cannot @bitCast to"},
		{"const n:u8=@bitCast(@as([2]u5,.{1,2})); _=n;", "@bitCast size mismatch"},
		{"const n:[513]u65535=@bitCast(@as([513]u65535,.{1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1})); _=n;", "packed constant word budget exceeded"},
		{"const n:[1][536870912]u8=@bitCast(@as([1][536870912]u8,undefined)); _=n;", "representation exceeds 32-bit bit count"},
		{"const n:[1048577]u0=@bitCast(@as(void,{})); _=n;", "constant array element budget exceeded"},
	};
	for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		Arena arena;
		arena_init(&arena);
		SourceTable sources;
		source_table_init(&sources, &arena);
		Diag diag;
		diag_init(&diag, &arena, &sources);
		Interner interner;
		intern_init(&interner, &arena);
		char source[4096];
		int length = snprintf(source, sizeof(source), "pub fn main() void { %s }", cases[i].body);
		CHECK(length > 0 && (size_t)length < sizeof(source));
		FileId file = source_add_memory(&sources, "bit_casts.zig", source, (uint32_t)length);
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		CHECK(!diag_has_errors(&diag));
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		CHECK(!diag_has_errors(&diag));
		Sema *sema = sema_create(&arena, &diag, &interner, &sources);
		bool success = sema_analyze_executable(sema, file, ir);
		CHECK(success == (cases[i].error == NULL));
		if (cases[i].error != NULL)
			CHECK(diag.count != 0 && strstr(diag.items[0].message, cases[i].error) != NULL);
		arena_free(&arena);
	}
}

static void
test_runtime_aggregate_bit_casts(void)
{
	const struct {
		const char *function;
		const char *call;
		const char *error;
	} cases[] = {
		{"fn f(n:[2]u5) u10 { return @bitCast(n); }", "f(.{1,2})", NULL},
		{"fn f(n:u10) [2]i5 { return @bitCast(n); }", "f(63)", NULL},
		{"fn f(n:[3]bool) u3 { return @bitCast(n); }", "f(.{true,false,true})", NULL},
		{"fn f(n:u3) [3]bool { return @bitCast(n); }", "f(5)", NULL},
		{"fn f(n:i64) [2]i32 { return @bitCast(n); }", "f(-1)", NULL},
		{"fn f(n:[2]i32) i64 { return @bitCast(n); }", "f(.{-1,-1})", NULL},
		{"fn f(n:[2]u5) [5]u2 { return @bitCast(n); }", "f(.{1,2})", NULL},
		{"fn f(n:u20) [2][2]u5 { return @bitCast(n); }", "f(134209)", NULL},
		{"fn f(n:[2][2]u5) u20 { return @bitCast(n); }", "f(.{.{1,2},.{3,4}})", NULL},
		{"fn f(n:u24) [2:17]u8 { return @bitCast(n); }", "f(0x110201)", NULL},
		{"fn f(n:[2:17]u8) u24 { return @bitCast(n); }", "f(.{1,2})", NULL},
		{"fn f(n:[2]u0) [0]u8 { return @bitCast(n); }", "f(.{0,0})", NULL},
		{"fn f(n:[2]u64) u128 { return @bitCast(n); }", "f(.{1,2})", "runtime aggregate @bitCast wider than 64 bits"},
		{"fn f(n:u128) [2]u64 { return @bitCast(n); }", "f(258)", "runtime aggregate @bitCast wider than 64 bits"},
	};
	for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		Arena arena;
		arena_init(&arena);
		SourceTable sources;
		source_table_init(&sources, &arena);
		Diag diag;
		diag_init(&diag, &arena, &sources);
		Interner interner;
		intern_init(&interner, &arena);
		char source[1024];
		int length = snprintf(source, sizeof(source), "%s pub fn main() void { _=%s; }", cases[i].function, cases[i].call);
		CHECK(length > 0 && (size_t)length < sizeof(source));
		FileId file = source_add_memory(&sources, "runtime_bit_casts.zig", source, (uint32_t)length);
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		CHECK(!diag_has_errors(&diag));
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		CHECK(!diag_has_errors(&diag));
		Sema *sema = sema_create(&arena, &diag, &interner, &sources);
		bool success = sema_analyze_executable(sema, file, ir);
		CHECK(success == (cases[i].error == NULL));
		if (cases[i].error != NULL)
			CHECK(diag.count != 0 && strstr(diag.items[0].message, cases[i].error) != NULL);
		arena_free(&arena);
	}
}

static void
test_thin_pointer_casts(void)
{
	const struct {
		const char *source;
		const char *error;
		uint32_t null_checks;
		uint32_t align_checks;
	} cases[] = {
		{"const n:u32=17; const p:*const u8=@ptrCast(&n); const q:*const u32=@alignCast(@ptrCast(p)); const x=q.*; pub fn main() void {const z:u8=if(x==17) 0 else 256; _=z;}", NULL, 0, 0},
		{"const n:u33=8589934591; const p:*const u8=@ptrCast(&n); const q:*const u33=@alignCast(@ptrCast(p)); const x=q.*; pub fn main() void {const z:u8=if(x==n) 0 else 256; _=z;}", NULL, 0, 0},
		{"const n:u32=17; const p:*const u8=@ptrCast(&n); const q:*const u64=@alignCast(@ptrCast(p)); const x=q.*; pub fn main() void {_=x;}", "known immutable/comptime load through reinterpreted pointer", 0, 0},
		{"const a=[2]u32{17,29}; const p:[*]const u8=@ptrCast(&a); const shifted=p+4; const q:*const u32=@alignCast(@ptrCast(shifted)); const x=q.*; pub fn main() void {_=x;}", "known immutable/comptime load through reinterpreted pointer", 0, 0},
		{"fn f(p:[]u16) []u8 {return @ptrCast(p);} pub fn main() void {_=f(undefined);}", NULL, 0, 0},
		{"fn f(p:[]allowzero u8) []u16 {return @alignCast(@ptrCast(p));} pub fn main() void {_=f(undefined);}", NULL, 1, 1},
		{"fn f(p:[][3]u8) []u16 {return @alignCast(@ptrCast(p));} pub fn main() void {_=f(undefined);}", NULL, 0, 1},
		{"fn f(p:[]u0) []u8 {return @ptrCast(p);} pub fn main() void {_=f(undefined);}", NULL, 0, 0},
		{"pub fn main() void {const p:[*]u8=@ptrFromInt(16); const s:[]u8=p[0..3]; const q:[]u16=@alignCast(@ptrCast(s)); _=q;}", "slice length '3' does not divide exactly", 0, 0},
		{"fn f(p:[]u8) []i8 {return @ptrCast(p);} pub fn main() void {_=f(undefined);}", NULL, 0, 0},
		{"fn f(p:[]allowzero u8) []i8 {return @ptrCast(p);} pub fn main() void {_=f(undefined);}", NULL, 1, 0},
		{"fn f(p:[]align(1) allowzero u16) []u16 {return @alignCast(@ptrCast(p));} pub fn main() void {_=f(undefined);}", NULL, 1, 1},
		{"fn f(p:[]const volatile u8) []i8 {return @volatileCast(@constCast(@ptrCast(p)));} pub fn main() void {_=f(undefined);}", NULL, 0, 0},
		{"fn f(p:[]u0) []u0 {return @ptrCast(p);} pub fn main() void {_=f(undefined);}", NULL, 0, 0},
		{"pub fn main() void {const p:[*]allowzero u8=@ptrFromInt(0); const s:[]allowzero u8=p[0..0]; const q:[]i8=@ptrCast(s); _=q;}", NULL, 0, 0},
		{"pub fn main() void {const p:[*]align(1) u16=@ptrFromInt(17); const s:[]align(1) u16=p[0..0]; const q:[]u16=@alignCast(@ptrCast(s)); _=q;}", "pointer address 0x11 is not aligned to 2 bytes", 0, 0},
		{"fn f(p:*[4]u8) []u16 {return @alignCast(@ptrCast(p));} pub fn main() void {_=f(undefined);}", NULL, 0, 1},
		{"fn f(p:*allowzero [4]u8) []u16 {return @alignCast(@ptrCast(p));} pub fn main() void {_=f(undefined);}", NULL, 1, 1},
		{"fn f(p:*[3]u8) []u16 {return @alignCast(@ptrCast(p));} pub fn main() void {_=f(undefined);}", "does not divide exactly", 0, 0},
		{"fn f(p:*u8) []u0 {return @ptrCast(p);} pub fn main() void {_=f(undefined);}", "cannot infer length of slice of zero-bit", 0, 0},
		{"fn f(p:*[5]u0) []u0 {return @ptrCast(p);} pub fn main() void {_=f(undefined);}", NULL, 0, 0},
		{"fn f(p:*u0) []u8 {return @ptrCast(p);} pub fn main() void {_=f(undefined);}", NULL, 0, 0},
		{"fn f(p:*const [2:0]u8) [:0]const u8 {return @ptrCast(p);} pub fn main() void {_=f(undefined);}", NULL, 0, 0},
		{"fn f(p:[]u8) []u16 {return @alignCast(@ptrCast(p));} pub fn main() void {_=f(undefined);}", NULL, 0, 1},
		{"fn f(p:[]u8) *u16 {return @alignCast(@ptrCast(p));} pub fn main() void {_=f(undefined);}", NULL, 0, 1},
		{"fn f(p:[]allowzero u8) *i8 {return @ptrCast(p);} pub fn main() void {_=f(undefined);}", NULL, 1, 0},
		{"fn f(p:[]allowzero u8) *u16 {return @alignCast(@ptrCast(p));} pub fn main() void {_=f(undefined);}", NULL, 1, 1},
		{"fn f(p:[]allowzero u8) *allowzero u16 {return @alignCast(@ptrCast(p));} pub fn main() void {_=f(undefined);}", NULL, 0, 1},
		{"fn f(p:[]const volatile u8) *u16 {return @constCast(@volatileCast(@alignCast(@ptrCast(p))));} pub fn main() void {_=f(undefined);}", NULL, 0, 1},
		{"pub fn main() void {const p:[*]allowzero u8=@ptrFromInt(0); const s:[]allowzero u8=p[0..0]; const q:*i8=@ptrCast(s); _=q;}", NULL, 0, 0},
		{"pub fn main() void {const p:[*]u8=@ptrFromInt(17); const s:[]u8=p[0..0]; const q:*u16=@alignCast(@ptrCast(s)); _=q;}", "pointer address 0x11 is not aligned to 2 bytes", 0, 0},
		{"pub fn main() void {const s:[]u8=undefined; const q:*allowzero i8=@ptrCast(s); _=q;}", NULL, 0, 0},
		{"pub fn main() void {const s:[]u8=undefined; const q:*i8=@ptrCast(s); _=q;}", "use of undefined", 0, 0},
		{"fn f(p:*u8) *i8 {return @ptrCast(p);} pub fn main() void {_=f(undefined);}", NULL, 0, 0},
		{"fn f(p:*u8) *u16 {return @ptrCast(p);} pub fn main() void {_=f(undefined);}", "@ptrCast increases pointer alignment", 0, 0},
		{"fn f(p:*u8) *u16 {return @alignCast(@ptrCast(p));} pub fn main() void {_=f(undefined);}", NULL, 0, 1},
		{"fn f(p:*allowzero u8) *i8 {return @ptrCast(p);} pub fn main() void {_=f(undefined);}", NULL, 1, 0},
		{"fn f(p:*allowzero u8) *u16 {return @alignCast(@ptrCast(p));} pub fn main() void {_=f(undefined);}", NULL, 1, 1},
		{"fn f(p:*allowzero u8) *allowzero u16 {return @alignCast(@ptrCast(p));} pub fn main() void {_=f(undefined);}", NULL, 0, 1},
		{"fn f(p:*const u8) *i8 {return @alignCast(@ptrCast(p));} pub fn main() void {_=f(undefined);}", "@ptrCast discards const qualifier", 0, 0},
		{"fn f(p:*volatile u8) *i8 {return @constCast(@ptrCast(p));} pub fn main() void {_=f(undefined);}", "@ptrCast discards volatile qualifier", 0, 0},
		{"fn f(p:*const volatile u8) *u16 {return @volatileCast(@alignCast(@constCast(@ptrCast(p))));} pub fn main() void {_=f(undefined);}", NULL, 0, 1},
		{"pub fn main() void {const p:*allowzero u8=@ptrFromInt(0); const q:*i8=@ptrCast(p); _=q;}", "null pointer casted to type", 0, 0},
		{"pub fn main() void {const p:*allowzero u8=@ptrFromInt(0); const q:*allowzero u16=@alignCast(@ptrCast(p)); _=q;}", NULL, 0, 0},
		{"pub fn main() void {const p:*u8=@ptrFromInt(17); const q:*u16=@alignCast(@ptrCast(p)); _=q;}", "pointer address 0x11 is not aligned to 2 bytes", 0, 0},
		{"pub fn main() void {const p:*u8=@ptrFromInt(16); const q:*u16=@alignCast(@ptrCast(p)); _=q;}", NULL, 0, 0},
		{"pub fn main() void {const p:*allowzero u8=undefined; const q:*i8=@ptrCast(p); _=q;}", "use of undefined", 0, 0},
		{"pub fn main() void {const p:*u8=undefined; const q:*allowzero i8=@ptrCast(p); _=q;}", NULL, 0, 0},
		{"pub fn main() void {const p:*u8=@ptrFromInt(16); const q:[*:17]i8=@ptrCast(p); _=q;}", NULL, 0, 0},
		{"pub fn main() void {const n:u16=258; const q:*const u8=@ptrCast(&n); _=q;}", NULL, 0, 0},
		{"pub fn main() void {const n:u16=258; const q:*const u16=@ptrCast(&n); _=q;}", NULL, 0, 0},
		{"fn f(p:[]u8) *u8 {return @ptrCast(p);} pub fn main() void {_=f(undefined);}", NULL, 0, 0},
		{"fn f(p:[*]u8) []u8 {return @ptrCast(p);} pub fn main() void {_=f(undefined);}", "cannot infer length of slice from many pointer", 0, 0},
	};
	for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		Arena arena;
		arena_init(&arena);
		SourceTable sources;
		source_table_init(&sources, &arena);
		Diag diag;
		diag_init(&diag, &arena, &sources);
		Interner interner;
		intern_init(&interner, &arena);
		FileId file = source_add_memory(&sources, "thin_pointer_casts.zig", cases[i].source, (uint32_t)strlen(cases[i].source));
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		CHECK(!diag_has_errors(&diag));
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		CHECK(!diag_has_errors(&diag));
		Sema *sema = sema_create(&arena, &diag, &interner, &sources);
		bool success = sema_analyze_executable(sema, file, ir);
		CHECK(success == (cases[i].error == NULL));
		if (cases[i].error != NULL)
			CHECK(diag.count != 0 && strstr(diag.items[0].message, cases[i].error) != NULL);
		else {
			uint32_t null_checks = 0;
			uint32_t align_checks = 0;
			for (FuncId f = 1; f < sema_func_count(sema); f++) {
				const TirFunc *tir = &sema_func(sema, f)->tir;
				for (TirRef j = 0; j < tir->inst_count; j++) {
					null_checks += tir->insts[j].op == TIR_NULL_CHECK || tir->insts[j].op == TIR_SLICE_NULL_CHECK;
					align_checks += tir->insts[j].op == TIR_ALIGN_CHECK;
				}
			}
			CHECK(null_checks == cases[i].null_checks);
			CHECK(align_checks == cases[i].align_checks);
		}
		arena_free(&arena);
	}
}

static void
test_packed_bits(void)
{
	Arena arena;
	arena_init(&arena);
	static const uint32_t widths[] = {0,1,5,8,31,32,33,63,64};
	static const uint32_t offsets[] = {0,1,7,15,31,32,33,63,64,65};
	for (uint32_t i = 0; i < sizeof(widths) / sizeof(widths[0]); i++)
		for (uint32_t j = 0; j < sizeof(offsets) / sizeof(offsets[0]); j++) {
			PackedBits bits = packed_bits_init(&arena, 160);
			uint64_t raw = UINT64_C(0xfedcba9876543210);
			BigInt value = bigint_from_u64(&arena, raw);
			CHECK(packed_bits_write(&arena, &bits, offsets[j], widths[i], &value));
			BigInt result;
			CHECK(packed_bits_read(&arena, &bits, offsets[j], widths[i], false, &result));
			uint64_t mask = widths[i] == 64 ? UINT64_MAX : (UINT64_C(1) << widths[i]) - 1;
			CHECK(bigint_to_u64_bits(&result) == (raw & mask));
		}
	PackedBits bits = packed_bits_init(&arena, 128);
	BigInt negative = bigint_from_i64(&arena, -1);
	CHECK(packed_bits_write(&arena, &bits, 0, 128, &negative));
	BigInt zero = bigint_from_u64(&arena, 0);
	CHECK(packed_bits_write(&arena, &bits, 29, 38, &zero));
	BigInt result;
	CHECK(packed_bits_read(&arena, &bits, 0, 29, false, &result));
	CHECK(bigint_to_u64_bits(&result) == (UINT64_C(1) << 29) - 1);
	CHECK(packed_bits_read(&arena, &bits, 29, 38, false, &result));
	CHECK(bigint_is_zero(&result));
	CHECK(packed_bits_read(&arena, &bits, 67, 61, false, &result));
	CHECK(bigint_to_u64_bits(&result) == (UINT64_C(1) << 61) - 1);
	CHECK(packed_bits_write(&arena, &bits, 3, 65, &negative));
	CHECK(packed_bits_read(&arena, &bits, 3, 65, true, &result));
	CHECK(bigint_compare(&result, &negative) == 0);
	CHECK(!packed_bits_write(&arena, &bits, 129, 0, &zero));
	CHECK(!packed_bits_write(&arena, &bits, 0, 129, &zero));
	CHECK(!packed_bits_read(&arena, &bits, 129, 0, false, &result));
	CHECK(!packed_bits_read(&arena, &bits, 0, 129, false, &result));
	CHECK(bigint_compare(&result, &negative) == 0);
	bits = packed_bits_init(&arena, 0);
	CHECK(packed_bits_write(&arena, &bits, 0, 0, &zero));
	CHECK(packed_bits_read(&arena, &bits, 0, 0, false, &result));
	CHECK(bigint_is_zero(&result));
	arena_free(&arena);
}

static void
test_binary_diagnostics(void)
{
	Arena arena;
	arena_init(&arena);
	SourceTable sources;
	source_table_init(&sources, &arena);
	FileId file = source_add_memory(&sources, "binary.zig", "x", 1);
	Diag diag;
	diag_init(&diag, &arena, &sources);
	char message[] = {'a', 0, 'b'};
	diag_report_bytes(&diag, DIAG_ERROR, file, 0, message, sizeof(message));
	message[2] = 'c';
	CHECK(diag.error_count == 1 && diag.count == 1);
	CHECK(diag.items[0].message_length == 3);
	CHECK(diag.items[0].message[2] == 'b');
	FILE *out = tmpfile();
	CHECK(out != NULL);
	if (out != NULL) {
		diag_print(&diag, out);
		rewind(out);
		char output[128];
		size_t length = fread(output, 1, sizeof(output), out);
		static const char expected[] = "binary.zig:1:1: error: a\0b\nx\n^\n";
		CHECK(length == sizeof(expected) - 1 && memcmp(output, expected, length) == 0);
		fclose(out);
	}
	arena_free(&arena);
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
	test_bigint_random();
	test_bigint_div_zero();
	test_bigint_bit_counts();
	test_bigint_transforms();
	test_byte_values();
	test_constant_references();
	test_global_references();
	test_element_pointers();
	test_array_values();
	test_array_layout();
	test_slice_values();
	test_wide_integer_layout();
	test_sentinel_pointer_types();
	test_sentinel_array_types();
	test_sentinel_slice_types();
	test_sentinel_assertion_order();
	test_integer_pointer_values();
	test_qualifier_cast_types();
	test_qualifier_cast_chains();
	test_alignment_cast_types();
	test_thin_pointer_casts();
	test_scalar_bit_casts();
	test_character_literals();
	test_packed_bits();
	test_constant_aggregate_bit_casts();
	test_runtime_aggregate_bit_casts();
	test_binary_diagnostics();

	printf("unit: %u checks, %u failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
