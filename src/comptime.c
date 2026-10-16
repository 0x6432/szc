/*
 * Comptime values (interned) and big-integer arithmetic.
 *
 * sema.c decides *when* something is evaluated at comptime; this file holds
 * the value representation and the operations on values.
 */
#include "comptime.h"

#include <stdalign.h>
#include <stdio.h>
#include <string.h>

/* ---------- Big integers ---------- */

static BigInt
bigint_alloc(Arena *arena, uint32_t limb_count)
{
	BigInt result;
	result.negative = false;
	result.limb_count = limb_count;
	result.limbs = limb_count == 0 ? NULL : arena_zalloc(arena, limb_count * sizeof(uint32_t), alignof(uint32_t));
	return result;
}

/* Drops leading zero limbs and clears the sign of zero. */
static void
bigint_normalize(BigInt *a)
{
	while (a->limb_count > 0 && a->limbs[a->limb_count - 1] == 0)
		a->limb_count = a->limb_count - 1;
	if (a->limb_count == 0)
		a->negative = false;
}

BigInt
bigint_from_limbs(Arena *arena, const uint32_t *limbs, uint32_t limb_count)
{
	BigInt result = bigint_alloc(arena, limb_count);
	if (limb_count > 0)
		memcpy(result.limbs, limbs, limb_count * sizeof(uint32_t));
	bigint_normalize(&result);
	return result;
}

BigInt
bigint_from_u64(Arena *arena, uint64_t value)
{
	uint32_t limbs[2];
	limbs[0] = (uint32_t)value;
	limbs[1] = (uint32_t)(value >> 32);
	return bigint_from_limbs(arena, limbs, 2);
}

bool
bigint_is_zero(const BigInt *a)
{
	return a->limb_count == 0;
}

static int
compare_magnitude(const BigInt *a, const BigInt *b)
{
	if (a->limb_count != b->limb_count)
		return a->limb_count < b->limb_count ? -1 : 1;
	for (uint32_t i = a->limb_count; i > 0; i--) {
		if (a->limbs[i - 1] != b->limbs[i - 1])
			return a->limbs[i - 1] < b->limbs[i - 1] ? -1 : 1;
	}
	return 0;
}

int
bigint_compare(const BigInt *a, const BigInt *b)
{
	if (a->negative != b->negative)
		return a->negative ? -1 : 1;
	int magnitude = compare_magnitude(a, b);
	return a->negative ? -magnitude : magnitude;
}

static BigInt
add_magnitude(Arena *arena, const BigInt *a, const BigInt *b)
{
	uint32_t count = (a->limb_count > b->limb_count ? a->limb_count : b->limb_count) + 1;
	BigInt result = bigint_alloc(arena, count);
	uint64_t carry = 0;
	for (uint32_t i = 0; i < count; i++) {
		uint64_t sum = carry;
		if (i < a->limb_count)
			sum = sum + a->limbs[i];
		if (i < b->limb_count)
			sum = sum + b->limbs[i];
		result.limbs[i] = (uint32_t)sum;
		carry = sum >> 32;
	}
	bigint_normalize(&result);
	return result;
}

/* |a| - |b| where |a| >= |b|. */
static BigInt
sub_magnitude(Arena *arena, const BigInt *a, const BigInt *b)
{
	BigInt result = bigint_alloc(arena, a->limb_count);
	int64_t borrow = 0;
	for (uint32_t i = 0; i < a->limb_count; i++) {
		int64_t difference = (int64_t)a->limbs[i] - borrow;
		if (i < b->limb_count)
			difference = difference - (int64_t)b->limbs[i];
		borrow = 0;
		if (difference < 0) {
			difference = difference + ((int64_t)1 << 32);
			borrow = 1;
		}
		result.limbs[i] = (uint32_t)difference;
	}
	bigint_normalize(&result);
	return result;
}

BigInt
bigint_add(Arena *arena, const BigInt *a, const BigInt *b)
{
	if (a->negative == b->negative) {
		BigInt result = add_magnitude(arena, a, b);
		result.negative = a->negative && !bigint_is_zero(&result);
		return result;
	}
	/* Different signs: subtract the smaller magnitude from the larger. */
	if (compare_magnitude(a, b) >= 0) {
		BigInt result = sub_magnitude(arena, a, b);
		result.negative = a->negative && !bigint_is_zero(&result);
		return result;
	}
	BigInt result = sub_magnitude(arena, b, a);
	result.negative = b->negative && !bigint_is_zero(&result);
	return result;
}

