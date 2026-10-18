/* Unit tests for support code and the lexer. Built with ASan+UBSan by `make unit-test`. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "arena.h"
#include "comptime.h"
#include "codegen.h"
#include "diag.h"
#include "lexer.h"
#include "parser.h"
#include "ir.h"
#include "sema.h"
#include "struct_layout.h"
#include "struct_layout_expected.h"

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
test_scalar_memcpy(void)
{
	const struct {
		const char *body;
		const char *error;
	} cases[] = {
		{"var a:[3]u16=undefined; const b:[3]u16=.{7,9,11}; @memcpy(&a,&b);", NULL},
		{"@memcpy(@as(*[0]u8,undefined),@as(*const [0]u8,undefined));", "use of undefined"},
		{"@memcpy(@as(*[1]u0,undefined),@as(*const [1]u0,undefined));", NULL},
		{"@memcpy(@as([]u0,undefined),@as([]const u0,undefined));", "use of undefined"},
		{"@memcpy(@as(*[0]u0,undefined),@as(*const [1]u0,undefined));", "non-matching copy lengths"},
		{"@memcpy(@as(*const [0]u0,undefined),@as(*const [0]u0,undefined));", "cannot copy to constant pointer"},
		{"@memcpy(@as([*]u0,undefined),@as([*]const u0,undefined));", "unknown copy length"},
		{"var a:[0]u8=.{}; const b:[0]i8=.{}; @memcpy(&a,&b);", "cannot coerce into element type"},
		{"var a:[3]u8=undefined; @memcpy(&a,\"abc\");", NULL},
		{"@memcpy(1,1);", "not an indexable pointer"},
		{"var a:[1][1]u8=undefined; const b:[1][1]u8=.{.{7}}; @memcpy(&a,&b);", NULL},
		{"var a:[0][1]u8=.{}; const b:[0][1]u8=.{}; @memcpy(&a,&b);", NULL},
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
		FileId file = source_add_memory(&sources, "scalar_memcpy.zig", source, (uint32_t)length);
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
test_aggregate_memcpy(void)
{
	const struct {
		const char *body;
		const char *error;
	} cases[] = {
		{"@memcpy(@as(*[1][1]type,undefined),@as(*const [1][1]type,undefined));", "use of undefined"},
		{"@memcpy(@as(*[1][0]type,undefined),@as(*const [1][0]type,undefined));", NULL},
		{"const d:[*]anyerror=@ptrFromInt(4096); const s:[*]const anyerror=@ptrFromInt(8192); @memcpy(d[0..1],s[0..1]);", "@memcpy element type 'anyerror'"},
		{"var a:[1][1]u8=undefined; const b:[1][1]u8=.{.{7}}; const d:*volatile [1][1]u8=&a; @memcpy(d,&b);", "volatile aggregate @memcpy"},
		{"var a:[1][1]u8=undefined; const b:[1][1]u8=.{.{7}}; const b_ptr:*const volatile [1][1]u8=&b; @memcpy(&a,b_ptr);", "volatile aggregate @memcpy"},
		{"var a:[2][3]u8=undefined; const b:[2][3]u8=.{.{1,2,3},.{4,5,6}}; @memcpy(&a,&b);", NULL},
		{"var a:[1][]const u8=undefined; const b:[1][]const u8=.{\"abc\"}; @memcpy(&a,&b);", NULL},
		{"@memcpy(@as(*[1][0]u8,undefined),@as(*const [1][0]u8,undefined));", NULL},
		{"@memcpy(@as(*[0][1]u8,undefined),@as(*const [0][1]u8,undefined));", "use of undefined"},
		{"var a:[1][1]u8=undefined; const b:[1][2]u8=.{.{1,2}}; @memcpy(&a,&b);", "cannot coerce into element type"},
		{"@memcpy(@as(*volatile [1][1]u8,undefined),@as(*const [1][1]u8,undefined));", "use of undefined"},
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
		FileId file = source_add_memory(&sources, "aggregate_memcpy.zig", source, (uint32_t)length);
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
test_scalar_memset(void)
{
	const struct {
		const char *body;
		const char *error;
	} cases[] = {
		{"var a:[3]u16=undefined; @memset(&a,1234);", NULL},
		{"@memset(@as(*[0]u8,undefined),1);", NULL},
		{"@memset(@as([]u0,undefined),undefined);", NULL},
		{"@memset(@as([]void,undefined),{});", NULL},
		{"@memset(@as(*const [0]u8,undefined),1);", "cannot memset constant pointer"},
		{"@memset(@as(*[1]u8,undefined),1);", "use of undefined"},
		{"@memset(@as([]u8,undefined),1);", "use of undefined"},
		{"var a:[0]u8=.{}; @memset(&a,256);", "cannot represent integer value"},
		{"@memset(@as([*]u0,undefined),0);", "unknown @memset length"},
		{"@memset(1,1);", "not an indexable pointer"},
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
		FileId file = source_add_memory(&sources, "scalar_memset.zig", source, (uint32_t)length);
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
test_aggregate_memset(void)
{
	const struct {
		const char *body;
		const char *error;
	} cases[] = {
		{"var a:[2][2]u8=undefined; @memset(&a,.{1,2});", NULL},
		{"@memset(@as(*volatile [0][2]u8,undefined),.{1,2});", NULL},
		{"@memset(@as([]volatile [2]u0,undefined),undefined);", NULL},
		{"const p:*[1]anyerror=@ptrFromInt(4096); @memset(p,undefined);", "@memset element type 'anyerror'"},
		{"var a:[2][2]u33=undefined; @memset(&a,.{1,8589934591});", NULL},
		{"var a:[2][]const u8=undefined; @memset(&a,\"abc\");", NULL},
		{"@memset(@as(*[0][2]u8,undefined),.{1,2});", NULL},
		{"@memset(@as([][2]u0,undefined),undefined);", NULL},
		{"@memset(@as([][0]type,undefined),undefined);", NULL},
		{"@memset(@as(*const [0][2]u8,undefined),.{1,2});", "cannot memset constant pointer"},
		{"@memset(@as(*[1][2]u8,undefined),.{1,2});", "use of undefined"},
		{"@memset(@as([][2]u8,undefined),.{1,2});", "use of undefined"},
		{"@memset(@as(*[0][2]u8,undefined),.{1,256});", "cannot represent integer value"},
		{"@memset(@as([*][0]u8,undefined),.{});", "unknown @memset length"},
		{"var a:[1][2]u8=undefined; const p:*volatile [1][2]u8=&a; @memset(p,.{1,2});", "volatile aggregate @memset"},
		{"var a:[1][1]u65=undefined; @memset(&a,.{1});", "runtime array storage layout"},

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
		FileId file = source_add_memory(&sources, "aggregate_memset.zig", source, (uint32_t)length);
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
test_empty_array_storage(void)
{
	const struct {
		const char *body;
		const char *error;
	} cases[] = {
		{"var a:[0]type=undefined; _=&a;", NULL},
		{"const a:[0]type=.{}; _=a[0];", "indexing into empty array is not allowed"},
		{"const a:[0]type=.{}; _=a[@as(usize,undefined)];", "indexing into empty array is not allowed"},
		{"const p:*[0]type=undefined; _=p[0];", "cannot index into empty array"},
		{"const p:*[0]type=undefined; _=p[@as(usize,undefined)];", "cannot index into empty array"},
		{"var a:[3][0]type=.{.{},.{},.{}}; @memset(&a,.{});", NULL},
		{"var a:[0][3]type=undefined; _=&a;", NULL},
		{"var a:[2][0]comptime_int=undefined; _=&a;", NULL},
		{"var a:[2][0]fn() void=undefined; _=&a;", NULL},
		{"var a:[2][0]u128=undefined; @memset(&a,.{});", NULL},
		{"var a:[2][0]f32=undefined; @memset(&a,.{});", NULL},
		{"var a:[1]type=undefined; _=&a;", "runtime array storage layout"},
		{"var a:[1]comptime_int=undefined; _=&a;", "runtime array storage layout"},
		{"var a:[1]u128=undefined; _=&a;", "runtime array storage layout"},
		{"var a:[0:0]u128=undefined; _=&a;", "runtime array storage layout"},
		{"var a:[0]anyerror=undefined; _=&a;", "runtime array storage layout"},
		{"var a:[0]f16=undefined; _=&a;", "runtime array storage layout"},
		{"var a:[0]f80=undefined; _=&a;", "runtime array storage layout"},
		{"var a:[0]f128=undefined; _=&a;", "runtime array storage layout"},
		{"var a:[1]f32=undefined; _=&a;", "runtime array storage layout"},

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
		FileId file = source_add_memory(&sources, "empty_array_storage.zig", source, (uint32_t)length);
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
test_struct_namespaces(void)
{
	const struct {
		const char *source;
		const char *error;
	} cases[] = {
		{"const R=@This(); pub fn main() void { const n:u8=if(R==@This())0 else 256; _=n; }", NULL},
		{"const N=struct { const Byte=u8; const n:Byte=7; }; pub fn main() void { const n:u8=N.n; _=n; }", NULL},
		{"const N=struct { var n:u8=7; }; pub fn main() void { N.n=8; const p=&N.n; p.*+=1; }", NULL},
		{"const N=struct { const len:u8=7; const ptr:u8=8; }; pub fn main() void { _=N.len; _=N.ptr; }", NULL},
		{"const N=struct { const Self=@This(); fn same() bool { return Self==@This(); } }; pub fn main() void { _=N.same(); }", NULL},
		{"const N=struct { const Self=@This(); const Child=struct { const Self=@This(); fn same() bool { return @This()==@This().Self; } }; }; pub fn main() void { _=N.Child.same(); }", NULL},
		{"const N=struct { const bad:u8=256; }; pub fn main() void { _=N; }", NULL},
		{"const N=struct { export fn exported() u8 { return @This().n; } const n:u8=7; }; pub fn main() void { _=N; }", NULL},
		{"const N=struct { export fn exported() u8 { return 256; } }; pub fn main() void {}", NULL},
		{"const N=struct { export fn exported() u8 { return 256; } }; pub fn main() void { _=N; }", "cannot represent"},
		{"const N=struct { const x:u8=N.x; }; pub fn main() void { _=N.x; }", "depends on itself"},
		{"const N=struct { const x:u8=y; const y:u8=x; }; pub fn main() void { _=N.x; }", "dependency loop"},
		{"const N=struct {}; pub fn main() void { _=N.missing; }", "has no member named"},
		{"pub fn main() void { _=@This().missing; }", "root source file"},
		{"const N=struct { const value:u8=7; const C=struct {}; }; pub fn main() void { _=N.C.value; }", "has no member named"},
		{"const T=u8; pub fn main() void { _=T.missing; }", "type 'u8' has no members"},
		{"const N=struct { value:u8, }; pub fn main() void { _=N; }", NULL},
		{"var n:u8=1; const N=struct { var n:u8=2; fn read() u8 { return n; } }; pub fn main() void { _=N.read(); }", "ambiguous reference"},
		{"var n:u8=1; const N=struct { var n:u8=2; fn read() void { n=3; } }; pub fn main() void { N.read(); }", "ambiguous reference"},
		{"var n:u8=1; const N=struct { var n:u8=2; fn read() void { _=&n; } }; pub fn main() void { N.read(); }", "ambiguous reference"},
		{"pub fn main() void { _=@This(1); }", "expected 0 argument"},
		{"pub fn main() void { const x:u8=7; const N=struct { const y=x; }; _=N.y; }", "capturing local"},
		{"pub fn main() void { const x:u8=7; const N=struct { fn read() u8 { return x; } }; _=N.read(); }", "capturing local"},
		{"pub fn main() void { var x:u8=7; _=&x; const N=struct { const y=x; }; _=N.y; }", "not accessible from here"},
		{"pub fn main() void { var x:u8=7; _=&x; const N=struct { fn read() void { x=3; } }; N.read(); }", "not accessible from here"},
		{"pub fn main() void { var x:u8=7; _=&x; const N=struct { fn read() void { _=&x; } }; N.read(); }", "not accessible from here"},
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
		const char *source = cases[i].source;
		FileId file = source_add_memory(&sources, "struct_namespaces.zig", source, (uint32_t)strlen(source));
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		CHECK(!diag_has_errors(&diag));
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		bool success = false;
		if (!diag_has_errors(&diag)) {
			Sema *sema = sema_create(&arena, &diag, &interner, &sources);
			success = sema_analyze_executable(sema, file, ir);
		}
		CHECK(success == (cases[i].error == NULL));
		if (cases[i].error != NULL)
			CHECK(diag.count != 0 && strstr(diag.items[0].message, cases[i].error) != NULL);
		arena_free(&arena);
	}
}

static void
test_has_decl(void)
{
	const struct {
		const char *source;
		const char *error;
	} cases[] = {
		{"const N=struct { pub const x:u8=256; const private:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { const n:u8=if(@hasDecl(N,\"x\"))0 else 256; _=n; }", NULL},
		{"const N=struct { pub const x:u8=256; const private:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { const n:u8=if(!@hasDecl(N,\"private\"))0 else 256; _=n; }", NULL},
		{"const N=struct { pub const x:u8=256; const private:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { const n:u8=if(@hasDecl(N,\"bad\"))0 else 256; _=n; }", NULL},
		{"const N=struct { pub const x:u8=256; const private:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { const n:u8=if(!@hasDecl(N,\"\") and !@hasDecl(N,\"x\\x00a\"))0 else 256; _=n; }", NULL},
		{"const N=struct { pub const a=@hasDecl(@This(),\"a\"); }; pub fn main() void { const n:u8=if(N.a)0 else 256; _=n; }", NULL},
		{"const N=struct { pub const x:u8=x; }; pub fn main() void { _=@hasDecl(N,\"x\"); }", NULL},
		{"const N=struct { pub const x:u8=256; const private:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { const n:u8=if(@hasDecl(N,\"x\"[0..]))0 else 256; _=n; }", NULL},
		{"const N=struct { pub const x:u8=256; const private:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { const n:u8=if(@hasDecl(N,\".x.\"[1..2]))0 else 256; _=n; }", NULL},
		{"const N=struct { pub const x:u8=256; const private:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { const n:u8=if(@hasDecl(N,\"\"++\"x\"))0 else 256; _=n; }", NULL},
		{"const N=struct { pub const x:u8=256; const private:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { const n:u8=if(@hasDecl(N,&[_]u8{120}))0 else 256; _=n; }", NULL},
		{"const N=struct { pub const x:u8=256; const private:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { const n:u8=if(@hasDecl(N,&[_:0]u8{120}))0 else 256; _=n; }", NULL},
		{"const N=struct { pub const x:u8=256; const private:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { const n:u8=if(@hasDecl(N,&.{120}))0 else 256; _=n; }", NULL},
		{"const N=struct { pub const x:u8=256; const private:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { _=@hasDecl(u8,\"x\"); }", "expected struct, enum, union, or opaque"},
		{"const N=struct { pub const x:u8=256; const private:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { _=@hasDecl(3,4); }", "expected type 'type'"},
		{"const N=struct { pub const x:u8=256; const private:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { _=@hasDecl(u8,3); }", "expected type '[]const u8'"},
		{"const N=struct { pub const x:u8=256; const private:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { _=@hasDecl(@as(type,undefined),3); }", "use of undefined"},
		{"const N=struct { pub const x:u8=256; const private:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { _=@hasDecl(N,undefined); }", "use of undefined"},
		{"const N=struct { pub const x:u8=256; const private:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { var a=[_]u8{120}; _=&a; _=@hasDecl(N,&a); }", "unable to resolve comptime value"},
		{"const N=struct { pub const x:u8=256; const private:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { var a=[_]u8{}; _=&a; _=@hasDecl(N,&a); }", "unable to resolve comptime value"},
		{"const N=struct { pub const x:u8=256; const private:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { const p:*const [0]u8=undefined; _=@hasDecl(N,p); }", "use of undefined"},
		{"const N=struct { pub const x:u8=256; const private:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { const p:*allowzero const [0]u8=@ptrFromInt(0); _=@hasDecl(N,p); }", "unable to resolve comptime value"},
		{"const N=struct { pub const x:u8=256; const private:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { _=@hasDecl(N); }", "expected 2 arguments"},
		{"const N=struct { pub const x:u8=256; const private:u8=256; pub fn bad() u8 { return 256; } }; fn f(s:[]const u8) bool { return @hasDecl(N,s); } pub fn main() void { _=f(\"x\"); }", "unable to resolve comptime value"},
		{"const N=struct { pub const x:u8=256; const private:u8=256; pub fn bad() u8 { return 256; } }; fn f() []const u8 { return \"x\"; } pub fn main() void { _=@hasDecl(N,f()); }", "comptime function calls"},
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
		const char *source = cases[i].source;
		FileId file = source_add_memory(&sources, "has_decl.zig", source, (uint32_t)strlen(source));
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		CHECK(!diag_has_errors(&diag));
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		bool success = false;
		if (!diag_has_errors(&diag)) {
			Sema *sema = sema_create(&arena, &diag, &interner, &sources);
			success = sema_analyze_executable(sema, file, ir);
		}
		CHECK(success == (cases[i].error == NULL));
		if (cases[i].error != NULL)
			CHECK(diag.count != 0 && strstr(diag.items[0].message, cases[i].error) != NULL);
		arena_free(&arena);
	}
}

static void
test_field(void)
{
	const struct {
		const char *source;
		const char *error;
	} cases[] = {
		{"const N=struct { const x:u8=7; var n:u8=1; const Byte=u8; const Self=@This(); fn f(arg:u8) u8 { return arg; } }; pub fn main() void { const n:u8=if(@field(N,\"x\")==7)0 else 256; _=n; }", NULL},
		{"const N=struct { const x:u8=7; var n:u8=1; const Byte=u8; const Self=@This(); fn f(arg:u8) u8 { return arg; } }; pub fn main() void { const n:u8=if(@field(&N,\"x\")==7)0 else 256; _=n; }", NULL},
		{"const N=struct { const x:u8=7; var n:u8=1; const Byte=u8; const Self=@This(); fn f(arg:u8) u8 { return arg; } }; pub fn main() void { const n:u8=if(@field(N,\"Self\")==N)0 else 256; _=n; }", NULL},
		{"const N=struct { const x:u8=7; var n:u8=1; const Byte=u8; const Self=@This(); fn f(arg:u8) u8 { return arg; } }; pub fn main() void { const n:@field(N,\"Byte\")=7; _=n; }", NULL},
		{"const N=struct { const x:u8=7; var n:u8=1; const Byte=u8; const Self=@This(); fn f(arg:u8) u8 { return arg; } }; pub fn main() void { const n:u8=if(&@field(N,\"x\")==&N.x)0 else 256; _=n; }", NULL},
		{"const N=struct { const x:u8=7; var n:u8=1; const Byte=u8; const Self=@This(); fn f(arg:u8) u8 { return arg; } }; pub fn main() void { @field(N,\"n\")=3; @field(N,\"n\")+=1; }", NULL},
		{"const N=struct { const x:u8=7; var n:u8=1; const Byte=u8; const Self=@This(); fn f(arg:u8) u8 { return arg; } }; pub fn main() void { const p=&@field(N,\"n\"); p.*=3; }", NULL},
		{"const N=struct { const x:u8=7; var n:u8=1; const Byte=u8; const Self=@This(); fn f(arg:u8) u8 { return arg; } }; pub fn main() void { const cb:*const fn(u8) u8=&@field(N,\"f\"); _=cb(3); }", NULL},
		{"const N=struct { const x:u8=7; var n:u8=1; const Byte=u8; const Self=@This(); fn f(arg:u8) u8 { return arg; } }; pub fn main() void { const name:[]const u8=\".x.\"[1..2]; _=@field(N,name); }", NULL},
		{"const N=struct { const x:u8=7; var n:u8=1; const Byte=u8; const Self=@This(); fn f(arg:u8) u8 { return arg; } }; pub fn main() void { const a:[2]u8=undefined; const n:u8=if(@field(a,\"len\")==2)0 else 256; _=n; }", NULL},
		{"const N=struct { const x:u8=7; var n:u8=1; const Byte=u8; const Self=@This(); fn f(arg:u8) u8 { return arg; } }; pub fn main() void { const p:*volatile [2]u8=undefined; const n:u8=if(@field(p,\"len\")==2)0 else 256; _=n; }", NULL},
		{"const N=struct { const x:u8=7; var n:u8=1; const Byte=u8; const Self=@This(); fn f(arg:u8) u8 { return arg; } }; pub fn main() void { var s:[]const u8=\"xx\"; @field(s,\"len\")=1; }", NULL},
		{"const N=struct { const x:u8=7; var n:u8=1; const Byte=u8; const Self=@This(); fn f(arg:u8) u8 { return arg; } }; pub fn main() void { const s:[]const u8=undefined; _=&@field(s,\"ptr\"); }", NULL},
		{"const N=struct { const x:u8=7; var n:u8=1; const Byte=u8; const Self=@This(); fn f(arg:u8) u8 { return arg; } }; pub fn main() void { _= @field(N,&.{120}); }", NULL},
		{"const N=struct { const x:u8=7; var n:u8=1; const Byte=u8; const Self=@This(); fn f(arg:u8) u8 { return arg; } }; pub fn main() void { _=@field(N,\"missing\"); }", "has no member named"},
		{"const N=struct { const x:u8=7; var n:u8=1; const Byte=u8; const Self=@This(); fn f(arg:u8) u8 { return arg; } }; pub fn main() void { _=@field(N,\"x\\x00suffix\"); }", "has no member named 'x"},
		{"const N=struct { const x:u8=7; var n:u8=1; const Byte=u8; const Self=@This(); fn f(arg:u8) u8 { return arg; } }; pub fn main() void { _=@field(u8,\"x\"); }", "type 'u8' has no members"},
		{"const N=struct { const x:u8=7; var n:u8=1; const Byte=u8; const Self=@This(); fn f(arg:u8) u8 { return arg; } }; pub fn main() void { _=@field(3,\"x\"); }", "does not support field access"},
		{"const N=struct { const x:u8=7; var n:u8=1; const Byte=u8; const Self=@This(); fn f(arg:u8) u8 { return arg; } }; pub fn main() void { _=@field(3,3); }", "expected type '[]const u8'"},
		{"const N=struct { const x:u8=7; var n:u8=1; const Byte=u8; const Self=@This(); fn f(arg:u8) u8 { return arg; } }; pub fn main() void { _=@field(N,undefined); }", "use of undefined"},
		{"const N=struct { const x:u8=7; var n:u8=1; const Byte=u8; const Self=@This(); fn f(arg:u8) u8 { return arg; } }; pub fn main() void { const p:*const type=undefined; _=@field(p,\"x\"); }", "use of undefined"},
		{"const N=struct { const x:u8=7; var n:u8=1; const Byte=u8; const Self=@This(); fn f(arg:u8) u8 { return arg; } }; pub fn main() void { @field(N,\"x\")=3; }", "cannot assign to constant"},
		{"const N=struct { const x:u8=7; var n:u8=1; const Byte=u8; const Self=@This(); fn f(arg:u8) u8 { return arg; } }; pub fn main() void { var s:[]u8=undefined; @field(s,\"missing\")=1; }", "no member named"},
		{"const N=struct { const x:u8=7; var n:u8=1; const Byte=u8; const Self=@This(); fn f(arg:u8) u8 { return arg; } }; pub fn main() void { var a=[_]u8{120}; _=&a; _=@field(N,&a); }", "unable to resolve comptime value"},
		{"const N=struct { const x:u8=7; var n:u8=1; const Byte=u8; const Self=@This(); fn f(arg:u8) u8 { return arg; } }; pub fn main() void { _=&@field(N); }", "expected 2 arguments"},
		{"const N=struct { const x:u8=7; var n:u8=1; const Byte=u8; const Self=@This(); fn f(arg:u8) u8 { return arg; } }; pub fn main() void { @field(N)=3; }", "expected 2 arguments"},
		{"pub fn main() void { const n:u8=if(!@hasDecl(anyopaque,\"x\"))0 else 256; _=n; }", NULL},
		{"pub fn main() void { _=@hasDecl(anyopaque,3); }", "expected type '[]const u8'"},
		{"pub fn main() void { _=@field(anyopaque,\"x\"); }", "opaque 'anyopaque' has no member named"},
		{"const N=struct { fn bad() u8 { return 256; } }; pub fn main() void { _=@field(N,\"bad\"); }", NULL},
		{"const N=struct { fn bad() u8 { return 256; } }; pub fn main() void { _=&@field(N,\"bad\"); }", "cannot represent"},
		{"const N=struct { fn bad() u8 { return 256; } }; pub fn main() void { _=&N.bad; }", "cannot represent"},
		{"fn bad() u8 { return 256; } pub fn main() void { _=&bad; }", "cannot represent"},
		{"const N=struct { fn bad() u8 { return 256; } }; pub fn main() void { const p:*const fn() u8=@field(N,\"bad\"); _=p; }", "cannot represent"},
		{"pub fn main() void { const a:[3]u8=undefined; const n:u8=if(@field(a,\"len\\x00tail\")==3)0 else 256; _=n; }", NULL},
		{"pub fn main() void { const s:[]const u8=\"abc\"; const n:u8=if(@field(s,\"ptr\\x00tail\")[0]==97)0 else 256; _=n; }", NULL},
		{"const N=struct { const len:u8=7; }; pub fn main() void { _=@field(N,\"len\\x00tail\"); }", "has no member named"},
		{"const N=struct { pub const x:u8=7; }; pub fn main() void { const n:u8=if(@hasDecl(N,\"x\\x00\") and @field(N,\"x\\x00\")==7 and !@hasDecl(N,\"x\\x00\\x00\"))0 else 256; _=n; }", NULL},
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
		const char *source = cases[i].source;
		FileId file = source_add_memory(&sources, "field.zig", source, (uint32_t)strlen(source));
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		CHECK(!diag_has_errors(&diag));
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		bool success = false;
		if (!diag_has_errors(&diag)) {
			Sema *sema = sema_create(&arena, &diag, &interner, &sources);
			success = sema_analyze_executable(sema, file, ir);
		}
		CHECK(success == (cases[i].error == NULL));
		if (cases[i].error != NULL)
			CHECK(diag.count != 0 && strstr(diag.items[0].message, cases[i].error) != NULL);
		arena_free(&arena);
	}
}

static void
test_has_field(void)
{
	const struct {
		const char *source;
		const char *error;
	} cases[] = {
		{"pub fn main() void { const n:u8=if(@hasField([3]u8,\"len\"))0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(@hasField([0]u0,\"len\"))0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(!@hasField([3:0]u8,\"ptr\"))0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(@hasField([]u8,\"ptr\"))0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(@hasField([:0]const u8,\"len\"))0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(@hasField([]align(4) volatile u8,\"ptr\"))0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(@hasField([]allowzero u8,\"len\"))0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(@hasField([]u8,\"len\\x00tail\"))0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(@hasField([]u8,\"len\\x00\\x00\"))0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(!@hasField([]u8,\"\\x00len\"))0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(!@hasField([3]u8,\"\"))0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(!@hasField([3]u8,\"Len\"))0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(!@hasField(struct {pub const len:u8=256;},\"len\"))0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(!@hasField(@This(),\"main\"))0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(@hasField([3]u8,&.{108,101,110}))0 else 256; _=n; }", NULL},
		{"pub fn main() void { const name:[]const u8=\".len.\"[1..4]; const n:u8=if(@hasField([3]u8,name))0 else 256; _=n; }", NULL},
		{"pub fn main() void { _=@hasField(u8,\"len\"); }", "does not support '@hasField'"},
		{"pub fn main() void { _=@hasField(anyopaque,\"len\"); }", "does not support '@hasField'"},
		{"pub fn main() void { _=@hasField(*[3]u8,\"len\"); }", "does not support '@hasField'"},
		{"pub fn main() void { _=@hasField(*[]u8,\"len\"); }", "does not support '@hasField'"},
		{"pub fn main() void { _=@hasField([*]u8,\"len\"); }", "does not support '@hasField'"},
		{"pub fn main() void { _=@hasField(fn() void,\"len\"); }", "does not support '@hasField'"},
		{"pub fn main() void { _=@hasField(3,\"len\"); }", "expected type 'type'"},
		{"pub fn main() void { _=@hasField(@as(type,undefined),3); }", "use of undefined"},
		{"pub fn main() void { _=@hasField(u8,3); }", "expected type '[]const u8'"},
		{"pub fn main() void { _=@hasField([3]u8,undefined); }", "use of undefined"},
		{"pub fn main() void { _=@hasField([3]u8,[_]u8{108,101,110}); }", "array literal requires address-of"},
		{"fn f(name:[]const u8) bool { return @hasField([3]u8,name); } pub fn main() void { _=f(\"len\"); }", "unable to resolve comptime value"},
		{"pub fn main() void { _=@hasField([3]u8); }", "expected 2 arguments"},
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
		const char *source = cases[i].source;
		FileId file = source_add_memory(&sources, "has_field.zig", source, (uint32_t)strlen(source));
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		CHECK(!diag_has_errors(&diag));
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		bool success = false;
		if (!diag_has_errors(&diag)) {
			Sema *sema = sema_create(&arena, &diag, &interner, &sources);
			success = sema_analyze_executable(sema, file, ir);
		}
		CHECK(success == (cases[i].error == NULL));
		if (cases[i].error != NULL)
			CHECK(diag.count != 0 && strstr(diag.items[0].message, cases[i].error) != NULL);
		arena_free(&arena);
	}
}

static void
test_struct_value_interning(void)
{
	Arena arena;
	arena_init(&arena);
	ValueTable table;
	value_table_init(&table, &arena);
	ValueId first = value_int_u64(&table, 10, 7);
	ValueId second = value_int_u64(&table, 10, 9);
	ValueId fields[] = {first, second};
	ValueId empty = value_struct(&table, 20, NULL, 0);
	CHECK(value_struct(&table, 20, fields, 0) == empty);
	CHECK(value_get(&table, empty)->kind == VALUE_STRUCT);
	CHECK(value_get(&table, empty)->as.structure.count == 0);
	CHECK(value_struct(&table, 21, NULL, 0) != empty);
	CHECK(value_array(&table, 20, NULL, 0) != empty);
	CHECK(value_void(&table, 20) != empty);
	CHECK(value_undefined(&table, 20) != empty);

	ValueId structure = value_struct(&table, 22, fields, 2);
	CHECK(value_get(&table, structure)->kind == VALUE_STRUCT);
	CHECK(value_get(&table, structure)->type == 22);
	CHECK(value_get(&table, structure)->as.structure.count == 2);
	CHECK(value_get(&table, structure)->as.structure.fields != fields);
	CHECK(value_struct(&table, 22, fields, 2) == structure);
	CHECK(value_struct(&table, 23, fields, 2) != structure);
	CHECK(value_struct(&table, 22, fields, 1) != structure);
	CHECK(value_array(&table, 22, fields, 2) != structure);
	fields[0] = second;
	fields[1] = first;
	CHECK(value_get(&table, structure)->as.structure.fields[0] == first);
	CHECK(value_get(&table, structure)->as.structure.fields[1] == second);
	CHECK(value_struct(&table, 22, fields, 2) != structure);
	fields[0] = first;
	fields[1] = second;
	CHECK(value_struct(&table, 22, fields, 2) == structure);

	/* The low-level interner, not just the constructor, owns a deep copy. */
	Value raw = {0};
	raw.kind = VALUE_STRUCT;
	raw.type = 24;
	raw.as.structure = (StructValue){fields, 2};
	ValueId copied = value_intern(&table, &raw);
	CHECK(value_get(&table, copied)->as.structure.fields != fields);
	fields[0] = second;
	CHECK(value_get(&table, copied)->as.structure.fields[0] == first);
	fields[0] = first;
	CHECK(value_struct(&table, 24, fields, 2) == copied);

	ValueId missing = value_undefined(&table, 10);
	ValueId function = value_function(&table, 30, 3);
	ValueId pointer = value_ref(&table, 31, first, 0, 8);
	ValueId distinct_pointer = value_ref(&table, 31, first, 1, 8);
	ValueId qualified = value_pointer_cast(&table, 32, pointer);
	ValueId absolute = value_int_pointer(&table, 31, UINT64_C(0x1234567890));
	ValueId leaf_fields[] = {missing, function, pointer, qualified, absolute};
	ValueId leaves = value_struct(&table, 33, leaf_fields, 5);
	CHECK(value_get(&table, value_get(&table, leaves)->as.structure.fields[0])->kind == VALUE_UNDEFINED);
	CHECK(value_get(&table, value_get(&table, leaves)->as.structure.fields[1])->as.function == 3);
	CHECK(value_get(&table, leaves)->as.structure.fields[2] == pointer);
	CHECK(value_get(&table, leaves)->as.structure.fields[3] == qualified);
	CHECK(value_get(&table, leaves)->as.structure.fields[4] == absolute);
	CHECK(value_struct(&table, 33, leaf_fields, 5) == leaves);
	leaf_fields[0] = first;
	CHECK(value_struct(&table, 33, leaf_fields, 5) != leaves);
	leaf_fields[0] = missing;
	leaf_fields[1] = value_function(&table, 30, 4);
	CHECK(value_struct(&table, 33, leaf_fields, 5) != leaves);
	leaf_fields[1] = function;
	leaf_fields[2] = distinct_pointer;
	CHECK(value_struct(&table, 33, leaf_fields, 5) != leaves);
	leaf_fields[2] = pointer;
	PointerAddress address = value_pointer_address(&table, qualified);
	CHECK(address.root == pointer && address.offset == 0 && !address.logical);

	ValueId nested_fields[] = {structure, leaves, empty};
	ValueId nested = value_struct(&table, 34, nested_fields, 3);
	ValueId array = value_array(&table, 35, nested_fields, 3);
	ValueId outer_fields[] = {nested, array};
	ValueId outer = value_struct(&table, 36, outer_fields, 2);
	CHECK(value_struct(&table, 34, nested_fields, 3) == nested);
	CHECK(value_struct(&table, 36, outer_fields, 2) == outer);
	CHECK(value_get(&table, nested)->as.structure.fields[0] == structure);
	CHECK(value_get(&table, array)->as.array.items[1] == leaves);
	CHECK(value_get(&table, outer)->as.structure.fields[0] == nested);
	CHECK(value_get(&table, outer)->as.structure.fields[1] == array);

	/* Several value/slot table growths; arena IDs avoid a large stack buffer. */
	uint32_t iterations = 1024;
	ValueId *ids = ARENA_ARRAY(&arena, ValueId, iterations);
	uint32_t initial_capacity = table.capacity;
	uint32_t initial_slot_count = table.slot_count;
	for (uint32_t i = 0; i < iterations; i++) {
		ValueId item = value_int_u64(&table, 10, i);
		ValueId growth_fields[] = {item, nested, missing};
		ids[i] = value_struct(&table, 37, growth_fields, 3);
		growth_fields[0] = missing;
	}
	CHECK(table.capacity > initial_capacity);
	CHECK(table.slot_count > initial_slot_count);
	uint32_t saved_count = table.count;
	for (uint32_t i = iterations; i != 0; i--) {
		ValueId item = value_int_u64(&table, 10, i - 1);
		ValueId growth_fields[] = {item, nested, missing};
		CHECK(value_struct(&table, 37, growth_fields, 3) == ids[i - 1]);
		CHECK(value_get(&table, ids[i - 1])->as.structure.fields[0] == item);
	}
	CHECK(table.count == saved_count);
	/* Arena-owned fields remain usable as source across additional growth. */
	const ValueId *owned = value_get(&table, ids[0])->as.structure.fields;
	for (uint32_t i = 0; i < iterations; i++)
		(void)value_struct(&table, 100 + i, owned, 3);
	CHECK(value_struct(&table, 37, owned, 3) == ids[0]);
	CHECK(value_struct(&table, 22, fields, 2) == structure);
	CHECK(value_struct(&table, 33, leaf_fields, 5) == leaves);
	CHECK(value_struct(&table, 34, nested_fields, 3) == nested);
	CHECK(value_struct(&table, 36, outer_fields, 2) == outer);
	arena_free(&arena);
}

