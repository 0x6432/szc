#ifndef SZC_COMPTIME_H
#define SZC_COMPTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "arena.h"

/*
 * Comptime values and arithmetic (comptime.c). Values are structurally interned:
 * equal representations have equal `ValueId`s. Pointer address equivalence instead
 * compares roots and offsets; distinct selection paths may denote the same address.
 */

typedef uint32_t TypeId;
typedef uint32_t ValueId;
typedef uint32_t FuncId;
typedef uint32_t GlobalId;
typedef uint32_t ConstObjectId;

#define VALUE_NONE ((ValueId)0)

/* Arbitrary-precision integer: sign + magnitude in 32-bit limbs (least significant first). */
typedef struct BigInt {
	bool     negative;	/* never set for zero */
	uint32_t limb_count;	/* 0 for zero; no leading zero limbs */
	uint32_t *limbs;
} BigInt;

typedef enum ValueKind {
	VALUE_VOID,
	VALUE_BOOL,
	VALUE_INT,	/* integer of any integer type, including comptime_int */
	VALUE_TYPE,
	VALUE_FUNCTION,
	VALUE_GLOBAL,	/* stable declaration address; contents resolve separately */
	VALUE_REF,	/* immutable address of an interned value */
	VALUE_INT_PTR,	/* absolute address; not compiler-owned memory */
	VALUE_PTR_CAST,	/* pointer qualification preserving the original byte backing */
	VALUE_ELEMENT_PTR,	/* subobject path into real backing, never a copied element */
	VALUE_SLICE_FIELD_PTR,	/* address of one descriptor word in original backing */
	VALUE_SLICE,	/* pointer/usize length, not the pointee contents */
	VALUE_ARRAY,	/* immutable aggregate of interned element IDs */
	VALUE_BYTES,	/* immutable bytes; length excludes the zero sentinel */
	VALUE_UNDEFINED,	/* `undefined` of any type */
} ValueKind;

typedef struct ByteString {
	const uint8_t *data;
	uint32_t length;
} ByteString;

typedef struct ArrayValue {
	const ValueId *items;
	uint32_t count;
} ArrayValue;

typedef struct SliceValue {
	ValueId pointer;
	ValueId length;
} SliceValue;

typedef struct ElementPointer {
	ValueId base;
	uint64_t index;
	uint64_t offset;	/* cumulative bytes, or leaf indices for comptime-only children */
	bool logical;
} ElementPointer;

typedef struct SliceFieldPointer {
	ValueId base;
	uint64_t offset;	/* cumulative bytes from the original root */
	bool length;
} SliceFieldPointer;

typedef struct PointerAddress {
	ValueId root;
	uint64_t offset;
	bool logical;
} PointerAddress;

typedef struct Value {
	ValueKind kind;
	TypeId    type;
	union {
		bool   boolean;
		BigInt integer;
		TypeId type_value;
		FuncId function;
		GlobalId global;
		ByteString bytes;
		ArrayValue array;
		SliceValue slice;
		ConstObjectId object;
		ElementPointer element;
		SliceFieldPointer field;
		ValueId pointer;
		uint64_t address;
	} as;
} Value;

/* Identity 0 denotes a deduplicated value address; declarations use unique identities. */
typedef struct ConstObject {
	ValueId value;
	uint32_t identity;
	uint32_t alignment;	/* 0: natural ABI alignment; otherwise byte units */
} ConstObject;

typedef struct ValueTable {
	Arena    *arena;
	Value    *values;	/* values[0] is unused (VALUE_NONE) */
	uint32_t *hashes;
	uint32_t count;
	uint32_t capacity;
	uint32_t *slots;	/* open addressing; 0 = empty */
	uint32_t slot_count;
	ConstObject *objects;	/* objects[0] is unused */
	uint32_t object_count;
	uint32_t object_capacity;
	uint32_t *object_slots;
	uint32_t object_slot_count;
} ValueTable;

void         value_table_init(ValueTable *table, Arena *arena);
ValueId      value_intern(ValueTable *table, const Value *value);	/* copies limbs */
const Value *value_get(const ValueTable *table, ValueId id);