BigInt
bigint_negate(Arena *arena, const BigInt *a)
{
	BigInt result = bigint_from_limbs(arena, a->limbs, a->limb_count);
	result.negative = !a->negative && !bigint_is_zero(&result);
	return result;
}

BigInt
bigint_sub(Arena *arena, const BigInt *a, const BigInt *b)
{
	BigInt negated = bigint_negate(arena, b);
	return bigint_add(arena, a, &negated);
}

BigInt
bigint_mul(Arena *arena, const BigInt *a, const BigInt *b)
{
	BigInt result = bigint_alloc(arena, a->limb_count + b->limb_count);
	for (uint32_t i = 0; i < a->limb_count; i++) {
		uint64_t carry = 0;
		for (uint32_t j = 0; j < b->limb_count; j++) {
			uint64_t product = (uint64_t)a->limbs[i] * b->limbs[j] + result.limbs[i + j] + carry;
			result.limbs[i + j] = (uint32_t)product;
			carry = product >> 32;
		}
		result.limbs[i + b->limb_count] = (uint32_t)carry;
	}
	bigint_normalize(&result);
	result.negative = (a->negative != b->negative) && !bigint_is_zero(&result);
	return result;
}

/* Number of significant bits of the magnitude. */
static uint32_t
magnitude_bits(const BigInt *a)
{
	if (a->limb_count == 0)
		return 0;
	uint32_t top = a->limbs[a->limb_count - 1];
	uint32_t bits = 0;
	while (top != 0) {
		bits = bits + 1;
		top = top >> 1;
	}
	return (a->limb_count - 1) * 32 + bits;
}

/* True if |a| == 2^(bits-1) exactly (the most negative signed value). */
static bool
is_power_of_two(const BigInt *a)
{
	if (a->limb_count == 0)
		return false;
	for (uint32_t i = 0; i + 1 < a->limb_count; i++) {
		if (a->limbs[i] != 0)
			return false;
	}
	uint32_t top = a->limbs[a->limb_count - 1];
	return (top & (top - 1)) == 0;
}

bool
bigint_fits(const BigInt *a, bool is_signed, uint32_t bits)
{
	uint32_t needed = magnitude_bits(a);
	if (!is_signed)
		return !a->negative && needed <= bits;
	if (bits == 0)
		return bigint_is_zero(a);
	if (!a->negative)
		return needed <= bits - 1;
	/* -2^(bits-1) fits. */
	return needed <= bits - 1 || (needed == bits && is_power_of_two(a));
}

BigInt
bigint_wrap(Arena *arena, const BigInt *a, bool is_signed, uint32_t bits)
{
	uint32_t limb_count = (bits + 31) / 32;
	BigInt result = bigint_alloc(arena, limb_count);
	if (limb_count == 0)
		return result;
	/* Two's complement of the magnitude, truncated to limb_count limbs. */
	uint64_t carry = 1;
	for (uint32_t i = 0; i < limb_count; i++) {
		uint32_t limb = i < a->limb_count ? a->limbs[i] : 0;
		if (a->negative) {
			uint64_t sum = (uint64_t)(uint32_t)~limb + carry;
			limb = (uint32_t)sum;
			carry = sum >> 32;
		}
		result.limbs[i] = limb;
	}
	uint32_t top_bits = bits - (limb_count - 1) * 32;
	if (top_bits < 32)
		result.limbs[limb_count - 1] = result.limbs[limb_count - 1] & ((1u << top_bits) - 1);
	/* Reinterpret as signed if the sign bit is set. */
	bool sign_set = is_signed && ((result.limbs[limb_count - 1] >> (top_bits - 1)) & 1) != 0;
	if (sign_set) {
		/* value - 2^bits, i.e. negate the two's complement within `bits`. */
		carry = 1;
		for (uint32_t i = 0; i < limb_count; i++) {
			uint64_t sum = (uint64_t)(uint32_t)~result.limbs[i] + carry;
			result.limbs[i] = (uint32_t)sum;
			carry = sum >> 32;
		}
		if (top_bits < 32)
			result.limbs[limb_count - 1] = result.limbs[limb_count - 1] & ((1u << top_bits) - 1);
		result.negative = true;
	}
	bigint_normalize(&result);
	return result;
}

uint64_t
bigint_to_u64_bits(const BigInt *a)
{
	uint64_t magnitude = 0;
	if (a->limb_count > 0)
		magnitude = a->limbs[0];
	if (a->limb_count > 1)
		magnitude = magnitude | ((uint64_t)a->limbs[1] << 32);
	return a->negative ? (uint64_t)0 - magnitude : magnitude;
}