/* Paste before unit_main.c's main(), then call test_struct_initializer_ir().
 * Frontend only: no sema, codegen, official compiler, or struct runtime ABI. */
static void
test_struct_initializer_ir(void)
{
	const struct {
		const char *source;
		uint32_t init_count;
		uint32_t typed_bits; /* bit i corresponds to ith emitted STRUCT_INIT */
		uint32_t empty_ptr_count;
		uint32_t array_count;
		uint32_t ref_type_count;
		uint32_t ref_array_count;
	} cases[] = {
		{"const S=struct{a:u8}; const g:S=.{.a=1};", 1, 1, 0, 0, 0, 0},
		{"const S=struct{a:u8}; fn f() void {const v:S=.{.a=1}; _=v;}", 1, 0, 0, 0, 0, 0},
		{"const S=struct{a:u8}; fn f() void {const v=S{.a=1}; _=v;}", 1, 1, 0, 0, 0, 0},
		{"const S=struct{a:u8}; fn f() void {const v=S{.a=@as(u8,1)}; _=v;}", 1, 1, 0, 0, 0, 0},
		{"const S=struct{a:u8}; fn f() void {const v:S=@as(S,.{.a=1}); _=v;}", 1, 1, 0, 0, 0, 0},
		{"const S=struct{a:u8}; fn f() S {return .{.a=1};}", 1, 0, 0, 0, 0, 0},
		{"const S=struct{a:u8}; fn f() S {return S{.a=1};}", 1, 1, 0, 0, 0, 0},
		{"const S=struct{a:u8}; fn f() S {return @as(S,.{.a=1});}", 1, 1, 0, 0, 0, 0},
		{"const S=struct{a:u8}; fn f() void {const v:S=(.{.a=1}); _=v;}", 1, 0, 0, 0, 0, 0},
		{"const S=struct{a:u8}; const O=struct{s:S}; fn f() void {const v:O=.{.s=.{.a=1}}; _=v;}", 2, 0, 0, 0, 0, 0},
		{"const S=struct{a:u8}; const O=struct{s:S}; fn f() void {const v=@as(O,.{.s=.{.a=1}}); _=v;}", 2, 3, 0, 0, 0, 0},
		{"const S=struct{a:u8}; const O=struct{s:S}; fn f() void {const v:O=.{.s=S{.a=1}}; _=v;}", 2, 1, 0, 0, 0, 0},
		{"const S=struct{a:u8}; fn f() void {const v:[1]S=.{.{.a=1}}; _=v;}", 1, 0, 0, 1, 0, 0},
		{"const S=struct{a:u8}; fn f() void {const v=[1]S{.{.a=1}}; _=v;}", 1, 1, 0, 1, 0, 0},
		{"const S=struct{a:u8}; fn f() [1]S {return .{.{.a=1}};}", 1, 0, 0, 1, 0, 0},
		{"fn f() void {const v=[_:7]u8{}; _=v;}", 1, 1, 0, 0, 0, 0},
		{"fn f() void {var v:[0:7]u8=undefined; v=.{};}", 0, 0, 1, 0, 0, 0},
		{"fn f() void {var v:[0]u8=undefined; v=.{};}", 0, 0, 1, 0, 0, 0},
		{"const S=struct{a:u8}; fn f() void {const p:*const S=&.{.a=1}; _=p;}", 1, 1, 0, 0, 1, 0},
		{"const S=struct{a:u8}; fn f() void {const p=&S{.a=1}; _=p;}", 1, 1, 0, 0, 0, 0},
		{"const S=struct{}; fn f() void {const p:*const S=&.{}; _=p;}", 1, 1, 0, 0, 0, 1},
		{"fn f() void {const p:[:7]const u8=&.{}; _=p;}", 1, 1, 0, 0, 0, 1},
		{"fn f() void {const p:[*:7]const u8=&.{}; _=p;}", 1, 1, 0, 0, 0, 1},
		{"fn f() void {const p:*const [1]u8=&.{1}; _=p;}", 0, 0, 0, 1, 0, 1},
		/* Reversed declaration order; escaped name must be normalized to a. */
		{"const S=struct{a:u8,b:u8}; fn f() void {const v=S{.b=2,.@\"\\x61\"=1}; _=v;}", 1, 1, 0, 0, 0, 0},
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
		const char *source = cases[i].source;
		FileId file = source_add_memory(&sources, "struct_init_ir.zig", source, (uint32_t)strlen(source));
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		CHECK(!diag_has_errors(&diag));
		if (diag_has_errors(&diag)) {
			arena_free(&arena);
			continue;
		}
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		CHECK(!diag_has_errors(&diag));
		uint32_t counts[IR_OP_COUNT] = {0};
		uint32_t init_count = 0;
		uint32_t typed_bits = 0;
		uint32_t field_count = 0;
		bool bodies_match = true;
		bool coercion_mode = true;
		bool source_order = true;
		bool normalized_order = true;
		for (uint32_t j = 0; j < ir->inst_count; j++) {
			const IrInst *inst = &ir->insts[j];
			counts[inst->op]++;
			if (inst->op != IR_STRUCT_INIT)
				continue;
			IrStructInit header;
			IR_READ(ir, inst->data.payload, &header);
			if (header.flags & IR_STRUCT_INIT_TYPED)
				typed_bits |= 1u << init_count;
			init_count++;
			uint32_t cursor = inst->data.payload + IR_WORDS(IrStructInit);
			uint32_t previous_src = 0;
			for (uint32_t k = 0; k < header.field_count; k++) {
				IrStructInitField field;
				IR_READ(ir, cursor, &field);
				cursor += IR_WORDS(IrStructInitField);
				field_count++;
				if (k != 0 && field.src <= previous_src)
					source_order = false;
				previous_src = field.src;
				uint32_t body_count = ir_body_count(ir, field.body);
				if (body_count < 2) {
					bodies_match = false;
					continue;
				}
				const IrInstIndex *insts = ir_body_insts(ir, field.body);
				const IrInst *first = &ir->insts[insts[0]];
				const IrInst *last = &ir->insts[insts[body_count - 1]];
				bodies_match = bodies_match && first->op == IR_STRUCT_FIELD_TYPE
					&& first->data.name.name == field.name && first->data.name.operand == header.type
					&& last->op == IR_BREAK_INLINE && last->data.brk.block == IR_BODY_RESULT;
				if (last->op == IR_BREAK_INLINE && ir_ref_is_inst(last->data.brk.operand)) {
					const IrInst *value = &ir->insts[ir_ref_inst(last->data.brk.operand)];
					bool final_as = value->op == IR_AS && value->data.bin.lhs == ir_ref_of_inst(insts[0]);
					coercion_mode = coercion_mode && final_as == ((header.flags & IR_STRUCT_INIT_TYPED) == 0);
					if (strstr(source, "a=@as(u8,1)") != NULL)
						coercion_mode = coercion_mode && value->op == IR_AS && value->data.bin.lhs == IR_REF_TYPE_U8;
				} else {
					coercion_mode = false;
				}
				if (header.field_count == 2)
					normalized_order = normalized_order && strcmp(intern_text(&interner, field.name), k == 0 ? "b" : "a") == 0;
			}
		}
		CHECK(init_count == cases[i].init_count && typed_bits == cases[i].typed_bits);
		CHECK(counts[IR_INIT_EMPTY_PTR] == cases[i].empty_ptr_count);
		CHECK(counts[IR_ARRAY_INIT] == cases[i].array_count);
		CHECK(counts[IR_REF_TYPE] == cases[i].ref_type_count && counts[IR_REF_ARRAY_TYPE] == cases[i].ref_array_count);
		CHECK(counts[IR_ARRAY_ELEM_TYPE] == cases[i].array_count);
		CHECK(counts[IR_STRUCT_FIELD_TYPE] == field_count && bodies_match);
		CHECK(source_order && normalized_order);
		CHECK(coercion_mode);
		arena_free(&arena);
	}

	const struct {
		const char *source;
		const char *error;
		const char *location;
		uint32_t error_count;
	} negatives[] = {
		{"const S=struct{a:u8}; fn f() void {_=S{.a=missing,.@\"\\x61\"=missing2};}", "duplicate struct field name", "a=missing", 1},
		{"fn f() void {_=Missing{.@\"\\x61\"=missing,.a=missing2};}", "duplicate struct field name", "@\"\\x61\"=missing", 1},
		{"const S=struct{a:u8,b:u8}; fn f() void {_=S{.a=missing,.b=0,.b=1,.a=2};}", "duplicate struct field name", "a=missing", 2},
		{"fn f() void {_=[1]u8{.a=missing,.@\"\\x61\"=0};}", "initializing array with struct syntax", "[1]u8", 1},
	};
	for (uint32_t i = 0; i < sizeof(negatives) / sizeof(negatives[0]); i++) {
		Arena arena;
		arena_init(&arena);
		SourceTable sources;
		source_table_init(&sources, &arena);
		Diag diag;
		diag_init(&diag, &arena, &sources);
		Interner interner;
		intern_init(&interner, &arena);
		const char *source = negatives[i].source;
		FileId file = source_add_memory(&sources, "struct_init_negative.zig", source, (uint32_t)strlen(source));
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		CHECK(!diag_has_errors(&diag));
		if (!diag_has_errors(&diag)) {
			(void)irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
			CHECK(diag.error_count == negatives[i].error_count);
			CHECK(diag.count != 0 && strcmp(diag.items[0].message, negatives[i].error) == 0);
			const char *location = strstr(source, negatives[i].location);
			CHECK(location != NULL && diag.count != 0 && diag.items[0].offset == (uint32_t)(location - source));
		}
		arena_free(&arena);
	}

	/* Read-reference lowering must not turn a borrowed field into a value load.
	 * In particular, namespace arrays retain type-only .len and slice descriptors
	 * retain word-at-a-time volatile access through their original storage. */
	const struct {
		const char *source;
		uint32_t named_read_refs;
		uint32_t named_values;
		uint32_t read_refs;
		uint32_t named_pointers;
		uint32_t pointers;
		uint32_t static_values;
	} read_cases[] = {
		{"const I=struct{x:u8}; const S=struct{inner:I}; const s=S{.inner=.{.x=1}}; fn f() void {_=@field(@field(s,\"inner\"),\"x\");}", 1, 1, 0, 0, 0, 0},
		{"const I=struct{x:u8}; const S=struct{inner:I}; const s=S{.inner=.{.x=1}}; fn f() void {_=@field(s.inner,\"x\");}", 0, 1, 1, 0, 0, 0},
		{"const I=struct{x:u8}; const S=struct{inner:I}; const s=S{.inner=.{.x=1}}; fn f() void {_=@field(((@field((s),\"inner\"))),\"x\");}", 1, 1, 0, 0, 0, 0},
		{"const N=struct{var data:[2]u8=undefined;}; fn f() void {_=@field(N.data,\"len\");}", 0, 1, 1, 0, 0, 0},
		{"const N=struct{var data:[2]u8=undefined;}; fn f() void {_=@field(@field(N,\"data\"),\"len\");}", 1, 1, 0, 0, 0, 0},
		{"const N=struct{var view:[]u8=undefined;}; fn f() void {_=@field(N.view,\"len\");}", 0, 1, 1, 0, 0, 0},
		{"const N=struct{var view:[]u8=undefined;}; fn f() void {_=@field(@field(N,\"view\"),\"ptr\");}", 1, 1, 0, 0, 0, 0},
		{"const N=struct{const Inner=struct{var view:[]u8=undefined;};}; fn f() void {_=@field(N.Inner.view,\"len\");}", 0, 1, 2, 0, 0, 0},
		{"const N=struct{const Inner=struct{var data:[2]u8=undefined;};}; fn f() void {_=@field(@field(@field(N,\"Inner\"),\"data\"),\"len\");}", 2, 1, 0, 0, 0, 0},
		/* Direct volatile descriptor projection stays on address_of's old path. */
		{"fn f(p:*volatile []u8) void {_=@field(p.*,\"len\");}", 0, 1, 0, 0, 0, 0},
		{"fn f(p:*volatile []u8) void {_=@field(p,\"ptr\");}", 0, 1, 0, 0, 0, 0},
		/* Loading a namespace's pointer object is necessary; no descriptor copy. */
		{"const N=struct{var p:*volatile []u8=undefined;}; fn f() void {_=@field(@field(N,\"p\").*,\"len\");}", 0, 2, 0, 0, 0, 0},
		{"const N=struct{var p:*volatile []u8=undefined;}; fn f() void {_=@field(N.p.*,\"len\");}", 0, 1, 0, 0, 0, 1},
		/* Explicit address/assignment still uses physical PTR ops, never READ_REF. */
		{"const I=struct{x:u8}; const S=struct{inner:I}; const s=S{.inner=.{.x=1}}; fn f() void {_=&@field(@field(s,\"inner\"),\"x\");}", 0, 0, 0, 2, 0, 0},
		{"const I=struct{x:u8}; const S=struct{inner:I}; const s=S{.inner=.{.x=1}}; fn f() void {_=&@field(s.inner,\"x\");}", 0, 0, 0, 1, 1, 0},
		{"const I=struct{x:u8}; const S=struct{inner:I}; fn f() void {var s:S=undefined; @field(s.inner,\"x\")=2;}", 0, 0, 0, 1, 1, 0},
		/* The parent sema liveness hook must queue the resulting function ref. */
		{"const N=struct{const Inner=struct{fn g() void {}};}; fn f() void {_=@field(@field(N,\"Inner\"),\"g\");}", 1, 1, 0, 0, 0, 0},
	};
	for (uint32_t i = 0; i < sizeof(read_cases) / sizeof(read_cases[0]); i++) {
		Arena arena;
		arena_init(&arena);
		SourceTable sources;
		source_table_init(&sources, &arena);
		Diag diag;
		diag_init(&diag, &arena, &sources);
		Interner interner;
		intern_init(&interner, &arena);
		const char *source = read_cases[i].source;
		FileId file = source_add_memory(&sources, "struct_read_ref_ir.zig", source, (uint32_t)strlen(source));
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		CHECK(!diag_has_errors(&diag));
		if (!diag_has_errors(&diag)) {
			IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
			CHECK(!diag_has_errors(&diag));
			uint32_t counts[IR_OP_COUNT] = {0};
			bool borrowed_chain = true;
			for (uint32_t j = 0; j < ir->inst_count; j++) {
				const IrInst *inst = &ir->insts[j];
				counts[inst->op]++;
				IrRef base = IR_REF_NONE;
				if (inst->op == IR_FIELD_NAMED_READ_REF) {
					IrNamedField field;
					IR_READ(ir, inst->data.payload, &field);
					base = field.base;
				} else if (inst->op == IR_FIELD_READ_REF) {
					base = inst->data.name.operand;
				}
				if (base != IR_REF_NONE && ir_ref_is_inst(base)) {
					IrOp op = ir->insts[ir_ref_inst(base)].op;
					borrowed_chain = borrowed_chain && op != IR_FIELD_VAL && op != IR_FIELD_NAMED_VAL
						&& op != IR_FIELD_PTR && op != IR_FIELD_NAMED_PTR && op != IR_LOAD;
				}
			}
			CHECK(counts[IR_FIELD_NAMED_READ_REF] == read_cases[i].named_read_refs);
			CHECK(counts[IR_FIELD_NAMED_VAL] == read_cases[i].named_values);
			CHECK(counts[IR_FIELD_READ_REF] == read_cases[i].read_refs);
			CHECK(counts[IR_FIELD_NAMED_PTR] == read_cases[i].named_pointers && counts[IR_FIELD_PTR] == read_cases[i].pointers);
			CHECK(counts[IR_FIELD_VAL] == read_cases[i].static_values);
			CHECK(counts[IR_LOAD] == 0 && counts[IR_ARRAY_INIT] == 0 && borrowed_chain);
		}
		arena_free(&arena);
	}
}

static void
test_struct_field_pointers(void)
{
	Arena arena;
	arena_init(&arena);
	ValueTable table;
	value_table_init(&table, &arena);
	ValueId item = value_int_u64(&table, 1, 42);
	ValueId root = value_ref(&table, 2, item, 1, 16);
	ValueId other_root = value_ref(&table, 2, item, 2, 16);
	ValueId first = value_struct_field_pointer(&table, 3, root, 1, 8);
	CHECK(value_get(&table, first)->kind == VALUE_STRUCT_FIELD_PTR);
	CHECK(value_get(&table, first)->type == 3);
	CHECK(value_get(&table, first)->as.struct_field.base == root);
	CHECK(value_get(&table, first)->as.struct_field.index == 1);
	CHECK(value_get(&table, first)->as.struct_field.offset == 8);
	CHECK(first == value_struct_field_pointer(&table, 3, root, 1, 8));
	CHECK(first != value_struct_field_pointer(&table, 4, root, 1, 8));
	CHECK(first != value_struct_field_pointer(&table, 3, other_root, 1, 8));
	CHECK(first != value_struct_field_pointer(&table, 3, root, 0, 8));
	CHECK(first != value_struct_field_pointer(&table, 3, root, 1, 9));
	CHECK(first != value_struct_field_pointer(&table, 3, root, 1, UINT64_C(0x100000008)));
	CHECK(first != value_struct_field_pointer(&table, 3, root, UINT32_MAX, 8));
	CHECK(first != value_element_pointer(&table, 3, root, 1, 8, false));
	CHECK(first != value_slice_field_pointer(&table, 3, root, 8, true));
	PointerAddress address = value_pointer_address(&table, first);
	CHECK(address.root == root && address.offset == 8 && !address.logical);
	ValueId overlap = value_struct_field_pointer(&table, 3, root, 2, 8);
	CHECK(overlap != first);
	address = value_pointer_address(&table, overlap);
	CHECK(address.root == root && address.offset == 8 && !address.logical);
	ValueId zero = value_struct_field_pointer(&table, 3, root, 0, 0);
	CHECK(zero != root);
	address = value_pointer_address(&table, zero);
	CHECK(address.root == root && address.offset == 0 && !address.logical);

	ValueId cast = value_pointer_cast(&table, 4, first);
	CHECK(value_get(&table, cast)->kind == VALUE_PTR_CAST);
	CHECK(value_get(&table, cast)->as.pointer == first);
	ValueId recast = value_pointer_cast(&table, 5, cast);
	CHECK(value_get(&table, recast)->as.pointer == first);
	address = value_pointer_address(&table, recast);
	CHECK(address.root == root && address.offset == 8 && !address.logical);
	ValueId view = value_pointer_reinterpret(&table, 4, first);
	CHECK(value_get(&table, view)->kind == VALUE_REINTERPRET_PTR);
	CHECK(value_get(&table, view)->as.pointer == first);
	CHECK(view != cast);
	address = value_pointer_address(&table, view);
	CHECK(address.root == root && address.offset == 8 && !address.logical);
	ValueId nested = value_struct_field_pointer(&table, 5, recast, 3, 24);
	address = value_pointer_address(&table, nested);
	CHECK(address.root == root && address.offset == 24 && !address.logical);
	CHECK(nested != value_struct_field_pointer(&table, 5, root, 3, 24));
	ValueId element = value_element_pointer(&table, 6, nested, 2, 40, false);
	address = value_pointer_address(&table, element);
	CHECK(address.root == root && address.offset == 40 && !address.logical);
	ValueId field_in_element = value_struct_field_pointer(&table, 7, element, 1, 48);
	address = value_pointer_address(&table, field_in_element);
	CHECK(address.root == root && address.offset == 48 && !address.logical);
	ValueId descriptor = value_slice_field_pointer(&table, 8, field_in_element, 56, true);
	address = value_pointer_address(&table, descriptor);
	CHECK(address.root == root && address.offset == 56 && !address.logical);
	ValueId field_in_descriptor = value_struct_field_pointer(&table, 9, descriptor, 0, 64);
	address = value_pointer_address(&table, field_in_descriptor);
	CHECK(address.root == root && address.offset == 64 && !address.logical);
	ValueId field_in_view = value_struct_field_pointer(&table, 5, view, 3, 24);
	address = value_pointer_address(&table, field_in_view);
	CHECK(address.root == root && address.offset == 24 && !address.logical);

	/* Construction is representation-only, including semantically gated roots. */
	ValueId numeric = value_int_pointer(&table, 2, 4096);
	ValueId numeric_field = value_struct_field_pointer(&table, 3, numeric, 1, 8);
	CHECK(value_get(&table, numeric_field)->kind == VALUE_STRUCT_FIELD_PTR);
	CHECK(value_get(&table, numeric_field)->as.struct_field.base == numeric);
	CHECK(value_get(&table, numeric_field)->as.struct_field.offset == 8);
	address = value_pointer_address(&table, numeric_field);
	CHECK(address.root == numeric && address.offset == 4104 && !address.logical);
	ValueId numeric_cast = value_pointer_cast(&table, 4, numeric_field);
	CHECK(value_get(&table, numeric_cast)->kind == VALUE_PTR_CAST);
	CHECK(value_get(&table, numeric_cast)->as.pointer == numeric_field);
	ValueId global = value_global(&table, 2, 1);
	ValueId global_field = value_struct_field_pointer(&table, 3, global, 1, 8);
	address = value_pointer_address(&table, global_field);
	CHECK(address.root == global && address.offset == 8 && !address.logical);

	/* All offset words and all index bits survive table/slot reallocation. */
	ValueId *ids = ARENA_ARRAY(&arena, ValueId, 1024);
	for (uint32_t i = 0; i < 1024; i++)
		ids[i] = value_struct_field_pointer(&table, 3, root, UINT32_MAX - i,
			((uint64_t)i << 32) | i);
	bool preserved = true;
	for (uint32_t i = 0; i < 1024; i++)
		if (ids[i] != value_struct_field_pointer(&table, 3, root, UINT32_MAX - i,
			((uint64_t)i << 32) | i))
			preserved = false;
	CHECK(preserved);
	CHECK(first == value_struct_field_pointer(&table, 3, root, 1, 8));
	CHECK(table.objects[value_get(&table, root)->as.object].value == item);
	ValueId chain = first;
	for (uint32_t i = 0; i < 128; i++)
		chain = value_struct_field_pointer(&table, 3, chain, i, 128 + i);
	address = value_pointer_address(&table, chain);
	CHECK(address.root == root && address.offset == 255 && !address.logical);
	arena_free(&arena);
}

