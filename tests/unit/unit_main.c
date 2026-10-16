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
	test_binary_diagnostics();

	printf("unit: %u checks, %u failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