char *
bigint_to_string(Arena *arena, const BigInt *a)
{
	if (bigint_is_zero(a))
		return arena_strndup(arena, "0", 1);
	/* Repeated division by 10^9 of a scratch copy. */
	uint32_t count = a->limb_count;
	uint32_t *scratch = arena_alloc(arena, count * sizeof(uint32_t), alignof(uint32_t));
	memcpy(scratch, a->limbs, count * sizeof(uint32_t));
	uint32_t chunk_capacity = count * 2 + 1;
	uint32_t *chunks = arena_alloc(arena, chunk_capacity * sizeof(uint32_t), alignof(uint32_t));
	uint32_t chunk_count = 0;
	while (count > 0) {
		uint64_t remainder = 0;
		for (uint32_t i = count; i > 0; i--) {
			uint64_t current = (remainder << 32) | scratch[i - 1];
			scratch[i - 1] = (uint32_t)(current / 1000000000u);
			remainder = current % 1000000000u;
		}
		chunks[chunk_count] = (uint32_t)remainder;
		chunk_count = chunk_count + 1;
		while (count > 0 && scratch[count - 1] == 0)
			count = count - 1;
	}
	char *text = arena_alloc(arena, chunk_count * 9 + 2, 1);
	size_t length = 0;
	if (a->negative)
		text[length++] = '-';
	length = length + (size_t)sprintf(text + length, "%u", chunks[chunk_count - 1]);
	for (uint32_t i = chunk_count - 1; i > 0; i--)
		length = length + (size_t)sprintf(text + length, "%09u", chunks[i - 1]);
	return text;
}

/* Magnitude shifted left by `shift` bits (sign kept): a * 2^shift. */
BigInt
bigint_shl(Arena *arena, const BigInt *a, uint32_t shift)
{
	if (bigint_is_zero(a))
		return *a;
	uint32_t limb_shift = shift / 32;
	uint32_t bit_shift = shift % 32;
	BigInt result = bigint_alloc(arena, a->limb_count + limb_shift + 1);
	for (uint32_t i = 0; i < a->limb_count; i++) {
		uint64_t wide = (uint64_t)a->limbs[i] << bit_shift;
		result.limbs[i + limb_shift] = result.limbs[i + limb_shift] | (uint32_t)wide;
		result.limbs[i + limb_shift + 1] = (uint32_t)(wide >> 32);
	}
	result.negative = a->negative;
	bigint_normalize(&result);
	return result;
}

/* Arithmetic shift right: floor(a / 2^shift). */
BigInt
bigint_shr(Arena *arena, const BigInt *a, uint32_t shift)
{
	uint32_t limb_shift = shift / 32;
	uint32_t bit_shift = shift % 32;
	if (limb_shift >= a->limb_count) {
		if (!a->negative)
			return bigint_alloc(arena, 0);
		BigInt one = bigint_from_u64(arena, 1);
		return bigint_negate(arena, &one);
	}
	uint32_t count = a->limb_count - limb_shift;
	BigInt result = bigint_alloc(arena, count);
	for (uint32_t i = 0; i < count; i++) {
		uint64_t wide = a->limbs[i + limb_shift];
		if (i + limb_shift + 1 < a->limb_count)
			wide = wide | ((uint64_t)a->limbs[i + limb_shift + 1] << 32);
		result.limbs[i] = (uint32_t)(wide >> bit_shift);
	}
	bigint_normalize(&result);
	if (!a->negative)
		return result;
	/* Round towards negative infinity: add one if any bit was shifted out. */
	bool lost = false;
	for (uint32_t i = 0; i < limb_shift; i++)
		lost = lost || a->limbs[i] != 0;
	if (bit_shift > 0 && (a->limbs[limb_shift] & ((1u << bit_shift) - 1)) != 0)
		lost = true;
	if (lost) {
		BigInt one = bigint_from_u64(arena, 1);
		result = bigint_add(arena, &result, &one);
	}
	return bigint_negate(arena, &result);
}

/* Compares limb arrays of equal length `count`. */
static int
compare_limbs(const uint32_t *a, const uint32_t *b, uint32_t count)
{
	for (uint32_t i = count; i > 0; i--) {
		if (a[i - 1] != b[i - 1])
			return a[i - 1] < b[i - 1] ? -1 : 1;
	}
	return 0;
}