static void
test_known_struct_values(void)
{
	const struct {
		const char *source;
		const char *error;
	} cases[] = {
		/* scalar/u0/typed */
		{"const S=struct { x:u0, }; pub fn main() void { const s=S{.x=0}; const n:u8=if(s.x==0 and @field(s,\"x\")==0 and @TypeOf(s)==S and @TypeOf(s.x)==u0)0 else 256; _=n; }", NULL},
		/* scalar/u8/as */
		{"const S=struct { x:u8, }; pub fn main() void { const s=@as(S,.{.x=7}); const n:u8=if(s.x==7 and @field(s,\"x\")==7 and @TypeOf(s)==S and @TypeOf(s.x)==u8)0 else 256; _=n; }", NULL},
		/* scalar/u128/typed */
		{"const S=struct { x:u128, }; pub fn main() void { const s=S{.x=0x10000000000000000}; const n:u8=if(s.x==0x10000000000000000 and @field(s,\"x\")==0x10000000000000000 and @TypeOf(s)==S and @TypeOf(s.x)==u128)0 else 256; _=n; }", NULL},
		/* order/5/context */
		{"const S=struct {c:u16,b:u64,a:u8,}; pub fn main() void { const s:S=.{.a=11,.b=22,.c=33}; const n:u8=if(s.a==11 and s.b==22 and s.c==33)0 else 256; _=n; }", NULL},
		/* copy */
		{"const S=struct { x:u8, }; pub fn main() void { const s=S{.x=7}; const copy:S=s; const n:u8=if(copy.x==7 and @TypeOf(copy)==S)0 else 256; _=n; }", NULL},
		/* global */
		{"const S=struct { x:u8, }; const global=S{.x=9}; pub fn main() void {  const n:u8=if(global.x==9 and @field(global,\"x\")==9)0 else 256; _=n; }", NULL},
		/* temporary */
		{"const S=struct { x:u8, }; pub fn main() void {  const n:u8=if((S{.x=13}).x==13 and @field(S{.x=14},\"x\")==14)0 else 256; _=n; }", NULL},
		/* nested-typed */
		{"const I=struct { x:u8, }; const S=struct { inner:I, tag:u16, }; pub fn main() void { const s=S{.tag=3,.inner=I{.x=7}}; const n:u8=if(s.inner.x==7 and @field(@field(s,\"inner\"),\"x\")==7 and s.tag==3)0 else 256; _=n; }", NULL},
		/* nested-context */
		{"const I=struct { x:u8, }; const S=struct { inner:I, }; pub fn main() void { const s:S=.{.inner=.{.x=8}}; const n:u8=if(s.inner.x==8 and @TypeOf(s.inner)==I)0 else 256; _=n; }", NULL},
		/* array-of-structs */
		{"const I=struct { x:u8, }; const S=struct { a:[2]I, }; pub fn main() void { const s:S=.{.a=.{.{.x=4},.{.x=9}}}; const n:u8=if(s.a[0].x==4 and s.a[1].x==9)0 else 256; _=n; }", NULL},
		/* pointer-field */
		{"const S=struct { p:*const u8, }; pub fn main() void { const s=S{.p=@ptrFromInt(4096)}; const n:u8=if(@intFromPtr(s.p)==4096 and @TypeOf(s.p)==*const u8)0 else 256; _=n; }", NULL},
		/* slice-field */
		{"const S=struct { text:[]const u8, }; pub fn main() void { const s=S{.text=\"abc\"}; const n:u8=if(s.text.len==3 and s.text[1]==98 and @field(s,\"text\").len==3)0 else 256; _=n; }", NULL},
		/* empty-typed */
		{"const S=struct {}; pub fn main() void { const s=S{}; const n:u8=if(@TypeOf(s)==S and @sizeOf(@TypeOf(s))==0)0 else 256; _=n; }", NULL},
		/* empty-context */
		{"const S=struct {}; pub fn main() void { const s:S=.{}; const n:u8=if(@TypeOf(s)==S)0 else 256; _=n; }", NULL},
		/* zero-fields */
		{"const S=struct { v:void,u:u0,a:[0]u64, }; pub fn main() void { const s=S{.a=.{},.u=0,.v={}}; const n:u8=if(s.u==0 and @TypeOf(s.v)==void and s.a.len==0)0 else 256; _=n; }", NULL},
		/* partial-undefined */
		{"const S=struct { a:u8,b:u16, }; pub fn main() void { const s=S{.a=undefined,.b=7}; const n:u8=if(s.b==7 and @TypeOf(s.a)==u8)0 else 256; _=n; }", NULL},
		/* whole-undefined-one-value */
		{"const S=struct { v:void,u:u0,a:[0]u64, }; pub fn main() void { const s:S=undefined; const n:u8=if(s.u==0 and @TypeOf(s.v)==void and s.a.len==0)0 else 256; _=n; }", NULL},
		/* undefined-inner-type */
		{"const I=struct { x:u8, }; const S=struct { inner:I, b:u8, }; pub fn main() void { const s:S=.{.inner=undefined,.b=9}; const n:u8=if(s.b==9 and @TypeOf(s.inner.x)==u8)0 else 256; _=n; }", NULL},
		/* nul-name */
		{"const S=struct { item:u8, }; pub fn main() void { const s=S{.item=7}; const n:u8=if(@field(s,\"item\\x00\")==7)0 else 256; _=n; }", NULL},
		/* quoted/hex */
		{"const S=struct {@\"\\x61\":u8,}; pub fn main() void { const s=S{.@\"\\x61\"=7}; const n:u8=if(s.@\"\\x61\"==7 and @field(s,\"a\")==7)0 else 256; _=n; }", NULL},
		/* missing/normal */
		{"const S=struct {a:u8,b:u16,}; pub fn main() void { const s=S{.a=1}; _=s; }", "missing struct field: b"},
		/* missing/void */
		{"const S=struct {v:void,}; pub fn main() void { const s=S{}; _=s; }", "missing struct field: v"},
		/* missing/empty-struct */
		{"const S=struct {e:struct {},}; pub fn main() void { const s=S{}; _=s; }", "missing struct field: e"},
		/* many-fields/25 */
		{"const S=struct {f0:u8,f1:u64,f2:u8,f3:u64,f4:u8,f5:u64,f6:u8,f7:u64,f8:u8,f9:u64,f10:u8,f11:u64,f12:u8,f13:u64,f14:u8,f15:u64,f16:u8,f17:u64,f18:u8,f19:u64,f20:u8,f21:u64,f22:u8,f23:u64,f24:u8,}; pub fn main() void { const s=S{.f24=25,.f23=24,.f22=23,.f21=22,.f20=21,.f19=20,.f18=19,.f17=18,.f16=17,.f15=16,.f14=15,.f13=14,.f12=13,.f11=12,.f10=11,.f9=10,.f8=9,.f7=8,.f6=7,.f5=6,.f4=5,.f3=4,.f2=3,.f1=2,.f0=1}; const n:u8=if(s.f0==1 and s.f1==2 and s.f2==3 and s.f3==4 and s.f4==5 and s.f5==6 and s.f6==7 and s.f7==8 and s.f8==9 and s.f9==10 and s.f10==11 and s.f11==12 and s.f12==13 and s.f13==14 and s.f14==15 and s.f15==16 and s.f16==17 and s.f17==18 and s.f18==19 and s.f19==20 and s.f20==21 and s.f21==22 and s.f22==23 and s.f23==24 and s.f24==25)0 else 256; _=n; }", NULL},
		/* anonymous-nested-field */
		{"const S=struct { inner:struct { x:u8, }, }; pub fn main() void { const s:S=.{.inner=.{.x=7}}; const n:u8=if(s.inner.x==7)0 else 256; _=n; }", NULL},
		/* whole-undefined-builtin */
		{"const S=struct { x:u8, }; pub fn main() void { const s:S=undefined; const x=@field(s,\"x\"); _=x; const n:u8=if(@TypeOf(@field(s,\"x\"))==u8)0 else 256; _=n; }", NULL},
		/* validation/typed/bad-before-unknown */
		{"const S=struct { a:u8,b:u8, }; pub fn main() void { const s=S{.a=256,.unknown=1}; _=s; }", "no field named 'unknown'"},
		/* validation/typed/duplicate-with-undeclared */
		{"const S=struct { a:u8,b:u8, }; pub fn main() void { const s=S{.a=missing,.a=1}; _=s; }", "duplicate struct field name"},
		/* validation/context/bad-before-unknown */
		{"const S=struct { a:u8,b:u8, }; pub fn main() void { const s:S=.{.a=256,.unknown=1}; _=s; }", "type 'u8' cannot represent integer value '256'"},
		/* validation/as/bad-before-unknown */
		{"const S=struct { a:u8,b:u8, }; pub fn main() void { const s=@as(S,.{.a=256,.unknown=1}); _=s; }", "no field named 'unknown'"},
		/* error/nominal-coercion */
		{"const A=struct { x:u8, }; const B=struct { x:u8, }; pub fn main() void { const a=A{.x=1}; const b:B=a; _=b; }", "expected type 'struct_values.B', found 'struct_values.A'"},
		/* error/undefined-whole-use */
		{"const S=struct { a:u8, }; pub fn main() void { const s:S=undefined; const n:u8=if(s.a==0)0 else 1; _=n; }", "use of undefined value here causes illegal behavior"},
		/* error/embedded-nul-value-name */
		{"const S=struct { item:u8, }; pub fn main() void { const s=S{.item=1}; _=@field(s,\"item\\x00tail\"); }", "no field named 'item"},
		/* context-path/global-context */
		{"const S=struct { a:u8,b:u8, }; const s:S=.{.a=256,.unknown=1}; pub fn main() void { _=s; }", "no field named 'unknown'"},
		/* context-path/nested-local-context */
		{"const S=struct { a:u8,b:u8, }; const O=struct { inner:S, }; pub fn main() void { const o:O=.{.inner=.{.a=256,.unknown=1}}; _=o; }", "type 'u8' cannot represent integer value '256'"},
		/* context-path/array-explicit */
		{"const S=struct { a:u8,b:u8, }; pub fn main() void { const a=[_]S{.{.a=256,.unknown=1}}; _=a; }", "no field named 'unknown'"},
		/* context-path/array-local-context */
		{"const S=struct { a:u8,b:u8, }; pub fn main() void { const a:[1]S=.{.{.a=256,.unknown=1}}; _=a; }", "type 'u8' cannot represent integer value '256'"},
		/* eager-value/typed/explicit-as */
		{"const S=struct { a:u8,b:u8, }; fn bad() u8 { return 256; } pub fn main() void { const s=S{.a=@as(u8,256),.unknown=1}; _=s; }", "type 'u8' cannot represent integer value '256'"},
		/* eager-value/typed/division */
		{"const S=struct { a:u8,b:u8, }; fn bad() u8 { return 256; } pub fn main() void { const s=S{.a=@as(u8,1)/0,.unknown=1}; _=s; }", "division by zero here causes illegal behavior"},
		/* eager-value/typed/arithmetic-overflow */
		{"const S=struct { a:u8,b:u8, }; fn bad() u8 { return 256; } pub fn main() void { const s=S{.a=@as(u8,255)+1,.unknown=1}; _=s; }", "overflow of integer type 'u8' with value '256'"},
		/* logical-reference/known-pointer */
		{"const S=struct { a:u8,b:u8, }; pub fn main() void { const s=S{.a=1,.b=2}; const p=&s; const n:u8=if(p.a==1 and p.b==2)0 else 256; _=n; }", NULL},
		/* logical-reference/known-builtin-pointer */
		{"const S=struct { a:u8,b:u8, }; pub fn main() void { const s=S{.a=1,.b=2}; const p=&s; const n:u8=if(@field(p,\"a\")==1 and @field(p,\"b\")==2)0 else 256; _=n; }", NULL},
		/* logical-reference/undefined-pointer-target */
		{"const S=struct { a:u8,b:u8, }; pub fn main() void { const s:S=undefined; const p=&s; const x=p.a; _=x; const n:u8=if(@TypeOf(p.a)==u8)0 else 256; _=n; }", NULL},
		/* logical-reference/known-struct-array-slice */
		{"const S=struct { a:u8,b:u8, }; pub fn main() void { const a=[_]S{S{.a=1,.b=2},S{.a=3,.b=4}}; const sl:[]const S=&a; const n:u8=if(sl.len==2 and sl[0].a==1 and sl[1].b==4)0 else 256; _=n; }", NULL},
		/* undefined-one-possible-fields */
		{"const E=struct {}; const S=struct { u:u0,a:[3]u0,e:E, }; pub fn main() void { const s=S{.u=undefined,.a=undefined,.e=undefined}; const n:u8=if(s.u==0 and s.a[2]==0 and @TypeOf(s.e)==E)0 else 256; _=n; }", NULL},
		/* provenance/control_absolute */
		{"const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nfn use(p: *const S) void { _ = p; }\npub fn main() void { use(@ptrFromInt(4096)); }\n", NULL},
		/* provenance/control_discard_erased */
		{"const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\npub fn main() void { const p: *const anyopaque = @ptrCast(&obj); _ = p; }\n", NULL},
		/* provenance/control_hidden_absolute */
		{"const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nconst items = [1]*const S{ @ptrFromInt(4096) };\nfn use(p: *const [1]*const S) void { _ = p; }\npub fn main() void { use(&items); }\n", NULL},
		/* provenance/control_logical_only */
		{"const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\npub fn main() u8 { const p = &obj; return p.a; }\n", NULL},
		/* provenance/control_runtime */
		{"const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nfn relay(p: *const S) void { _ = p; }\nfn use(p: *const S) void { relay(p); }\npub fn main() void { use(@ptrFromInt(4096)); }\n", NULL},
		/* provenance/escape_array_element */
		{"const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nconst items = [2]S{ obj, obj };\nfn use(p: *const S) void { _ = p; }\npub fn main() void { use(&items[1]); }\n", NULL},
		/* provenance/escape_direct */
		{"const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nfn use(p: *const S) void { _ = p; }\npub fn main() void { use(&obj); }\n", NULL},
		/* provenance/escape_empty */
		{"const S = struct {};\nconst obj = S{};\nfn use(p: *const S) void { _ = p; }\npub fn main() void { use(&obj); }\n", NULL},
		/* provenance/escape_erased */
		{"const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nfn use(p: *const anyopaque) void { _ = p; }\npub fn main() void { use(@ptrCast(&obj)); }\n", NULL},
		/* provenance/escape_global_alias_cold */
		{"const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nconst alias: *const S = &obj;\nfn use(p: *const *const S) void { _ = p; }\npub fn main() void { use(&alias); }\n", NULL},
		/* provenance/escape_global_alias_resolved */
		{"const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nconst alias: *const S = &obj;\nfn use(p: *const *const S) void { _ = p; }\npub fn main() void { _ = alias; use(&alias); }\n", NULL},
		/* provenance/escape_global_erased_alias */
		{"const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nconst alias: *const anyopaque = @ptrCast(&obj);\nfn use(p: *const *const anyopaque) void { _ = p; }\npub fn main() void { use(&alias); }\n", NULL},
		/* provenance/escape_global_pointer */
		{"const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nvar alias: *const S = &obj;\npub fn main() void { _ = alias; }\n", NULL},
		/* provenance/escape_hidden_erased_pointer_array */
		{"const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nconst items = [1]*const anyopaque{ @ptrCast(&obj) };\nfn use(p: *const [1]*const anyopaque) void { _ = p; }\npub fn main() void { use(&items); }\n", NULL},
		/* provenance/escape_hidden_pointer_array */
		{"const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nconst items = [2]*const S{ &obj, &obj };\nfn use(p: *const [2]*const S) void { _ = p; }\npub fn main() void { use(&items); }\n", NULL},
		/* provenance/escape_integer */
		{"const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nfn use(p: usize) void { _ = p; }\npub fn main() void { use(@intFromPtr(&obj)); }\n", NULL},
		/* provenance/escape_slice */
		{"const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nconst items = [1]S{ obj };\nfn use(p: []const S) void { _ = p; }\npub fn main() void { use(&items); }\n", NULL},
		/* provenance/escape_undefined */
		{"const S = struct { a: u8 };\nconst obj: S = undefined;\nfn use(p: *const S) void { _ = p; }\npub fn main() void { use(&obj); }\n", NULL},
		/* provenance/escape_zero_array */
		{"const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nconst items: [0]S = .{};\nfn use(p: *const [0]S) void { _ = p; }\npub fn main() void { use(&items); }\n", NULL},
		/* provenance/volatile_array */
		{"const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nconst items = [1]S{ obj };\npub fn main() u8 { const p: *const volatile [1]S = &items; return p[0].a; }\n", NULL},
		/* provenance/volatile_deref */
		{"const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\npub fn main() void { const p: *const volatile S = &obj; _ = p.*; }\n", "not supported by szc yet"},
		/* provenance/volatile_dot */
		{"const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\npub fn main() u8 { const p: *const volatile S = &obj; return p.a; }\n", NULL},
		/* provenance/volatile_many */
		{"const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\nconst items = [1]S{ obj };\npub fn main() u8 { const p: [*]const volatile S = &items; return p[0].a; }\n", NULL},
		/* provenance/volatile_named */
		{"const S = struct { a: u8 };\nconst obj = S{ .a = 7 };\npub fn main() u8 { const p: *const volatile S = &obj; return @field(p, \"a\"); }\n", NULL},
		/* indexed static and dynamic read references */
		{"const I=struct { a:u8 };\nconst O=struct { items:[1]I };\nconst s=O{.items=.{I{.a=7}}};\npub fn main() u8 { return @field(s.items[0],\"a\") + @field(@field(s,\"items\")[0],\"a\") - 14; }\n", NULL},
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
		const char *source = cases[i].source;
		FileId file = source_add_memory(&sources, "struct_values.zig", source, (uint32_t)strlen(source));
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		bool success = false;
		if (!diag_has_errors(&diag)) {
			Sema *sema = sema_create(&arena, &diag, &interner, &sources);
			success = sema_analyze_executable(sema, file, ir);
		}
		CHECK(success == (cases[i].error == NULL));
		if (cases[i].error != NULL)
			CHECK(diag.count != 0 && strstr(diag.items[0].message, cases[i].error) != NULL);
		arena_free(&arena);
	}
}