ValueId value_global(ValueTable *table, TypeId type, GlobalId global);
ValueId value_ref(ValueTable *table, TypeId type, ValueId pointee, uint32_t identity, uint32_t alignment);
ValueId value_element_pointer(ValueTable *table, TypeId type, ValueId base, uint64_t index, uint64_t offset, bool logical);
ValueId value_slice_field_pointer(ValueTable *table, TypeId type, ValueId base, uint64_t offset, bool length);
ValueId value_int_pointer(ValueTable *table, TypeId type, uint64_t address);
ValueId value_pointer_cast(ValueTable *table, TypeId type, ValueId pointer);
PointerAddress value_pointer_address(const ValueTable *table, ValueId id);
ValueId value_slice(ValueTable *table, TypeId type, ValueId pointer, ValueId length);
ValueId value_array(ValueTable *table, TypeId type, const ValueId *items, uint32_t count);
ValueId value_bytes(ValueTable *table, TypeId type, const uint8_t *data, uint32_t length);
ValueId value_void(ValueTable *table, TypeId void_type);
ValueId value_bool(ValueTable *table, TypeId bool_type, bool value);
ValueId value_type(ValueTable *table, TypeId type_type, TypeId type);
ValueId value_int_u64(ValueTable *table, TypeId type, uint64_t value);
ValueId value_int(ValueTable *table, TypeId type, const BigInt *value);
ValueId value_undefined(ValueTable *table, TypeId type);
ValueId value_function(ValueTable *table, TypeId fn_type, FuncId function);

/* ---------- Big integers (results are allocated in `arena`) ---------- */

BigInt bigint_from_u64(Arena *arena, uint64_t value);
BigInt bigint_from_limbs(Arena *arena, const uint32_t *limbs, uint32_t limb_count);
BigInt bigint_add(Arena *arena, const BigInt *a, const BigInt *b);
BigInt bigint_sub(Arena *arena, const BigInt *a, const BigInt *b);
BigInt bigint_mul(Arena *arena, const BigInt *a, const BigInt *b);
BigInt bigint_negate(Arena *arena, const BigInt *a);
int    bigint_compare(const BigInt *a, const BigInt *b);
bool   bigint_is_zero(const BigInt *a);
/* True if the value fits in an integer with `bits` bits and the given signedness. */
bool   bigint_fits(const BigInt *a, bool is_signed, uint32_t bits);
/* Two's complement truncation to `bits` bits (wrapping arithmetic). */
BigInt bigint_wrap(Arena *arena, const BigInt *a, bool is_signed, uint32_t bits);
BigInt bigint_shl(Arena *arena, const BigInt *a, uint32_t shift);
BigInt bigint_shr(Arena *arena, const BigInt *a, uint32_t shift);	/* floor(a / 2^shift) */
bool   bigint_divmod_trunc(Arena *arena, const BigInt *a, const BigInt *b, BigInt *quotient, BigInt *remainder);

typedef enum BigIntDivRound {
	BIGINT_DIV_TRUNC,
	BIGINT_DIV_FLOOR,
	BIGINT_DIV_CEIL,
} BigIntDivRound;

/* Rounded quotient/remainder; distinct outputs may alias inputs.
 * A zero divisor leaves both outputs untouched and returns false. */
bool bigint_divmod(Arena *arena, const BigInt *a, const BigInt *b, BigIntDivRound round, BigInt *quotient, BigInt *remainder);

typedef enum BigIntBitOp {
	BIGINT_AND,
	BIGINT_OR,
	BIGINT_XOR,
} BigIntBitOp;

BigInt bigint_bitop(Arena *arena, const BigInt *a, const BigInt *b, BigIntBitOp op);
BigInt bigint_not(Arena *arena, const BigInt *a);
/* True if 0 <= a < 2^32 (then stores it). */
bool   bigint_to_u32(const BigInt *a, uint32_t *out);
/* Low 64 bits of the two's complement representation. */
uint64_t bigint_to_u64_bits(const BigInt *a);
/* Decimal text (for diagnostics and dumps). */
char  *bigint_to_string(Arena *arena, const BigInt *a);

/* Logical packed representations, independent of ABI padding and relocations. */
typedef struct PackedBits {
	uint32_t *words;
	uint32_t bit_count;
} PackedBits;

PackedBits packed_bits_init(Arena *arena, uint32_t bit_count);
/* Read/write exactly width bits; invalid bounds leave the destination untouched. */
bool packed_bits_write(Arena *arena, PackedBits *bits, uint32_t offset, uint32_t width, const BigInt *value);
bool packed_bits_read(Arena *arena, const PackedBits *bits, uint32_t offset, uint32_t width, bool is_signed, BigInt *out);

#endif