/* Truncating division. Returns false (and leaves the outputs alone) if `b` is zero. */
bool
bigint_divmod_trunc(Arena *arena, const BigInt *a, const BigInt *b, BigInt *quotient, BigInt *remainder)
{
	if (bigint_is_zero(b))
		return false;
	/* Shift-subtract long division on the magnitudes, in place. */
	uint32_t count = b->limb_count + 1;
	uint32_t *divisor = arena_zalloc(arena, count * sizeof(uint32_t), alignof(uint32_t));
	memcpy(divisor, b->limbs, b->limb_count * sizeof(uint32_t));
	uint32_t *rest = arena_zalloc(arena, count * sizeof(uint32_t), alignof(uint32_t));
	BigInt q = bigint_alloc(arena, a->limb_count);
	for (uint32_t bit = magnitude_bits(a); bit > 0; bit--) {
		uint32_t index = bit - 1;
		/* rest = rest * 2 + bit */
		uint32_t carry = (a->limbs[index / 32] >> (index % 32)) & 1;
		for (uint32_t i = 0; i < count; i++) {
			uint32_t next = rest[i] >> 31;
			rest[i] = (rest[i] << 1) | carry;
			carry = next;
		}
		if (compare_limbs(rest, divisor, count) >= 0) {
			uint64_t borrow = 0;
			for (uint32_t i = 0; i < count; i++) {
				uint64_t difference = (uint64_t)rest[i] - divisor[i] - borrow;
				rest[i] = (uint32_t)difference;
				borrow = (difference >> 32) & 1;
			}
			q.limbs[index / 32] = q.limbs[index / 32] | (1u << (index % 32));
		}
	}
	bigint_normalize(&q);
	q.negative = (a->negative != b->negative) && !bigint_is_zero(&q);
	BigInt r = bigint_from_limbs(arena, rest, count);
	r.negative = a->negative && !bigint_is_zero(&r);
	*quotient = q;
	*remainder = r;
	return true;
}

/* Two's complement of `a` in `count` limbs (count must exceed a's limb count). */
static uint32_t *
to_twos_complement(Arena *arena, const BigInt *a, uint32_t count)
{
	uint32_t *limbs = arena_zalloc(arena, count * sizeof(uint32_t), alignof(uint32_t));
	uint64_t carry = 1;
	for (uint32_t i = 0; i < count; i++) {
		uint32_t limb = i < a->limb_count ? a->limbs[i] : 0;
		if (a->negative) {
			uint64_t sum = (uint64_t)(uint32_t)~limb + carry;
			limb = (uint32_t)sum;
			carry = sum >> 32;
		}
		limbs[i] = limb;
	}
	return limbs;
}

static BigInt
from_twos_complement(Arena *arena, uint32_t *limbs, uint32_t count)
{
	bool negative = (limbs[count - 1] >> 31) != 0;
	if (negative) {
		uint64_t carry = 1;
		for (uint32_t i = 0; i < count; i++) {
			uint64_t sum = (uint64_t)(uint32_t)~limbs[i] + carry;
			limbs[i] = (uint32_t)sum;
			carry = sum >> 32;
		}
	}
	BigInt result = bigint_from_limbs(arena, limbs, count);
	result.negative = negative && !bigint_is_zero(&result);
	return result;
}

/* Bitwise and/or/xor with infinite two's complement semantics. */
bool
bigint_divmod(Arena *arena, const BigInt *a, const BigInt *b, BigIntDivRound round, BigInt *quotient, BigInt *remainder)
{
	/* Preserve inputs when the output descriptors alias a or b. */
	BigInt lhs = *a;
	BigInt rhs = *b;
	if (!bigint_divmod_trunc(arena, &lhs, &rhs, quotient, remainder))
		return false;
	if (bigint_is_zero(remainder))
		return true;
	bool negative = lhs.negative != rhs.negative;
	BigInt one = bigint_from_u64(arena, 1);
	if (round == BIGINT_DIV_FLOOR && negative) {
		*quotient = bigint_sub(arena, quotient, &one);
		*remainder = bigint_add(arena, remainder, &rhs);
	} else if (round == BIGINT_DIV_CEIL && !negative) {
		*quotient = bigint_add(arena, quotient, &one);
		*remainder = bigint_sub(arena, remainder, &rhs);
	}
	return true;
}