static void
test_runtime_struct_values(void)
{
	const struct {
		const char *source;
		const char *error;
	} cases[] = {
		/* init-context */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; s.a+=1;}", NULL},
		/* init-explicit */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s=S{.a=seed(1),.b=seed(2)}; s.b=9;}", NULL},
		/* init-as */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s=@as(S,.{.a=seed(1),.b=seed(2)}); s.a=9;}", NULL},
		/* whole-assign */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; s=S{.a=seed(3),.b=seed(4)};}", NULL},
		/* self-assign */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; s=s;}", NULL},
		/* contextual-overwrite */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; s=.{.a=@intCast(s.b),.b=s.a};}", NULL},
		/* explicit-overwrite */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; s=S{.a=@intCast(s.b),.b=s.a};}", NULL},
		/* as-overwrite */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; s=@as(S,.{.a=@intCast(s.b),.b=s.a});}", NULL},
		/* temporary-overwrite */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; const t:S=.{.a=@intCast(s.b),.b=s.a};s=t;}", NULL},
		/* value-snapshot */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; const t=s; s.a=7; _=t.b;}", NULL},
		/* scalar-snapshot */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; const t=s.a; s.a=7; _=t;}", NULL},
		/* dot-write */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; s.a=7;s.b+=2;}", NULL},
		/* builtin-write */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; @field(s,\"a\")=7;@field(s,\"b\")+=2;}", NULL},
		/* pointer-dot */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; const p=&s;p.a=7;_=p.b;}", NULL},
		/* pointer-builtin */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; const p=&s;@field(p,\"a\")=7;_= @field(p,\"b\");}", NULL},
		/* pointer-deref */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; const p=&s; p.*.a=7;}", NULL},
		/* field-alias */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; const p=&s.a;s.a=7;p.*+=1;}", NULL},
		/* qualified-read */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; const p:*const S=&s;_=p.a;}", NULL},
		/* volatile-scalar */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; const p:*volatile S=&s;p.a=7;_=p.b;}", NULL},
		/* allowzero-fields */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; const p:*allowzero S=&s;p.a=7;_=p.b;}", NULL},
		/* under-aligned */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; const p:*align(1) S=&s;p.b=513;_=p.b;}", NULL},
		/* all-flags */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; const p:*align(1) volatile allowzero S=&s;p.b=513;_=p.b;}", NULL},
		/* root-overalign */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S align(64)=.{.a=seed(1),.b=2};s.b=9;_= &s;}", NULL},
		/* if-value */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; const t:S=if(seed(1)==1)s else S{.a=3,.b=4};s.a=9;_=t.b;}", NULL},
		/* block-value */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; const t:S=blk:{if(seed(1)==1)break :blk s;break :blk S{.a=3,.b=4};};s.a=9;_=t.b;}", NULL},
		/* if-assignment */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; s=if(seed(1)==1)S{.a=3,.b=4} else s;}", NULL},
		/* undefined-whole */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=undefined;s.a=seed(7);s.b=2;_=s.a;}", NULL},
		/* undefined-field */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=undefined,.b=seed(2)};s.a=7;}", NULL},
		/* nested-snapshot */
		{"noinline fn seed(x:u8)u8{return x;} const I=struct{x:u8,};const O=struct{i:I,a:u8,}; pub fn main() void {var s:O=.{.i=.{.x=seed(1)},.a=2};const t=s.i;s.i.x=7;_=t.x;}", NULL},
		/* nested-context */
		{"noinline fn seed(x:u8)u8{return x;} const I=struct{x:u8,y:u8,};const O=struct{i:I,}; pub fn main() void {var s:O=.{.i=.{.x=seed(1),.y=2}};s=.{.i=.{.x=s.i.y,.y=s.i.x}};}", NULL},
		/* nested-address */
		{"noinline fn seed(x:u8)u8{return x;} const I=struct{x:u16,};const O=struct{i:I,}; pub fn main() void {var s:O=.{.i=.{.x=seed(1)}};const p=&s.i.x;p.*=9;}", NULL},
		/* array-field */
		{"noinline fn seed(x:u8)u8{return x;} const O=struct{a:[2]u8,}; pub fn main() void {var s:O=.{.a=.{seed(1),2}};s.a[seed(1)]=7;}", NULL},
		/* array-field-snapshot */
		{"noinline fn seed(x:u8)u8{return x;} const O=struct{a:[2]u8,}; pub fn main() void {var s:O=.{.a=.{seed(1),2}};const t=s.a;s.a[0]=7;_=t[0];}", NULL},
		/* array-of-structs */
		{"noinline fn seed(x:u8)u8{return x;} const I=struct{x:u8,}; pub fn main() void {var a=[_]I{.{.x=seed(1)},.{.x=2}};a[seed(1)].x=7;_=a[0].x;}", NULL},
		/* array-struct-snapshot */
		{"noinline fn seed(x:u8)u8{return x;} const I=struct{x:u8,}; pub fn main() void {var a=[_]I{.{.x=seed(1)},.{.x=2}};const t=a[seed(0)];a[0].x=7;_=t.x;}", NULL},
		/* nested-array-struct */
		{"noinline fn seed(x:u8)u8{return x;} const I=struct{x:u16,};const O=struct{a:[2]I,}; pub fn main() void {var s:O=.{.a=.{.{.x=seed(1)},.{.x=2}}};s.a[seed(1)].x=7;}", NULL},
		/* matrix-struct */
		{"noinline fn seed(x:u8)u8{return x;} const I=struct{x:u8,}; pub fn main() void {var a:[2][1]I=.{.{.{.x=seed(1)}},.{.{.x=2}}};a[seed(1)][0].x=7;}", NULL},
		/* slice-of-structs */
		{"noinline fn seed(x:u8)u8{return x;} const I=struct{x:u8,}; pub fn main() void {var a=[_]I{.{.x=seed(1)},.{.x=2}};const p:[]I=&a;p[seed(1)].x=7;}", NULL},
		/* slice-field */
		{"noinline fn seed(x:u8)u8{return x;} const O=struct{s:[]u8,}; pub fn main() void {var a=[_]u8{seed(1),2};var s:O=.{.s=&a};s.s[1]=7;s.s=s.s[0..1];}", NULL},
		/* slice-field-snapshot */
		{"noinline fn seed(x:u8)u8{return x;} const O=struct{s:[]u8,}; pub fn main() void {var a=[_]u8{seed(1),2};var s:O=.{.s=&a};const t=s;s.s=s.s[0..1];_=t.s.len;}", NULL},
		/* pointer-field */
		{"noinline fn seed(x:u8)u8{return x;} const O=struct{p:*u8,}; pub fn main() void {var a:u8=seed(1);var b:u8=2;var s:O=.{.p=&a};s.p.*=7;s.p=&b;}", NULL},
		/* sentinel-field */
		{"noinline fn seed(x:u8)u8{return x;} const O=struct{a:[2:9]u8,}; pub fn main() void {var s:O=.{.a=.{seed(1),2}};s.a[0]=7;_=s.a[2];}", NULL},
		/* zero-sentinel */
		{"noinline fn seed(x:u8)u8{return x;} const O=struct{a:[0:9]u8,x:u8,}; pub fn main() void {var s:O=.{.a=.{},.x=seed(1)};s.x=7;_=s.a[0];}", NULL},
		/* zero-size-fields */
		{"noinline fn seed(x:u8)u8{return x;} const E=struct{};const O=struct{a:void,b:u0,c:[0]u64,e:E,x:u8,}; pub fn main() void {var s:O=.{.a={},.b=0,.c=.{},.e=.{},.x=seed(1)};s.b=0;s.e=E{};s.x=7;_= &s.c;}", NULL},
		/* empty-runtime-local */
		{"noinline fn seed(x:u8)u8{return x;} const E=struct{}; pub fn main() void {var e:E=.{};e=E{};_= &e;}", NULL},
		/* odd-bool */
		{"noinline fn seed(x:u8)u8{return x;} const O=struct{n:u9,flag:bool,}; pub fn main() void {var s:O=.{.n=seed(1),.flag=true};s.n+=256;s.flag=false;}", NULL},
		/* quoted-name */
		{"noinline fn seed(x:u8)u8{return x;} const O=struct{@\"two words\":u8,}; pub fn main() void {var s:O=.{.@\"two words\"=seed(1)};@field(s,\"two words\")=7;}", NULL},
		/* single-nul-name */
		{"noinline fn seed(x:u8)u8{return x;} const O=struct{x:u8,}; pub fn main() void {var s:O=.{.x=seed(1)};@field(s,\"x\\x00\")=7;}", NULL},
		/* dynamic-root */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} noinline fn root(p:*S)*S{return p;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; const p=root(&s);p.a=7;_=p.b;}", NULL},
		/* absolute-root */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} noinline fn address(p:*S)usize{return @intFromPtr(p);}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; const p:*S=@ptrFromInt(address(&s));p.a=7;_=p.b;}", NULL},
		/* pointer-scalar-helper */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} noinline fn mutate(p:*S)u8{p.a=7;return p.a;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; _=mutate(&s);}", NULL},
		/* compound-once */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} noinline fn field(p:*S)*u8{return &p.a;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; field(&s).*+=2;}", NULL},
		/* builtin-dynamic */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} noinline fn root(p:*S)*S{return p;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; @field(root(&s),\"a\")=7;}", NULL},
		/* error/const-write */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {const s:S=.{.a=1,.b=2};s.a=7;}", "cannot assign to constant"},
		/* error/const-pointer-write */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; const p:*const S=&s;p.a=7;}", "cannot assign to constant"},
		/* error/discard-const */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; const p:*const S=&s;const q:*u8=&p.a;_=q;}", "expected type '*u8', found '*const u8'"},
		/* error/discard-volatile */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; const p:*volatile S=&s;const q:*u8=&p.a;_=q;}", "expected type '*u8', found '*volatile u8'"},
		/* error/discard-allowzero */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; const p:*allowzero S=&s;const q:*u8=&p.a;_=q;}", "expected type '*u8', found '*allowzero u8'"},
		/* error/promote-alignment */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; const p:*align(1) S=&s;const q:*u16=&p.b;_=q;}", "expected type '*u16', found '*align(1) u16'"},
		/* error/missing */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1)};s.a=2;}", "missing struct field: b"},
		/* error/duplicate */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.a=2,.b=3};s.a=2;}", "duplicate struct field name"},
		/* error/unknown */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=2,.extra=3};s.a=2;}", "no field named 'extra' in struct 'test.S'"},
		/* error/overflow */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=256,.b=seed(2)};s.a=2;}", "type 'u8' cannot represent integer value '256'"},
		/* error/bad-write-type */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; s.a=\"bad\";}", "expected type 'u8', found '*const [3:0]u8'"},
		/* error/missing-projection */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; s.nope=7;}", "no field named 'nope' in struct 'test.S'"},
		/* error/bad-field-name */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; @field(s,3)=7;}", "expected type '[]const u8', found 'comptime_int'"},
		/* error/double-nul */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=seed(1),.b=seed(2)}; @field(s,\"a\\x00\\x00\")=7;}", "no field named 'a"},
		/* error/array-oob */
		{"const O=struct{a:[2]u8,};noinline fn seed(x:u8)u8{return x;} pub fn main() void {var s:O=.{.a=.{seed(1),2}};s.a[2]=7;}", "index 2 outside array of length 2"},
		/* error/nominal-mismatch */
		{"const A=struct{x:u8,};const B=struct{x:u8,};noinline fn seed(x:u8)u8{return x;} pub fn main() void {var a:A=.{.x=seed(1)};const b:B=.{.x=seed(2)};a=b;}", "expected type 'test.A', found 'test.B'"},
		/* error/missing-zero-field */
		{"const O=struct{x:u8,z:void,};noinline fn seed(x:u8)u8{return x;} pub fn main() void {var s:O=.{.x=seed(1)};s.x=2;}", "missing struct field: z"},
		/* order/local-overflow-before-unknown */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=256,.unknown=1,.b=2};s.a=1;}", "type 'u8' cannot represent integer value '256'"},
		/* order/explicit-unknown-before-overflow */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s=S{.a=256,.unknown=1,.b=2};s.a=1;}", "no field named 'unknown' in struct 'test.S'"},
		/* order/as-unknown-before-overflow */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s=@as(S,.{.a=256,.unknown=1,.b=2});s.a=1;}", "no field named 'unknown' in struct 'test.S'"},
		/* order/overflow-before-missing */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=256};s.a=1;}", "type 'u8' cannot represent integer value '256'"},
		/* order/duplicate-before-overflow */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;}  pub fn main() void {var s:S=.{.a=256,.a=2,.b=3};s.a=1;}", "duplicate struct field name"},
		/* absolute/field-numeric-comptime */
		{"const T=struct{wide:u64,byte:u8,};const p:*T=@ptrFromInt(4096);  pub fn main() void {const n:u8=if(@intFromPtr(&p.byte)==4096+@offsetOf(T,\"byte\"))0 else 256;_=n;}", NULL},
		/* absolute/field-provenance */
		{"const T=struct{wide:u64,byte:u8,};const p:*T=@ptrFromInt(4096);  pub fn main() void {const n:u8=if(@as(*anyopaque,@ptrCast(p))!=@as(*anyopaque,@ptrCast(&p.wide)))0 else 256;_=n;}", NULL},
		/* absolute/field-flags */
		{"const T=struct{wide:u64,byte:u8,};const p:*align(16) volatile allowzero T=@ptrFromInt(4096);  pub fn main() void {const q:*align(1) volatile allowzero T=p;const n:u8=if(@TypeOf(&p.wide)==*volatile allowzero u64 and @TypeOf(&q.wide)==*align(1) volatile allowzero u64)0 else 256;_=n;}", NULL},
		/* byvalue-parameter */
		{"const S=struct{a:u8,}; fn f(s:S)u8{return s.a;}pub fn main()void{_=f(.{.a=1});}", NULL},
		/* byvalue-return */
		{"const S=struct{a:u8,}; fn f()S{return .{.a=1};}pub fn main()void{_=f();}", NULL},
		/* recursive-array-parameter */
		{"const S=struct{a:u8,}; fn f(a:[2][1]S)u8{return a[0][0].a;}pub fn main()void{_=f(.{.{.{.a=1}},.{.{.a=2}}});}", NULL},
		/* recursive-array-return */
		{"const S=struct{a:u8,}; fn f()[1]S{return .{.{.a=1}};}pub fn main()void{_=f();}", NULL},
		/* mutable-global */
		{"const S=struct{a:u8,}; var s:S=.{.a=1};pub fn main()void{s.a=2;}", NULL},
		/* mutable-global-array */
		{"const S=struct{a:u8,}; var a:[1]S=.{.{.a=1}};pub fn main()void{a[0].a=2;}", NULL},
		/* volatile-whole-load */
		{"const S=struct{a:u8,}; pub fn main()void{var s:S=.{.a=1};const p:*volatile S=&s;const copy=p.*;_=copy;}", "not supported by szc yet:"},
		/* volatile-whole-store */
		{"const S=struct{a:u8,}; pub fn main()void{var s:S=.{.a=1};const p:*volatile S=&s;p.*=S{.a=2};}", "not supported by szc yet:"},
		/* cabi-struct-parameter */
		{"const S=struct{a:u8,}; export fn consume(s:S)u8{return s.a;}pub fn main()void{}", "not supported by szc yet:"},
		/* edge/nested-absolute-scalar-read */
		{"const I=struct{x:u8,wide:u64,};const O=struct{tag:u8,inner:I,array:[3]u16,slice:[]const u8,}; const p:*const O=@ptrFromInt(4096); noinline fn read()u8{return p.inner.x;} pub fn main()void{_=read();}", NULL},
		/* edge/absolute-array-index */
		{"const I=struct{x:u8,wide:u64,};const O=struct{tag:u8,inner:I,array:[3]u16,slice:[]const u8,}; const p:*const O=@ptrFromInt(4096);  pub fn main()void{const n:u8=if(@intFromPtr(&p.array[1])==4096+@offsetOf(O,\"array\")+@sizeOf(u16))0 else 256;_=n;}", NULL},
		/* edge/absolute-array-struct-inner */
		{"const I=struct{x:u8,wide:u64,};const O=struct{tag:u8,inner:I,array:[3]u16,slice:[]const u8,}; const W=struct{a:u8,inner:I,};const p:*const [3]W=@ptrFromInt(4096); pub fn main()void{const n:u8=if(@intFromPtr(&p[1].inner.x)==4096+@sizeOf(W)+@offsetOf(W,\"inner\")+@offsetOf(I,\"x\"))0 else 256;_=n;}", NULL},
		/* edge/near-max-formation */
		{"const T=struct{a:u64,b:u64,};const p:*const T=@ptrFromInt(18446744073709551608);pub fn main()void{const q=&p.b;_=q;}", NULL},
		/* edge/near-max-numeric-wrap */
		{"const T=struct{a:u64,b:u64,};const p:*const T=@ptrFromInt(18446744073709551608);pub fn main()void{const n:u8=if(@intFromPtr(&p.b)==0)0 else 256;_=n;}", NULL},
		/* edge/volatile-many-index-read */
		{"const I=struct{x:u8,wide:u64,};const O=struct{tag:u8,inner:I,array:[3]u16,slice:[]const u8,}; noinline fn read(p:[*]const volatile I)u8{return p[0].x;}pub fn main()void{_=read(@ptrFromInt(4096));}", NULL},
		/* edge/volatile-nested-read */
		{"const I=struct{x:u8,wide:u64,};const O=struct{tag:u8,inner:I,array:[3]u16,slice:[]const u8,}; noinline fn read(p:*const volatile O)u8{return p.inner.x;}pub fn main()void{_=read(@ptrFromInt(4096));}", NULL},
		/* error/runtime-struct-to-[0]u8 */
		{"const S=struct{x:u8,};noinline fn seed()u8{return 1;}pub fn main()void{var s:S=.{.x=seed()};const a:[0]u8=s;_=a;s.x=2;}", "expected type '[0]u8', found 'test.S'"},
		/* error/runtime-struct-to-[1]u8 */
		{"const S=struct{x:u8,};noinline fn seed()u8{return 1;}pub fn main()void{var s:S=.{.x=seed()};const a:[1]u8=s;_=a;s.x=2;}", "expected type '[1]u8', found 'test.S'"},
		/* error/runtime-struct-to-[]u8 */
		{"const S=struct{x:u8,};noinline fn seed()u8{return 1;}pub fn main()void{var s:S=.{.x=seed()};const a:[]u8=s;_=a;s.x=2;}", "expected type '[]u8', found 'test.S'"},
		/* absolute/nominal-field-base */
		{"const A=struct {x:u8,};\nconst B=struct {x:u8,};\nconst a:*A=@ptrFromInt(4096);\nconst b:*B=@ptrFromInt(4096);\npub fn main()void{\nconst x:*u8=&a.x;\nconst y:*u8=&b.x;\nconst n:u8=if(x!=y)0 else 256;\n_=n;\n}\n", NULL},
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
		const char *source = cases[i].source;
		FileId file = source_add_memory(&sources, "test.zig", source, (uint32_t)strlen(source));
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		bool success = false;
		if (!diag_has_errors(&diag)) {
			Sema *sema = sema_create(&arena, &diag, &interner, &sources);
			success = sema_analyze_executable(sema, file, ir);
		}
		CHECK(success == (cases[i].error == NULL));
		if (cases[i].error != NULL)
			CHECK(diag.count != 0 && strstr(diag.items[0].message, cases[i].error) != NULL);
		arena_free(&arena);
	}
}

/* Frontend-only protocol coverage. Paste before main() and call once.
 * Existing normalized-duplicate negative tests remain in their original suite. */
/* Private transport stays pointer-only; each admitted case must also emit QBE,
 * so unsupported scalar struct lowering cannot hide behind successful sema. */
static void
test_struct_internal_abi(void)
{
	const struct {
		const char *source;
		const char *error;
	} cases[] = {
		/* parameter */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S)u16{return s.b;}pub fn main()void{_=f(.{.a=seed(1),.b=2});}", NULL},
		/* return-literal */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(x:u8)S{return .{.a=x,.b=2};}pub fn main()void{_=f(seed(1));}", NULL},
		/* identity */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S)S{return s;}pub fn main()void{const s=f(.{.a=seed(1),.b=2});_=s.a;}", NULL},
		/* return-explicit */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(x:u8)S{return S{.a=x,.b=2};}pub fn main()void{_=f(seed(1));}", NULL},
		/* return-local */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(x:u8)S{var s:S=.{.a=x,.b=2};s.b+=1;return s;}pub fn main()void{_=f(seed(1));}", NULL},
		/* return-constant */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} const c:S=.{.a=1,.b=2};fn f()S{return c;}pub fn main()void{_=f();}", NULL},
		/* return-undefined */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f()S{return undefined;}pub fn main()void{_=f();}", NULL},
		/* return-branch */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(x:u8)S{return if(x==1)S{.a=1,.b=2} else S{.a=3,.b=4};}pub fn main()void{_=f(seed(1));}", NULL},
		/* return-block */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(x:u8)S{return blk:{if(x==1)break :blk S{.a=1,.b=2};break :blk S{.a=3,.b=4};};}pub fn main()void{_=f(seed(1));}", NULL},
		/* private-copy */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S)S{var t=s;t.a+=1;return t;}pub fn main()void{var s:S=.{.a=seed(1),.b=2}; _=f(s);s.a=7;}", NULL},
		/* parameter-const-address */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S)u8{const p:*const S=&s;return p.a;}pub fn main()void{_=f(.{.a=seed(1),.b=2});}", NULL},
		/* value-alias */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S,p:*S)u16{p.b=9;return s.b;}pub fn main()void{var s:S=.{.a=seed(1),.b=2}; _=f(s,&s);}", NULL},
		/* later-arg-effect */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn mutate(p:*S)u8{p.a=9;return 3;}fn f(s:S,x:u8)u8{_=x;return s.a;}pub fn main()void{var s:S=.{.a=seed(1),.b=2}; _=f(s,mutate(&s));}", NULL},
		/* return-alias */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S,p:*S)S{p.a=7;return s;}pub fn main()void{var s:S=.{.a=seed(1),.b=2}; s=f(s,&s);}", NULL},
		/* defer-pointer-source */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(p:*S)S{defer p.a=9;return p.*;}pub fn main()void{var s:S=.{.a=seed(1),.b=2}; _=f(&s);}", NULL},
		/* defer-local-source */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(x:u8)S{var s:S=.{.a=x,.b=2};defer s.a=9;return s;}pub fn main()void{_=f(seed(1));}", NULL},
		/* defer-parameter-copy */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S)S{var t=s;defer t.a=9;return t;}pub fn main()void{_=f(.{.a=seed(1),.b=2});}", NULL},
		/* mixed-args */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(x:u8,s:S,y:u16,t:S,z:u8)S{return .{.a=x+s.a+z,.b=s.b+y+t.b};}pub fn main()void{_=f(seed(1),.{.a=2,.b=3},4,.{.a=5,.b=6},7);}", NULL},
		/* recursion */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S,n:u8)S{if(n==0)return s;var t=s;t.a+=1;return f(t,n-1);}pub fn main()void{_=f(.{.a=seed(1),.b=2},3);}", NULL},
		/* two-results */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(x:u8)S{return .{.a=x,.b=2};}pub fn main()void{const a=f(seed(1));const b=f(seed(2));_=a.a+b.a;}", NULL},
		/* under-aligned-source */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S)u16{return s.b;}fn read(p:*align(1)const S)u16{return f(p.*);}pub fn main()void{var s:S=.{.a=seed(1),.b=2}; _=read(&s);}", NULL},
		/* overaligned-local */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S)S{var t:S align(64)=s;t.a+=1;return t;}pub fn main()void{_=f(.{.a=seed(1),.b=2});}", NULL},
		/* return-nested */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} const O=struct{inner:S,};fn f(o:O)S{return o.inner;}pub fn main()void{_=f(.{.inner=.{.a=seed(1),.b=2}});}", NULL},
		/* array-parameter */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(a:[2]S)u16{return a[1].b;}pub fn main()void{_=f(.{.{.a=seed(1),.b=2},.{.a=3,.b=4}});}", NULL},
		/* array-return */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(x:u8)[2]S{return .{.{.a=x,.b=2},.{.a=3,.b=4}};}pub fn main()void{_=f(seed(1));}", NULL},
		/* array-identity */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(a:[2]S)[2]S{return a;}pub fn main()void{_=f(.{.{.a=seed(1),.b=2},.{.a=3,.b=4}});}", NULL},
		/* recursive-array */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(a:[2][1]S)[2][1]S{return a;}pub fn main()void{_=f(.{.{.{.a=seed(1),.b=2}},.{.{.a=3,.b=4}}});}", NULL},
		/* array-later-effect */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn m(p:*[1]S)u8{p[0].a=9;return 1;}fn f(a:[1]S,x:u8)u8{_=x;return a[0].a;}pub fn main()void{var a:[1]S=.{.{.a=seed(1),.b=2}};_=f(a,m(&a));}", NULL},
		/* nested-struct-array */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} const O=struct{rows:[2]S,};fn f(o:O)O{return o;}pub fn main()void{_=f(.{.rows=.{.{.a=seed(1),.b=2},.{.a=3,.b=4}}});}", NULL},
		/* zero-array */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(a:[0]S)[0]S{return a;}pub fn main()void{_=f(.{});}", NULL},
		/* zero-outer-array */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(a:[0][2]S)[0][2]S{return a;}pub fn main()void{_=f(.{});}", NULL},
		/* zero-inner-array */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(a:[2][0]S)[2][0]S{return a;}pub fn main()void{_=f(.{.{},.{}});}", NULL},
		/* empty-struct */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} const E=struct{};fn f(e:E)E{return e;}pub fn main()void{_=f(.{});}", NULL},
		/* zero-aligned-field */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} const Z=struct{z:[0]u64,v:void,n:u0,};fn f(z:Z)Z{return z;}pub fn main()void{_=f(.{.z=.{},.v={},.n=0});}", NULL},
		/* sentinel-field */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} const T=struct{b:[2:9]u8,};fn f(t:T)T{return t;}pub fn main()void{_=f(.{.b=.{seed(1),2}});}", NULL},
		/* zero-sentinel-field */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} const T=struct{b:[0:9]u8,a:u8,};fn f(t:T)T{return t;}pub fn main()void{_=f(.{.b=.{},.a=seed(1)});}", NULL},
		/* slice-pointer-field */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} const T=struct{s:[]const u8,p:*const u8,};const byte:u8=7;fn f(t:T)T{return t;}pub fn main()void{_=f(.{.s=\"abc\",.p=&byte});}", NULL},
		/* indirect-known */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S)S{return s;}pub fn main()void{const p:*const fn(S)S=&f;_=p(.{.a=seed(1),.b=2});}", NULL},
		/* indirect-runtime */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S)S{return s;}fn g(s:S)S{return s;}fn choose(x:u8)*const fn(S)S{return if(x==1)&f else &g;}pub fn main()void{_=choose(seed(1))(.{.a=seed(1),.b=2});}", NULL},
		/* indirect-array */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(a:[1]S)[1]S{return a;}pub fn main()void{const p:*const fn([1]S)[1]S=&f;_=p(.{.{.a=seed(1),.b=2}});}", NULL},
		/* callback-struct */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S)S{return s;}const H=struct{cb:*const fn(S)S,};const h:H=.{.cb=&f};pub fn main()void{_=h.cb(.{.a=seed(1),.b=2});}", NULL},
		/* callback-array */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S)S{return s;}const a=[_]*const fn(S)S{&f};pub fn main()void{_=a[seed(0)](.{.a=seed(1),.b=2});}", NULL},
		/* callback-slice */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S)S{return s;}const a=[_]*const fn(S)S{&f};const view:[]const *const fn(S)S=&a;pub fn main()void{_=view[seed(0)](.{.a=seed(1),.b=2});}", NULL},
		/* aggregate-function-address */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S)S{return s;}const H=struct{cb:*const fn(S)S,};const h:H=.{.cb=&f};extern fn consume(p:*const H)void;pub fn main()void{consume(&h);}", NULL},
		/* dead-bad-body */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn bad(s:S)S{_=s;return .{.a=\"bad\",.b=2};}pub fn main()void{_=seed(1);}", NULL},
		/* unused-parameter */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S)u8{_=s;return 7;}pub fn main()void{_=f(.{.a=seed(1),.b=2});}", NULL},
		/* indirect-mixed */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(x:u8,s:S,y:u16)S{return .{.a=s.a+x,.b=s.b+y};}pub fn main()void{const p:*const fn(u8,S,u16)S=&f;_=p(seed(1),.{.a=2,.b=3},4);}", NULL},
		/* error/nominal-param */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} const T=struct{a:u8,b:u16,};fn f(s:S)u8{return s.a;}pub fn main()void{const t:T=.{.a=seed(1),.b=2};_=f(t);}", "expected type 'test.S', found 'test.T'"},
		/* error/nominal-return */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} const T=struct{a:u8,b:u16,};fn f()S{const t:T=.{.a=1,.b=2};return t;}pub fn main()void{_=f();}", "expected type 'test.S', found 'test.T'"},
		/* error/nominal-array */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} const T=struct{a:u8,b:u16,};fn f(a:[1]S)u8{return a[0].a;}pub fn main()void{const t:[1]T=.{.{.a=seed(1),.b=2}};_=f(t);}", "expected type '[1]test.S', found '[1]test.T'"},
		/* error/parameter-mutation */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S)u8{s.a=7;return s.a;}pub fn main()void{_=f(.{.a=seed(1),.b=2});}", "cannot assign to constant"},
		/* error/parameter-mutable-address */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S)u8{const p:*S=&s;return p.a;}pub fn main()void{_=f(.{.a=seed(1),.b=2});}", "expected type '*test.S', found '*const test.S'"},
		/* error/return-pointer-const */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S)*u8{return &s.a;}pub fn main()void{_=f(.{.a=seed(1),.b=2});}", "expected type '*u8', found '*const u8'"},
		/* error/few-args */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S,x:u8)u8{return s.a+x;}pub fn main()void{_=f(.{.a=seed(1),.b=2});}", "expected 2 argument(s), found 1"},
		/* error/many-args */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S)u8{return s.a;}pub fn main()void{_=f(.{.a=seed(1),.b=2},3);}", "expected 1 argument(s), found 2"},
		/* error/indirect-few */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S,x:u8)u8{return s.a+x;}pub fn main()void{const p:*const fn(S,u8)u8=&f;_=p(.{.a=seed(1),.b=2});}", "expected 2 argument(s), found 1"},
		/* error/indirect-many */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S)u8{return s.a;}pub fn main()void{const p:*const fn(S)u8=&f;_=p(.{.a=seed(1),.b=2},3);}", "expected 1 argument(s), found 2"},
		/* error/bad-arg-field */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S)u8{return s.a;}pub fn main()void{_=f(.{.a=256,.b=seed(1)});}", "type 'u8' cannot represent integer value '256'"},
		/* error/missing-arg-field */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S)u8{return s.a;}pub fn main()void{_=f(.{.a=seed(1)});}", "missing struct field: b"},
		/* error/bad-return-field */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(x:u8)S{return .{.a=x,.b=\"bad\"};}pub fn main()void{_=f(seed(1));}", "expected type 'u16', found '*const [3:0]u8'"},
		/* error/function-pointer-type */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S)S{return s;}pub fn main()void{const p:*const fn(S)u8=&f;_=p;}", "expected type '*const fn (test.S) u8', found '*const fn (test.S) test.S'"},
		/* error/live-callback-bad-body */
		{"const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S)S{_=s;return .{.a=\"bad\",.b=2};}const H=struct{cb:*const fn(S)S,};const h:H=.{.cb=&f};extern fn consume(p:*const H)void;pub fn main()void{consume(&h);}", "expected type 'u8', found '*const [3:0]u8'"},
		/* nyi/direct-C-parameter */
		{"const S=struct{a:u8,}; fn f(s:S)callconv(.c)u8{return s.a;}pub fn main()void{_=f(.{.a=1});}", "not supported by szc yet:"},
		/* nyi/direct-C-return */
		{"const S=struct{a:u8,}; fn f()callconv(.c)S{return .{.a=1};}pub fn main()void{_=f();}", "not supported by szc yet:"},
		/* nyi/indirect-C-parameter */
		{"const S=struct{a:u8,}; pub fn main()void{const p:*const fn(S)callconv(.c)u8=@ptrFromInt(4096);_=p(.{.a=1});}", "not supported by szc yet:"},
		/* nyi/indirect-C-return */
		{"const S=struct{a:u8,}; pub fn main()void{const p:*const fn()callconv(.c)S=@ptrFromInt(4096);_=p();}", "not supported by szc yet:"},
		/* nyi/direct-SysV */
		{"const S=struct{a:u8,}; fn f(s:S)callconv(.x86_64_sysv)u8{return s.a;}pub fn main()void{_=f(.{.a=1});}", "not supported by szc yet:"},
		/* nyi/indirect-SysV */
		{"const S=struct{a:u8,}; pub fn main()void{const p:*const fn(S)callconv(.x86_64_sysv)u8=@ptrFromInt(4096);_=p(.{.a=1});}", "not supported by szc yet:"},
		/* nyi/extern-recursive-array */
		{"const S=struct{a:u8,}; extern fn f(a:[2][1]S)void;pub fn main()void{f(.{.{.{.a=1}},.{.{.a=2}}});}", "not supported by szc yet:"},
		/* nyi/export-struct-return */
		{"const S=struct{a:u8,}; export fn f()S{return .{.a=1};}pub fn main()void{}", "not supported by szc yet:"},
		/* nyi/mutable-global */
		{"const S=struct{a:u8,}; var s:S=.{.a=1};pub fn main()void{s.a=2;}", NULL},
		/* nyi/volatile-whole-return-source */
		{"const S=struct{a:u8,}; fn f(p:*const volatile S)S{return p.*;}pub fn main()void{var s:S=.{.a=1};_=f(&s);}", "not supported by szc yet:"},
		/* nyi/default-field-signature */
		{"const S=struct{a:u8=7,};fn f(s:S)S{return s;}pub fn main()void{_=f(.{.a=1});}", "not supported by szc yet:"},
		/* nyi/generic-call */
		{"const S=struct{a:u8,}; fn f(comptime T:type,s:T)T{return s;}pub fn main()void{_=f(S,.{.a=1});}", "not supported by szc yet:"},
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
		const char *source = cases[i].source;
		FileId file = source_add_memory(&sources, "test.zig", source, (uint32_t)strlen(source));
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		bool success = false;
		Sema *sema = sema_create(&arena, &diag, &interner, &sources);
		if (!diag_has_errors(&diag))
			success = sema_analyze_executable(sema, file, ir);
		CHECK(success == (cases[i].error == NULL));
		if (cases[i].error != NULL) {
			CHECK(diag.count != 0 && strstr(diag.items[0].message, cases[i].error) != NULL);
		} else if (success) {
			FILE *output = tmpfile();
			FILE *assembly = tmpfile();
			CHECK(output != NULL && assembly != NULL);
			if (output != NULL && assembly != NULL)
				CHECK(codegen_qbe(output, assembly, sema, &diag));
			if (output != NULL)
				fclose(output);
			if (assembly != NULL)
				fclose(assembly);
		}
		arena_free(&arena);
	}
}

/* Failed total layouts must not publish even an address-only mutable symbol. */
static void
test_struct_global_publication(void)
{
	const char *cases[] = {
		"const S=struct{x:u16,};var huge:[18446744073709551615]S=undefined;pub fn main()void{_=&huge;}",
		"const S=struct{x:u16,};var huge:[18446744073709551615]S=undefined;pub fn main()void{_=&huge[0].x;}",
		"const S=struct{x:[18446744073709551615]u64,};var huge:S=undefined;pub fn main()void{_=&huge;}",
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
		FileId file = source_add_memory(&sources, "test.zig", cases[i], (uint32_t)strlen(cases[i]));
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		Sema *sema = sema_create(&arena, &diag, &interner, &sources);
		CHECK(!diag_has_errors(&diag));
		CHECK(!sema_analyze_executable(sema, file, ir));
		CHECK(diag.count != 0 && strstr(diag.items[0].message, "exceeds 64-bit address space") != NULL);
		CHECK(sema_global_count(sema) == 1);
		arena_free(&arena);
	}
}

/* Mutable globals retain runtime reads and must serialize complete writable data.
 * Paired sema/emission checks keep publication and backend admission consistent. */