BigInt
bigint_bitop(Arena *arena, const BigInt *a, const BigInt *b, BigIntBitOp op)
{
	uint32_t count = (a->limb_count > b->limb_count ? a->limb_count : b->limb_count) + 1;
	uint32_t *x = to_twos_complement(arena, a, count);
	uint32_t *y = to_twos_complement(arena, b, count);
	for (uint32_t i = 0; i < count; i++) {
		switch (op) {
		case BIGINT_AND:
			x[i] = x[i] & y[i];
			break;
		case BIGINT_OR:
			x[i] = x[i] | y[i];
			break;
		case BIGINT_XOR:
			x[i] = x[i] ^ y[i];
			break;
		}
	}
	return from_twos_complement(arena, x, count);
}

/* Bitwise not: -a - 1. */
BigInt
bigint_not(Arena *arena, const BigInt *a)
{
	BigInt one = bigint_from_u64(arena, 1);
	BigInt negated = bigint_negate(arena, a);
	return bigint_sub(arena, &negated, &one);
}

bool
bigint_to_u32(const BigInt *a, uint32_t *out)
{
	if (a->negative || a->limb_count > 1)
		return false;
	*out = a->limb_count == 0 ? 0 : a->limbs[0];
	return true;
}

/* ---------- Interned values ---------- */

static uint32_t
hash_mix(uint32_t hash, uint32_t word)
{
	hash = hash ^ word;
	return hash * 16777619u;
}

static uint32_t
value_hash(const Value *value)
{
	uint32_t hash = 2166136261u;
	hash = hash_mix(hash, (uint32_t)value->kind);
	hash = hash_mix(hash, value->type);
	switch (value->kind) {
	case VALUE_VOID:
	case VALUE_UNDEFINED:
		break;
	case VALUE_BOOL:
		hash = hash_mix(hash, value->as.boolean ? 1u : 0u);
		break;
	case VALUE_INT:
		hash = hash_mix(hash, value->as.integer.negative ? 1u : 0u);
		for (uint32_t i = 0; i < value->as.integer.limb_count; i++)
			hash = hash_mix(hash, value->as.integer.limbs[i]);
		break;
	case VALUE_TYPE:
		hash = hash_mix(hash, value->as.type_value);
		break;
	case VALUE_GLOBAL:
		hash = hash_mix(hash, value->as.global);
		break;
	case VALUE_REF:
		hash = hash_mix(hash, value->as.object);
		break;
	case VALUE_INT_PTR:
		hash = hash_mix(hash, (uint32_t)value->as.address);
		hash = hash_mix(hash, (uint32_t)(value->as.address >> 32));
		break;
	case VALUE_PTR_CAST:
		hash = hash_mix(hash, value->as.pointer);
		break;
	case VALUE_ELEMENT_PTR:
		hash = hash_mix(hash, value->as.element.base);
		hash = hash_mix(hash, (uint32_t)value->as.element.index);
		hash = hash_mix(hash, (uint32_t)(value->as.element.index >> 32));
		hash = hash_mix(hash, (uint32_t)value->as.element.offset);
		hash = hash_mix(hash, (uint32_t)(value->as.element.offset >> 32));
		hash = hash_mix(hash, value->as.element.logical ? 1u : 0u);
		break;
	case VALUE_SLICE_FIELD_PTR:
		hash = hash_mix(hash, value->as.field.base);
		hash = hash_mix(hash, (uint32_t)value->as.field.offset);
		hash = hash_mix(hash, (uint32_t)(value->as.field.offset >> 32));
		hash = hash_mix(hash, value->as.field.length ? 1u : 0u);
		break;
	case VALUE_SLICE:
		hash = hash_mix(hash, value->as.slice.pointer);
		hash = hash_mix(hash, value->as.slice.length);
		break;
	case VALUE_ARRAY:
		hash = hash_mix(hash, value->as.array.count);
		for (uint32_t i = 0; i < value->as.array.count; i++)
			hash = hash_mix(hash, value->as.array.items[i]);
		break;
	case VALUE_BYTES:
		hash = hash_mix(hash, value->as.bytes.length);
		for (uint32_t i = 0; i < value->as.bytes.length; i++)
			hash = hash_mix(hash, value->as.bytes.data[i]);
		break;
	case VALUE_FUNCTION:
		hash = hash_mix(hash, value->as.function);
		break;
	}
	return hash;
}

static bool
value_equal(const Value *a, const Value *b)
{
	if (a->kind != b->kind || a->type != b->type)
		return false;
	switch (a->kind) {
	case VALUE_VOID:
	case VALUE_UNDEFINED:
		return true;
	case VALUE_BOOL:
		return a->as.boolean == b->as.boolean;
	case VALUE_INT:
		return bigint_compare(&a->as.integer, &b->as.integer) == 0;
	case VALUE_TYPE:
		return a->as.type_value == b->as.type_value;
	case VALUE_GLOBAL:
		return a->as.global == b->as.global;
	case VALUE_REF:
		return a->as.object == b->as.object;
	case VALUE_INT_PTR:
		return a->as.address == b->as.address;
	case VALUE_PTR_CAST:
		return a->as.pointer == b->as.pointer;
	case VALUE_ELEMENT_PTR:
		return a->as.element.base == b->as.element.base
			&& a->as.element.index == b->as.element.index
			&& a->as.element.offset == b->as.element.offset
			&& a->as.element.logical == b->as.element.logical;
	case VALUE_SLICE_FIELD_PTR:
		return a->as.field.base == b->as.field.base
			&& a->as.field.offset == b->as.field.offset && a->as.field.length == b->as.field.length;
	case VALUE_SLICE:
		return a->as.slice.pointer == b->as.slice.pointer && a->as.slice.length == b->as.slice.length;
	case VALUE_ARRAY:
		return a->as.array.count == b->as.array.count && (a->as.array.count == 0
			|| memcmp(a->as.array.items, b->as.array.items, (size_t)a->as.array.count * sizeof(ValueId)) == 0);
	case VALUE_BYTES:
		return a->as.bytes.length == b->as.bytes.length && (a->as.bytes.length == 0 || memcmp(a->as.bytes.data, b->as.bytes.data, a->as.bytes.length) == 0);
	case VALUE_FUNCTION:
		return a->as.function == b->as.function;
	}
	return false;
}

void
value_table_init(ValueTable *table, Arena *arena)
{
	table->objects = NULL;
	table->object_count = 0;
	table->object_capacity = 0;
	table->object_slots = NULL;
	table->object_slot_count = 0;
	table->arena = arena;
	table->capacity = 256;
	table->values = arena_zalloc(arena, table->capacity * sizeof(Value), alignof(Value));
	table->hashes = arena_zalloc(arena, table->capacity * sizeof(uint32_t), alignof(uint32_t));
	table->count = 1;
	table->slot_count = 512;
	table->slots = arena_zalloc(arena, table->slot_count * sizeof(uint32_t), alignof(uint32_t));
}

static void
insert_value_slot(uint32_t *slots, uint32_t slot_count, uint32_t hash, ValueId id)
{
	uint32_t mask = slot_count - 1;
	uint32_t index = hash & mask;
	while (slots[index] != 0)
		index = (index + 1) & mask;
	slots[index] = id;
}

ValueId
value_intern(ValueTable *table, const Value *value)
{
	uint32_t hash = value_hash(value);
	uint32_t mask = table->slot_count - 1;
	for (uint32_t index = hash & mask; table->slots[index] != 0; index = (index + 1) & mask) {
		ValueId id = table->slots[index];
		if (table->hashes[id] == hash && value_equal(&table->values[id], value))
			return id;
	}

	if (table->count == table->capacity) {
		uint32_t new_capacity = table->capacity * 2;
		table->values = arena_grow_array(table->arena, table->values, table->count, new_capacity, sizeof(Value), alignof(Value));
		table->hashes = arena_grow_array(table->arena, table->hashes, table->count, new_capacity, sizeof(uint32_t), alignof(uint32_t));
		table->capacity = new_capacity;
	}
	ValueId id = table->count;
	table->values[id] = *value;
	if (value->kind == VALUE_INT)
		table->values[id].as.integer = bigint_from_limbs(table->arena, value->as.integer.limbs, value->as.integer.limb_count);
	if (value->kind == VALUE_INT)
		table->values[id].as.integer.negative = value->as.integer.negative;
	if (value->kind == VALUE_ARRAY) {
		ValueId *copy = ARENA_ARRAY(table->arena, ValueId, value->as.array.count);
		if (value->as.array.count != 0)
			memcpy(copy, value->as.array.items, (size_t)value->as.array.count * sizeof(ValueId));
		table->values[id].as.array.items = copy;
	}
	if (value->kind == VALUE_BYTES) {
		uint8_t *copy = arena_zalloc(table->arena, (size_t)value->as.bytes.length + 1, 1);
		if (value->as.bytes.length != 0)
			memcpy(copy, value->as.bytes.data, value->as.bytes.length);
		table->values[id].as.bytes.data = copy;
	}
	table->hashes[id] = hash;
	table->count = table->count + 1;

	if (table->count * 2 > table->slot_count) {
		uint32_t new_slot_count = table->slot_count * 2;
		uint32_t *new_slots = arena_zalloc(table->arena, new_slot_count * sizeof(uint32_t), alignof(uint32_t));
		for (ValueId other = 1; other < table->count; other++)
			insert_value_slot(new_slots, new_slot_count, table->hashes[other], other);
		table->slots = new_slots;
		table->slot_count = new_slot_count;
	} else {
		insert_value_slot(table->slots, table->slot_count, hash, id);
	}
	return id;
}