static void
test_struct_mutable_globals(void)
{
	const struct {
		const char *source;
		const char *error;
	} cases[] = {
		/* literal */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=1,.b=2}; pub fn main()void{g.a=7;_=g.b;}", NULL},
		/* inferred */
		{"const S=struct{a:u8,b:u16,}; var g=S{.a=1,.b=2};pub fn main()void{g.b=513;}", NULL},
		/* explicit-init */
		{"const S=struct{a:u8,b:u16,}; var g:S=S{.a=1,.b=2};pub fn main()void{_=g.a;}", NULL},
		/* as-init */
		{"const S=struct{a:u8,b:u16,}; var g:S=@as(S,.{.a=1,.b=2});pub fn main()void{_=g.a;}", NULL},
		/* const-source */
		{"const S=struct{a:u8,b:u16,}; const initial:S=.{.a=7,.b=513};var g:S=initial;pub fn main()void{g.a=2;}", NULL},
		/* undefined */
		{"const S=struct{a:u8,b:u16,}; var g:S=undefined;pub fn main()void{g.a=7;g.b=513;_=g.a;}", NULL},
		/* partial-undefined */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=undefined,.b=513};pub fn main()void{g.a=7;}", NULL},
		/* root-overalign */
		{"const S=struct{a:u8,b:u16,}; var g:S align(64)=.{.a=1,.b=2};pub fn main()void{g.a=7;_= &g;}", NULL},
		/* dot-alias */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=1,.b=2}; const alias:*S=&g;pub fn main()void{alias.a=7;_=g.a;}", NULL},
		/* builtin-alias */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=1,.b=2}; pub fn main()void{@field(&g,\"b\")=513;}", NULL},
		/* whole-snapshot */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=1,.b=2}; pub fn main()void{const old=g;g.a=7;_=old.b;}", NULL},
		/* contextual-overwrite */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=1,.b=2}; pub fn main()void{g=.{.a=@intCast(g.b),.b=g.a};}", NULL},
		/* explicit-overwrite */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=1,.b=2}; pub fn main()void{g=S{.a=@intCast(g.b),.b=g.a};}", NULL},
		/* as-overwrite */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=1,.b=2}; pub fn main()void{g=@as(S,.{.a=@intCast(g.b),.b=g.a});}", NULL},
		/* named-distinct */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=1,.b=2}; var h:S=.{.a=1,.b=2};pub fn main()void{const n:u8=if(&g!=&h)0 else 256;_=n;}", NULL},
		/* field-qualifiers */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=1,.b=2}; pub fn main()void{const p:*align(1)volatile allowzero S=&g;const q:*align(1)volatile allowzero u16=&p.b;q.*=513;}", NULL},
		/* volatile-scalar */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=1,.b=2}; pub fn main()void{const p:*volatile S=&g;p.a=7;_=p.b;}", NULL},
		/* internal-value */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=1,.b=2}; fn identity(s:S)S{return s;}pub fn main()void{const old=identity(g);g.a=9;_=old.a;}", NULL},
		/* internal-alias */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=1,.b=2}; fn f(s:S,p:*S)S{p.a=9;return s;}pub fn main()void{g=f(g,&g);}", NULL},
		/* first-arg-before-mutate */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=1,.b=2}; fn change()u8{g.a=9;return 1;}fn f(s:S,n:u8)u8{_=n;return s.a;}pub fn main()void{_=f(g,change());}", NULL},
		/* defer-return */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=1,.b=2}; fn f()S{defer g.a=9;return g;}pub fn main()void{g=f();}", NULL},
		/* dynamic-root */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=1,.b=2}; fn root()*S{return &g;}pub fn main()void{root().a=7;_=root().b;}", NULL},
		/* nested */
		{"const S=struct{a:u8,b:u16,}; const O=struct{i:S,tag:u8,};var g:O=.{.i=.{.a=1,.b=2},.tag=3};pub fn main()void{const old=g.i;g.i.a=7;_=old.b;}", NULL},
		/* nested-volatile */
		{"const S=struct{a:u8,b:u16,}; const O=struct{i:S,};var g:O=.{.i=.{.a=1,.b=2}};pub fn main()void{const p:*volatile O=&g;p.i.b=513;_=p.i.a;}", NULL},
		/* array-of-struct */
		{"const S=struct{a:u8,b:u16,}; var a:[2]S=.{.{.a=1,.b=2},.{.a=3,.b=4}};fn index()usize{return 1;}pub fn main()void{a[index()].a=7;}", NULL},
		/* recursive-array */
		{"const S=struct{a:u8,b:u16,}; var a:[2][1]S=.{.{.{.a=1,.b=2}},.{.{.a=3,.b=4}}};pub fn main()void{const old=a;a[1][0].b=513;_=old[0][0].a;}", NULL},
		/* array-abi */
		{"const S=struct{a:u8,b:u16,}; var a:[1]S=.{.{.a=1,.b=2}};fn identity(x:[1]S)[1]S{return x;}pub fn main()void{a=identity(a);}", NULL},
		/* nested-array */
		{"const S=struct{a:u8,b:u16,}; const O=struct{rows:[2]S,};var g:O=.{.rows=.{.{.a=1,.b=2},.{.a=3,.b=4}}};pub fn main()void{g.rows[1].b=513;}", NULL},
		/* slice-view-of-global-array */
		{"const S=struct{a:u8,b:u16,}; var a:[2]S=.{.{.a=1,.b=2},.{.a=3,.b=4}};pub fn main()void{const s:[]S=&a;s[1].a=7;}", NULL},
		/* sentinel-field */
		{"const T=struct{bytes:[2:9]u8,};var g:T=.{.bytes=.{1,2}};pub fn main()void{g.bytes[1]=7;_=g.bytes[2];}", NULL},
		/* zero-sentinel-field */
		{"const T=struct{bytes:[0:9]u8,x:u8,};var g:T=.{.bytes=.{},.x=7};pub fn main()void{g.x=8;_=g.bytes[0];}", NULL},
		/* empty-struct */
		{"const E=struct{};var g:E=.{};pub fn main()void{g=E{};_= &g;}", NULL},
		/* zero-array */
		{"const S=struct{a:u8,b:u16,}; var a:[0]S=.{};pub fn main()void{_= &a;}", NULL},
		/* zero-nested-array */
		{"const S=struct{a:u8,b:u16,}; var a:[2][0]S=.{.{},.{}};pub fn main()void{const old=a;_=old.len;}", NULL},
		/* zero-align-field */
		{"const T=struct{z:[0]u64,v:void,n:u0,x:u8,};var g:T=.{.z=.{},.v={},.n=0,.x=7};pub fn main()void{g.x=8;g.n=0;}", NULL},
		/* pointer-field */
		{"const T=struct{p:*u8,};var byte:u8=7;var g:T=.{.p=&byte};pub fn main()void{g.p.*=8;}", NULL},
		/* slice-field */
		{"const T=struct{s:[]u8,};var bytes=[_]u8{1,2,3};var g:T=.{.s=&bytes};pub fn main()void{const old=g;g.s=g.s[0..1];old.s[1]=7;}", NULL},
		/* slice-volatile-descriptor */
		{"const T=struct{s:[]u8,};var bytes=[_]u8{1,2,3};var g:T=.{.s=&bytes};pub fn main()void{const p:*volatile T=&g;const n=p.s.len;_=n;}", NULL},
		/* forward-address */
		{"const S=struct{a:u8,b:u16,}; const H=struct{p:*S,};var h:H=.{.p=&later};var later:S=.{.a=7,.b=2};pub fn main()void{h.p.a=8;}", NULL},
		/* self-address */
		{"const N=struct{p:*N,x:u8,};var g:N=.{.p=&g,.x=7};pub fn main()void{g.p.x=8;}", NULL},
		/* mutual-address */
		{"const N=struct{p:*N,x:u8,};var a:N=.{.p=&b,.x=7};var b:N=.{.p=&a,.x=8};pub fn main()void{a.p.p.x=9;}", NULL},
		/* callback */
		{"const S=struct{a:u8,b:u16,}; fn f(s:S)S{return s;}const H=struct{cb:*const fn(S)S,};var h:H=.{.cb=&f};pub fn main()void{_=h.cb(.{.a=7,.b=2});}", NULL},
		/* callback-array */
		{"const S=struct{a:u8,b:u16,}; fn f(s:S)S{return s;}const H=struct{a:[1]*const fn(S)S,};var h:H=.{.a=.{&f}};pub fn main()void{_=h.a[0](.{.a=7,.b=2});}", NULL},
		/* unused-bad-init */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=256,.b=2};pub fn main()void{}", NULL},
		/* unused-bad-callback */
		{"const S=struct{a:u8,b:u16,}; fn bad(s:S)S{_=s;return .{.a=\"bad\",.b=2};}const H=struct{cb:*const fn(S)S,};var h:H=.{.cb=&bad};pub fn main()void{}", NULL},
		/* error/overflow */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=256,.b=2};pub fn main()void{_=g.a;}", "type 'u8' cannot represent integer value '256'"},
		/* error/typeof-live-init */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=256,.b=2};pub fn main()void{_=@TypeOf(&g);}", "type 'u8' cannot represent integer value '256'"},
		/* error/missing */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=1};pub fn main()void{_=g.a;}", "missing struct field: b"},
		/* error/unknown */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=1,.b=2,.extra=3};pub fn main()void{_=g.a;}", "no field named 'extra' in struct 'test.S'"},
		/* error/duplicate */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=1,.a=2,.b=3};pub fn main()void{_=g.a;}", "duplicate struct field name"},
		/* error/mutable-read-init */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=1,.b=2}; var h:S=g;pub fn main()void{_=h.a;}", "unable to resolve comptime value"},
		/* error/const-read-init */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=1,.b=2}; const h:S=g;pub fn main()void{_=h.a;}", "unable to resolve comptime value"},
		/* error/const-alias-write */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=1,.b=2}; pub fn main()void{const p:*const S=&g;p.a=7;}", "cannot assign to constant"},
		/* error/bad-field */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=1,.b=2}; pub fn main()void{g.missing=7;}", "no field named 'missing' in struct 'test.S'"},
		/* error/nominal-global-assign */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=1,.b=2}; const T=struct{a:u8,b:u16,};const t:T=.{.a=1,.b=2};pub fn main()void{g=t;}", "expected type 'test.S', found 'test.T'"},
		/* error/array-len-live-init */
		{"const S=struct{a:u8,b:u16,}; var a:[1]S=.{.{.a=256,.b=2}};pub fn main()void{_=a.len;}", "type 'u8' cannot represent integer value '256'"},
		/* error/array-oob */
		{"const S=struct{a:u8,b:u16,}; var a:[1]S=.{.{.a=1,.b=2}};pub fn main()void{a[1].a=7;}", "index 1 outside array of length 1"},
		/* error/discard-volatile */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=1,.b=2}; pub fn main()void{const p:*volatile S=&g;const q:*u8=&p.a;_=q;}", "expected type '*u8', found '*volatile u8'"},
		/* error/promote-alignment */
		{"const S=struct{a:u8,b:u16,}; var g:S=.{.a=1,.b=2}; pub fn main()void{const p:*align(1)S=&g;const q:*u16=&p.b;_=q;}", "expected type '*u16', found '*align(1) u16'"},
		/* error/callback-live-bad-body */
		{"const S=struct{a:u8,b:u16,}; fn bad(s:S)S{_=s;return .{.a=\"bad\",.b=2};}const H=struct{cb:*const fn(S)S,};var h:H=.{.cb=&bad};extern fn consume(p:*H)void;pub fn main()void{consume(&h);}", "expected type 'u8', found '*const [3:0]u8'"},
		/* scope/tls */
		{"const S=struct{a:u8,}; threadlocal var g:S=.{.a=1};pub fn main()void{g.a=2;}", "not supported by szc yet:"},
		/* scope/export-global */
		{"const S=struct{a:u8,}; export var g:S=.{.a=1};pub fn main()void{g.a=2;}", "not supported by szc yet:"},
		/* scope/extern-global */
		{"const S=struct{a:u8,}; extern var g:S;pub fn main()void{g.a=2;}", "not supported by szc yet:"},
		/* scope/packed */
		{"const S=packed struct{a:u8,};var g:S=.{.a=1};pub fn main()void{_= &g;}", "not supported by szc yet:"},
		/* scope/default */
		{"const S=struct{a:u8=7,};var g:S=.{};pub fn main()void{_= &g;}", "not supported by szc yet:"},
		/* scope/field-align */
		{"const S=struct{a:u8 align(16),};var g:S=.{.a=1};pub fn main()void{_= &g;}", "not supported by szc yet:"},
		/* scope/whole-volatile-load */
		{"const S=struct{a:u8,}; var g:S=.{.a=1};pub fn main()void{const p:*volatile S=&g;const old=p.*;_=old;}", "not supported by szc yet:"},
		/* scope/whole-volatile-store */
		{"const S=struct{a:u8,}; var g:S=.{.a=1};pub fn main()void{const p:*volatile S=&g;p.*=S{.a=2};}", "not supported by szc yet:"},
		/* scope/initializer-function-call */
		{"const S=struct{a:u8,}; fn make()S{return .{.a=7};}var g:S=make();pub fn main()void{_=g.a;}", "not supported by szc yet:"},
		/* scope/opv-global-comptime-read */
		{"const Z=struct{a:u0,v:void,};var a:Z=.{.a=0,.v={}};var b:Z=a;pub fn main()void{_= &b;}", "not supported by szc yet:"},
		/* scope/huge-array-before-publication */
		{"const S=struct{a:u16,};var a:[9223372036854775808]S=undefined;pub fn main()void{_= &a;}", "not supported by szc yet:"},
		/* scope/self-value-cycle */
		{"const S=struct{a:u8,}; var g:S=g;pub fn main()void{_=g.a;}", "depends on itself"},
		/* scope/mutual-value-cycle */
		{"const S=struct{a:u8,}; var a:S=b;var b:S=a;pub fn main()void{_=a.a;}", "depends on itself"},
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
		const char *source = cases[i].source;
		FileId file = source_add_memory(&sources, "test.zig", source, (uint32_t)strlen(source));
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		bool success = false;
		Sema *sema = sema_create(&arena, &diag, &interner, &sources);
		if (!diag_has_errors(&diag))
			success = sema_analyze_executable(sema, file, ir);
		CHECK(success == (cases[i].error == NULL));
		if (cases[i].error != NULL) {
			CHECK(diag.count != 0 && strstr(diag.items[0].message, cases[i].error) != NULL);
		} else if (success) {
			FILE *output = tmpfile();
			FILE *assembly = tmpfile();
			CHECK(output != NULL && assembly != NULL);
			if (output != NULL && assembly != NULL)
				CHECK(codegen_qbe(output, assembly, sema, &diag));
			if (output != NULL)
				fclose(output);
			if (assembly != NULL)
				fclose(assembly);
		}
		arena_free(&arena);
	}
}