const Value *
value_get(const ValueTable *table, ValueId id)
{
	return &table->values[id];
}

static Value
make_value(ValueKind kind, TypeId type)
{
	Value value;
	memset(&value, 0, sizeof(value));
	value.kind = kind;
	value.type = type;
	return value;
}

ValueId
value_void(ValueTable *table, TypeId void_type)
{
	Value value = make_value(VALUE_VOID, void_type);
	return value_intern(table, &value);
}

ValueId
value_bool(ValueTable *table, TypeId bool_type, bool boolean)
{
	Value value = make_value(VALUE_BOOL, bool_type);
	value.as.boolean = boolean;
	return value_intern(table, &value);
}

ValueId
value_type(ValueTable *table, TypeId type_type, TypeId type)
{
	Value value = make_value(VALUE_TYPE, type_type);
	value.as.type_value = type;
	return value_intern(table, &value);
}

ValueId
value_int(ValueTable *table, TypeId type, const BigInt *integer)
{
	Value value = make_value(VALUE_INT, type);
	value.as.integer = *integer;
	return value_intern(table, &value);
}

ValueId
value_int_u64(ValueTable *table, TypeId type, uint64_t integer)
{
	uint32_t limbs[2];
	limbs[0] = (uint32_t)integer;
	limbs[1] = (uint32_t)(integer >> 32);
	BigInt big;
	big.negative = false;
	big.limb_count = limbs[1] != 0 ? 2 : limbs[0] != 0 ? 1 : 0;
	big.limbs = limbs;
	return value_int(table, type, &big);
}

ValueId
value_slice(ValueTable *table, TypeId type, ValueId pointer, ValueId length)
{
	Value value = make_value(VALUE_SLICE, type);
	value.as.slice.pointer = pointer;
	value.as.slice.length = length;
	return value_intern(table, &value);
}

ValueId
value_array(ValueTable *table, TypeId type, const ValueId *items, uint32_t count)
{
	Value value = make_value(VALUE_ARRAY, type);
	value.as.array.items = items;
	value.as.array.count = count;
	return value_intern(table, &value);
}

ValueId
value_undefined(ValueTable *table, TypeId type)
{
	Value value = make_value(VALUE_UNDEFINED, type);
	return value_intern(table, &value);
}

ValueId
value_function(ValueTable *table, TypeId fn_type, FuncId function)
{
	Value value = make_value(VALUE_FUNCTION, fn_type);
	value.as.function = function;
	return value_intern(table, &value);
}

static uint32_t
object_hash(ValueId value, uint32_t identity, uint32_t alignment)
{
	return hash_mix(hash_mix(hash_mix(2166136261u, value), identity), alignment);
}

static void
insert_object_slot(uint32_t *slots, uint32_t count, const ConstObject *object, ConstObjectId id)
{
	uint32_t mask = count - 1;
	uint32_t index = object_hash(object->value, object->identity, object->alignment) & mask;
	while (slots[index] != 0)
		index = (index + 1) & mask;
	slots[index] = id;
}

static ConstObjectId
intern_object(ValueTable *table, ValueId value, uint32_t identity, uint32_t alignment)
{
	if (table->object_count == 0) {
		table->object_capacity = 64;
		table->object_count = 1;
		table->objects = ARENA_ARRAY(table->arena, ConstObject, table->object_capacity);
		table->object_slot_count = 128;
		table->object_slots = ARENA_ARRAY(table->arena, uint32_t, table->object_slot_count);
	}
	uint32_t mask = table->object_slot_count - 1;
	for (uint32_t index = object_hash(value, identity, alignment) & mask; table->object_slots[index] != 0; index = (index + 1) & mask) {
		ConstObjectId id = table->object_slots[index];
		const ConstObject *object = &table->objects[id];
		if (object->value == value && object->identity == identity && object->alignment == alignment)
			return id;
	}
	if (table->object_count == table->object_capacity) {
		uint32_t capacity = table->object_capacity * 2;
		table->objects = arena_grow_array(table->arena, table->objects, table->object_count, capacity, sizeof(ConstObject), alignof(ConstObject));
		table->object_capacity = capacity;
	}
	ConstObjectId id = table->object_count++;
	table->objects[id].value = value;
	table->objects[id].identity = identity;
	table->objects[id].alignment = alignment;
	if (table->object_count * 2 > table->object_slot_count) {
		uint32_t count = table->object_slot_count * 2;
		uint32_t *slots = ARENA_ARRAY(table->arena, uint32_t, count);
		for (ConstObjectId other = 1; other < table->object_count; other++)
			insert_object_slot(slots, count, &table->objects[other], other);
		table->object_slots = slots;
		table->object_slot_count = count;
	} else {
		insert_object_slot(table->object_slots, table->object_slot_count, &table->objects[id], id);
	}
	return id;
}

ValueId
value_global(ValueTable *table, TypeId type, GlobalId global)
{
	Value value = make_value(VALUE_GLOBAL, type);
	value.as.global = global;
	return value_intern(table, &value);
}

ValueId
value_ref(ValueTable *table, TypeId type, ValueId pointee, uint32_t identity, uint32_t alignment)
{
	Value value = make_value(VALUE_REF, type);
	value.as.object = intern_object(table, pointee, identity, alignment);
	return value_intern(table, &value);
}

ValueId
value_int_pointer(ValueTable *table, TypeId type, uint64_t address)
{
	Value value = make_value(VALUE_INT_PTR, type);
	value.as.address = address;
	return value_intern(table, &value);
}

ValueId
value_element_pointer(ValueTable *table, TypeId type, ValueId base, uint64_t index, uint64_t offset, bool logical)
{
	if (value_get(table, base)->kind == VALUE_INT_PTR)
		return value_int_pointer(table, type, offset);
	Value value = make_value(VALUE_ELEMENT_PTR, type);
	value.as.element = (ElementPointer){base, index, offset, logical};
	return value_intern(table, &value);
}

ValueId
value_slice_field_pointer(ValueTable *table, TypeId type, ValueId base, uint64_t offset, bool length)
{
	if (value_get(table, base)->kind == VALUE_INT_PTR)
		return value_int_pointer(table, type, offset);
	Value value = make_value(VALUE_SLICE_FIELD_PTR, type);
	value.as.field = (SliceFieldPointer){base, offset, length};
	return value_intern(table, &value);
}

ValueId
value_pointer_cast(ValueTable *table, TypeId type, ValueId pointer)
{
	if (value_get(table, pointer)->kind == VALUE_INT_PTR)
		return value_int_pointer(table, type, value_get(table, pointer)->as.address);
	if (value_get(table, pointer)->kind == VALUE_PTR_CAST)
		pointer = value_get(table, pointer)->as.pointer;
	Value value = make_value(VALUE_PTR_CAST, type);
	value.as.pointer = pointer;
	return value_intern(table, &value);
}

/* Qualifier coercions retain the path; address identity ignores pointer types. */
PointerAddress
value_pointer_address(const ValueTable *table, ValueId id)
{
	PointerAddress address = {id, 0, false};
	bool selected = false;
	const Value *value = value_get(table, id);
	while (value->kind == VALUE_ELEMENT_PTR || value->kind == VALUE_PTR_CAST || value->kind == VALUE_SLICE_FIELD_PTR) {
		if (value->kind == VALUE_ELEMENT_PTR) {
			if (!selected) {
				address.offset = value->as.element.offset;
				address.logical = value->as.element.logical;
				selected = true;
			}
			address.root = value->as.element.base;
		} else if (value->kind == VALUE_SLICE_FIELD_PTR) {
			if (!selected) {
				address.offset = value->as.field.offset;
				selected = true;
			}
			address.root = value->as.field.base;
		} else {
			address.root = value->as.pointer;
		}
		value = value_get(table, address.root);
	}
	if (value->kind == VALUE_INT_PTR)
		address.offset += value->as.address;
	return address;
}

ValueId
value_bytes(ValueTable *table, TypeId type, const uint8_t *data, uint32_t length)
{
	Value value = make_value(VALUE_BYTES, type);
	value.as.bytes.data = data;
	value.as.bytes.length = length;
	return value_intern(table, &value);
}