static void
test_runtime_struct_initializer_ir(void)
{
	const struct {
		const char *source;
		uint32_t pointer_inits;
		uint32_t value_inits;
		uint32_t empty_inits;
		uint32_t nested_destinations;
		uint32_t field_refs;
		uint32_t read_refs;
		uint32_t named_read_refs;
		uint32_t field_values;
		uint32_t field_pointers;
		uint32_t index_pointers;
	} cases[] = {
		/* Source order differs from declaration order; quoted a is normalized. */
		{"const S=struct{a:u8,b:u8}; fn f() void {var s:S=undefined; s=.{.b=2,.@\"\\x61\"=1};}", 1, 0, 0, 0, 0, 0, 0, 0, 2, 0},
		/* A nested contextual body receives its parent's actual field pointer. */
		{"const I=struct{x:u8}; const S=struct{inner:I}; fn f() void {var s:S=undefined; s=.{.inner=.{.x=1}};}", 2, 0, 0, 1, 0, 0, 0, 0, 2, 0},
		/* Explicit and @as RHS boundaries must not inherit that destination. */
		{"const I=struct{x:u8}; const S=struct{inner:I}; fn f() void {var s:S=undefined; s=.{.inner=I{.x=1}};}", 1, 1, 0, 0, 0, 0, 0, 0, 1, 0},
		{"const I=struct{x:u8}; const S=struct{inner:I}; fn f() void {var s:S=undefined; s=.{.inner=@as(I,.{.x=1})};}", 1, 1, 0, 0, 0, 0, 0, 0, 1, 0},
		/* Empty destinations retain ambiguous array/struct semantic dispatch. */
		{"fn f() void {var a:[0]u8=undefined; var b:[0:7]u8=undefined; a=.{}; b=.{};}", 0, 0, 2, 0, 0, 0, 0, 0, 0, 0},
		{"const S=struct{}; fn f() void {var s:S=undefined; s=.{};}", 0, 0, 1, 0, 0, 0, 0, 0, 0, 0},
		/* These chains must project from references, not whole aggregate reads. */
		{"const S=struct{x:u8}; fn f(p:[*]volatile S) void {_=p[0].x;}", 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
		{"const S=struct{x:u8}; fn f(p:*volatile S) void {_=(p.*).x;}", 0, 0, 0, 0, 1, 0, 0, 0, 0, 0},
		{"const I=struct{x:u8}; const S=struct{inner:I}; fn f(p:*volatile S) void {_=p.inner.x;}", 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
		{"const I=struct{x:u8}; const S=struct{inner:I}; fn f(p:*volatile S) void {_=(@field(p,\"inner\")).x;}", 0, 0, 0, 0, 1, 0, 1, 0, 0, 0},
		/* Ordinary direct identifier reads and true address-taking stay distinct. */
		{"const S=struct{x:u8}; fn f(p:*volatile S) void {_=p.x;}", 0, 0, 0, 0, 0, 0, 0, 1, 0, 0},
		{"const I=struct{x:u8}; const S=struct{inner:I}; fn f(p:*S) void {_=&p.inner.x;}", 0, 0, 0, 0, 0, 0, 0, 0, 2, 0},
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
		const char *source = cases[i].source;
		FileId file = source_add_memory(&sources, "runtime_struct_initializer_ir.zig", source, (uint32_t)strlen(source));
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		CHECK(!diag_has_errors(&diag));
		if (diag_has_errors(&diag)) {
			arena_free(&arena);
			continue;
		}
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		CHECK(!diag_has_errors(&diag));
		if (diag_has_errors(&diag)) {
			arena_free(&arena);
			continue;
		}
		uint32_t counts[IR_OP_COUNT] = {0};
		uint32_t nested_destinations = 0;
		bool protocol = true;
		bool source_order = true;
		bool normalized_order = true;
		bool borrowed_reads = true;
		for (uint32_t j = 0; j < ir->inst_count; j++) {
			const IrInst *inst = &ir->insts[j];
			counts[inst->op]++;
			if (inst->op == IR_FIELD_REF) {
				IrRef base = inst->data.name.operand;
				if (!ir_ref_is_inst(base))
					borrowed_reads = false;
				else {
					IrOp op = ir->insts[ir_ref_inst(base)].op;
					borrowed_reads = borrowed_reads && op != IR_FIELD_VAL
						&& op != IR_FIELD_NAMED_VAL && op != IR_ARRAY_INDEX && op != IR_DEREF;
				}
			}
			if (inst->op != IR_STRUCT_INIT_PTR)
				continue;
			IrStructInitPtr header;
			IR_READ(ir, inst->data.payload, &header);
			if (ir_ref_is_inst(header.pointer) && ir->insts[ir_ref_inst(header.pointer)].op == IR_FIELD_PTR)
				nested_destinations++;
			uint32_t cursor = inst->data.payload + IR_WORDS(IrStructInitPtr);
			uint32_t previous_src = 0;
			for (uint32_t k = 0; k < header.field_count; k++) {
				IrStructInitField field;
				IR_READ(ir, cursor, &field);
				cursor += IR_WORDS(IrStructInitField);
				if (k != 0 && field.src <= previous_src)
					source_order = false;
				previous_src = field.src;
				if (i == 0)
					normalized_order = normalized_order && strcmp(intern_text(&interner, field.name), k == 0 ? "b" : "a") == 0;
				uint32_t length = ir_body_count(ir, field.body);
				if (length < 2) {
					protocol = false;
					continue;
				}
				const IrInstIndex *body = ir_body_insts(ir, field.body);
				const IrInst *first = &ir->insts[body[0]];
				const IrInst *last = &ir->insts[body[length - 1]];
				protocol = protocol && first->op == IR_FIELD_PTR
					&& first->data.name.operand == header.pointer && first->data.name.name == field.name
					&& last->op == IR_BREAK_INLINE && last->data.brk.block == IR_BODY_RESULT;
				/* Every contextual pointee hint in this body uses the derived field,
				 * never the enclosing aggregate pointer or a synthetic type value. */
				for (uint32_t n = 1; n + 1 < length; n++) {
					const IrInst *part = &ir->insts[body[n]];
					if (part->op == IR_ELEM_TYPE)
						protocol = protocol && part->data.un.operand == ir_ref_of_inst(body[0]);
				}
			}
		}
		CHECK(counts[IR_STRUCT_INIT_PTR] == cases[i].pointer_inits);
		CHECK(counts[IR_STRUCT_INIT] == cases[i].value_inits);
		CHECK(counts[IR_INIT_EMPTY_PTR] == cases[i].empty_inits);
		CHECK(nested_destinations == cases[i].nested_destinations);
		CHECK(protocol && source_order && normalized_order);
		CHECK(counts[IR_FIELD_REF] == cases[i].field_refs && borrowed_reads);
		CHECK(counts[IR_FIELD_READ_REF] == cases[i].read_refs);
		CHECK(counts[IR_FIELD_NAMED_READ_REF] == cases[i].named_read_refs);
		CHECK(counts[IR_FIELD_VAL] == cases[i].field_values);
		CHECK(counts[IR_FIELD_PTR] == cases[i].field_pointers);
		CHECK(counts[IR_ARRAY_INDEX_PTR] == cases[i].index_pointers);
		CHECK(counts[IR_DEREF] == 0 && counts[IR_ARRAY_INDEX] == 0);
		arena_free(&arena);
	}
}


static void
test_struct_static_values(void)
{
	const struct {
		const char *source;
		const char *error;
	} cases[] = {
		/* dot */
		{"const S=struct { a:u8, wide:u64, }; const s:S=.{.a=7,.wide=19};  pub fn main() void { const p=&s.a; const n:u8=if(p.*==7 and @TypeOf(p)==*const u8)0 else 256; _=n; }", NULL},
		/* builtin */
		{"const S=struct { a:u8, wide:u64, }; const s:S=.{.a=7,.wide=19};  pub fn main() void { const p=&@field(s,\"a\"); const n:u8=if(p.*==7 and p==&s.a)0 else 256; _=n; }", NULL},
		/* pointer-base */
		{"const S=struct { a:u8, wide:u64, }; const s:S=.{.a=7,.wide=19};  pub fn main() void { const p=&s; const q=&p.a; const n:u8=if(q.*==7 and q==&s.a)0 else 256; _=n; }", NULL},
		/* pointer-base-builtin */
		{"const S=struct { a:u8, wide:u64, }; const s:S=.{.a=7,.wide=19};  pub fn main() void { const p=&s; const q=&@field(p,\"wide\"); const n:u8=if(q.*==19 and q==&s.wide)0 else 256; _=n; }", NULL},
		/* qualified */
		{"const S=struct { a:u8, wide:u64, }; const s:S=.{.a=7,.wide=19};  pub fn main() void { const p:*const volatile allowzero S=&s; const n:u8=if(@TypeOf(&p.a)==*const volatile allowzero u8 and @TypeOf(&p.wide)==*const volatile allowzero u64)0 else 256; _=n; }", NULL},
		/* underaligned */
		{"const S=struct { a:u8, wide:u64, }; const s:S=.{.a=7,.wide=19};  pub fn main() void { const p:*align(1) const S=&s; const n:u8=if(@TypeOf(&p.wide)==*align(1) const u64 and @TypeOf(&p.a)==*const u8)0 else 256; _=n; }", NULL},
		/* underaligned-all-flags */
		{"const S=struct { a:u8, wide:u64, }; const s:S=.{.a=7,.wide=19};  pub fn main() void { const p:*align(1) const volatile allowzero S=&s; const n:u8=if(@TypeOf(&p.wide)==*align(1) const volatile allowzero u64)0 else 256; _=n; }", NULL},
		/* qualifier-cast */
		{"const S=struct { a:u8, wide:u64, }; const s:S=.{.a=7,.wide=19};  pub fn main() void { const p:*const volatile u8=&s.a; _=p; const n:u8=if(@TypeOf(&s.a)==*const u8)0 else 256; _=n; }", NULL},
		/* root-deref */
		{"const S=struct { a:u8, wide:u64, }; const s:S=.{.a=7,.wide=19};  pub fn main() void { const p=&s; const q=&p.*.a; const n:u8=if(q.*==7 and q==&s.a)0 else 256; _=n; }", NULL},
		/* name/space */
		{"const S=struct {@\"two words\":u8,}; const s=S{.@\"two words\"=7}; pub fn main() void { const p=&@field(s,\"two words\"); const n:u8=if(p.*==7 and p==&s.@\"two words\")0 else 256; _=n; }", NULL},
		/* name/keyword */
		{"const S=struct {@\"fn\":u8,}; const s=S{.@\"fn\"=7}; pub fn main() void { const p=&@field(s,\"fn\"); const n:u8=if(p.*==7 and p==&s.@\"fn\")0 else 256; _=n; }", NULL},
		/* name/hex */
		{"const S=struct {@\"\\x61\":u8,}; const s=S{.@\"\\x61\"=7}; pub fn main() void { const p=&@field(s,\"a\"); const n:u8=if(p.*==7 and p==&s.@\"\\x61\")0 else 256; _=n; }", NULL},
		/* name/nul */
		{"const S=struct { a:u8, wide:u64, }; const s:S=.{.a=7,.wide=19};  pub fn main() void { const p=&@field(s,\"a\\x00\"); const n:u8=if(p.*==7 and p==&s.a)0 else 256; _=n; }", NULL},
		/* name/concat */
		{"const S=struct { a:u8, wide:u64, }; const s:S=.{.a=7,.wide=19};  pub fn main() void { const p=&@field(s,\"\"++\"a\"); const n:u8=if(p.*==7 and p==&s.a)0 else 256; _=n; }", NULL},
		/* name/array */
		{"const S=struct { a:u8, wide:u64, }; const s:S=.{.a=7,.wide=19};  pub fn main() void { const p=&@field(s,&[_]u8{97}); const n:u8=if(p.*==7 and p==&s.a)0 else 256; _=n; }", NULL},
		/* nested */
		{"const I=struct { x:u16, }; const S=struct { inner:I, tag:u8, }; const s:S=.{.inner=.{.x=513},.tag=7}; pub fn main() void { const p=&s.inner.x; const n:u8=if(p.*==513 and p==&@field(@field(s,\"inner\"),\"x\"))0 else 256; _=n; }", NULL},
		/* field-array-field */
		{"const I=struct { x:u16, }; const S=struct { a:[2]I, }; const s:S=.{.a=.{.{.x=11},.{.x=22}}}; pub fn main() void { const p=&s.a[1].x; const n:u8=if(p.*==22 and p==&@field(s.a[1],\"x\"))0 else 256; _=n; }", NULL},
		/* array-field */
		{"const S=struct { a:u8, }; const a=[_]S{.{.a=2},.{.a=9}}; pub fn main() void { const p=&a[1].a; const n:u8=if(p.*==9 and p!=&a[0].a)0 else 256; _=n; }", NULL},
		/* matrix-path */
		{"const S=struct { a:[2][2]u16, }; const s:S=.{.a=.{.{1,2},.{3,4}}}; pub fn main() void { const p=&s.a[1][0]; const n:u8=if(p.*==3)0 else 256; _=n; }", NULL},
		/* sentinel-field */
		{"const S=struct { a:[2:9]u8, }; const s:S=.{.a=.{3,5}}; pub fn main() void { const p=&s.a[2]; const n:u8=if(p.*==9 and @TypeOf(&s.a)==*const [2:9]u8)0 else 256; _=n; }", NULL},
		/* zero-sentinel-field */
		{"const S=struct { a:[0:9]u8, }; const s:S=.{.a=.{}}; pub fn main() void { const p=&s.a[0]; const n:u8=if(p.*==9)0 else 256; _=n; }", NULL},
		/* slice-field */
		{"const S=struct { text:[]const u8, }; const s:S=.{.text=\"abc\"}; pub fn main() void { const p=&s.text.len; const q=&s.text.ptr; const n:u8=if(p.*==3 and q.*[1]==98)0 else 256; _=n; }", NULL},
		/* pointer-field */
		{"const S=struct { p:*const u8, }; const byte:u8=31; const s:S=.{.p=&byte}; pub fn main() void { const p=&s.p; const n:u8=if(p.*.*==31)0 else 256; _=n; }", NULL},
		/* pointer-array-field */
		{"const S=struct { p:[2]*const u8, }; const a:u8=11; const b:u8=22; const s:S=.{.p=.{&a,&b}}; pub fn main() void { const p=&s.p[1]; const n:u8=if(p.*.*==22)0 else 256; _=n; }", NULL},
		/* undefined-target-type */
		{"const S=struct { a:u8, wide:u64, }; const s:S=undefined; pub fn main() void { const p=&s.a; const v=p.*; _=v; const n:u8=if(@TypeOf(p)==*const u8 and @TypeOf(p.*)==u8)0 else 256; _=n; }", NULL},
		/* partial-undefined */
		{"const S=struct { a:u8, wide:u64, }; const s:S=.{.a=undefined,.wide=19}; pub fn main() void { const p=&s.a; _=p; const n:u8=if((&s.wide).*==19)0 else 256; _=n; }", NULL},
		/* empty-field */
		{"const E=struct {}; const S=struct { e:E, a:u8, }; const s:S=.{.e=.{},.a=7}; pub fn main() void { const p=&s.e; const n:u8=if(@TypeOf(p)==*const E and @TypeOf(p.*)==E)0 else 256; _=n; }", NULL},
		/* zero-overlap */
		{"const S=struct {a:void,b:u0,c:[0]u8,x:u8,}; const s:S=.{.a={},.b=0,.c=.{},.x=7}; pub fn main() void { const a:*const anyopaque=@ptrCast(&s.a); const b:*const anyopaque=@ptrCast(&s.b); const c:*const anyopaque=@ptrCast(&s.c); const n:u8=if(@TypeOf(a)==*const anyopaque and @TypeOf(b)==*const anyopaque and @TypeOf(c)==*const anyopaque and (&s.b).*==0 and (&s.c).*.len==0)0 else 256; _=n; }", NULL},
		/* global-distinct */
		{"const S=struct { a:u8, wide:u64, }; const a:S=.{.a=7,.wide=19}; const b:S=.{.a=7,.wide=19}; pub fn main() void {  const n:u8=if(&a==&a and &a!=&b and &a.a!=&b.a)0 else 256; _=n; }", NULL},
		/* local-dedup */
		{"const S=struct { a:u8, wide:u64, };  pub fn main() void { const a:S=.{.a=7,.wide=19}; const b:S=.{.a=7,.wide=19}; const n:u8=if(&a==&b and &a.a==&b.a)0 else 256; _=n; }", NULL},
		/* temporary-dedup */
		{"const S=struct { a:u8, wide:u64, };  pub fn main() void { const a=&S{.a=7,.wide=19}; const b=&S{.a=7,.wide=19}; const n:u8=if(a==b and &a.a==&b.a)0 else 256; _=n; }", NULL},
		/* global-local-distinct */
		{"const S=struct { a:u8, wide:u64, }; const s:S=.{.a=7,.wide=19};  pub fn main() void { const local:S=.{.a=7,.wide=19}; const n:u8=if(&s!=&local and &s.a!=&local.a)0 else 256; _=n; }", NULL},
		/* root-overalignment */
		{"const S=struct { a:u8, wide:u64, }; const s:S align(128)=.{.a=7,.wide=19}; pub fn main() void {  const n:u8=if(@TypeOf(&s)==*align(128) const S and @TypeOf(&s.wide)==*const u64 and @TypeOf(&s.a)==*const u8)0 else 256; _=n; }", NULL},
		/* completed-struct-dependency */
		{"const I=struct {x:u32,}; const S=struct {p:*const I,}; const s:S=.{.p=&inner}; const inner:I=.{.x=73}; pub fn main() void { const p=&s.p.x; const n:u8=if(p.*==73 and p==&inner.x)0 else 256; _=n; }", NULL},
		/* completed-forward */
		{"const S=struct {p:*const u8,}; const s:S=.{.p=&byte}; const byte:u8=31; pub fn main() void {  const n:u8=if((&s.p).*.*==31)0 else 256; _=n; }", NULL},
		/* error/discard-const */
		{"const S=struct { a:u8, wide:u64, }; const s:S=.{.a=7,.wide=19};  pub fn main() void { const p:*u8=&s.a; _=p; }", "expected type '*u8', found '*const u8'"},
		/* error/discard-volatile */
		{"const S=struct { a:u8, wide:u64, }; const s:S=.{.a=7,.wide=19};  pub fn main() void { const p:*const volatile S=&s; const q:*const u8=&p.a; _=q; }", "expected type '*const u8', found '*const volatile u8'"},
		/* error/discard-allowzero */
		{"const S=struct { a:u8, wide:u64, }; const s:S=.{.a=7,.wide=19};  pub fn main() void { const p:*allowzero const S=&s; const q:*const u8=&p.a; _=q; }", "expected type '*const u8', found '*allowzero const u8'"},
		/* error/promote-alignment */
		{"const S=struct { a:u8, wide:u64, }; const s:S=.{.a=7,.wide=19};  pub fn main() void { const p:*align(16) const u64=&s.wide; _=p; }", "expected type '*align(16) const u64', found '*const u64'"},
		/* error/under-promotion */
		{"const S=struct { a:u8, wide:u64, }; const s:S=.{.a=7,.wide=19};  pub fn main() void { const p:*align(1) const S=&s; const q:*const u64=&p.wide; _=q; }", "expected type '*const u64', found '*align(1) const u64'"},
		/* error/undefined-base */
		{"const S=struct { a:u8, wide:u64, };  pub fn main() void { const p:*const S=undefined; _=&p.a; }", "use of undefined value here causes illegal behavior"},
		/* error/undefined-base-type */
		{"const S=struct { a:u8, wide:u64, };  pub fn main() void { const p:*const S=undefined; _=@TypeOf(&p.a); }", "use of undefined value here causes illegal behavior"},
		/* error/undefined-read */
		{"const S=struct { a:u8, wide:u64, }; const s:S=undefined; pub fn main() void { const p=&s.a; const n:u8=if(p.*==0)0 else 256; _=n; }", "use of undefined value here causes illegal behavior"},
		/* error/partial-undefined-read */
		{"const S=struct { a:u8, wide:u64, }; const s:S=.{.a=undefined,.wide=19}; pub fn main() void { const n:u8=if((&s.a).*==0)0 else 256; _=n; }", "use of undefined value here causes illegal behavior"},
		/* error/missing */
		{"const S=struct { a:u8, wide:u64, }; const s:S=.{.a=7,.wide=19};  pub fn main() void { _=&s.missing; }", "no field named 'missing' in struct 'test.S'"},
		/* error/declaration */
		{"const S=struct {a:u8,pub const decl:u8=7;}; const s:S=.{.a=7}; pub fn main() void { _=&s.decl; }", "no field named 'decl' in struct 'test.S'"},
		/* error/double-nul */
		{"const S=struct { a:u8, wide:u64, }; const s:S=.{.a=7,.wide=19};  pub fn main() void { _=&@field(s,\"a\\x00\\x00\"); }", "no field named 'a"},
		/* error/nul-tail */
		{"const S=struct { a:u8, wide:u64, }; const s:S=.{.a=7,.wide=19};  pub fn main() void { _=&@field(s,\"a\\x00tail\"); }", "no field named 'a"},
		/* error/bad-name */
		{"const S=struct { a:u8, wide:u64, }; const s:S=.{.a=7,.wide=19};  pub fn main() void { _=&@field(s,3); }", "expected type '[]const u8', found 'comptime_int'"},
		/* error/undefined-name */
		{"const S=struct { a:u8, wide:u64, }; const s:S=.{.a=7,.wide=19};  pub fn main() void { _=&@field(s,undefined); }", "use of undefined value here causes illegal behavior"},
		/* error/oob-array */
		{"const S=struct { a:[2]u8, }; const s:S=.{.a=.{1,2}}; pub fn main() void { _=&s.a[2]; }", "index 2 outside array of length 2"},
		/* equality/same-field-casts */
		{"const S=struct {wide:u64,byte:u8,}; const s:S=.{.wide=9,.byte=7};  pub fn main() void { const p:*align(1) const volatile allowzero u64=&s.wide; const n:u8=if(@as(*allowzero const volatile anyopaque,@ptrCast(p))==@as(*allowzero const volatile anyopaque,@ptrCast(&s.wide)))0 else 256; _=n; }", NULL},
		/* equality/root-offset-provenance */
		{"const S=struct {wide:u64,byte:u8,}; const s:S=.{.wide=9,.byte=7};  pub fn main() void { const bytes:[*]const u8=@ptrCast(&s); const field_bytes:[*]const u8=@ptrCast(&s.wide); const n:u8=if(@as(*const anyopaque,@ptrCast(&s))!=@as(*const anyopaque,@ptrCast(&s.wide)) and &bytes[@offsetOf(S,\"byte\")]!=&s.byte and &field_bytes[@offsetOf(S,\"byte\")]!=&s.byte)0 else 256; _=n; }", NULL},
		/* equality/array-offset-casts */
		{"const S=struct {a:[2]u16,}; const s:S=.{.a=.{11,22}}; pub fn main() void { const p:[*]const u16=@ptrCast(&s.a); const n:u8=if(&p[0]==&s.a[0] and &p[1]==&s.a[1] and @as(*const anyopaque,@ptrCast(&s.a))==@as(*const anyopaque,@ptrCast(&s.a[0])))0 else 256; _=n; }", NULL},
		/* equality/mixed-array-field */
		{"const I=struct{x:u8,}; const S=struct{a:[2]I,}; const s:S=.{.a=.{.{.x=1},.{.x=2}}}; pub fn main() void { const p:[*]const I=@ptrCast(&s.a); const n:u8=if(&p[1].x==&s.a[1].x and &@field(p[1],\"x\")==&s.a[1].x)0 else 256; _=n; }", NULL},
		/* equality/zero-array-versus-field */
		{"const S=struct {a:[2]u0,b:void,}; const s:S=.{.a=.{0,0},.b={}}; pub fn main() void {  const n:u8=if(&s.a[0]==&s.a[1] and @as(*const anyopaque,@ptrCast(&s.a[0]))!=@as(*const anyopaque,@ptrCast(&s.b)))0 else 256; _=n; }", NULL},
		/* equality/overlapping-fields */
		{"const S=struct {a:void,b:u0,c:[0]u8,x:u8,}; const s:S=.{.a={},.b=0,.c=.{},.x=7}; pub fn main() void { const a:*const anyopaque=@ptrCast(&s.a); const b:*const anyopaque=@ptrCast(&s.b); const c:*const anyopaque=@ptrCast(&s.c); const n:u8=if(a==a and a!=b and b!=c and @as(*const anyopaque,@ptrCast(&s))!=@as(*const anyopaque,@ptrCast(&s.x)))0 else 256; _=n; }", NULL},
		/* provenance_depth_40.zig */
		{"const S0 = struct { a: [2]u8 };\nconst S1 = struct { inner: S0 };\nconst S2 = struct { inner: S1 };\nconst S3 = struct { inner: S2 };\nconst S4 = struct { inner: S3 };\nconst S5 = struct { inner: S4 };\nconst S6 = struct { inner: S5 };\nconst S7 = struct { inner: S6 };\nconst S8 = struct { inner: S7 };\nconst S9 = struct { inner: S8 };\nconst S10 = struct { inner: S9 };\nconst S11 = struct { inner: S10 };\nconst S12 = struct { inner: S11 };\nconst S13 = struct { inner: S12 };\nconst S14 = struct { inner: S13 };\nconst S15 = struct { inner: S14 };\nconst S16 = struct { inner: S15 };\nconst S17 = struct { inner: S16 };\nconst S18 = struct { inner: S17 };\nconst S19 = struct { inner: S18 };\nconst S20 = struct { inner: S19 };\nconst S21 = struct { inner: S20 };\nconst S22 = struct { inner: S21 };\nconst S23 = struct { inner: S22 };\nconst S24 = struct { inner: S23 };\nconst S25 = struct { inner: S24 };\nconst S26 = struct { inner: S25 };\nconst S27 = struct { inner: S26 };\nconst S28 = struct { inner: S27 };\nconst S29 = struct { inner: S28 };\nconst S30 = struct { inner: S29 };\nconst S31 = struct { inner: S30 };\nconst S32 = struct { inner: S31 };\nconst S33 = struct { inner: S32 };\nconst S34 = struct { inner: S33 };\nconst S35 = struct { inner: S34 };\nconst S36 = struct { inner: S35 };\nconst S37 = struct { inner: S36 };\nconst S38 = struct { inner: S37 };\nconst S39 = struct { inner: S38 };\nconst S40 = struct { inner: S39 };\nconst root: S40 = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .a = .{ 11, 22 } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } };\nconst p = &root.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.a;\npub fn main() void { const n: u8 = if (p == p) 0 else 256; _ = n; }\n", NULL},
		/* provenance_depth_64.zig */
		{"const S0 = struct { a: [2]u8 };\nconst S1 = struct { inner: S0 };\nconst S2 = struct { inner: S1 };\nconst S3 = struct { inner: S2 };\nconst S4 = struct { inner: S3 };\nconst S5 = struct { inner: S4 };\nconst S6 = struct { inner: S5 };\nconst S7 = struct { inner: S6 };\nconst S8 = struct { inner: S7 };\nconst S9 = struct { inner: S8 };\nconst S10 = struct { inner: S9 };\nconst S11 = struct { inner: S10 };\nconst S12 = struct { inner: S11 };\nconst S13 = struct { inner: S12 };\nconst S14 = struct { inner: S13 };\nconst S15 = struct { inner: S14 };\nconst S16 = struct { inner: S15 };\nconst S17 = struct { inner: S16 };\nconst S18 = struct { inner: S17 };\nconst S19 = struct { inner: S18 };\nconst S20 = struct { inner: S19 };\nconst S21 = struct { inner: S20 };\nconst S22 = struct { inner: S21 };\nconst S23 = struct { inner: S22 };\nconst S24 = struct { inner: S23 };\nconst S25 = struct { inner: S24 };\nconst S26 = struct { inner: S25 };\nconst S27 = struct { inner: S26 };\nconst S28 = struct { inner: S27 };\nconst S29 = struct { inner: S28 };\nconst S30 = struct { inner: S29 };\nconst S31 = struct { inner: S30 };\nconst S32 = struct { inner: S31 };\nconst S33 = struct { inner: S32 };\nconst S34 = struct { inner: S33 };\nconst S35 = struct { inner: S34 };\nconst S36 = struct { inner: S35 };\nconst S37 = struct { inner: S36 };\nconst S38 = struct { inner: S37 };\nconst S39 = struct { inner: S38 };\nconst S40 = struct { inner: S39 };\nconst S41 = struct { inner: S40 };\nconst S42 = struct { inner: S41 };\nconst S43 = struct { inner: S42 };\nconst S44 = struct { inner: S43 };\nconst S45 = struct { inner: S44 };\nconst S46 = struct { inner: S45 };\nconst S47 = struct { inner: S46 };\nconst S48 = struct { inner: S47 };\nconst S49 = struct { inner: S48 };\nconst S50 = struct { inner: S49 };\nconst S51 = struct { inner: S50 };\nconst S52 = struct { inner: S51 };\nconst S53 = struct { inner: S52 };\nconst S54 = struct { inner: S53 };\nconst S55 = struct { inner: S54 };\nconst S56 = struct { inner: S55 };\nconst S57 = struct { inner: S56 };\nconst S58 = struct { inner: S57 };\nconst S59 = struct { inner: S58 };\nconst S60 = struct { inner: S59 };\nconst S61 = struct { inner: S60 };\nconst S62 = struct { inner: S61 };\nconst S63 = struct { inner: S62 };\nconst S64 = struct { inner: S63 };\nconst root: S64 = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .inner = .{ .a = .{ 11, 22 } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } } };\nconst p = &root.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.a;\npub fn main() void { const n: u8 = if (p == p) 0 else 256; _ = n; }\n", "not supported by szc yet: pointer provenance comparison work budget exceeded (4096)"},
		/* sentinel_reverse_formation.zig */
		{"const S = struct { a: [3:9]u8, before: u64 };\nconst s: S = .{ .a = .{ 11, 22, 33 }, .before = 123 };\npub fn main() void {\n    const p: [*]const u8 = @ptrCast(&s.a);\n    const q = p - 1;\n    _ = q;\n}\n", "pointer computation here causes illegal behavior"},
		/* sentinel_reverse_to_start.zig */
		{"const S = struct { a: [3:9]u8, before: u64 };\nconst s: S = .{ .a = .{ 11, 22, 33 }, .before = 123 };\npub fn main() void { const p: [*]const u8 = @ptrCast(&s.a); const q = p + 3; const n: u8 = if ((q - 3)[0] == 11 and q - p == 3) 0 else 256; _ = n; }\n", NULL},
		/* empty_sentinel_onepast_read.zig */
		{"const S = struct { a: [0:7]u8, before: u64 };\nconst s: S = .{ .a = .{}, .before = 123 };\npub fn main() void { const p: [*]const u8 = @ptrCast(&s.a); const n = p[1]; _ = n; }\n", "dereference of '*const u8' exceeds bounds of containing decl of type '[0:7]u8'"},
		/* typed-root-layout/opaque-discard */
		{"const S=struct{x:anyopaque,}; pub fn main() void {const s:S=undefined; const p=&s; _=p;}", "cannot directly embed opaque type 'anyopaque' in struct"},
		/* typed-root-layout/opaque-typeof */
		{"const S=struct{x:anyopaque,}; pub fn main() void {const s:S=undefined; _=@TypeOf(&s);}", "cannot directly embed opaque type 'anyopaque' in struct"},
		/* typed-root-layout/default-discard */
		{"const S=struct{x:u8=256,}; pub fn main() void {const s:S=undefined; const p=&s; _=p;}", "not supported by szc yet:"},
		/* typed-root-layout/pointer-type-only */
		{"const S=struct{x:anyopaque,}; pub fn main() void {const p:*const S=undefined; const n:u8=if(@sizeOf(*const S)==8 and @alignOf(*const S)==8 and @TypeOf(p)==*const S)0 else 256; _=n;}", NULL},
		/* typed-root-layout/valid-undefined-address */
		{"const S=struct{x:u8,}; noinline fn address(p:*const S) usize {return @intFromPtr(p);} pub fn main() void {const s:S=undefined; _=address(&s); _=@intFromPtr(&s);}", NULL},
		/* typed-root-layout/default-typeof */
		{"const S=struct{x:u8=256,}; pub fn main() void {const s:S=undefined; _=@TypeOf(&s);}", "not supported by szc yet:"},
		/* arithmetic/same-field-difference */
		{"const S=struct {a:[3:9]u8,before:u64,}; const s:S=.{.a=.{1,2,3},.before=17}; pub fn main() void { const p:[*]const u8=@ptrCast(&s.a); const q=p+2; const n:u8=if(q-p==2 and (q-2)[0]==1)0 else 256; _=n; }", NULL},
		/* error/field-relative-underflow */
		{"const S=struct {a:[3:9]u8,before:u64,}; const s:S=.{.a=.{1,2,3},.before=17}; pub fn main() void { const p:[*]const u8=@ptrCast(&s.a); const q=p-1; _=q; }", "pointer computation here causes illegal behavior"},
		/* error/negative-field-difference */
		{"const S=struct {a:[3]u8,}; const s:S=.{.a=.{1,2,3}}; pub fn main() void { const p:[*]const u8=@ptrCast(&s.a); const n=p-(p+1); _=n; }", "operation results in overflow"},
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
		const char *source = cases[i].source;
		FileId file = source_add_memory(&sources, "test.zig", source, (uint32_t)strlen(source));
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		bool success = false;
		if (!diag_has_errors(&diag)) {
			Sema *sema = sema_create(&arena, &diag, &interner, &sources);
			success = sema_analyze_executable(sema, file, ir);
		}
		CHECK(success == (cases[i].error == NULL));
		if (cases[i].error != NULL)
			CHECK(diag.count != 0 && strstr(diag.items[0].message, cases[i].error) != NULL);
		arena_free(&arena);
	}
}

static void
test_struct_metadata(void)
{
	const struct {
		const char *source;
		const char *error;
	} cases[] = {
		/* leaf/u0 */
		{"const S=struct { item:u0, }; pub fn main() void { const n:u8=if(@hasField(S,\"item\") and !@hasField(S,\"Item\") and @FieldType(S,\"item\")==u0 and @sizeOf(S)==@sizeOf(u0) and @alignOf(S)==@alignOf(u0))0 else 256; _=n; }", NULL},
		/* leaf/void */
		{"const S=struct { item:void, }; pub fn main() void { const n:u8=if(@hasField(S,\"item\") and !@hasField(S,\"Item\") and @FieldType(S,\"item\")==void and @sizeOf(S)==@sizeOf(void) and @alignOf(S)==@alignOf(void))0 else 256; _=n; }", NULL},
		/* leaf/bool */
		{"const S=struct { item:bool, }; pub fn main() void { const n:u8=if(@hasField(S,\"item\") and !@hasField(S,\"Item\") and @FieldType(S,\"item\")==bool and @sizeOf(S)==@sizeOf(bool) and @alignOf(S)==@alignOf(bool))0 else 256; _=n; }", NULL},
		/* leaf/u9 */
		{"const S=struct { item:u9, }; pub fn main() void { const n:u8=if(@hasField(S,\"item\") and !@hasField(S,\"Item\") and @FieldType(S,\"item\")==u9 and @sizeOf(S)==@sizeOf(u9) and @alignOf(S)==@alignOf(u9))0 else 256; _=n; }", NULL},
		/* leaf/i17 */
		{"const S=struct { item:i17, }; pub fn main() void { const n:u8=if(@hasField(S,\"item\") and !@hasField(S,\"Item\") and @FieldType(S,\"item\")==i17 and @sizeOf(S)==@sizeOf(i17) and @alignOf(S)==@alignOf(i17))0 else 256; _=n; }", NULL},
		/* leaf/u65 */
		{"const S=struct { item:u65, }; pub fn main() void { const n:u8=if(@hasField(S,\"item\") and !@hasField(S,\"Item\") and @FieldType(S,\"item\")==u65 and @sizeOf(S)==@sizeOf(u65) and @alignOf(S)==@alignOf(u65))0 else 256; _=n; }", NULL},
		/* leaf/u257 */
		{"const S=struct { item:u257, }; pub fn main() void { const n:u8=if(@hasField(S,\"item\") and !@hasField(S,\"Item\") and @FieldType(S,\"item\")==u257 and @sizeOf(S)==@sizeOf(u257) and @alignOf(S)==@alignOf(u257))0 else 256; _=n; }", NULL},
		/* leaf/f64 */
		{"const S=struct { item:f64, }; pub fn main() void { const n:u8=if(@hasField(S,\"item\") and !@hasField(S,\"Item\") and @FieldType(S,\"item\")==f64 and @sizeOf(S)==@sizeOf(f64) and @alignOf(S)==@alignOf(f64))0 else 256; _=n; }", NULL},
		/* leaf/[]const u16 */
		{"const S=struct { item:[]const u16, }; pub fn main() void { const n:u8=if(@hasField(S,\"item\") and !@hasField(S,\"Item\") and @FieldType(S,\"item\")==[]const u16 and @sizeOf(S)==@sizeOf([]const u16) and @alignOf(S)==@alignOf([]const u16))0 else 256; _=n; }", NULL},
		/* layout/mixed-forward */
		{"const S=struct {a:u8, b:u64, c:u16,}; pub fn main() void { const n:u8=if(@sizeOf(S)==16 and @alignOf(S)==8)0 else 256; _=n; }", NULL},
		/* layout/mixed-reverse */
		{"const S=struct {c:u16, b:u64, a:u8,}; pub fn main() void { const n:u8=if(@sizeOf(S)==16 and @alignOf(S)==8)0 else 256; _=n; }", NULL},
		/* layout/odd */
		{"const S=struct {a:u9, b:i17, c:u33,}; pub fn main() void { const n:u8=if(@sizeOf(S)==16 and @alignOf(S)==8)0 else 256; _=n; }", NULL},
		/* layout/empty-array */
		{"const S=struct {a:[0]u64, b:u8,}; pub fn main() void { const n:u8=if(@sizeOf(S)==8 and @alignOf(S)==8)0 else 256; _=n; }", NULL},
		/* nested */
		{"const I=struct { x:u8, y:u32, }; const S=struct { a:[3]I, b:[2][3]u16, z:void, }; pub fn main() void { const n:u8=if(@FieldType(S,\"a\")==[3]I and @FieldType(I,\"y\")==u32 and @sizeOf(I)==8 and @alignOf(I)==4 and @sizeOf(S)==36 and @alignOf(S)==4)0 else 256; _=n; }", NULL},
		/* self-pointers */
		{"const S=struct { next:*S, many:[*]const S, }; pub fn main() void { const n:u8=if(@FieldType(S,\"next\")==*S and @FieldType(S,\"many\")==[*]const S and @sizeOf(S)==16 and @alignOf(S)==8)0 else 256; _=n; }", NULL},
		{"const S=struct { p:@TypeOf(@as(*const @This(),@as(*@This(),undefined))), a:u8, }; pub fn main() u8 { return @sizeOf(S)-16; }", NULL},
		{"const S=struct { p:@TypeOf(@as([]const @This(),@as([]@This(),undefined))), a:u8, }; pub fn main() u8 { return @sizeOf(S)-24; }", NULL},
		/* mutual-pointers */
		{"const A=struct { b:*B, }; const B=struct { a:*A, }; pub fn main() void { const n:u8=if(@FieldType(A,\"b\")==*B and @FieldType(B,\"a\")==*A and @sizeOf(A)==8 and @sizeOf(B)==8)0 else 256; _=n; }", NULL},
		/* declarations-not-fields */
		{"const S=struct { field:u8, pub const declaration:u8=256; const Other=struct { x:u16, }; pub fn bad() u8 { return 256; } }; pub fn main() void { const n:u8=if(@hasField(S,\"field\") and !@hasField(S,\"declaration\") and !@hasField(S,\"bad\") and !@hasField(S,\"Other\") and @FieldType(S,\"field\")==u8 and @FieldType(S.Other,\"x\")==u16 and @sizeOf(S)==1)0 else 256; _=n; }", NULL},
		/* name/quote */
		{"const S=struct { @\"a\\\"b\":u16, }; pub fn main() void { const n:u8=if(@hasField(S,\"a\\\"b\") and @FieldType(S,\"a\\\"b\")==u16)0 else 256; _=n; }", NULL},
		/* nul-query/"item\x00" */
		{"const S=struct { item:u8, }; pub fn main() void { const n:u8=if(@hasField(S,\"item\\x00\"))0 else 256; _=n; }", NULL},
		/* nul-query/"item\x00tail" */
		{"const S=struct { item:u8, }; pub fn main() void { const n:u8=if(!@hasField(S,\"item\\x00tail\"))0 else 256; _=n; }", NULL},
		/* nested-anonymous */
		{"const S=struct { inner:struct { value:u16, }, }; pub fn main() void { const n:u8=if(@FieldType(@FieldType(S,\"inner\"),\"value\")==u16 and @sizeOf(S)==2)0 else 256; _=n; }", NULL},
		/* typed-query-result */
		{"const S=struct { x:u16, }; const T=@FieldType(S,\"x\"); const answer:T=19; pub fn main() void { const n:u8=if(@TypeOf(answer)==u16 and answer==19 and @TypeOf(@hasField(S,\"x\"))==bool and @TypeOf(@FieldType(S,\"x\"))==type)0 else 256; _=n; }", NULL},
		/* dynamic-pointer-native */
		{"// exit: 0\n// Synthetic addresses only. No dereference, load/store, or struct value exists.\nconst S=struct { a:u8, b:u64, c:u16, };\nconst W=struct { wide:u65, byte:u8, };\nconst Z=struct { marker:void, zero:u0, };\nfn bytes(s:[]S) []u8 { return @ptrCast(s); }\nfn structs(s:[]u8) []S { return @alignCast(@ptrCast(s)); }\nfn wide_bytes(s:[]W) []u8 { return @ptrCast(s); }\nfn zero_bytes(s:[]Z) []u8 { return @ptrCast(s); }\nfn aligned(p:*align(1) S) *S { return @alignCast(p); }\nfn verify(address:usize,count:usize) u8 {\n    const p:[*]S=@ptrFromInt(address);\n    if(@intFromPtr(p+1)!=address+16 or @intFromPtr(p+3)!=address+48) return 1;\n    const wide:[*]W=@ptrFromInt(address);\n    if(@intFromPtr(wide+1)!=address+32) return 2;\n    const zero:[*]Z=@ptrFromInt(address);\n    if(@intFromPtr(zero+19)!=address) return 3;\n    const array:*[3]S=@ptrFromInt(address);\n    const s:[]S=array;\n    if(s.len!=3 or @intFromPtr(s.ptr)!=address) return 4;\n    const dynamic=p[0..count];\n    const b=bytes(dynamic);\n    if(b.len!=count*@sizeOf(S) or @intFromPtr(b.ptr)!=address) return 5;\n    const again=structs(b);\n    if(again.len!=count or @intFromPtr(again.ptr+2)!=address+32) return 6;\n    const wb=wide_bytes(wide[0..3]);\n    if(wb.len!=96 or @intFromPtr(wb.ptr)!=address) return 7;\n    const zb=zero_bytes(zero[0..5]);\n    if(zb.len!=0 or @intFromPtr(zb.ptr)!=address) return 8;\n    const unaligned:*align(1) S=@ptrFromInt(address);\n    if(@intFromPtr(aligned(unaligned))!=address) return 9;\n    const q:[*]S=@ptrCast(s);\n    if(@intFromPtr(q+1)!=address+16) return 10;\n    return 0;\n}\npub fn main() u8 { return verify(4096,3); }\n", NULL},
		/* dynamic-struct-slice-resize */
		{"const S=struct { a:u8, b:u64, }; fn f(p:[*]S,n:usize) []u8 { const s=p[0..n]; return @ptrCast(s); } pub fn main() void { const p:[*]S=@ptrFromInt(4096); const s=f(p,3); _=s; }", NULL},
		/* dynamic-struct-align-cast */
		{"const S=struct { a:u64, }; fn f(p:*align(1) S) *S { return @alignCast(p); } pub fn main() void { const p:*align(1) S=@ptrFromInt(4096); _=f(p); }", NULL},
		/* invalid-field/x:anyopaque,/@hasField(S,"x") */
		{"const S=struct {x:anyopaque,}; pub fn main() void { _=@hasField(S,\"x\"); }", "cannot directly embed opaque"},
		/* invalid-field/x:i0,/@hasField(S,"x") */
		{"const S=struct {x:i0,}; pub fn main() void { _=@hasField(S,\"x\"); }", "signed integer cannot have bit width 0"},
		/* invalid-field/x:3,/@hasField(S,"x") */
		{"const S=struct {x:3,}; pub fn main() void { _=@hasField(S,\"x\"); }", "expected type 'type'"},
		/* invalid-field/x:MissingType,/@hasField(S,"x") */
		{"const S=struct {x:MissingType,}; pub fn main() void { _=@hasField(S,\"x\"); }", "use of undeclared identifier"},
		/* invalid-field/x:S,/@hasField(S,"x") */
		{"const S=struct {x:S,}; pub fn main() void { _=@hasField(S,\"x\"); }", "depends on itself"},
		/* invalid-field/x:u8, x:u16,/@hasField(S,"x") */
		{"const S=struct {x:u8, x:u16,}; pub fn main() void { _=@hasField(S,\"x\"); }", "duplicate struct member name"},
		/* invalid-query/@FieldType(S,"missing") */
		{"const S=struct { x:u8, const declaration=1; }; pub fn main() void { _=@FieldType(S,\"missing\"); }", "no field named 'missing'"},
		/* invalid-query/@FieldType(S,"declaration") */
		{"const S=struct { x:u8, const declaration=1; }; pub fn main() void { _=@FieldType(S,\"declaration\"); }", "no field named 'declaration'"},
		/* invalid-query/@FieldType(u8,"x") */
		{"const S=struct { x:u8, const declaration=1; }; pub fn main() void { _=@FieldType(u8,\"x\"); }", "expected struct or union"},
		/* invalid-query/@FieldType(S,undefined) */
		{"const S=struct { x:u8, const declaration=1; }; pub fn main() void { _=@FieldType(S,undefined); }", "use of undefined"},
		/* invalid-query/@FieldType(S,3) */
		{"const S=struct { x:u8, const declaration=1; }; pub fn main() void { _=@FieldType(S,3); }", "expected type '[]const u8'"},
		/* NYI/packed */
		{"const S=packed struct { x:u8, }; pub fn main() void { _=@sizeOf(S); }", "not supported by szc yet"},
		/* NYI/extern */
		{"const S=extern struct { x:u8, }; pub fn main() void { _=@sizeOf(S); }", "not supported by szc yet"},
		/* NYI/tuple */
		{"const S=struct { u8, u16 }; pub fn main() void { _=@sizeOf(S); }", "not supported by szc yet"},
		/* NYI/comptime-field */
		{"const S=struct { comptime x:u8=1, }; pub fn main() void { _=@sizeOf(S); }", "not supported by szc yet"},
		/* NYI/valid-default */
		{"const S=struct { x:u8=7, }; pub fn main() void { _=@sizeOf(S); }", "not supported by szc yet"},
		/* NYI/invalid-default */
		{"const S=struct { x:u8=256, }; pub fn main() void { _=@sizeOf(S); }", "not supported by szc yet"},
		/* NYI/explicit-align */
		{"const S=struct { x:u8 align(16), }; pub fn main() void { _=@sizeOf(S); }", "not supported by szc yet"},
		/* NYI/type/@sizeOf(S) */
		{"const S=struct { x:type, }; pub fn main() void { _=@sizeOf(S); }", "not supported by szc yet"},
		/* NYI/noreturn/@sizeOf(S) */
		{"const S=struct { x:noreturn, }; pub fn main() void { _=@sizeOf(S); }", "not supported by szc yet"},
		/* Immutable known struct values need no runtime transport. */
		{"const S=struct { x:u8, }; pub fn main() u8 { const s:S=.{.x=7}; return s.x-7; }", NULL},
		/* Runtime aggregate loads now own local snapshots. */
		{"const S=struct { x:u8, }; fn f(p:*S) void { const value=p.*; _=value; } pub fn main() void { const p:*S=@ptrFromInt(4096); f(p); }", NULL},
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
		const char *source = cases[i].source;
		FileId file = source_add_memory(&sources, "struct_metadata.zig", source, (uint32_t)strlen(source));
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		CHECK(!diag_has_errors(&diag));
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		bool success = false;
		if (!diag_has_errors(&diag)) {
			Sema *sema = sema_create(&arena, &diag, &interner, &sources);
			success = sema_analyze_executable(sema, file, ir);
		}
		CHECK(success == (cases[i].error == NULL));
		if (cases[i].error != NULL)
			CHECK(diag.count != 0 && strstr(diag.items[0].message, cases[i].error) != NULL);
		arena_free(&arena);
	}
}

static void
test_struct_offsets(void)
{
	const struct {
		const char *source;
		const char *error;
	} cases[] = {
		/* layout/perm3/0 */
		{"const Inner=struct { byte:u8, word:u32, }; const S=struct {f0:u8,f1:u64,f2:u16,}; pub fn main() void { const n:u8=if(@sizeOf(S)==16 and @alignOf(S)==8 and @offsetOf(S,\"f0\")==10 and @offsetOf(S,\"f1\")==0 and @offsetOf(S,\"f2\")==8)0 else 256; _=n; }", NULL},
		/* layout/zero/0 */
		{"const Inner=struct { byte:u8, word:u32, }; const S=struct {a:u8,z:void,empty:[0]u64,bits:u0,b:u64,}; pub fn main() void { const n:u8=if(@sizeOf(S)==16 and @alignOf(S)==8 and @offsetOf(S,\"a\")==8 and @offsetOf(S,\"z\")==9 and @offsetOf(S,\"empty\")==0 and @offsetOf(S,\"bits\")==9 and @offsetOf(S,\"b\")==0)0 else 256; _=n; }", NULL},
		/* layout/many/25/forward */
		{"const Inner=struct { byte:u8, word:u32, }; const S=struct {f0:u8,f1:u64,f2:u16,f3:u65,f4:void,f5:[0]u32,f6:u24,f7:bool,f8:u8,f9:u64,f10:u16,f11:u65,f12:void,f13:[0]u32,f14:u24,f15:bool,f16:u8,f17:u64,f18:u16,f19:u65,f20:void,f21:[0]u32,f22:u24,f23:bool,f24:u8,}; pub fn main() void { const n:u8=if(@sizeOf(S)==112 and @alignOf(S)==16 and @offsetOf(S,\"f0\")==90 and @offsetOf(S,\"f1\")==48 and @offsetOf(S,\"f2\")==88 and @offsetOf(S,\"f3\")==0 and @offsetOf(S,\"f4\")==95 and @offsetOf(S,\"f5\")==76 and @offsetOf(S,\"f6\")==76 and @offsetOf(S,\"f7\")==94 and @offsetOf(S,\"f8\")==93 and @offsetOf(S,\"f9\")==64 and @offsetOf(S,\"f10\")==86 and @offsetOf(S,\"f11\")==32 and @offsetOf(S,\"f12\")==94 and @offsetOf(S,\"f13\")==72 and @offsetOf(S,\"f14\")==80 and @offsetOf(S,\"f15\")==91 and @offsetOf(S,\"f16\")==92 and @offsetOf(S,\"f17\")==56 and @offsetOf(S,\"f18\")==84 and @offsetOf(S,\"f19\")==16 and @offsetOf(S,\"f20\")==95 and @offsetOf(S,\"f21\")==76 and @offsetOf(S,\"f22\")==72 and @offsetOf(S,\"f23\")==95 and @offsetOf(S,\"f24\")==96)0 else 256; _=n; }", NULL},
		/* layout/many/51/forward */
		{"const Inner=struct { byte:u8, word:u32, }; const S=struct {f0:u8,f1:u64,f2:u16,f3:u65,f4:void,f5:[0]u32,f6:u24,f7:bool,f8:u8,f9:u64,f10:u16,f11:u65,f12:void,f13:[0]u32,f14:u24,f15:bool,f16:u8,f17:u64,f18:u16,f19:u65,f20:void,f21:[0]u32,f22:u24,f23:bool,f24:u8,f25:u64,f26:u16,f27:u65,f28:void,f29:[0]u32,f30:u24,f31:bool,f32:u8,f33:u64,f34:u16,f35:u65,f36:void,f37:[0]u32,f38:u24,f39:bool,f40:u8,f41:u64,f42:u16,f43:u65,f44:void,f45:[0]u32,f46:u24,f47:bool,f48:u8,f49:u64,f50:u16,}; pub fn main() void { const n:u8=if(@sizeOf(S)==208 and @alignOf(S)==16 and @offsetOf(S,\"f0\")==198 and @offsetOf(S,\"f1\")==96 and @offsetOf(S,\"f2\")==182 and @offsetOf(S,\"f3\")==16 and @offsetOf(S,\"f4\")==199 and @offsetOf(S,\"f5\")==152 and @offsetOf(S,\"f6\")==164 and @offsetOf(S,\"f7\")==191 and @offsetOf(S,\"f8\")==202 and @offsetOf(S,\"f9\")==128 and @offsetOf(S,\"f10\")==176 and @offsetOf(S,\"f11\")==80 and @offsetOf(S,\"f12\")==200 and @offsetOf(S,\"f13\")==152 and @offsetOf(S,\"f14\")==156 and @offsetOf(S,\"f15\")==199 and @offsetOf(S,\"f16\")==194 and @offsetOf(S,\"f17\")==136 and @offsetOf(S,\"f18\")==186 and @offsetOf(S,\"f19\")==0 and @offsetOf(S,\"f20\")==190 and @offsetOf(S,\"f21\")==172 and @offsetOf(S,\"f22\")==172 and @offsetOf(S,\"f23\")==192 and @offsetOf(S,\"f24\")==193 and @offsetOf(S,\"f25\")==144 and @offsetOf(S,\"f26\")==178 and @offsetOf(S,\"f27\")==64 and @offsetOf(S,\"f28\")==202 and @offsetOf(S,\"f29\")==176 and @offsetOf(S,\"f30\")==168 and @offsetOf(S,\"f31\")==190 and @offsetOf(S,\"f32\")==197 and @offsetOf(S,\"f33\")==120 and @offsetOf(S,\"f34\")==188 and @offsetOf(S,\"f35\")==48 and @offsetOf(S,\"f36\")==190 and @offsetOf(S,\"f37\")==152 and @offsetOf(S,\"f38\")==160 and @offsetOf(S,\"f39\")==195 and @offsetOf(S,\"f40\")==196 and @offsetOf(S,\"f41\")==112 and @offsetOf(S,\"f42\")==184 and @offsetOf(S,\"f43\")==32 and @offsetOf(S,\"f44\")==199 and @offsetOf(S,\"f45\")==160 and @offsetOf(S,\"f46\")==152 and @offsetOf(S,\"f47\")==200 and @offsetOf(S,\"f48\")==201 and @offsetOf(S,\"f49\")==104 and @offsetOf(S,\"f50\")==180)0 else 256; _=n; }", NULL},
		/* layout/nested */
		{"const Inner=struct { byte:u8, word:u32, }; const S=struct {a:u8,inner:Inner,array:[3]Inner,matrix:[2][3]u16,z:void,}; pub fn main() void { const n:u8=if(@sizeOf(S)==48 and @alignOf(S)==4 and @offsetOf(S,\"a\")==44 and @offsetOf(S,\"inner\")==0 and @offsetOf(S,\"array\")==8 and @offsetOf(S,\"matrix\")==32 and @offsetOf(S,\"z\")==45)0 else 256; _=n; }", NULL},
		/* layout/self-pointer */
		{"const Inner=struct { byte:u8, word:u32, }; const S=struct {tag:u8,next:*const @This(),children:[]@This(),}; pub fn main() void { const n:u8=if(@sizeOf(S)==32 and @alignOf(S)==8 and @offsetOf(S,\"tag\")==24 and @offsetOf(S,\"next\")==0 and @offsetOf(S,\"children\")==8)0 else 256; _=n; }", NULL},
		/* layout/all-zero */
		{"const Inner=struct { byte:u8, word:u32, }; const S=struct {v:void,u:u0,a:[0]u64,}; pub fn main() void { const n:u8=if(@sizeOf(S)==0 and @alignOf(S)==8 and @offsetOf(S,\"v\")==0 and @offsetOf(S,\"u\")==0 and @offsetOf(S,\"a\")==0)0 else 256; _=n; }", NULL},
		/* layout/u32-size-limit */
		{"const Inner=struct { byte:u8, word:u32, }; const S=struct {a:[4294967294]u8,b:u8,}; pub fn main() void { const n:u8=if(@sizeOf(S)==4294967295 and @alignOf(S)==1 and @offsetOf(S,\"a\")==0 and @offsetOf(S,\"b\")==4294967294)0 else 256; _=n; }", NULL},
		/* name/trailing-nul */
		{"const S=struct { item:u8, wide:u64, pub const declaration:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { const n:u8=if(@offsetOf(S,\"item\\x00\")==8)0 else 256; _=n; }", NULL},
		/* result-comptime-int */
		{"const S=struct { item:u8, wide:u64, pub const declaration:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { const n:u8=if(@TypeOf(@offsetOf(S,\"item\"))==comptime_int)0 else 256; _=n; }", NULL},
		/* type-context */
		{"const S=struct { item:u8, wide:u64, pub const declaration:u8=256; pub fn bad() u8 { return 256; } }; const A=[@offsetOf(S,\"item\")]u8; pub fn main() void { const n:u8=if(@sizeOf(A)==8)0 else 256; _=n; }", NULL},
		/* global-query */
		{"const S=struct { item:u8, wide:u64, pub const declaration:u8=256; pub fn bad() u8 { return 256; } }; const offset=@offsetOf(S,\"item\"); pub fn main() void { const n:u8=if(offset==8)0 else 256; _=n; }", NULL},
		/* nested-type */
		{"const S=struct { inner:Inner, }; const Inner=struct { byte:u8, word:u32, }; pub fn main() void { const n:u8=if(@offsetOf(@FieldType(S,\"inner\"),\"word\")==0)0 else 256; _=n; }", NULL},
		/* absent/"missing" */
		{"const S=struct { item:u8, wide:u64, pub const declaration:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { _=@offsetOf(S,\"missing\"); }", "no field named 'missing' in struct 'test.S'"},
		/* nonstruct/u8 */
		{"const S=struct { item:u8, wide:u64, pub const declaration:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { _=@offsetOf(u8,\"item\"); }", "expected struct type, found 'u8'"},
		/* nonstruct/anyopaque */
		{"const S=struct { item:u8, wide:u64, pub const declaration:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { _=@offsetOf(anyopaque,\"item\"); }", "expected struct type, found 'anyopaque'"},
		/* nonstruct/noreturn */
		{"const S=struct { item:u8, wide:u64, pub const declaration:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { _=@offsetOf(noreturn,\"item\"); }", "expected struct type, found 'noreturn'"},
		/* arguments/zero-args */
		{"const S=struct { item:u8, wide:u64, pub const declaration:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { _=@offsetOf(); }", "expected 2 arguments, found 0"},
		/* arguments/non-type */
		{"const S=struct { item:u8, wide:u64, pub const declaration:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { _=@offsetOf(3,\"item\"); }", "expected type 'type', found 'comptime_int'"},
		/* arguments/undefined-type */
		{"const S=struct { item:u8, wide:u64, pub const declaration:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { _=@offsetOf(@as(type,undefined),\"item\"); }", "use of undefined value here causes illegal behavior"},
		/* arguments/nonstruct-bad-name-order */
		{"const S=struct { item:u8, wide:u64, pub const declaration:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { _=@offsetOf(u8,3); }", "expected type '[]const u8', found 'comptime_int'"},
		/* runtime-name */
		{"const S=struct { item:u8, wide:u64, pub const declaration:u8=256; pub fn bad() u8 { return 256; } }; fn f(name:[]const u8) usize { return @offsetOf(S,name); } pub fn main() void { _=f(\"item\"); }", "unable to resolve comptime value"},
		/* layout-error/by-value-cycle */
		{"const S=struct { x:S, }; pub fn main() void { _=@offsetOf(S,\"x\"); }", "type 'test.S' depends on itself for field declared here"},
		/* layout-error/invalid-field-before-missing */
		{"const S=struct { x:anyopaque, }; pub fn main() void { _=@offsetOf(S,\"missing\"); }", "cannot directly embed opaque type 'anyopaque' in struct"},
		/* layout-error/cycle-bad-name-order */
		{"const S=struct { x:S, }; pub fn main() void { _=@offsetOf(S,3); }", "expected type '[]const u8', found 'comptime_int'"},
		/* size-limit/exact-overflow/size */
		{"const S=struct {a:[4294967295]u8,b:u8,}; pub fn main() void { _=@sizeOf(S); }", "struct layout requires size 4294967296, this compiler implementation supports up to 4294967295"},
		/* size-limit/tail-padding-overflow/size */
		{"const S=struct {a:[4294967287]u8,b:u64,}; pub fn main() void { _=@sizeOf(S); }", "struct layout requires size 4294967296, this compiler implementation supports up to 4294967295"},
		/* zero-array-order/valid/valid-name */
		{"const S=struct { x:u64, }; pub fn main() void { _=@offsetOf([0]S,\"x\"); }", "expected struct type, found '[0]test.S'"},
		/* zero-array-order/invalid-embedded/valid-name */
		{"const S=struct { x:anyopaque, }; pub fn main() void { _=@offsetOf([0]S,\"x\"); }", "cannot directly embed opaque type 'anyopaque' in struct"},
		/* zero-array-order/oversize/valid-name */
		{"const S=struct { a:[4294967295]u8,b:u8, }; pub fn main() void { _=@offsetOf([0]S,\"x\"); }", "struct layout requires size 4294967296, this compiler implementation supports up to 4294967295"},
		/* zero-array-order/oversize/bad-name */
		{"const S=struct { a:[4294967295]u8,b:u8, }; pub fn main() void { _=@offsetOf([0]S,3); }", "expected type '[]const u8', found 'comptime_int'"},
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
		const char *source = cases[i].source;
		FileId file = source_add_memory(&sources, "test.zig", source, (uint32_t)strlen(source));
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		CHECK(!diag_has_errors(&diag));
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		bool success = false;
		if (!diag_has_errors(&diag)) {
			Sema *sema = sema_create(&arena, &diag, &interner, &sources);
			success = sema_analyze_executable(sema, file, ir);
		}
		CHECK(success == (cases[i].error == NULL));
		if (cases[i].error != NULL)
			CHECK(diag.count != 0 && strstr(diag.items[0].message, cases[i].error) != NULL);
		arena_free(&arena);
	}
}

static void
test_struct_bit_layout(void)
{
	const struct {
		const char *source;
		const char *error;
	} cases[] = {
		/* layout/perm3/0 */
		{"const Inner=struct { byte:u8, word:u32, }; const S=struct {f0:u8,f1:u64,f2:u16,}; pub fn main() void { const n:u8=if(@sizeOf(S)==16 and @alignOf(S)==8 and @bitOffsetOf(S,\"f0\")==80 and @bitOffsetOf(S,\"f1\")==0 and @bitOffsetOf(S,\"f2\")==64 and @bitOffsetOf(S,\"f0\")==@offsetOf(S,\"f0\")*8 and @bitOffsetOf(S,\"f1\")==@offsetOf(S,\"f1\")*8 and @bitOffsetOf(S,\"f2\")==@offsetOf(S,\"f2\")*8 and @TypeOf(@bitOffsetOf(S,\"f2\"))==comptime_int)0 else 256; _=n; }", NULL},
		/* layout/zero/0 */
		{"const Inner=struct { byte:u8, word:u32, }; const S=struct {a:u8,z:void,empty:[0]u64,bits:u0,b:u64,}; pub fn main() void { const n:u8=if(@sizeOf(S)==16 and @alignOf(S)==8 and @bitOffsetOf(S,\"a\")==64 and @bitOffsetOf(S,\"z\")==72 and @bitOffsetOf(S,\"empty\")==0 and @bitOffsetOf(S,\"bits\")==72 and @bitOffsetOf(S,\"b\")==0 and @bitOffsetOf(S,\"a\")==@offsetOf(S,\"a\")*8 and @bitOffsetOf(S,\"z\")==@offsetOf(S,\"z\")*8 and @bitOffsetOf(S,\"empty\")==@offsetOf(S,\"empty\")*8 and @bitOffsetOf(S,\"bits\")==@offsetOf(S,\"bits\")*8 and @bitOffsetOf(S,\"b\")==@offsetOf(S,\"b\")*8 and @TypeOf(@bitOffsetOf(S,\"b\"))==comptime_int)0 else 256; _=n; }", NULL},
		/* layout/many/25/shuffle */
		{"const Inner=struct { byte:u8, word:u32, }; const S=struct {f1:u64,f23:bool,f24:u8,f14:u24,f6:u24,f5:[0]u32,f2:u16,f21:[0]u32,f18:u16,f11:u65,f16:u8,f4:void,f7:bool,f20:void,f9:u64,f3:u65,f8:u8,f0:u8,f15:bool,f17:u64,f19:u65,f10:u16,f22:u24,f12:void,f13:[0]u32,}; pub fn main() void { const n:u8=if(@sizeOf(S)==112 and @alignOf(S)==16 and @bitOffsetOf(S,\"f1\")==448 and @bitOffsetOf(S,\"f23\")==768 and @bitOffsetOf(S,\"f24\")==760 and @bitOffsetOf(S,\"f14\")==608 and @bitOffsetOf(S,\"f6\")==640 and @bitOffsetOf(S,\"f5\")==672 and @bitOffsetOf(S,\"f2\")==672 and @bitOffsetOf(S,\"f21\")==672 and @bitOffsetOf(S,\"f18\")==688 and @bitOffsetOf(S,\"f11\")==0 and @bitOffsetOf(S,\"f16\")==752 and @bitOffsetOf(S,\"f4\")==752 and @bitOffsetOf(S,\"f7\")==720 and @bitOffsetOf(S,\"f20\")==752 and @bitOffsetOf(S,\"f9\")==512 and @bitOffsetOf(S,\"f3\")==256 and @bitOffsetOf(S,\"f8\")==728 and @bitOffsetOf(S,\"f0\")==736 and @bitOffsetOf(S,\"f15\")==744 and @bitOffsetOf(S,\"f17\")==384 and @bitOffsetOf(S,\"f19\")==128 and @bitOffsetOf(S,\"f10\")==704 and @bitOffsetOf(S,\"f22\")==576 and @bitOffsetOf(S,\"f12\")==768 and @bitOffsetOf(S,\"f13\")==576 and @bitOffsetOf(S,\"f1\")==@offsetOf(S,\"f1\")*8 and @bitOffsetOf(S,\"f23\")==@offsetOf(S,\"f23\")*8 and @bitOffsetOf(S,\"f24\")==@offsetOf(S,\"f24\")*8 and @bitOffsetOf(S,\"f14\")==@offsetOf(S,\"f14\")*8 and @bitOffsetOf(S,\"f6\")==@offsetOf(S,\"f6\")*8 and @bitOffsetOf(S,\"f5\")==@offsetOf(S,\"f5\")*8 and @bitOffsetOf(S,\"f2\")==@offsetOf(S,\"f2\")*8 and @bitOffsetOf(S,\"f21\")==@offsetOf(S,\"f21\")*8 and @bitOffsetOf(S,\"f18\")==@offsetOf(S,\"f18\")*8 and @bitOffsetOf(S,\"f11\")==@offsetOf(S,\"f11\")*8 and @bitOffsetOf(S,\"f16\")==@offsetOf(S,\"f16\")*8 and @bitOffsetOf(S,\"f4\")==@offsetOf(S,\"f4\")*8 and @bitOffsetOf(S,\"f7\")==@offsetOf(S,\"f7\")*8 and @bitOffsetOf(S,\"f20\")==@offsetOf(S,\"f20\")*8 and @bitOffsetOf(S,\"f9\")==@offsetOf(S,\"f9\")*8 and @bitOffsetOf(S,\"f3\")==@offsetOf(S,\"f3\")*8 and @bitOffsetOf(S,\"f8\")==@offsetOf(S,\"f8\")*8 and @bitOffsetOf(S,\"f0\")==@offsetOf(S,\"f0\")*8 and @bitOffsetOf(S,\"f15\")==@offsetOf(S,\"f15\")*8 and @bitOffsetOf(S,\"f17\")==@offsetOf(S,\"f17\")*8 and @bitOffsetOf(S,\"f19\")==@offsetOf(S,\"f19\")*8 and @bitOffsetOf(S,\"f10\")==@offsetOf(S,\"f10\")*8 and @bitOffsetOf(S,\"f22\")==@offsetOf(S,\"f22\")*8 and @bitOffsetOf(S,\"f12\")==@offsetOf(S,\"f12\")*8 and @bitOffsetOf(S,\"f13\")==@offsetOf(S,\"f13\")*8 and @TypeOf(@bitOffsetOf(S,\"f13\"))==comptime_int)0 else 256; _=n; }", NULL},
		/* layout/u32-size-limit */
		{"const Inner=struct { byte:u8, word:u32, }; const S=struct {a:[4294967294]u8,b:u8,}; pub fn main() void { const n:u8=if(@sizeOf(S)==4294967295 and @alignOf(S)==1 and @bitOffsetOf(S,\"a\")==0 and @bitOffsetOf(S,\"b\")==34359738352 and @bitOffsetOf(S,\"a\")==@offsetOf(S,\"a\")*8 and @bitOffsetOf(S,\"b\")==@offsetOf(S,\"b\")*8 and @TypeOf(@bitOffsetOf(S,\"b\"))==comptime_int)0 else 256; _=n; }", NULL},
		/* layout/zero-offset-nonzero */
		{"const S=struct { big:u64, byte:u8, zero:[0]u8, }; pub fn main() void { const n:u8=if(@sizeOf(S)==16 and @alignOf(S)==8 and @bitOffsetOf(S,\"big\")==0 and @bitOffsetOf(S,\"byte\")==64 and @bitOffsetOf(S,\"zero\")==72 and @bitOffsetOf(S,\"big\")==@offsetOf(S,\"big\")*8 and @bitOffsetOf(S,\"byte\")==@offsetOf(S,\"byte\")*8 and @bitOffsetOf(S,\"zero\")==@offsetOf(S,\"zero\")*8 and @TypeOf(@bitOffsetOf(S,\"zero\"))==comptime_int)0 else 256; _=n; }", NULL},
		/* layout/zero-alignment */
		{"const S=struct { byte:u8, zero:[0]u128, }; pub fn main() void { const n:u8=if(@sizeOf(S)==16 and @alignOf(S)==16 and @bitOffsetOf(S,\"byte\")==0 and @bitOffsetOf(S,\"zero\")==0 and @bitOffsetOf(S,\"byte\")==@offsetOf(S,\"byte\")*8 and @bitOffsetOf(S,\"zero\")==@offsetOf(S,\"zero\")*8 and @TypeOf(@bitOffsetOf(S,\"zero\"))==comptime_int)0 else 256; _=n; }", NULL},
		/* name/one-nul */
		{"const S=struct { item:u8, wide:u64, pub const declaration:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { const n:u8=if(@bitOffsetOf(S,\"item\\x00\")==64)0 else 256; _=n; }", NULL},
		/* escaped/space */
		{"const S=struct {@\"two words\":u8, }; pub fn main() void { const n:u8=if(@bitOffsetOf(S,\"two words\")==0)0 else 256; _=n; }", NULL},
		/* logical-bits/[2]u9 */
		{"const S=struct { invalid:anyopaque, }; pub fn main() void { const n:u8=if(@bitSizeOf([2]u9)==18 and @TypeOf(@bitSizeOf([2]u9))==comptime_int)0 else 256; _=n; }", NULL},
		/* logical-bits/[0:0]u9 */
		{"const S=struct { invalid:anyopaque, }; pub fn main() void { const n:u8=if(@bitSizeOf([0:0]u9)==9 and @TypeOf(@bitSizeOf([0:0]u9))==comptime_int)0 else 256; _=n; }", NULL},
		/* logical-bits: pointer to S */
		{"const S=struct { invalid:anyopaque, }; pub fn main() void { const n:u8=if(@bitSizeOf(*S)==64 and @TypeOf(@bitSizeOf(*S))==comptime_int)0 else 256; _=n; }", NULL},
		/* no-representation/empty */
		{"const S=struct {}; pub fn main() void { _=@bitSizeOf(S); }", "no bit size available for type 'test.S'"},
		/* no-representation/ordinary */
		{"const S=struct { a:u8, b:u64, }; pub fn main() void { _=@bitSizeOf(S); }", "no bit size available for type 'test.S'"},
		/* no-representation/zero-fields */
		{"const S=struct { a:void, b:u0, }; pub fn main() void { _=@bitSizeOf(S); }", "no bit size available for type 'test.S'"},
		/* no-representation/invalid-field */
		{"const S=struct { a:anyopaque, }; pub fn main() void { _=@bitSizeOf(S); }", "no bit size available for type 'test.S'"},
		/* no-representation/cycle */
		{"const S=struct { a:S, }; pub fn main() void { _=@bitSizeOf(S); }", "no bit size available for type 'test.S'"},
		/* no-representation/invalid-default */
		{"const S=struct { a:u8=256, }; pub fn main() void { _=@bitSizeOf(S); }", "no bit size available for type 'test.S'"},
		/* no-representation/comptime-field */
		{"const S=struct { comptime a:u8=1, }; pub fn main() void { _=@bitSizeOf(S); }", "no bit size available for type 'test.S'"},
		/* no-representation/oversize */
		{"const S=struct { a:[4294967295]u8,b:u8, }; pub fn main() void { _=@bitSizeOf(S); }", "no bit size available for type 'test.S'"},
		/* no-representation/array */
		{"const S=struct { a:u8, }; pub fn main() void { _=@bitSizeOf([3]S); }", "no bit size available for type '[3]test.S'"},
		/* no-representation/zero-array */
		{"const S=struct { a:u8, }; pub fn main() void { _=@bitSizeOf([0]S); }", "no bit size available for type '[0]test.S'"},
		/* no-representation/nested-zero-array */
		{"const S=struct { a:u8, }; pub fn main() void { _=@bitSizeOf([0][2]S); }", "no bit size available for type '[0][2]test.S'"},
		/* no-representation/zero-array-invalid */
		{"const S=struct { a:anyopaque, }; pub fn main() void { _=@bitSizeOf([0]S); }", "no bit size available for type '[0]test.S'"},
		/* no-representation/zero-array-cycle */
		{"const S=struct { a:S, }; pub fn main() void { _=@bitSizeOf([0]S); }", "no bit size available for type '[0]test.S'"},
		/* order/arity-zero */
		{"pub fn main() void { _=@bitOffsetOf(); }", "expected 2 arguments, found 0"},
		/* order/type-before-name */
		{"pub fn main() void { _=@bitOffsetOf(3,3); }", "expected type 'type', found 'comptime_int'"},
		/* order/nonstruct-name-before-kind */
		{"pub fn main() void { _=@bitOffsetOf(u8,3); }", "expected type '[]const u8', found 'comptime_int'"},
		/* order/cycle-name-before-layout */
		{"const S=struct { a:S, }; pub fn main() void { _=@bitOffsetOf(S,3); }", "expected type '[]const u8', found 'comptime_int'"},
		/* order/cycle-before-missing */
		{"const S=struct { a:S, }; pub fn main() void { _=@bitOffsetOf(S,\"missing\"); }", "type 'test.S' depends on itself for field declared here"},
		/* order/invalid-before-missing */
		{"const S=struct { a:anyopaque, }; pub fn main() void { _=@bitOffsetOf(S,\"missing\"); }", "cannot directly embed opaque type 'anyopaque' in struct"},
		/* order/zero-array-kind */
		{"const S=struct { item:u8, wide:u64, pub const declaration:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { _=@bitOffsetOf([0]S,\"item\"); }", "expected struct type, found '[0]test.S'"},
		/* order/zero-array-layout */
		{"const S=struct { a:anyopaque, }; pub fn main() void { _=@bitOffsetOf([0]S,\"missing\"); }", "cannot directly embed opaque type 'anyopaque' in struct"},
		/* order/size-limit */
		{"const S=struct { a:[4294967295]u8,b:u8, }; pub fn main() void { _=@bitOffsetOf(S,\"missing\"); }", "struct layout requires size 4294967296, this compiler implementation supports up to 4294967295"},
		/* order/tail-align-limit */
		{"const S=struct { a:[4294967287]u8,b:u64, }; pub fn main() void { _=@bitOffsetOf(S,\"b\"); }", "struct layout requires size 4294967296, this compiler implementation supports up to 4294967295"},
		/* not-field/missing */
		{"const S=struct { item:u8, wide:u64, pub const declaration:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { _=@bitOffsetOf(S,\"missing\"); }", "no field named 'missing' in struct 'test.S'"},
		/* not-field/declaration */
		{"const S=struct { item:u8, wide:u64, pub const declaration:u8=256; pub fn bad() u8 { return 256; } }; pub fn main() void { _=@bitOffsetOf(S,\"declaration\"); }", "no field named 'declaration' in struct 'test.S'"},
		/* runtime-name */
		{"const S=struct { item:u8, wide:u64, pub const declaration:u8=256; pub fn bad() u8 { return 256; } }; fn f(name:[]const u8) usize { return @bitOffsetOf(S,name); } pub fn main() void { _=f(\"item\"); }", "unable to resolve comptime value"},
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
		const char *source = cases[i].source;
		FileId file = source_add_memory(&sources, "test.zig", source, (uint32_t)strlen(source));
		AstContainerDecl *tree = parse_file(&arena, &diag, &sources, file);
		CHECK(!diag_has_errors(&diag));
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, tree);
		bool success = false;
		if (!diag_has_errors(&diag)) {
			Sema *sema = sema_create(&arena, &diag, &interner, &sources);
			success = sema_analyze_executable(sema, file, ir);
		}
		CHECK(success == (cases[i].error == NULL));
		if (cases[i].error != NULL)
			CHECK(diag.count != 0 && strcmp(diag.items[0].message, cases[i].error) == 0);
		arena_free(&arena);
	}
}

static void
test_struct_layout_api(void)
{
	for (uint32_t resolved = 0; resolved < 2; resolved++) {
		Arena arena;
		arena_init(&arena);
		SourceTable sources;
		source_table_init(&sources, &arena);
		Diag diag;
		diag_init(&diag, &arena, &sources);
		Interner interner;
		intern_init(&interner, &arena);
		const char *source = resolved
			? "const S=struct { z:[0]u64, }; fn use(p:*S) void { _=p; } pub fn main() void { _=@sizeOf(S); use(undefined); }"
			: "const S=struct { x:u64, }; fn use(p:*S) void { _=p; } pub fn main() void { use(undefined); }";
		FileId file = source_add_memory(&sources, "struct_layout_api.zig", source, (uint32_t)strlen(source));
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		CHECK(!diag_has_errors(&diag));
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		CHECK(!diag_has_errors(&diag));
		Sema *sema = sema_create(&arena, &diag, &interner, &sources);
		CHECK(sema_analyze_executable(sema, file, ir));
		CHECK(sema_func_count(sema) == 3);
		TypeFn fn = sema_type(sema, sema_func(sema, 2)->type)->as.fn;
		TypeId type = sema_type(sema, fn.params[0])->as.pointer.child;
		uint64_t size = 123;
		CHECK(sema_type_size(sema, type, &size) == (resolved != 0));
		CHECK(size == (resolved ? 0u : 123u));
		CHECK(sema_type_align(sema, type) == (resolved ? 8u : 0u));
		arena_free(&arena);
	}
}

static void
struct_layout_test_alignments(uint32_t *alignments, uint32_t count, StructLayoutPattern pattern)
{
	for (uint32_t i = 0; i < count; i++) {
		uint32_t exponent = 0;
		switch (pattern) {
		case STRUCT_LAYOUT_EQUAL:
			exponent = 3;
			break;
		case STRUCT_LAYOUT_ALTERNATE:
			exponent = i % 2 * 3;
			break;
		case STRUCT_LAYOUT_ASCENDING:
			exponent = i * 32 / count;
			break;
		case STRUCT_LAYOUT_DESCENDING:
		case STRUCT_LAYOUT_NEAR_SORTED:
			exponent = 31 - i * 32 / count;
			break;
		case STRUCT_LAYOUT_ORGAN: {
			uint32_t mirror = count - 1 - i;
			exponent = (i < mirror ? i : mirror) % 32;
			break;
		}
		case STRUCT_LAYOUT_SAW:
			exponent = i % 32;
			break;
		case STRUCT_LAYOUT_BLOCKS:
			exponent = i / 7 % 32;
			break;
		case STRUCT_LAYOUT_SINGLETON:
			exponent = i == count / 2 ? 31 : 0;
			break;
		}
		alignments[i] = UINT32_C(1) << exponent;
	}
	if (pattern == STRUCT_LAYOUT_NEAR_SORTED && count > 1) {
		uint32_t a = count / 3;
		uint32_t b = 2 * count / 3;
		uint32_t tmp = alignments[a];
		alignments[a] = alignments[b];
		alignments[b] = tmp;
	}
}

static void
test_struct_layout_order(void)
{
	Arena arena;
	arena_init(&arena);
	const uint32_t capacity = 65536;
	uint32_t *alignments = ARENA_ARRAY(&arena, uint32_t, capacity);
	uint32_t *order = ARENA_ARRAY(&arena, uint32_t, capacity);
	uint8_t *seen = ARENA_ARRAY(&arena, uint8_t, capacity);
	/* These calls deliberately cannot be checked via memory reads. */
	struct_layout_order(NULL, NULL, 0);
	struct_layout_order(NULL, NULL, 1);
	for (size_t test_index = 0; test_index < sizeof(struct_layout_expected) / sizeof(struct_layout_expected[0]); test_index++) {
		StructLayoutExpected expected = struct_layout_expected[test_index];
		uint32_t count = expected.count;
		struct_layout_test_alignments(alignments, count, expected.pattern);
		for (uint32_t i = 0; i < count; i++) {
			order[i] = i;
			seen[i] = 0;
		}
		struct_layout_order(order, alignments, count);
		bool permutation = true;
		bool sorted = true;
		uint64_t hash = UINT64_C(14695981039346656037);
		for (uint32_t i = 0; i < count; i++) {
			uint32_t word = order[i];
			hash = (hash ^ word) * UINT64_C(1099511628211);
			if (word >= count) {
				permutation = false;
				sorted = false;
				continue;
			}
			if (seen[word] != 0)
				permutation = false;
			seen[word] = 1;
			if (i != 0 && order[i - 1] < count && alignments[order[i - 1]] < alignments[word])
				sorted = false;
		}
		bool matches_reference = hash == expected.hash;
		if (!permutation || !sorted || !matches_reference)
			fprintf(stderr, "struct layout regression: row %zu, pattern %u, count %u\n", test_index, (unsigned)expected.pattern, (unsigned)count);
		CHECK(permutation);
		CHECK(sorted);
		CHECK(matches_reference);
	}
	arena_free(&arena);
}

static void
test_struct_field_layout_api(void)
{
	const struct {
		const char *source;
		bool success;
		bool resolved;
		uint32_t fields;
	} cases[] = {
		{"const S=struct { a:u8, }; fn use(p:*S) void { _=p; } pub fn main() void { use(undefined); }", true, false, 0},
		{"const S=struct { a:u8, b:u64, z:[0]u64, }; fn use(p:*S) void { _=p; } pub fn main() void { use(undefined); _=@sizeOf(S); }", true, true, 3},
		{"const S=struct {}; fn use(p:*S) void { _=p; } pub fn main() void { use(undefined); _=@sizeOf(S); }", true, true, 0},
		{"const S=struct { a:u8=0, }; fn use(p:*S) void { _=p; } pub fn main() void { use(undefined); _=@sizeOf(S); }", false, false, 0},
		{"const S=struct { a:[4294967295]u8, b:u8, }; fn use(p:*S) void { _=p; } pub fn main() void { use(undefined); _=@sizeOf(S); }", false, false, 0},
		{"const S=struct { a:[4294967291]u8, b:u32, }; fn use(p:*S) void { _=p; } pub fn main() void { use(undefined); _=@sizeOf(S); }", false, false, 0},
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
		FileId file = source_add_memory(&sources, "struct_field_layout_api.zig", cases[i].source, (uint32_t)strlen(cases[i].source));
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		CHECK(!diag_has_errors(&diag));
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		CHECK(!diag_has_errors(&diag));
		Sema *sema = sema_create(&arena, &diag, &interner, &sources);
		CHECK(sema_analyze_executable(sema, file, ir) == cases[i].success);
		CHECK(sema_func_count(sema) == 3);
		TypeFn fn = sema_type(sema, sema_func(sema, 2)->type)->as.fn;
		TypeId type = sema_type(sema, fn.params[0])->as.pointer.child;
		const uint32_t marker = 42;
		const uint32_t *order = &marker;
		uint32_t count = 123;
		CHECK(sema_struct_layout(sema, type, &order, &count) == cases[i].resolved);
		CHECK(count == (cases[i].resolved ? cases[i].fields : 123u));
		if (!cases[i].resolved)
			CHECK(order == &marker);
		SemaStructFieldLayout field = {STRING_NONE, UINT32_MAX, 123};
		CHECK(!sema_struct_field_layout(sema, type, cases[i].fields, &field));
		CHECK(field.name == STRING_NONE && field.type == UINT32_MAX && field.offset == 123);
		if (!cases[i].resolved) {
			for (uint32_t retry = 0; retry < 3; retry++) {
				CHECK(!sema_struct_layout(sema, type, &order, &count));
				CHECK(order == &marker && count == 123);
				CHECK(!sema_struct_field_layout(sema, type, 0, &field));
				CHECK(field.name == STRING_NONE && field.type == UINT32_MAX && field.offset == 123);
			}
		}
		if (cases[i].fields == 3) {
			CHECK(order[0] == 1 && order[1] == 2 && order[2] == 0);
			CHECK(sema_struct_field_layout(sema, type, 0, &field));
			CHECK(strcmp(intern_text(&interner, field.name), "a") == 0);
			CHECK(field.type == type_id_of_well_known(IR_REF_TYPE_U8) && field.offset == 8);
			CHECK(sema_struct_field_layout(sema, type, 1, &field));
			CHECK(strcmp(intern_text(&interner, field.name), "b") == 0);
			CHECK(field.type == type_id_of_well_known(IR_REF_TYPE_U64) && field.offset == 0);
			CHECK(sema_struct_field_layout(sema, type, 2, &field));
			CHECK(strcmp(intern_text(&interner, field.name), "z") == 0 && field.offset == 8);
		}
		order = &marker;
		count = 123;
		field = (SemaStructFieldLayout){STRING_NONE, UINT32_MAX, 123};
		TypeId byte = type_id_of_well_known(IR_REF_TYPE_U8);
		CHECK(!sema_struct_layout(sema, byte, &order, &count));
		CHECK(order == &marker && count == 123);
		CHECK(!sema_struct_field_layout(sema, byte, 0, &field));
		CHECK(field.name == STRING_NONE && field.type == UINT32_MAX && field.offset == 123);
		arena_free(&arena);
	}
}

static void
test_in_comptime(void)
{
	const struct {
		const char *source;
		const char *error;
	} cases[] = {
		{"pub fn main() void { const n:u8=if((@inComptime())==false)0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if((!@inComptime())==true)0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(((@inComptime()))==false)0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if((if(true) @inComptime() else true)==false)0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if((@inComptime() or @inComptime())==false)0 else 256; _=n; }", NULL},
		{"const x=@inComptime(); pub fn main() void { _=x; }", "redundant '@inComptime'"},
		{"var x=@inComptime(); pub fn main() void { _=&x; }", "redundant '@inComptime'"},
		{"const x=@inComptime(); pub fn main() void {}", "redundant '@inComptime'"},
		{"pub fn main() void { const x:if(@inComptime())u8 else u16=0; _=x; }", "redundant '@inComptime'"},
		{"pub fn main() void { _=@sizeOf(if(@inComptime())u8 else u16); }", "redundant '@inComptime'"},
		{"pub fn main() void { _=@alignOf(if(@inComptime())u8 else u16); }", "redundant '@inComptime'"},
		{"pub fn main() void { _=@bitSizeOf(if(@inComptime())u8 else u16); }", "redundant '@inComptime'"},
		{"pub fn main() void { _=@as(if(@inComptime())u8 else u16,0); }", "redundant '@inComptime'"},
		{"pub fn main() void { _=[if(@inComptime())1 else 2]u8; }", "redundant '@inComptime'"},
		{"pub fn main() void { _=[2]if(@inComptime())u8 else u16; }", "redundant '@inComptime'"},
		{"pub fn main() void { _=*if(@inComptime())u8 else u16; }", "redundant '@inComptime'"},
		{"pub fn main() void { _=[2:if(@inComptime())0 else 1]u8; }", "redundant '@inComptime'"},
		{"pub fn main() void { _=[:if(@inComptime())0 else 1]u8; }", "redundant '@inComptime'"},
		{"pub fn main() void { _=[*:if(@inComptime())0 else 1]u8; }", "redundant '@inComptime'"},
		{"pub fn main() void { _=*align(if(@inComptime())4 else 8)u8; }", "redundant '@inComptime'"},
		{"pub fn main() void { var x:u8 align(if(@inComptime())4 else 8)=0; _=&x; }", "redundant '@inComptime'"},
		{"pub fn main() void { _=@hasField([]u8,if(@inComptime())\"ptr\" else \"len\"); }", "redundant '@inComptime'"},
		{"pub fn main() void { _=@hasDecl(struct {},if(@inComptime())\"a\" else \"b\"); }", "redundant '@inComptime'"},
		{"pub fn main() void { _=@hasField(if(@inComptime())[]u8 else []u16,\"len\"); }", "redundant '@inComptime'"},
		{"pub fn main() void { const x=[_]u8{1}; _=@field(x,if(@inComptime())\"len\" else \"len\"); }", "redundant '@inComptime'"},
		{"pub fn main() void { comptime var x=@inComptime(); _=&x; }", "redundant '@inComptime'"},
		{"fn f() if(@inComptime())u8 else u16 { return 0; } pub fn main() void { _=f(); }", "redundant '@inComptime'"},
		{"fn f(x:if(@inComptime())u8 else u16) void { _=x; } pub fn main() void { f(0); }", "redundant '@inComptime'"},
		{"pub fn main() void { _=@TypeOf(@as(if(@inComptime())u8 else u16,0)); }", "redundant '@inComptime'"},
		{"pub fn main() void { _=@TypeOf([if(@inComptime())1 else 2]u8); }", "redundant '@inComptime'"},
		{"const N=struct { const x=@inComptime(); }; pub fn main() void { _=N; }", "redundant '@inComptime'"},
		{"pub fn main() void { _=@TypeOf(@inComptime()); }", NULL},
		{"pub fn main() void { _=@TypeOf(blk: { const n:u8=if(!@inComptime())0 else 256; _=n; break :blk false; }); }", NULL},
		{"const T=@TypeOf(blk: { const n:u8=if(!@inComptime())0 else 256; _=n; break :blk false; }); pub fn main() void { _=@TypeOf(blk: { const n:u8=if(!@inComptime())0 else 256; _=n; break :blk false; }); }", NULL},
		{"pub fn main() void { _=@as(bool,@inComptime()); }", NULL},
		{"pub fn main() void { const a=[_]bool{@inComptime(),true}; _=a; }", NULL},
		{"pub fn main() void { var a=[_]bool{true,false}; const s=a[0..1:@inComptime()]; _=s; }", NULL},
		{"pub fn main() void { const T=@TypeOf(@inComptime()); const n:u8=if(T==bool)0 else 256; _=n; }", NULL},
		{"pub fn main() void { const a:u8=if(@inComptime())256 else 0; _=a; }", NULL},
		{"pub fn main() void { _=@inComptime(1); }", "expected 0 arguments"},
		{"pub fn main() void { _=@inComptime(1,2); }", "expected 0 arguments"},
		{"const x=@inComptime(1); pub fn main() void { _=x; }", "expected 0 arguments"},
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
		const char *source = cases[i].source;
		FileId file = source_add_memory(&sources, "in_comptime.zig", source, (uint32_t)strlen(source));
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		CHECK(!diag_has_errors(&diag));
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		bool success = false;
		if (!diag_has_errors(&diag)) {
			Sema *sema = sema_create(&arena, &diag, &interner, &sources);
			success = sema_analyze_executable(sema, file, ir);
		}
		CHECK(success == (cases[i].error == NULL));
		if (cases[i].error != NULL)
			CHECK(diag.count != 0 && strstr(diag.items[0].message, cases[i].error) != NULL);
		arena_free(&arena);
	}
}

static void
test_typeof(void)
{
	const struct {
		const char *source;
		const char *error;
	} cases[] = {
		{"pub fn main() void { const n:u8=if(@TypeOf(1)==comptime_int)0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(@TypeOf(true)==bool)0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(@TypeOf(u8)==type)0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(@TypeOf(struct {})==type)0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(@TypeOf(@as(u33,undefined))==u33)0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(@TypeOf(@as(f32,undefined))==f32)0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(@TypeOf(@as([2]u8,undefined))==[2]u8)0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(@TypeOf(@as([]u8,undefined))==[]u8)0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(@TypeOf(\"abc\")==*const [3:0]u8)0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(@TypeOf(@TypeOf(undefined))==type)0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(@TypeOf(@as(u8,undefined),@as(i8,undefined))==u8)0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(@TypeOf(@as(i8,undefined),@as(u8,undefined))==i8)0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(@TypeOf(@as(u8,undefined),@as(i8,undefined),@as(i16,undefined))==i16)0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(@TypeOf(1,@as(u8,2),@as(u16,3))==u16)0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(@TypeOf(@as(u8,1),256)==u8)0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(@TypeOf(\"a\",\"abc\")==[:0]const u8)0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(@TypeOf(@as(f32,undefined),@as(f64,undefined))==f64)0 else 256; _=n; }", NULL},
		{"pub fn main() void { const n:u8=if(@TypeOf(@as(f32,undefined),@as(u64,undefined))==f32)0 else 256; _=n; }", NULL},
		{"const T=@TypeOf(f()); fn f() [2]u8 { return .{1,2}; } pub fn main() void { const a:T=.{3,4}; _=a; }", NULL},
		{"fn f(a:u8,b:u16) u16 { _=@TypeOf(a+b); return a+b; } pub fn main() void { _=f(1,2); }", NULL},
		{"fn f(a:[2]u8) [2]u8 { _=@TypeOf(a[0..]); return a; } pub fn main() void { _=f(.{1,2}); }", NULL},
		{"fn f(b:bool) u8 { _=@TypeOf(if(b) return 99 else @as(u8,3)); return 4; } pub fn main() void { _=f(true); }", NULL},
		{"fn f(b:bool) void { out: { _=@TypeOf(if(b) break :out else @as(u8,3)); } } pub fn main() void { f(true); }", NULL},
		{"pub fn main() void { var a:u8=1; _=@TypeOf({ a=2; }); _=&a; }", NULL},
		{"pub fn main() void { _=@TypeOf(blk: { var a:[9000000]u8=undefined; _=&a; break :blk @as(u8,1); }); _=@TypeOf(blk: { var a:[9000000]u8=undefined; _=&a; break :blk @as(u8,1); }); }", NULL},
		{"pub fn main() void { _=@TypeOf(); }", "expected at least 1 argument"},
		{"pub fn main() void { _=@TypeOf(unreachable); }", "unreachable code"},
		{"pub fn main() void { _=@TypeOf(return); }", "unreachable code"},
		{"fn bad() u8 { return 256; } pub fn main() void { _=@TypeOf(bad()); }", "cannot represent integer value '256'"},
		{"fn bad() u8 { return 256; } pub fn main() void { _=@TypeOf(&bad); }", "cannot represent integer value '256'"},
		{"var x:u8=256; pub fn main() void { _=@TypeOf(x); }", "cannot represent integer value '256'"},
		{"pub fn main() void { _=@TypeOf(@as(*u8,undefined).*); }", "cannot dereference undefined"},
		{"pub fn main() void { _=@TypeOf(@as(u8,255)+1); }", "overflow of integer type"},
		{"pub fn main() void { _=@TypeOf(1,true); }", "incompatible types"},
		{"pub fn main() void { _=@TypeOf(@as(comptime_int,undefined),@as(u8,1)); }", "incompatible types"},
		{"fn f(a:u8,b:i8) void { _=@TypeOf(a,b); } pub fn main() void { f(1,2); }", "incompatible types"},
		{"pub fn main() void { _=@TypeOf(null,true); }", "not supported by szc yet: optional peer"},
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
		const char *source = cases[i].source;
		FileId file = source_add_memory(&sources, "typeof.zig", source, (uint32_t)strlen(source));
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		CHECK(!diag_has_errors(&diag));
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		bool success = false;
		if (!diag_has_errors(&diag)) {
			Sema *sema = sema_create(&arena, &diag, &interner, &sources);
			success = sema_analyze_executable(sema, file, ir);
		}
		CHECK(success == (cases[i].error == NULL));
		if (cases[i].error != NULL)
			CHECK(diag.count != 0 && strstr(diag.items[0].message, cases[i].error) != NULL);
		arena_free(&arena);
	}
}

static void
test_quoted_identifiers(void)
{
	const struct {
		const char *source;
		const char *error;
	} cases[] = {
		{"const @\"answer\":u8=7; pub fn main() void { _=answer; }", NULL},
		{"const answer:u8=7; pub fn main() void { _=@\"\\x61nswer\"; }", NULL},
		{"const @\"\\u{e9}\":u8=7; pub fn main() void { _=@\"é\"; }", NULL},
		{"const N=struct { var @\"\\x76alue\":u8=7; }; pub fn main() void { N.value+=1; }", NULL},
		{"fn @\"space name\"(@\"\\x6e\":u8) u8 { return n; } pub fn main() void { _=@\"space\\x20name\"(7); }", NULL},
		{"pub fn @\"\\x6dain\"() void {}", NULL},
		{"pub fn main() void { @\"\\x6cab\": { break :lab; } }", NULL},
		{"const @\"u8\":u8=7; pub fn main() void { _=@\"u8\"; _=@sizeOf(u8); }", NULL},
		{"var @\"bool\":u8=7; pub fn main() void { @\"bool\"+=1; const p:*const type=&bool; _=p; }", NULL},
		{"var @\"u8\":u8=7; pub fn main() void { u8=3; }", "cannot assign to constant"},
		{"pub fn main() void { _=@\"u8\"; }", "use of undeclared identifier 'u8'"},
		{"const @\"\":u8=7; pub fn main() void {}", "identifier cannot be empty"},
		{"const @\"a\\x00b\":u8=7; pub fn main() void {}", "identifier cannot contain null bytes"},
		{"const @\"\\u{0}\":u8=7; pub fn main() void {}", "identifier cannot contain null bytes"},
		{"const @\"\\q\":u8=7; pub fn main() void {}", "invalid escape character"},
		{"const @\"\\u{d800}\":u8=7; pub fn main() void {}", "valid unicode scalar"},
		{"const answer:u8=7; const @\"\\x61nswer\":u8=8; pub fn main() void { _=answer; }", "duplicate struct member name"},
		{"const answer:u8=7; pub fn main() void { const @\"\\x61nswer\":u8=8; _=answer; }", "local constant shadows declaration"},
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
		const char *source = cases[i].source;
		FileId file = source_add_memory(&sources, "quoted_identifiers.zig", source, (uint32_t)strlen(source));
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
		CHECK(!diag_has_errors(&diag));
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		bool success = false;
		if (!diag_has_errors(&diag)) {
			Sema *sema = sema_create(&arena, &diag, &interner, &sources);
			success = sema_analyze_executable(sema, file, ir);
		}
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
	test_scalar_memset();
	test_aggregate_memset();
	test_empty_array_storage();
	test_struct_namespaces();
	test_quoted_identifiers();
	test_has_decl();
	test_has_field();
	test_struct_metadata();
	test_struct_value_interning();
	test_struct_static_values();
	test_struct_field_pointers();
	test_known_struct_values();
	test_runtime_struct_values();
	test_struct_mutable_globals();
	test_struct_internal_abi();
	test_struct_global_publication();
	test_runtime_struct_initializer_ir();
	test_struct_initializer_ir();
	test_struct_offsets();
	test_struct_bit_layout();
	test_struct_layout_api();
	test_struct_field_layout_api();
	test_struct_layout_order();
	test_typeof();
	test_in_comptime();
	test_field();
	test_scalar_memcpy();
	test_aggregate_memcpy();
	test_packed_bits();
	test_constant_aggregate_bit_casts();
	test_runtime_aggregate_bit_casts();
	test_binary_diagnostics();

	printf("unit: %u checks, %u failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
