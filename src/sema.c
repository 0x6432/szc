/*
 * sema: untyped IR -> typed IR.
 *
 * Analysis is lazy, like Zig's Sema: a declaration is analysed when it is first
 * referenced, a function body when the function is first referenced at
 * runtime. Each IR instruction of the body being analysed maps to an Operand:
 * either a comptime-known value or a runtime typed-IR instruction. Operations
 * whose operands are all comptime-known are evaluated (via comptime.c) instead
 * of emitted.
 */
#include "sema.h"

#include <inttypes.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Data structures ---------- */

typedef enum DeclState {
	DECL_UNRESOLVED,
	DECL_IN_PROGRESS,
	DECL_RESOLVED,
	DECL_FAILED,
} DeclState;

typedef enum OperandKind {
	OPERAND_NONE,	/* an error was reported; use as "poison" */
	OPERAND_COMPTIME,	/* value is known */
	OPERAND_RUNTIME,	/* inst is a typed-IR instruction */
	OPERAND_PENDING_CALL,	/* a call whose arguments are being analysed (type = callee's fn type) */
	OPERAND_NO_RESULT_TYPE,	/* arg_type of a variadic argument: no coercion */
} OperandKind;

typedef struct Operand {
	OperandKind kind;
	TypeId      type;
	ValueId     value;
	TirRef      inst;
} Operand;

typedef struct SemaDecl {
	IrDecl    ir;
	DeclState state;
	Operand   value;
	uint32_t address_identity;
} SemaDecl;

struct SemaNamespace {
	SemaNamespace *parent;
	const IrFile  *ir;
	FileId        file;
	IrInstIndex   inst;
	TypeId        type;
	SemaDecl      *decls;
	uint32_t      decl_count;
};

struct Sema {
	Arena      *arena;
	Diag       *diag;
	Interner   *interner;
	SourceTable *sources;

	Type       *types;
	uint32_t   *type_hashes;
	uint32_t   type_count;
	uint32_t   type_capacity;
	uint32_t   *type_slots;
	uint32_t   type_slot_count;

	ValueTable values;

	SemaFunc   **funcs;	/* funcs[0] is unused; each SemaFunc stays at its address */
	uint32_t   func_count;
	uint32_t   func_capacity;
	FuncId     *queue;
	uint32_t   queue_count;
	uint32_t   queue_capacity;
	FuncId     main_func;
	uint32_t next_address_identity;
};

/* Maps IR instruction indices to operands while a body is analysed. */
typedef struct InstMapEntry {
	IrInstIndex key;	/* UINT32_MAX = empty */
	Operand     operand;
} InstMapEntry;

typedef struct InstMap {
	const struct InstMap *parent;	/* captured operands outside a reusable body */
	InstMapEntry *entries;
	uint32_t     count;
	uint32_t     capacity;	/* power of two */
} InstMap;

typedef struct BreakExit {
	Operand value;
	TirRef wrapper;	/* deferred noreturn body: coercion followed by br */
	uint32_t src;
} BreakExit;

typedef struct ControlTarget {
	struct ControlTarget *parent;
	IrInstIndex ir_index;
	TirRef tir_ref;
	bool is_loop;
	BreakExit *exits;
	uint32_t count;
	uint32_t capacity;
} ControlTarget;

typedef struct Block {
	Sema          *sema;
	SemaNamespace *namespace_scope;
	const IrFile  *ir;
	InstMap       *map;
	bool          is_comptime;
	StringId      decl_name;	/* declaration whose value is analysed (names structs) */
	SemaFunc      *func;	/* function whose body is analysed, or NULL */
	TirFunc       *tir;	/* where runtime instructions go, or NULL */
	TirRef        *insts;	/* typed-IR body being built */
	uint32_t      count;
	uint32_t      capacity;
	ControlTarget *control;
} Block;

/* How the analysis of a body ended. */
typedef enum BodyEnd {
	BODY_FALLTHROUGH,	/* no terminator (cannot happen for well-formed IR) */
	BODY_BREAK_RESULT,	/* break_inline result: result holds the value */
	BODY_NORETURN,	/* ret, unreachable, ... */
} BodyEnd;

typedef struct BodyResult {
	BodyEnd end;
	Operand result;
} BodyResult;

static const Operand operand_none = {OPERAND_NONE, 0, VALUE_NONE, 0};

/* ---------- Diagnostics ---------- */

static void
sema_error(Block *block, uint32_t src, const char *format, ...)
{
	char message[512];
	va_list args;
	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	diag_report(block->sema->diag, DIAG_ERROR, block->ir->file, src, "%s", message);
}

/* ---------- Types ---------- */

static uint32_t
hash_word(uint32_t hash, uint32_t word)
{
	hash = hash ^ word;
	return hash * 16777619u;
}

static uint32_t
type_hash(const Type *type)
{
	uint32_t hash = hash_word(2166136261u, (uint32_t)type->kind);
	switch (type->kind) {
	case TYPE_INT:
		hash = hash_word(hash, type->as.integer.is_signed ? 1u : 0u);
		hash = hash_word(hash, type->as.integer.bits);
		break;
	case TYPE_FLOAT:
		hash = hash_word(hash, type->as.floating.bits);
		break;
	case TYPE_ARRAY:
		hash = hash_word(hash, type->as.array.child);
		hash = hash_word(hash, type->as.array.length);
		hash = hash_word(hash, type->as.array.sentinel_zero ? 1u : 0u);
		break;
	case TYPE_POINTER:
		hash = hash_word(hash, type->as.pointer.child);
		hash = hash_word(hash, (uint32_t)type->as.pointer.size);
		hash = hash_word(hash, type->as.pointer.is_const ? 1u : 0u);
		hash = hash_word(hash, type->as.pointer.is_volatile ? 1u : 0u);
		break;
	case TYPE_FN:
		hash = hash_word(hash, type->as.fn.ret);
		hash = hash_word(hash, type->as.fn.is_var_args ? 1u : 0u);
		hash = hash_word(hash, type->as.fn.is_generic ? 1u : 0u);
		for (uint32_t i = 0; i < type->as.fn.param_count; i++)
			hash = hash_word(hash, type->as.fn.params[i]);
		break;
	default:
		break;
	}
	return hash;
}

static bool
type_equal(const Type *a, const Type *b)
{
	if (a->kind != b->kind)
		return false;
	switch (a->kind) {
	case TYPE_INT:
		return a->as.integer.is_signed == b->as.integer.is_signed && a->as.integer.bits == b->as.integer.bits;
	case TYPE_FLOAT:
		return a->as.floating.bits == b->as.floating.bits;
	case TYPE_ARRAY:
		return a->as.array.child == b->as.array.child && a->as.array.length == b->as.array.length && a->as.array.sentinel_zero == b->as.array.sentinel_zero;
	case TYPE_POINTER:
		return a->as.pointer.child == b->as.pointer.child && a->as.pointer.size == b->as.pointer.size && a->as.pointer.is_const == b->as.pointer.is_const && a->as.pointer.is_volatile == b->as.pointer.is_volatile;
	case TYPE_FN:
		if (a->as.fn.ret != b->as.fn.ret || a->as.fn.is_var_args != b->as.fn.is_var_args || a->as.fn.is_generic != b->as.fn.is_generic || a->as.fn.param_count != b->as.fn.param_count)
			return false;
		for (uint32_t i = 0; i < a->as.fn.param_count; i++) {
			if (a->as.fn.params[i] != b->as.fn.params[i])
				return false;
		}
		return true;
	case TYPE_STRUCT:
		return false;	/* nominal: never interned */
	default:
		return true;
	}
}

static TypeId
type_append(Sema *sema, const Type *type, uint32_t hash)
{
	if (sema->type_count == sema->type_capacity) {
		uint32_t new_capacity = sema->type_capacity * 2;
		sema->types = arena_grow_array(sema->arena, sema->types, sema->type_count, new_capacity, sizeof(Type), alignof(Type));
		sema->type_hashes = arena_grow_array(sema->arena, sema->type_hashes, sema->type_count, new_capacity, sizeof(uint32_t), alignof(uint32_t));
		sema->type_capacity = new_capacity;
	}
	TypeId id = sema->type_count;
	sema->types[id] = *type;
	sema->type_hashes[id] = hash;
	sema->type_count = sema->type_count + 1;
	return id;
}

static void
type_slot_insert(uint32_t *slots, uint32_t slot_count, uint32_t hash, TypeId id)
{
	uint32_t mask = slot_count - 1;
	uint32_t index = hash & mask;
	while (slots[index] != 0)
		index = (index + 1) & mask;
	slots[index] = id;
}

/* Returns the id of a structural type, creating it if needed. */
static TypeId
type_intern(Sema *sema, const Type *type)
{
	uint32_t hash = type_hash(type);
	uint32_t mask = sema->type_slot_count - 1;
	for (uint32_t index = hash & mask; sema->type_slots[index] != 0; index = (index + 1) & mask) {
		TypeId id = sema->type_slots[index];
		if (sema->type_hashes[id] == hash && type_equal(&sema->types[id], type))
			return id;
	}
	Type copy = *type;
	if (copy.kind == TYPE_FN && copy.as.fn.param_count > 0) {
		TypeId *params = arena_alloc(sema->arena, copy.as.fn.param_count * sizeof(TypeId), alignof(TypeId));
		memcpy(params, type->as.fn.params, copy.as.fn.param_count * sizeof(TypeId));
		copy.as.fn.params = params;
	}
	TypeId id = type_append(sema, &copy, hash);
	if (sema->type_count * 2 > sema->type_slot_count) {
		uint32_t new_slot_count = sema->type_slot_count * 2;
		uint32_t *new_slots = arena_zalloc(sema->arena, new_slot_count * sizeof(uint32_t), alignof(uint32_t));
		for (TypeId other = 1; other < sema->type_count; other++) {
			if (sema->types[other].kind != TYPE_STRUCT)
				type_slot_insert(new_slots, new_slot_count, sema->type_hashes[other], other);
		}
		sema->type_slots = new_slots;
		sema->type_slot_count = new_slot_count;
	} else {
		type_slot_insert(sema->type_slots, sema->type_slot_count, hash, id);
	}
	return id;
}

static Type
make_type(TypeKind kind)
{
	Type type;
	memset(&type, 0, sizeof(type));
	type.kind = kind;
	return type;
}

static TypeId
type_int(Sema *sema, bool is_signed, uint32_t bits)
{
	Type type = make_type(TYPE_INT);
	type.as.integer.is_signed = is_signed;
	type.as.integer.bits = (uint16_t)bits;
	return type_intern(sema, &type);
}

static TypeId
type_pointer(Sema *sema, TypeId child, PointerSize size, bool is_const)
{
	Type type = make_type(TYPE_POINTER);
	type.as.pointer.child = child;
	type.as.pointer.size = size;
	type.as.pointer.is_const = is_const;
	return type_intern(sema, &type);
}

const Type *
sema_type(const Sema *sema, TypeId type)
{
	return &sema->types[type];
}

TypeId
sema_void_type(const Sema *sema)
{
	(void)sema;
	return type_id_of_well_known(IR_REF_TYPE_VOID);
}

#define T_VOID type_id_of_well_known(IR_REF_TYPE_VOID)
#define T_BOOL type_id_of_well_known(IR_REF_TYPE_BOOL)
#define T_TYPE type_id_of_well_known(IR_REF_TYPE_TYPE)
#define T_NORETURN type_id_of_well_known(IR_REF_TYPE_NORETURN)
#define T_COMPTIME_INT type_id_of_well_known(IR_REF_TYPE_COMPTIME_INT)
#define T_USIZE type_id_of_well_known(IR_REF_TYPE_USIZE)
#define T_UNDEFINED type_id_of_well_known(IR_REF_TYPE_UNDEFINED)

/* Creates the builtin types at the ids of their well-known IR refs. */
static void
init_builtin_types(Sema *sema)
{
	for (IrRef ref = 0; ref < IR_REF_FIRST_INST; ref++) {
		Type type = make_type(TYPE_VOID);
		bool is_type = true;
		switch ((IrWellKnown)ref) {
#define INT_TYPE(name, signed_, bits_) \
	case IR_REF_##name: \
		type.kind = TYPE_INT; \
		type.as.integer.is_signed = signed_; \
		type.as.integer.bits = bits_; \
		break;
		INT_TYPE(TYPE_U1, false, 1)
		INT_TYPE(TYPE_U8, false, 8)
		INT_TYPE(TYPE_I8, true, 8)
		INT_TYPE(TYPE_U16, false, 16)
		INT_TYPE(TYPE_I16, true, 16)
		INT_TYPE(TYPE_U29, false, 29)
		INT_TYPE(TYPE_U32, false, 32)
		INT_TYPE(TYPE_I32, true, 32)
		INT_TYPE(TYPE_U64, false, 64)
		INT_TYPE(TYPE_I64, true, 64)
		INT_TYPE(TYPE_U128, false, 128)
		INT_TYPE(TYPE_I128, true, 128)
		INT_TYPE(TYPE_USIZE, false, 64)
		INT_TYPE(TYPE_ISIZE, true, 64)
		INT_TYPE(TYPE_C_CHAR, true, 8)
		INT_TYPE(TYPE_C_SHORT, true, 16)
		INT_TYPE(TYPE_C_USHORT, false, 16)
		INT_TYPE(TYPE_C_INT, true, 32)
		INT_TYPE(TYPE_C_UINT, false, 32)
		INT_TYPE(TYPE_C_LONG, true, 64)
		INT_TYPE(TYPE_C_ULONG, false, 64)
		INT_TYPE(TYPE_C_LONGLONG, true, 64)
		INT_TYPE(TYPE_C_ULONGLONG, false, 64)
#undef INT_TYPE
		case IR_REF_TYPE_C_LONGDOUBLE:
			type.kind = TYPE_FLOAT;
			type.as.floating.bits = 80;
			break;
		case IR_REF_TYPE_F16:
			type.kind = TYPE_FLOAT;
			type.as.floating.bits = 16;
			break;
		case IR_REF_TYPE_F32:
			type.kind = TYPE_FLOAT;
			type.as.floating.bits = 32;
			break;
		case IR_REF_TYPE_F64:
			type.kind = TYPE_FLOAT;
			type.as.floating.bits = 64;
			break;
		case IR_REF_TYPE_F80:
			type.kind = TYPE_FLOAT;
			type.as.floating.bits = 80;
			break;
		case IR_REF_TYPE_F128:
			type.kind = TYPE_FLOAT;
			type.as.floating.bits = 128;
			break;
		case IR_REF_TYPE_ANYOPAQUE:
			type.kind = TYPE_ANYOPAQUE;
			break;
		case IR_REF_TYPE_BOOL:
			type.kind = TYPE_BOOL;
			break;
		case IR_REF_TYPE_VOID:
			type.kind = TYPE_VOID;
			break;
		case IR_REF_TYPE_TYPE:
			type.kind = TYPE_TYPE;
			break;
		case IR_REF_TYPE_ANYERROR:
			type.kind = TYPE_ANYERROR;
			break;
		case IR_REF_TYPE_COMPTIME_INT:
			type.kind = TYPE_COMPTIME_INT;
			break;
		case IR_REF_TYPE_COMPTIME_FLOAT:
			type.kind = TYPE_COMPTIME_FLOAT;
			break;
		case IR_REF_TYPE_NORETURN:
			type.kind = TYPE_NORETURN;
			break;
		case IR_REF_TYPE_NULL:
			type.kind = TYPE_NULL;
			break;
		case IR_REF_TYPE_UNDEFINED:
			type.kind = TYPE_UNDEFINED;
			break;
		case IR_REF_TYPE_ENUM_LITERAL:
			type.kind = TYPE_ENUM_LITERAL;
			break;
		default:
			is_type = false;
			break;
		}
		/* Value refs get a placeholder slot so ids stay aligned with refs. */
		uint32_t hash = type_hash(&type);
		TypeId id = type_append(sema, &type, hash);
		if (is_type && type.kind != TYPE_INT)
			type_slot_insert(sema->type_slots, sema->type_slot_count, hash, id);
	}
	/* Canonical integer types are the first ones of each signedness/width; register them. */
	for (IrRef ref = IR_REF_TYPE_U1; ref <= IR_REF_TYPE_I128; ref++)
		type_slot_insert(sema->type_slots, sema->type_slot_count, sema->type_hashes[ref], ref);
}

static bool
is_int(const Sema *sema, TypeId type)
{
	return sema->types[type].kind == TYPE_INT;
}

static bool
is_comptime_only_type(const Sema *sema, TypeId type)
{
	switch (sema->types[type].kind) {
	case TYPE_TYPE:
	case TYPE_COMPTIME_INT:
	case TYPE_COMPTIME_FLOAT:
	case TYPE_NULL:
	case TYPE_UNDEFINED:
	case TYPE_ENUM_LITERAL:
		return true;
	default:
		return false;
	}
}

const char *
sema_type_name(Sema *sema, TypeId type_id)
{
	const Type *type = &sema->types[type_id];
	switch (type->kind) {
	case TYPE_VOID:
		return "void";
	case TYPE_BOOL:
		return "bool";
	case TYPE_NORETURN:
		return "noreturn";
	case TYPE_TYPE:
		return "type";
	case TYPE_COMPTIME_INT:
		return "comptime_int";
	case TYPE_COMPTIME_FLOAT:
		return "comptime_float";
	case TYPE_INT:
		if (type_id == type_id_of_well_known(IR_REF_TYPE_USIZE))
			return "usize";
		if (type_id == type_id_of_well_known(IR_REF_TYPE_ISIZE))
			return "isize";
		if (type_id == type_id_of_well_known(IR_REF_TYPE_C_INT))
			return "c_int";
		return arena_sprintf(sema->arena, "%c%u", type->as.integer.is_signed ? 'i' : 'u', (unsigned)type->as.integer.bits);
	case TYPE_FLOAT:
		return arena_sprintf(sema->arena, "f%u", (unsigned)type->as.floating.bits);
	case TYPE_ARRAY:
		return arena_sprintf(sema->arena, "[%u%s]%s", type->as.array.length, type->as.array.sentinel_zero ? ":0" : "", sema_type_name(sema, type->as.array.child));
	case TYPE_POINTER: {
		const char *prefix = type->as.pointer.size == POINTER_ONE ? "*" : type->as.pointer.size == POINTER_MANY ? "[*]" : type->as.pointer.size == POINTER_SLICE ? "[]" : "[*c]";
		return arena_sprintf(sema->arena, "%s%s%s", prefix, type->as.pointer.is_const ? "const " : "", sema_type_name(sema, type->as.pointer.child));
	}
	case TYPE_FN: {
		const char *text = "fn (";
		for (uint32_t i = 0; i < type->as.fn.param_count; i++)
			text = arena_sprintf(sema->arena, "%s%s%s", text, i == 0 ? "" : ", ", sema_type_name(sema, type->as.fn.params[i]));
		return arena_sprintf(sema->arena, "%s%s) %s", text, type->as.fn.is_var_args ? ", ..." : "", sema_type_name(sema, type->as.fn.ret));
	}
	case TYPE_STRUCT:
		return type->name != STRING_NONE ? intern_text(sema->interner, type->name) : "struct";
	case TYPE_ANYOPAQUE:
		return "anyopaque";
	case TYPE_ANYERROR:
		return "anyerror";
	case TYPE_NULL:
		return "@TypeOf(null)";
	case TYPE_UNDEFINED:
		return "@TypeOf(undefined)";
	case TYPE_ENUM_LITERAL:
		return "@TypeOf(.enum_literal)";
	}
	return "?";
}

uint64_t
sema_type_size(const Sema *sema, TypeId type_id)
{
	const Type *type = &sema->types[type_id];
	switch (type->kind) {
	case TYPE_BOOL:
		return 1;
	case TYPE_INT: {
		uint32_t bits = type->as.integer.bits;
		if (bits == 0)
			return 0;
		uint64_t bytes = 1;
		while (bytes * 8 < bits)
			bytes = bytes * 2;
		return bytes;
	}
	case TYPE_ARRAY:
		return ((uint64_t)type->as.array.length + (type->as.array.sentinel_zero ? 1u : 0u)) * sema_type_size(sema, type->as.array.child);
	case TYPE_FLOAT:
		return type->as.floating.bits / 8;
	case TYPE_POINTER:
		return type->as.pointer.size == POINTER_SLICE ? 16 : 8;
	default:
		return 0;
	}
}

uint32_t
sema_type_align(const Sema *sema, TypeId type_id)
{
	if (sema->types[type_id].kind == TYPE_ARRAY)
		return sema_type_align(sema, sema->types[type_id].as.array.child);
	uint64_t size = sema_type_size(sema, type_id);
	if (size == 0)
		return 1;
	if (size > 16)
		return 16;
	if (sema->types[type_id].kind == TYPE_POINTER)
		return 8;
	return (uint32_t)size;
}

/* ---------- Values and operands ---------- */

static Operand
comptime_operand(TypeId type, ValueId value)
{
	Operand operand;
	operand.kind = OPERAND_COMPTIME;
	operand.type = type;
	operand.value = value;
	operand.inst = 0;
	return operand;
}

static Operand
runtime_operand(TypeId type, TirRef inst)
{
	Operand operand;
	operand.kind = OPERAND_RUNTIME;
	operand.type = type;
	operand.value = VALUE_NONE;
	operand.inst = inst;
	return operand;
}

static Operand
type_operand(Sema *sema, TypeId type)
{
	return comptime_operand(T_TYPE, value_type(&sema->values, T_TYPE, type));
}

static Operand
void_operand(Sema *sema)
{
	return comptime_operand(T_VOID, value_void(&sema->values, T_VOID));
}

static const Value *
operand_value(const Block *block, Operand operand)
{
	return value_get(&block->sema->values, operand.value);
}

/* ---------- Instruction map ---------- */

static void
inst_map_init(InstMap *map, Arena *arena)
{
	map->parent = NULL;
	map->capacity = 64;
	map->count = 0;
	map->entries = arena_alloc(arena, map->capacity * sizeof(InstMapEntry), alignof(InstMapEntry));
	for (uint32_t i = 0; i < map->capacity; i++)
		map->entries[i].key = UINT32_MAX;
}

static uint32_t
inst_hash(IrInstIndex key)
{
	return key * 2654435761u;
}

static void
inst_map_put(InstMap *map, Arena *arena, IrInstIndex key, Operand operand)
{
	if ((map->count + 1) * 2 > map->capacity) {
		InstMap bigger;
		bigger.parent = map->parent;
		bigger.capacity = map->capacity * 2;
		bigger.count = 0;
		bigger.entries = arena_alloc(arena, bigger.capacity * sizeof(InstMapEntry), alignof(InstMapEntry));
		for (uint32_t i = 0; i < bigger.capacity; i++)
			bigger.entries[i].key = UINT32_MAX;
		for (uint32_t i = 0; i < map->capacity; i++) {
			if (map->entries[i].key != UINT32_MAX)
				inst_map_put(&bigger, arena, map->entries[i].key, map->entries[i].operand);
		}
		*map = bigger;
	}
	uint32_t mask = map->capacity - 1;
	uint32_t index = inst_hash(key) & mask;
	while (map->entries[index].key != UINT32_MAX && map->entries[index].key != key)
		index = (index + 1) & mask;
	if (map->entries[index].key == UINT32_MAX)
		map->count = map->count + 1;
	map->entries[index].key = key;
	map->entries[index].operand = operand;
}

static bool
inst_map_get(const InstMap *map, IrInstIndex key, Operand *out)
{
	uint32_t mask = map->capacity - 1;
	for (uint32_t index = inst_hash(key) & mask; map->entries[index].key != UINT32_MAX; index = (index + 1) & mask) {
		if (map->entries[index].key == key) {
			*out = map->entries[index].operand;
			return true;
		}
	}
	return map->parent != NULL && inst_map_get(map->parent, key, out);
}

/* The operand of an IR ref in this block. */
static Operand
resolve_ref(Block *block, IrRef ref)
{
	Sema *sema = block->sema;
	if (!ir_ref_is_inst(ref)) {
		switch ((IrWellKnown)ref) {
		case IR_REF_NONE:
			return operand_none;
		case IR_REF_VOID_VALUE:
			return void_operand(sema);
		case IR_REF_TRUE:
			return comptime_operand(T_BOOL, value_bool(&sema->values, T_BOOL, true));
		case IR_REF_FALSE:
			return comptime_operand(T_BOOL, value_bool(&sema->values, T_BOOL, false));
		case IR_REF_UNDEFINED:
			return comptime_operand(T_UNDEFINED, value_undefined(&sema->values, T_UNDEFINED));
		case IR_REF_ZERO_USIZE:
			return comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, 0));
		case IR_REF_ONE_USIZE:
			return comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, 1));
		case IR_REF_NULL_VALUE: {
			TypeId null_type = type_id_of_well_known(IR_REF_TYPE_NULL);
			Value value;
			memset(&value, 0, sizeof(value));
			value.kind = VALUE_VOID;
			value.type = null_type;
			return comptime_operand(null_type, value_intern(&sema->values, &value));
		}
		default:
			return type_operand(sema, type_id_of_well_known(ref));
		}
	}
	Operand operand;
	if (inst_map_get(block->map, ir_ref_inst(ref), &operand))
		return operand;
	/* Not analysed: an earlier error, or an instruction from a body that is not in scope. */
	return operand_none;
}

/* ---------- Typed IR emission ---------- */

static TirRef
tir_new(TirFunc *tir, Arena *arena, TirInst inst)
{
	if (tir->inst_count == tir->inst_capacity) {
		uint32_t new_capacity = tir->inst_capacity == 0 ? 64 : tir->inst_capacity * 2;
		tir->insts = arena_grow_array(arena, tir->insts, tir->inst_count, new_capacity, sizeof(TirInst), alignof(TirInst));
		tir->inst_capacity = new_capacity;
	}
	TirRef ref = tir->inst_count;
	tir->insts[ref] = inst;
	tir->inst_count = tir->inst_count + 1;
	return ref;
}

static uint32_t
tir_extra(TirFunc *tir, Arena *arena, const void *data, uint32_t words)
{
	while (tir->extra_count + words > tir->extra_capacity) {
		uint32_t new_capacity = tir->extra_capacity == 0 ? 64 : tir->extra_capacity * 2;
		tir->extra = arena_grow_array(arena, tir->extra, tir->extra_count, new_capacity, sizeof(uint32_t), alignof(uint32_t));
		tir->extra_capacity = new_capacity;
	}
	if (tir->extra_count == 0) {
		/* extra index 0 means "no body" */
		tir->extra[0] = 0;
		tir->extra_count = 1;
		return tir_extra(tir, arena, data, words);
	}
	uint32_t index = tir->extra_count;
	memcpy(&tir->extra[index], data, words * sizeof(uint32_t));
	tir->extra_count = tir->extra_count + words;
	return index;
}

static void
block_push(Block *block, TirRef ref)
{
	if (block->count == block->capacity) {
		uint32_t new_capacity = block->capacity == 0 ? 16 : block->capacity * 2;
		block->insts = arena_grow_array(block->sema->arena, block->insts, block->count, new_capacity, sizeof(TirRef), alignof(TirRef));
		block->capacity = new_capacity;
	}
	block->insts[block->count] = ref;
	block->count = block->count + 1;
}

static TirRef
emit_tir(Block *block, TirOp op, TypeId type, uint32_t src)
{
	TirInst inst;
	memset(&inst, 0, sizeof(inst));
	inst.op = op;
	inst.type = type;
	inst.src = src;
	TirRef ref = tir_new(block->tir, block->sema->arena, inst);
	block_push(block, ref);
	return ref;
}

static TirRef
emit_tir_un(Block *block, TirOp op, TypeId type, uint32_t src, TirRef operand)
{
	TirRef ref = emit_tir(block, op, type, src);
	block->tir->insts[ref].data.un.operand = operand;
	return ref;
}

static TirRef
emit_tir_bin(Block *block, TirOp op, TypeId type, uint32_t src, TirRef lhs, TirRef rhs)
{
	TirRef ref = emit_tir(block, op, type, src);
	block->tir->insts[ref].data.bin.lhs = lhs;
	block->tir->insts[ref].data.bin.rhs = rhs;
	return ref;
}

static TirBody
block_finish(Block *block)
{
	TirFunc *tir = block->tir;
	uint32_t count = block->count;
	uint32_t index = tir_extra(tir, block->sema->arena, &count, 1);
	if (count > 0)
		tir_extra(tir, block->sema->arena, block->insts, count);
	return index;
}

/* Turns an operand into a typed-IR instruction (materializing constants). */
static TirRef
materialize(Block *block, Operand operand, uint32_t src)
{
	if (operand.kind == OPERAND_RUNTIME)
		return operand.inst;
	TirRef ref = emit_tir(block, TIR_CONST, operand.type, src);
	block->tir->insts[ref].data.value = operand.value;
	return ref;
}

static void
block_init_child(Block *child, const Block *parent)
{
	*child = *parent;
	child->insts = NULL;
	child->count = 0;
	child->capacity = 0;
}

/* ---------- Functions and namespaces ---------- */

static FuncId
func_create(Sema *sema)
{
	if (sema->func_count == sema->func_capacity) {
		uint32_t new_capacity = sema->func_capacity * 2;
		sema->funcs = arena_grow_array(sema->arena, sema->funcs, sema->func_count, new_capacity, sizeof(SemaFunc *), alignof(SemaFunc *));
		sema->func_capacity = new_capacity;
	}
	FuncId id = sema->func_count;
	sema->funcs[id] = ARENA_NEW(sema->arena, SemaFunc);
	sema->func_count = sema->func_count + 1;
	return id;
}

/* Marks a function as referenced at runtime: its body will be analysed. */
static void
func_queue(Sema *sema, FuncId id)
{
	SemaFunc *func = sema->funcs[id];
	if (func->state != FUNC_DECLARED)
		return;
	func->state = FUNC_QUEUED;
	if (func->is_extern)
		return;
	if (sema->queue_count == sema->queue_capacity) {
		uint32_t new_capacity = sema->queue_capacity * 2;
		sema->queue = arena_grow_array(sema->arena, sema->queue, sema->queue_count, new_capacity, sizeof(FuncId), alignof(FuncId));
		sema->queue_capacity = new_capacity;
	}
	sema->queue[sema->queue_count] = id;
	sema->queue_count = sema->queue_count + 1;
}

/* A symbol made of the declaration name (if it is a plain identifier) and the function id. */
static const char *
func_symbol(Sema *sema, StringId name, FuncId id)
{
	const char *text = name == STRING_NONE ? "" : intern_text(sema->interner, name);
	bool plain = text[0] != '\0';
	for (const char *c = text; *c != '\0'; c++) {
		bool ok = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_';
		plain = plain && ok;
	}
	return arena_sprintf(sema->arena, "szc.%s.%u", plain ? text : "fn", (unsigned)id);
}

static SemaNamespace *
namespace_create(Sema *sema, SemaNamespace *parent, const IrFile *ir, IrInstIndex inst, StringId name)
{
	SemaNamespace *ns = ARENA_NEW(sema->arena, SemaNamespace);
	ns->parent = parent;
	ns->ir = ir;
	ns->file = ir->file;
	ns->inst = inst;
	uint32_t payload = ir->insts[inst].data.payload;
	IrContainerHeader header;
	IR_READ(ir, payload, &header);
	uint32_t decls = payload + IR_WORDS(IrContainerHeader) + header.field_count * IR_WORDS(IrField);
	ns->decl_count = header.decl_count;
	ns->decls = ARENA_ARRAY(sema->arena, SemaDecl, header.decl_count + 1);
	for (uint32_t i = 0; i < header.decl_count; i++) {
		IR_READ(ir, decls + i * IR_WORDS(IrDecl), &ns->decls[i].ir);
		ns->decls[i].state = DECL_UNRESOLVED;
		ns->decls[i].value = operand_none;
	}
	Type type = make_type(TYPE_STRUCT);
	type.as.structure = ns;
	type.name = name;
	ns->type = type_append(sema, &type, type_hash(&type));
	return ns;
}

/* Finds a named const/var/fn declaration in `ns` (not in its parents). */
static SemaDecl *
namespace_find(SemaNamespace *ns, StringId name)
{
	for (uint32_t i = 0; i < ns->decl_count; i++) {
		SemaDecl *decl = &ns->decls[i];
		bool named = decl->ir.kind == IR_DECL_CONST || decl->ir.kind == IR_DECL_VAR || decl->ir.kind == IR_DECL_FN;
		if (named && decl->ir.name == name)
			return decl;
	}
	return NULL;
}

/* ---------- Analysis: forward declarations ---------- */

typedef enum InstOutcome {
	INST_CONTINUE,
	INST_BREAK,	/* break_inline out of the body: the value is the body's result */
	INST_NORETURN,	/* control does not reach the next instruction */
} InstOutcome;

static BodyResult analyze_body(Block *block, IrBody body);

static void
block_init_comptime(Block *block, Sema *sema, SemaNamespace *ns, InstMap *map, StringId decl_name)
{
	memset(block, 0, sizeof(*block));
	block->sema = sema;
	block->namespace_scope = ns;
	block->ir = ns->ir;
	block->map = map;
	block->is_comptime = true;
	block->decl_name = decl_name;
}

/* Analyses the value of a container-level declaration (once). */
static Operand
resolve_decl(Block *user, uint32_t src, SemaNamespace *ns, SemaDecl *decl)
{
	Sema *sema = user->sema;
	switch (decl->state) {
	case DECL_RESOLVED:
		return decl->value;
	case DECL_FAILED:
		return operand_none;
	case DECL_IN_PROGRESS:
		sema_error(user, src, "dependency loop detected");
		return operand_none;
	case DECL_UNRESOLVED:
		break;
	}
	if (decl->ir.kind == IR_DECL_VAR) {
		decl->state = DECL_FAILED;
		sema_error(user, src, "not supported by szc yet: container-level variables");
		return operand_none;
	}
	decl->state = DECL_IN_PROGRESS;
	InstMap map;
	inst_map_init(&map, sema->arena);
	Block block;
	block_init_comptime(&block, sema, ns, &map, decl->ir.name);
	BodyResult result = analyze_body(&block, decl->ir.value_body);
	if (result.end != BODY_BREAK_RESULT || result.result.kind != OPERAND_COMPTIME) {
		if (result.result.kind == OPERAND_RUNTIME)
			sema_error(&block, decl->ir.src, "unable to resolve comptime value");
		decl->state = DECL_FAILED;
		return operand_none;
	}
	decl->value = result.result;
	decl->state = DECL_RESOLVED;
	return decl->value;
}

/* ---------- Analysis: types and coercion ---------- */

/* zigTypeTag name, as used by some of Zig's messages. */
static const char *
type_tag_name(const Sema *sema, TypeId type)
{
	switch (sema->types[type].kind) {
	case TYPE_VOID:
		return "void";
	case TYPE_BOOL:
		return "bool";
	case TYPE_NORETURN:
		return "noreturn";
	case TYPE_TYPE:
		return "type";
	case TYPE_COMPTIME_INT:
		return "comptime_int";
	case TYPE_COMPTIME_FLOAT:
		return "comptime_float";
	case TYPE_INT:
		return "int";
	case TYPE_FLOAT:
		return "float";
	case TYPE_ARRAY:
		return "array";
	case TYPE_POINTER:
		return "pointer";
	case TYPE_FN:
		return "fn";
	case TYPE_STRUCT:
		return "struct";
	case TYPE_ANYOPAQUE:
		return "opaque";
	case TYPE_ANYERROR:
		return "error_set";
	case TYPE_NULL:
		return "null";
	case TYPE_UNDEFINED:
		return "undefined";
	case TYPE_ENUM_LITERAL:
		return "enum_literal";
	}
	return "?";
}

static bool
is_int_or_comptime_int(const Sema *sema, TypeId type)
{
	return sema->types[type].kind == TYPE_INT || sema->types[type].kind == TYPE_COMPTIME_INT;
}

static bool
is_signed_type(const Sema *sema, TypeId type)
{
	const Type *t = &sema->types[type];
	return t->kind == TYPE_COMPTIME_INT || (t->kind == TYPE_INT && t->as.integer.is_signed);
}

/* True if every value of integer type `from` is a value of integer type `to`. */
static bool
int_type_contains(const Sema *sema, TypeId to, TypeId from)
{
	const Type *a = &sema->types[to];
	const Type *b = &sema->types[from];
	if (a->as.integer.is_signed == b->as.integer.is_signed)
		return a->as.integer.bits >= b->as.integer.bits;
	if (a->as.integer.is_signed)
		return a->as.integer.bits > b->as.integer.bits;
	return b->as.integer.bits == 0;
}

static bool
operand_is_undefined(const Block *block, Operand operand)
{
	return operand.kind == OPERAND_COMPTIME && operand_value(block, operand)->kind == VALUE_UNDEFINED;
}

/* Gets the type that a `type`-typed operand denotes. */
static bool
operand_to_type(Block *block, Operand operand, uint32_t src, TypeId *out)
{
	Sema *sema = block->sema;
	if (operand.kind == OPERAND_NONE)
		return false;
	if (operand.kind != OPERAND_COMPTIME || operand.type != T_TYPE) {
		sema_error(block, src, "expected type 'type', found '%s'", sema_type_name(sema, operand.type));
		return false;
	}
	*out = operand_value(block, operand)->as.type_value;
	return true;
}

/* A mutable outer pointee requires all inner pointer qualifiers to be invariant. */
static bool
pointer_coercion_allowed(const Sema *sema, TypeId dest, TypeId source, bool dest_is_mut)
{
	if (dest == source)
		return true;
	if (sema->types[dest].kind != TYPE_POINTER || sema->types[source].kind != TYPE_POINTER)
		return false;
	TypePointer to = sema->types[dest].as.pointer;
	TypePointer from = sema->types[source].as.pointer;
	if (to.size != from.size || to.is_volatile != from.is_volatile)
		return false;
	if (to.is_const != from.is_const && (dest_is_mut || !to.is_const))
		return false;
	return pointer_coercion_allowed(sema, to.child, from.child, dest_is_mut || !to.is_const);
}

/* Coerces `operand` to type `dest` (Zig's implicit coercions that szc supports so far). */
static Operand
coerce(Block *block, Operand operand, TypeId dest, uint32_t src)
{
	Sema *sema = block->sema;
	if (operand.kind == OPERAND_NONE || operand.type == dest || operand.type == T_NORETURN)
		return operand;
	TypeKind dest_kind = sema->types[dest].kind;
	if (operand.kind == OPERAND_COMPTIME) {
		const Value *value = operand_value(block, operand);
		if (value->kind == VALUE_UNDEFINED) {
			bool pointer_conversion = dest_kind == TYPE_POINTER || sema->types[operand.type].kind == TYPE_POINTER;
			if (!pointer_conversion || operand.type == type_id_of_well_known(IR_REF_TYPE_UNDEFINED) || pointer_coercion_allowed(sema, dest, operand.type, false))
				return comptime_operand(dest, value_undefined(&sema->values, dest));
		}
		if (value->kind == VALUE_INT && (dest_kind == TYPE_INT || dest_kind == TYPE_COMPTIME_INT)) {
			BigInt integer = value->as.integer;
			if (dest_kind == TYPE_INT && !bigint_fits(&integer, sema->types[dest].as.integer.is_signed, sema->types[dest].as.integer.bits)) {
				sema_error(block, src, "type '%s' cannot represent integer value '%s'", sema_type_name(sema, dest), bigint_to_string(sema->arena, &integer));
				return operand_none;
			}
			return comptime_operand(dest, value_int(&sema->values, dest, &integer));
		}
	}
	if (dest_kind == TYPE_POINTER && pointer_coercion_allowed(sema, dest, operand.type, false)) {
		if (operand.kind == OPERAND_RUNTIME)
			return runtime_operand(dest, emit_tir_un(block, TIR_PTR_CAST, dest, src, operand.inst));
		Value value = *operand_value(block, operand);
		value.type = dest;
		return comptime_operand(dest, value_intern(&sema->values, &value));
	}
	if (operand.kind == OPERAND_RUNTIME && dest_kind == TYPE_INT && is_int(sema, operand.type) && int_type_contains(sema, dest, operand.type))
		return runtime_operand(dest, emit_tir_un(block, TIR_INT_CAST, dest, src, operand.inst));
	sema_error(block, src, "expected type '%s', found '%s'", sema_type_name(sema, dest), sema_type_name(sema, operand.type));
	return operand_none;
}

/* Return-result coercion has a distinct noreturn diagnostic, even for undefined. */
static Operand
coerce_return(Block *block, Operand operand, TypeId dest, uint32_t src)
{
	if (dest == T_NORETURN && operand.kind != OPERAND_NONE && operand.type != T_NORETURN) {
		sema_error(block, src, "function declared 'noreturn' returns");
		return operand_none;
	}
	return coerce(block, operand, dest, src);
}

/* Integer casts use a result type supplied by irgen. */
static Operand
analyze_cast(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	Operand dest = resolve_ref(block, inst->data.bin.lhs);
	Operand operand = resolve_ref(block, inst->data.bin.rhs);
	TypeId type;
	uint32_t src = inst->src;
	if (operand.kind == OPERAND_NONE || !operand_to_type(block, dest, src, &type))
		return operand_none;
	if (!is_int_or_comptime_int(sema, type)) {
		sema_error(block, src, "expected integer or vector, found '%s'", sema_type_name(sema, type));
		return operand_none;
	}
	if (!is_int_or_comptime_int(sema, operand.type)) {
		sema_error(block, src, "expected integer or vector, found '%s'", sema_type_name(sema, operand.type));
		return operand_none;
	}
	if (inst->op == IR_TRUNCATE && is_int(sema, type)) {
		const Type *to = &sema->types[type];
		if (to->as.integer.bits == 0)
			return comptime_operand(type, value_int_u64(&sema->values, type, 0));
		if (is_int(sema, operand.type)) {
			const Type *from = &sema->types[operand.type];
			if (to->as.integer.is_signed != from->as.integer.is_signed) {
				sema_error(block, src, "expected %s integer type, found '%s'", to->as.integer.is_signed ? "signed" : "unsigned", sema_type_name(sema, operand.type));
				return operand_none;
			}
			if (to->as.integer.bits >= from->as.integer.bits)
				return coerce(block, operand, type, src);
		}
		if (operand_is_undefined(block, operand))
			return comptime_operand(type, value_undefined(&sema->values, type));
		if (operand.kind == OPERAND_COMPTIME) {
			BigInt value = bigint_wrap(sema->arena, &operand_value(block, operand)->as.integer, to->as.integer.is_signed, to->as.integer.bits);
			return comptime_operand(type, value_int(&sema->values, type, &value));
		}
		return runtime_operand(type, emit_tir_un(block, TIR_TRUNCATE, type, src, operand.inst));
	}
	if (operand.kind == OPERAND_COMPTIME)
		return coerce(block, operand, type, src);
	if (type == T_COMPTIME_INT) {
		sema_error(block, src, "unable to cast runtime value to 'comptime_int'");
		return operand_none;
	}
	return runtime_operand(type, emit_tir_un(block, TIR_INT_CAST, type, src, operand.inst));
}

static Operand
analyze_int_from_bool(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	Operand operand = resolve_ref(block, inst->data.un.operand);
	if (operand.kind == OPERAND_NONE)
		return operand_none;
	if (operand.type != T_BOOL) {
		sema_error(block, inst->src, "expected 'bool', found '%s'", type_tag_name(sema, operand.type));
		return operand_none;
	}
	TypeId type = type_int(sema, false, 1);
	if (operand_is_undefined(block, operand))
		return comptime_operand(type, value_undefined(&sema->values, type));
	if (operand.kind == OPERAND_COMPTIME)
		return comptime_operand(type, value_int_u64(&sema->values, type, operand_value(block, operand)->as.boolean ? 1 : 0));
	return runtime_operand(type, emit_tir_un(block, TIR_INT_FROM_BOOL, type, inst->src, operand.inst));
}

/* Peer type of two integer operands (Zig's peer type resolution, integer part). */
static bool
peer_type(Block *block, Operand lhs, Operand rhs, uint32_t src, TypeId *out)
{
	Sema *sema = block->sema;
	TypeId a = lhs.type;
	TypeId b = rhs.type;
	if (a == b) {
		*out = a;
		return true;
	}
	TypeKind ak = sema->types[a].kind;
	TypeKind bk = sema->types[b].kind;
	if (ak == TYPE_UNDEFINED || ak == TYPE_NORETURN) {
		*out = b;
		return true;
	}
	if (bk == TYPE_UNDEFINED || bk == TYPE_NORETURN) {
		*out = a;
		return true;
	}
	if (ak == TYPE_COMPTIME_INT && bk == TYPE_INT) {
		*out = b;
		return true;
	}
	if (ak == TYPE_INT && bk == TYPE_COMPTIME_INT) {
		*out = a;
		return true;
	}
	if (ak == TYPE_INT && bk == TYPE_INT) {
		if (int_type_contains(sema, a, b)) {
			*out = a;
			return true;
		}
		if (int_type_contains(sema, b, a)) {
			*out = b;
			return true;
		}
	}
	if (ak == TYPE_POINTER && bk == TYPE_POINTER) {
		if (pointer_coercion_allowed(sema, a, b, false)) {
			*out = a;
			return true;
		}
		if (pointer_coercion_allowed(sema, b, a, false)) {
			*out = b;
			return true;
		}
		TypePointer ap = sema->types[a].as.pointer;
		TypePointer bp = sema->types[b].as.pointer;
		if (ap.size == POINTER_ONE && bp.size == POINTER_ONE && sema->types[ap.child].kind == TYPE_ARRAY && sema->types[bp.child].kind == TYPE_ARRAY) {
			sema_error(block, src, "not supported by szc yet: runtime slice joins");
			return false;
		}
	}
	sema_error(block, src, "incompatible types: '%s' and '%s'", sema_type_name(sema, a), sema_type_name(sema, b));
	return false;
}

/* Operands of an operation that needs runtime code inside a comptime-only context. */
static bool
require_runtime(Block *block, uint32_t src)
{
	if (block->tir != NULL)
		return true;
	sema_error(block, src, "unable to evaluate comptime expression");
	return false;
}

/* ---------- Analysis: arithmetic ---------- */

static bool
is_wrapping_op(IrOp op)
{
	return op == IR_ADD_WRAP || op == IR_SUB_WRAP || op == IR_MUL_WRAP;
}

static bool
is_saturating_op(IrOp op)
{
	return op == IR_ADD_SAT || op == IR_SUB_SAT || op == IR_MUL_SAT;
}

static bool
is_division_op(IrOp op)
{
	switch (op) {
	case IR_DIV:
	case IR_MOD:
	case IR_DIV_TRUNC:
	case IR_DIV_FLOOR:
	case IR_DIV_CEIL:
	case IR_DIV_EXACT:
	case IR_REM:
	case IR_MOD_BUILTIN:
		return true;
	default:
		return false;
	}
}

/* Smallest and largest value of an integer type. */
static void
int_type_bounds(Sema *sema, TypeId type, BigInt *min, BigInt *max)
{
	bool is_signed = sema->types[type].as.integer.is_signed;
	uint32_t bits = sema->types[type].as.integer.bits;
	BigInt one = bigint_from_u64(sema->arena, 1);
	if (bits == 0) {
		*min = bigint_from_u64(sema->arena, 0);
		*max = *min;
		return;
	}
	if (is_signed) {
		BigInt half = bigint_shl(sema->arena, &one, bits - 1);
		*min = bigint_negate(sema->arena, &half);
		*max = bigint_sub(sema->arena, &half, &one);
	} else {
		BigInt full = bigint_shl(sema->arena, &one, bits);
		*min = bigint_from_u64(sema->arena, 0);
		*max = bigint_sub(sema->arena, &full, &one);
	}
}

/* Fits `value` into integer type `type` the way `op` (plain, wrapping, saturating) requires. */
static Operand
int_result(Block *block, IrOp op, TypeId type, BigInt value, uint32_t src)
{
	Sema *sema = block->sema;
	if (sema->types[type].kind == TYPE_INT) {
		bool is_signed = sema->types[type].as.integer.is_signed;
		uint32_t bits = sema->types[type].as.integer.bits;
		if (is_wrapping_op(op) || op == IR_NEGATE_WRAP || op == IR_SHL) {
			value = bigint_wrap(sema->arena, &value, is_signed, bits);
		} else if (is_saturating_op(op) || op == IR_SHL_SAT) {
			BigInt min;
			BigInt max;
			int_type_bounds(sema, type, &min, &max);
			if (bigint_compare(&value, &min) < 0)
				value = min;
			if (bigint_compare(&value, &max) > 0)
				value = max;
		} else if (!bigint_fits(&value, is_signed, bits)) {
			sema_error(block, src, "overflow of integer type '%s' with value '%s'", sema_type_name(sema, type), bigint_to_string(sema->arena, &value));
			return operand_none;
		}
	}
	return comptime_operand(type, value_int(&sema->values, type, &value));
}

static TirOp
tir_arith_op(IrOp op)
{
	switch (op) {
	case IR_ADD:
		return TIR_ADD;
	case IR_ADD_WRAP:
		return TIR_ADD_WRAP;
	case IR_ADD_SAT:
		return TIR_ADD_SAT;
	case IR_SUB:
		return TIR_SUB;
	case IR_SUB_WRAP:
		return TIR_SUB_WRAP;
	case IR_SUB_SAT:
		return TIR_SUB_SAT;
	case IR_MUL:
		return TIR_MUL;
	case IR_MUL_WRAP:
		return TIR_MUL_WRAP;
	case IR_MUL_SAT:
		return TIR_MUL_SAT;
	case IR_DIV:
	case IR_DIV_TRUNC:
		return TIR_DIV_TRUNC;
	case IR_DIV_FLOOR:
		return TIR_DIV_FLOOR;
	case IR_DIV_CEIL:
		return TIR_DIV_CEIL;
	case IR_DIV_EXACT:
		return TIR_DIV_EXACT;
	case IR_MOD:
	case IR_REM:
		return TIR_REM;
	case IR_MOD_BUILTIN:
		return TIR_MOD;
	case IR_BIT_AND:
		return TIR_BIT_AND;
	case IR_BIT_OR:
		return TIR_BIT_OR;
	case IR_BIT_XOR:
		return TIR_BIT_XOR;
	case IR_SHL:
		return TIR_SHL;
	case IR_SHL_SAT:
		return TIR_SHL_SAT;
	case IR_SHR:
		return TIR_SHR;
	case IR_CMP_EQ:
		return TIR_CMP_EQ;
	case IR_CMP_NEQ:
		return TIR_CMP_NEQ;
	case IR_CMP_LT:
		return TIR_CMP_LT;
	case IR_CMP_GT:
		return TIR_CMP_GT;
	case IR_CMP_LTE:
		return TIR_CMP_LTE;
	case IR_CMP_GTE:
		return TIR_CMP_GTE;
	default:
		return TIR_UNREACHABLE;
	}
}

static bool
is_comptime_known(const Block *block, Operand operand)
{
	return operand.kind == OPERAND_COMPTIME && !operand_is_undefined(block, operand);
}

static bool
check_defined(Block *block, Operand operand, uint32_t src)
{
	if (!operand_is_undefined(block, operand))
		return true;
	sema_error(block, src, "use of undefined value here causes illegal behavior");
	return false;
}

/* +, -, *, /, %, wrapping and saturating forms, &, |, ^. */
static Operand
analyze_arith(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	IrOp op = inst->op;
	uint32_t src = inst->src;
	Operand lhs = resolve_ref(block, inst->data.bin.lhs);
	Operand rhs = resolve_ref(block, inst->data.bin.rhs);
	if (lhs.kind == OPERAND_NONE || rhs.kind == OPERAND_NONE)
		return operand_none;
	/* u0 is known zero even when peer coercion widens it. */
	bool rhs_is_zero_type = is_int(sema, rhs.type) && sema->types[rhs.type].as.integer.bits == 0;
	TypeId type;
	if (!peer_type(block, lhs, rhs, src, &type))
		return operand_none;
	if (!is_int_or_comptime_int(sema, type)) {
		const char *kind = op == IR_BIT_AND || op == IR_BIT_OR || op == IR_BIT_XOR ? "binary bitwise expression" : "binary expression";
		sema_error(block, src, "invalid operands to %s: '%s' and '%s'", kind, type_tag_name(sema, lhs.type), type_tag_name(sema, rhs.type));
		return operand_none;
	}
	lhs = coerce(block, lhs, type, src);
	rhs = coerce(block, rhs, type, src);
	if (lhs.kind == OPERAND_NONE || rhs.kind == OPERAND_NONE)
		return operand_none;
	if (is_saturating_op(op) && (operand_is_undefined(block, lhs) || operand_is_undefined(block, rhs)))
		return comptime_operand(type, value_undefined(&sema->values, type));
	if ((op == IR_DIV || op == IR_MOD) && is_signed_type(sema, type)) {
		bool known_positive = is_comptime_known(block, lhs) && is_comptime_known(block, rhs) && !operand_value(block, lhs)->as.integer.negative && !operand_value(block, rhs)->as.integer.negative;
		if (!known_positive) {
			const char *ln = sema_type_name(sema, lhs.type);
			const char *rn = sema_type_name(sema, rhs.type);
			if (op == IR_DIV)
				sema_error(block, src, "division with '%s' and '%s': signed integers must use @divTrunc, @divFloor, @divCeil, or @divExact", ln, rn);
			else
				sema_error(block, src, "remainder division with '%s' and '%s': signed integers and floats must use @rem or @mod", ln, rn);
			return operand_none;
		}
	}
	if (is_division_op(op)) {
		if (!check_defined(block, lhs, src) || !check_defined(block, rhs, src))
			return operand_none;
		if (rhs_is_zero_type) {
			sema_error(block, src, "division by zero here causes illegal behavior");
			return operand_none;
		}
		if (is_comptime_known(block, rhs) && bigint_is_zero(&operand_value(block, rhs)->as.integer)) {
			sema_error(block, src, "division by zero here causes illegal behavior");
			return operand_none;
		}
	}
	if (lhs.kind == OPERAND_COMPTIME && rhs.kind == OPERAND_COMPTIME) {
		if (!check_defined(block, lhs, src) || !check_defined(block, rhs, src))
			return operand_none;
		BigInt a = operand_value(block, lhs)->as.integer;
		BigInt b = operand_value(block, rhs)->as.integer;
		BigInt result;
		BigInt quotient;
		BigInt remainder;
		switch (op) {
		case IR_ADD:
		case IR_ADD_WRAP:
		case IR_ADD_SAT:
			result = bigint_add(sema->arena, &a, &b);
			break;
		case IR_SUB:
		case IR_SUB_WRAP:
		case IR_SUB_SAT:
			result = bigint_sub(sema->arena, &a, &b);
			break;
		case IR_MUL:
		case IR_MUL_WRAP:
		case IR_MUL_SAT:
			result = bigint_mul(sema->arena, &a, &b);
			break;
		case IR_DIV:
		case IR_MOD:
		case IR_DIV_TRUNC:
		case IR_DIV_FLOOR:
		case IR_DIV_CEIL:
		case IR_DIV_EXACT:
		case IR_REM:
		case IR_MOD_BUILTIN: {
			BigIntDivRound round = op == IR_DIV_FLOOR || op == IR_MOD_BUILTIN ? BIGINT_DIV_FLOOR : op == IR_DIV_CEIL ? BIGINT_DIV_CEIL : BIGINT_DIV_TRUNC;
			bigint_divmod(sema->arena, &a, &b, round, &quotient, &remainder);
			if (op == IR_DIV_EXACT && !bigint_is_zero(&remainder)) {
				sema_error(block, src, "exact division produced remainder");
				return operand_none;
			}
			result = op == IR_MOD || op == IR_REM || op == IR_MOD_BUILTIN ? remainder : quotient;
			break;
		}
		case IR_BIT_AND:
			result = bigint_bitop(sema->arena, &a, &b, BIGINT_AND);
			break;
		case IR_BIT_OR:
			result = bigint_bitop(sema->arena, &a, &b, BIGINT_OR);
			break;
		default:
			result = bigint_bitop(sema->arena, &a, &b, BIGINT_XOR);
			break;
		}
		return int_result(block, op, type, result, src);
	}
	if (sema->types[type].kind == TYPE_COMPTIME_INT || !require_runtime(block, src))
		return operand_none;
	TirRef l = materialize(block, lhs, src);
	TirRef r = materialize(block, rhs, src);
	return runtime_operand(type, emit_tir_bin(block, tir_arith_op(op), type, src, l, r));
}

/* Bit count of the shift amount type for an integer type of `bits` bits (Log2Int). */
static uint32_t
log2_int_bits(uint32_t bits)
{
	uint32_t result = 0;
	while (bits > 1 && ((uint64_t)1 << result) < bits)
		result = result + 1;
	return result;
}

/* <<, >>, <<|. */
static Operand
analyze_shift(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	IrOp op = inst->op;
	uint32_t src = inst->src;
	Operand lhs = resolve_ref(block, inst->data.bin.lhs);
	Operand rhs = resolve_ref(block, inst->data.bin.rhs);
	if (lhs.kind == OPERAND_NONE || rhs.kind == OPERAND_NONE)
		return operand_none;
	if (!is_int_or_comptime_int(sema, lhs.type) || !is_int_or_comptime_int(sema, rhs.type)) {
		sema_error(block, src, "invalid operands to binary expression: '%s' and '%s'", type_tag_name(sema, lhs.type), type_tag_name(sema, rhs.type));
		return operand_none;
	}
	TypeId type = lhs.type;
	if (op == IR_SHL_SAT && (operand_is_undefined(block, lhs) || operand_is_undefined(block, rhs)))
		return comptime_operand(type, value_undefined(&sema->values, type));
	if (op == IR_SHL_SAT && rhs.kind == OPERAND_RUNTIME && is_signed_type(sema, rhs.type)) {
		sema_error(block, src, "shift by signed type '%s'", sema_type_name(sema, rhs.type));
		return operand_none;
	}
	if (is_int(sema, type)) {
		if (op != IR_SHL_SAT) {
			TypeId amount_type = type_int(sema, false, log2_int_bits(sema->types[type].as.integer.bits));
			rhs = coerce(block, rhs, amount_type, src);
			if (rhs.kind == OPERAND_NONE)
				return operand_none;
		}
	} else if (rhs.kind != OPERAND_COMPTIME) {
		sema_error(block, src, "LHS of shift must be a fixed-width integer type, or RHS must be comptime-known");
		return operand_none;
	}
	if (rhs.kind == OPERAND_COMPTIME) {
		if (!check_defined(block, rhs, src))
			return operand_none;
		BigInt amount = operand_value(block, rhs)->as.integer;
		if (amount.negative) {
			sema_error(block, src, "shift by negative amount '%s'", bigint_to_string(sema->arena, &amount));
			return operand_none;
		}
		if (op != IR_SHL_SAT && is_int(sema, type) && !bigint_is_zero(&amount)) {
			BigInt bits = bigint_from_u64(sema->arena, sema->types[type].as.integer.bits);
			if (bigint_compare(&amount, &bits) >= 0) {
				sema_error(block, src, "shift amount '%s' is too large for operand type '%s'", bigint_to_string(sema->arena, &amount), sema_type_name(sema, type));
				return operand_none;
			}
		}
		if (op == IR_SHL_SAT && is_int(sema, type)) {
			/* Match 0.17 arith.intShlSat: a comptime amount beyond u64
			 * saturates every nonzero value to max, even a negative lhs. */
			if (lhs.kind == OPERAND_COMPTIME && !bigint_fits(&amount, false, 64)) {
				BigInt a = operand_value(block, lhs)->as.integer;
				BigInt min;
				BigInt max;
				int_type_bounds(sema, type, &min, &max);
				return comptime_operand(type, value_int(&sema->values, type, bigint_is_zero(&a) ? &a : &max));
			}
			uint32_t bits = sema->types[type].as.integer.bits;
			BigInt limit = bigint_from_u64(sema->arena, bits);
			if (bigint_compare(&amount, &limit) > 0)
				amount = limit;
			TypeId amount_type = type_int(sema, false, log2_int_bits(bits + 1));
			rhs = comptime_operand(amount_type, value_int(&sema->values, amount_type, &amount));
		}
		if (lhs.kind == OPERAND_COMPTIME) {
			if (!check_defined(block, lhs, src))
				return operand_none;
			uint32_t shift;
			if (!bigint_to_u32(&amount, &shift) || shift > 65535) {
				sema_error(block, src, "not supported by szc yet: shift amount '%s'", bigint_to_string(sema->arena, &amount));
				return operand_none;
			}
			BigInt a = operand_value(block, lhs)->as.integer;
			BigInt result = op == IR_SHR ? bigint_shr(sema->arena, &a, shift) : bigint_shl(sema->arena, &a, shift);
			return int_result(block, op, type, result, src);
		}
	}
	if (!require_runtime(block, src))
		return operand_none;
	TirRef l = materialize(block, lhs, src);
	TirRef r = materialize(block, rhs, src);
	return runtime_operand(type, emit_tir_bin(block, tir_arith_op(op), type, src, l, r));
}

static bool
compare_result(IrOp op, int order)
{
	switch (op) {
	case IR_CMP_EQ:
		return order == 0;
	case IR_CMP_NEQ:
		return order != 0;
	case IR_CMP_LT:
		return order < 0;
	case IR_CMP_GT:
		return order > 0;
	case IR_CMP_LTE:
		return order <= 0;
	default:
		return order >= 0;
	}
}

static Operand
bool_operand(Sema *sema, bool value)
{
	return comptime_operand(T_BOOL, value_bool(&sema->values, T_BOOL, value));
}

/*
 * If one side is a comptime integer outside the range of the other side's
 * type, the comparison is known: returns true and sets *result.
 */
static bool
compare_out_of_range(Block *block, IrOp op, Operand lhs, Operand rhs, bool *result)
{
	Sema *sema = block->sema;
	bool constant_on_left = lhs.kind == OPERAND_COMPTIME && rhs.kind == OPERAND_RUNTIME;
	bool constant_on_right = rhs.kind == OPERAND_COMPTIME && lhs.kind == OPERAND_RUNTIME;
	if (!constant_on_left && !constant_on_right)
		return false;
	Operand constant = constant_on_left ? lhs : rhs;
	Operand variable = constant_on_left ? rhs : lhs;
	if (!is_int(sema, variable.type) || !is_comptime_known(block, constant))
		return false;
	BigInt value = operand_value(block, constant)->as.integer;
	BigInt min;
	BigInt max;
	int_type_bounds(sema, variable.type, &min, &max);
	/* order of the variable relative to the constant */
	int order;
	if (bigint_compare(&value, &max) > 0)
		order = -1;
	else if (bigint_compare(&value, &min) < 0)
		order = 1;
	else
		return false;
	if (constant_on_left)
		order = -order;
	*result = compare_result(op, order);
	return true;
}

static Operand
analyze_compare(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	IrOp op = inst->op;
	uint32_t src = inst->src;
	Operand lhs = resolve_ref(block, inst->data.bin.lhs);
	Operand rhs = resolve_ref(block, inst->data.bin.rhs);
	if (lhs.kind == OPERAND_NONE || rhs.kind == OPERAND_NONE)
		return operand_none;
	bool is_equality = op == IR_CMP_EQ || op == IR_CMP_NEQ;
	TypeKind lk = sema->types[lhs.type].kind;
	TypeKind rk = sema->types[rhs.type].kind;
	if (is_equality && lk == TYPE_TYPE && rk == TYPE_TYPE) {
		bool equal = operand_value(block, lhs)->as.type_value == operand_value(block, rhs)->as.type_value;
		return bool_operand(sema, (op == IR_CMP_EQ) == equal);
	}
	if (is_equality && lk == TYPE_BOOL && rk == TYPE_BOOL) {
		if (lhs.kind == OPERAND_COMPTIME && rhs.kind == OPERAND_COMPTIME) {
			if (!check_defined(block, lhs, src) || !check_defined(block, rhs, src))
				return operand_none;
			bool equal = operand_value(block, lhs)->as.boolean == operand_value(block, rhs)->as.boolean;
			return bool_operand(sema, (op == IR_CMP_EQ) == equal);
		}
		if (!require_runtime(block, src))
			return operand_none;
		TirRef l = materialize(block, lhs, src);
		TirRef r = materialize(block, rhs, src);
		return runtime_operand(T_BOOL, emit_tir_bin(block, tir_arith_op(op), T_BOOL, src, l, r));
	}
	if (is_equality && lk == TYPE_POINTER && rk == TYPE_POINTER) {
		TypeId type;
		if (!peer_type(block, lhs, rhs, src, &type))
			return operand_none;
		lhs = coerce(block, lhs, type, src);
		rhs = coerce(block, rhs, type, src);
		if (operand_is_undefined(block, lhs) || operand_is_undefined(block, rhs))
			return comptime_operand(T_BOOL, value_undefined(&sema->values, T_BOOL));
		if (lhs.kind == OPERAND_COMPTIME && rhs.kind == OPERAND_COMPTIME) {
			const Value *a = operand_value(block, lhs);
			const Value *b = operand_value(block, rhs);
			bool equal;
			if (a->kind == VALUE_REF && b->kind == VALUE_REF)
				equal = a->as.object == b->as.object;
			else if (a->kind == VALUE_BYTES && b->kind == VALUE_BYTES)
				equal = lhs.value == rhs.value;
			else {
				sema_error(block, src, "not supported by szc yet: comptime pointer equality");
				return operand_none;
			}
			return bool_operand(sema, (op == IR_CMP_EQ) == equal);
		}
		if (!require_runtime(block, src))
			return operand_none;
		TirRef left = materialize(block, lhs, src);
		TirRef right = materialize(block, rhs, src);
		return runtime_operand(T_BOOL, emit_tir_bin(block, tir_arith_op(op), T_BOOL, src, left, right));
	}
	if (!is_int_or_comptime_int(sema, lhs.type) || !is_int_or_comptime_int(sema, rhs.type)) {
		const char *text = op == IR_CMP_EQ ? "==" : op == IR_CMP_NEQ ? "!=" : op == IR_CMP_LT ? "<" : op == IR_CMP_GT ? ">" : op == IR_CMP_LTE ? "<=" : ">=";
		if (lhs.type != rhs.type)
			sema_error(block, src, "incompatible types: '%s' and '%s'", sema_type_name(sema, lhs.type), sema_type_name(sema, rhs.type));
		else
			sema_error(block, src, "operator %s not allowed for type '%s'", text, sema_type_name(sema, lhs.type));
		return operand_none;
	}
	bool known;
	if (compare_out_of_range(block, op, lhs, rhs, &known))
		return bool_operand(sema, known);
	TypeId type;
	if (!peer_type(block, lhs, rhs, src, &type))
		return operand_none;
	lhs = coerce(block, lhs, type, src);
	rhs = coerce(block, rhs, type, src);
	if (lhs.kind == OPERAND_NONE || rhs.kind == OPERAND_NONE)
		return operand_none;
	if (lhs.kind == OPERAND_COMPTIME && rhs.kind == OPERAND_COMPTIME) {
		if (!check_defined(block, lhs, src) || !check_defined(block, rhs, src))
			return operand_none;
		int order = bigint_compare(&operand_value(block, lhs)->as.integer, &operand_value(block, rhs)->as.integer);
		return bool_operand(sema, compare_result(op, order));
	}
	if (!require_runtime(block, src))
		return operand_none;
	TirRef l = materialize(block, lhs, src);
	TirRef r = materialize(block, rhs, src);
	return runtime_operand(T_BOOL, emit_tir_bin(block, tir_arith_op(op), T_BOOL, src, l, r));
}

/* -x, -%x, ~x, !x. */
static Operand
analyze_unary(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	IrOp op = inst->op;
	uint32_t src = inst->src;
	Operand operand = resolve_ref(block, inst->data.un.operand);
	if (operand.kind == OPERAND_NONE)
		return operand_none;
	TypeId type = operand.type;
	if (op == IR_BOOL_NOT) {
		operand = coerce(block, operand, T_BOOL, src);
		if (operand.kind == OPERAND_NONE)
			return operand_none;
		if (operand.kind == OPERAND_COMPTIME) {
			if (!check_defined(block, operand, src))
				return operand_none;
			return bool_operand(sema, !operand_value(block, operand)->as.boolean);
		}
		return runtime_operand(T_BOOL, emit_tir_un(block, TIR_NOT, T_BOOL, src, operand.inst));
	}
	if (op == IR_BIT_NOT) {
		if (!is_int(sema, type)) {
			sema_error(block, src, "unable to perform binary not operation on type '%s'", sema_type_name(sema, type));
			return operand_none;
		}
		if (operand.kind == OPERAND_COMPTIME) {
			if (!check_defined(block, operand, src))
				return operand_none;
			BigInt result = bigint_not(sema->arena, &operand_value(block, operand)->as.integer);
			result = bigint_wrap(sema->arena, &result, sema->types[type].as.integer.is_signed, sema->types[type].as.integer.bits);
			return comptime_operand(type, value_int(&sema->values, type, &result));
		}
		return runtime_operand(type, emit_tir_un(block, TIR_NOT, type, src, operand.inst));
	}
	/* negation */
	bool is_unsigned_int = is_int(sema, type) && !sema->types[type].as.integer.is_signed;
	if (!is_int_or_comptime_int(sema, type) || (is_unsigned_int && op == IR_NEGATE && operand.kind == OPERAND_RUNTIME)) {
		sema_error(block, src, "negation of type '%s'", sema_type_name(sema, type));
		return operand_none;
	}
	if (operand.kind == OPERAND_COMPTIME) {
		if (!check_defined(block, operand, src))
			return operand_none;
		BigInt result = bigint_negate(sema->arena, &operand_value(block, operand)->as.integer);
		return int_result(block, op, type, result, src);
	}
	return runtime_operand(type, emit_tir_un(block, op == IR_NEGATE_WRAP ? TIR_NEGATE_WRAP : TIR_NEGATE, type, src, operand.inst));
}


/* String literals are pointers to immutable, zero-sentinel u8 arrays. */
static Operand
analyze_string(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	StringId id = inst->data.name.name;
	uint32_t length = intern_length(sema->interner, id);
	Type array = make_type(TYPE_ARRAY);
	array.as.array.child = type_id_of_well_known(IR_REF_TYPE_U8);
	array.as.array.length = length;
	array.as.array.sentinel_zero = true;
	TypeId child = type_intern(sema, &array);
	TypeId pointer = type_pointer(sema, child, POINTER_ONE, true);
	return comptime_operand(pointer, value_bytes(&sema->values, pointer, (const uint8_t *)intern_text(sema->interner, id), length));
}

/* Lower the pointer-to-byte-array -> byte-slice conversion directly to ptr/len. */
static InstOutcome
analyze_panic(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	IrPanic payload;
	IR_READ(block->ir, inst->data.payload, &payload);
	Operand message = resolve_ref(block, payload.message);
	if (message.kind == OPERAND_NONE)
		return INST_NORETURN;
	if (message.type == type_id_of_well_known(IR_REF_TYPE_UNDEFINED) && !block->is_comptime) {
		sema_error(block, payload.argument_src, "not supported by szc yet: undefined panic message");
		return INST_NORETURN;
	}
	if (!check_defined(block, message, payload.argument_src))
		return INST_NORETURN;
	const Type *pointer = &sema->types[message.type];
	const Type *array = pointer->kind == TYPE_POINTER && pointer->as.pointer.size == POINTER_ONE ? &sema->types[pointer->as.pointer.child] : NULL;
	if (array == NULL || array->kind != TYPE_ARRAY || array->as.array.child != type_id_of_well_known(IR_REF_TYPE_U8)) {
		sema_error(block, payload.argument_src, "expected type '[]const u8', found '%s'", sema_type_name(sema, message.type));
		return INST_NORETURN;
	}
	uint32_t length = array->as.array.length;
	if (block->is_comptime) {
		if (message.kind != OPERAND_COMPTIME || operand_value(block, message)->kind != VALUE_BYTES)
			sema_error(block, inst->src, "unable to evaluate comptime expression");
		else {
			static const char prefix[] = "encountered @panic at comptime: ";
			ByteString bytes = operand_value(block, message)->as.bytes;
			size_t size = sizeof(prefix) - 1 + (size_t)bytes.length;
			char *text = arena_alloc(sema->arena, size, 1);
			memcpy(text, prefix, sizeof(prefix) - 1);
			memcpy(text + sizeof(prefix) - 1, bytes.data, bytes.length);
			diag_report_bytes(sema->diag, DIAG_ERROR, block->ir->file, inst->src, text, size);
		}
		return INST_NORETURN;
	}
	if (!require_runtime(block, inst->src))
		return INST_NORETURN;
	TirRef data = materialize(block, message, inst->src);
	Operand size = comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, length));
	emit_tir_bin(block, TIR_PANIC, T_NORETURN, inst->src, data, materialize(block, size, inst->src));
	return INST_NORETURN;
}

/* ---------- Analysis: control flow ---------- */

/* Prefer a known bound, then assert every runtime input has that length. */
static Operand
analyze_for_len(Block *block, const IrInst *inst)
{
	uint32_t count = block->ir->extra[inst->data.payload];
	if (count == 0) {
		sema_error(block, inst->src, "unbounded for loop");
		return operand_none;
	}
	Operand *lengths = ARENA_ARRAY(block->sema->arena, Operand, count);
	Operand result = operand_none;
	bool failed = false;
	TypeId type = type_id_of_well_known(IR_REF_TYPE_USIZE);
	for (uint32_t i = 0; i < count; i++) {
		IrRef ref = block->ir->extra[inst->data.payload + 1 + i];
		lengths[i] = coerce(block, resolve_ref(block, ref), type, inst->src);
		if (lengths[i].kind == OPERAND_NONE || !check_defined(block, lengths[i], inst->src)) {
			failed = true;
			continue;
		}
		if (result.kind == OPERAND_NONE || (result.kind == OPERAND_RUNTIME && lengths[i].kind == OPERAND_COMPTIME))
			result = lengths[i];
	}
	if (failed)
		return operand_none;
	for (uint32_t i = 0; i < count; i++) {
		if (result.kind == OPERAND_COMPTIME && lengths[i].kind == OPERAND_COMPTIME) {
			if (bigint_compare(&operand_value(block, result)->as.integer, &operand_value(block, lengths[i])->as.integer) != 0) {
				sema_error(block, inst->src, "non-matching for loop lengths");
				failed = true;
			}
			continue;
		}
		if (result.kind == OPERAND_RUNTIME && lengths[i].kind == OPERAND_RUNTIME && result.inst == lengths[i].inst)
			continue;
		if (!require_runtime(block, inst->src))
			return operand_none;
		TirRef lhs = materialize(block, result, inst->src);
		TirRef rhs = materialize(block, lengths[i], inst->src);
		emit_tir_bin(block, TIR_FOR_LEN_CHECK, T_VOID, inst->src, lhs, rhs);
	}
	return failed ? operand_none : result;
}

/* `and` / `or` with a short-circuiting right-hand side. */
static Operand
analyze_bool_br(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	bool is_and = inst->op == IR_BOOL_AND;
	uint32_t src = inst->src;
	IrBoolBr payload;
	IR_READ(block->ir, inst->data.payload, &payload);
	Operand lhs = coerce(block, resolve_ref(block, payload.lhs), T_BOOL, src);
	if (lhs.kind == OPERAND_NONE)
		return operand_none;
	if (lhs.kind == OPERAND_COMPTIME) {
		if (!check_defined(block, lhs, src))
			return operand_none;
		bool value = operand_value(block, lhs)->as.boolean;
		if (value != is_and)
			return lhs;
		BodyResult rhs = analyze_body(block, payload.rhs_body);
		if (rhs.end == BODY_NORETURN)
			return runtime_operand(T_NORETURN, TIR_REF_NONE);
		if (rhs.end != BODY_BREAK_RESULT)
			return operand_none;
		return coerce(block, rhs.result, T_BOOL, src);
	}
	/* block { condbr lhs, { br rhs }, { br short-circuit value } } */
	TirRef result = emit_tir(block, TIR_BLOCK, T_BOOL, src);
	Block inner;
	block_init_child(&inner, block);
	Block then_block;
	block_init_child(&then_block, &inner);
	BodyResult rhs = analyze_body(&then_block, payload.rhs_body);
	if (rhs.end == BODY_BREAK_RESULT) {
		Operand value = coerce(&then_block, rhs.result, T_BOOL, src);
		if (value.kind == OPERAND_NONE)
			return operand_none;
		emit_tir_bin(&then_block, TIR_BR, T_NORETURN, src, result, materialize(&then_block, value, src));
	}
	Block else_block;
	block_init_child(&else_block, &inner);
	TirRef constant = materialize(&else_block, bool_operand(sema, !is_and), src);
	emit_tir_bin(&else_block, TIR_BR, T_NORETURN, src, result, constant);
	TirCondbr condbr;
	condbr.condition = lhs.inst;
	TirBody rhs_body = block_finish(&then_block);
	TirBody short_body = block_finish(&else_block);
	condbr.then_body = is_and ? rhs_body : short_body;
	condbr.else_body = is_and ? short_body : rhs_body;
	TirRef branch = emit_tir(&inner, TIR_CONDBR, T_NORETURN, src);
	block->tir->insts[branch].data.payload = tir_extra(block->tir, sema->arena, &condbr, TIR_WORDS(TirCondbr));
	block->tir->insts[result].data.payload = block_finish(&inner);
	return runtime_operand(T_BOOL, result);
}

/* Source if: analyze one known arm, or merge the two runtime exits. */
static Operand
analyze_if(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	uint32_t src = inst->src;
	IrCondbr payload;
	IR_READ(block->ir, inst->data.payload, &payload);
	Operand condition = coerce(block, resolve_ref(block, payload.condition), T_BOOL, src);
	if (condition.kind == OPERAND_NONE)
		return operand_none;
	if (condition.kind == OPERAND_COMPTIME) {
		if (!check_defined(block, condition, src))
			return operand_none;
		IrBody body = operand_value(block, condition)->as.boolean ? payload.then_body : payload.else_body;
		BodyResult arm = analyze_body(block, body);
		return arm.end == BODY_NORETURN ? runtime_operand(T_NORETURN, TIR_REF_NONE) : arm.result;
	}
	if (!require_runtime(block, src))
		return operand_none;

	TirRef merge = emit_tir(block, TIR_BLOCK, T_VOID, src);
	Block arms[2];
	BodyResult exits[2];
	IrBody bodies[2] = {payload.then_body, payload.else_body};
	uint32_t exit_count = 0;
	Operand value = operand_none;
	TypeId type = T_NORETURN;
	for (uint32_t i = 0; i < 2; i++) {
		block_init_child(&arms[i], block);
		exits[i] = analyze_body(&arms[i], bodies[i]);
	}
	for (uint32_t i = 0; i < 2; i++) {
		if (exits[i].end != BODY_BREAK_RESULT)
			continue;
		if (exits[i].result.kind == OPERAND_NONE)
			return operand_none;
		if (exit_count == 0) {
			type = exits[i].result.type;
			value = exits[i].result;
		} else if (!peer_type(block, value, exits[i].result, src, &type)) {
			return operand_none;
		}
		exit_count++;
	}
	if (exit_count > 1 && is_comptime_only_type(sema, type)) {
		sema_error(block, src, "value with comptime-only type '%s' depends on runtime control flow", sema_type_name(sema, type));
		return operand_none;
	}
	/* One surviving constant exit stays known, but its control effects remain. */
	bool known = exit_count == 1 && value.kind == OPERAND_COMPTIME;
	TypeId merge_type = known ? T_VOID : type;
	block->tir->insts[merge].type = merge_type;
	for (uint32_t i = 0; i < 2; i++) {
		if (exits[i].end != BODY_BREAK_RESULT)
			continue;
		Operand operand = coerce(&arms[i], exits[i].result, type, src);
		if (operand.kind == OPERAND_NONE)
			return operand_none;
		TirRef ref = merge_type == T_VOID ? TIR_REF_NONE : materialize(&arms[i], operand, src);
		emit_tir_bin(&arms[i], TIR_BR, T_NORETURN, src, merge, ref);
	}
	Block inner;
	block_init_child(&inner, block);
	TirCondbr branch;
	branch.condition = condition.inst;
	branch.then_body = block_finish(&arms[0]);
	branch.else_body = block_finish(&arms[1]);
	TirRef ref = emit_tir(&inner, TIR_CONDBR, T_NORETURN, src);
	block->tir->insts[ref].data.payload = tir_extra(block->tir, sema->arena, &branch, TIR_WORDS(TirCondbr));
	block->tir->insts[merge].data.payload = block_finish(&inner);
	if (known)
		return value;
	if (type == T_VOID)
		return void_operand(sema);
	return runtime_operand(type, merge);
}

static ControlTarget *
find_control(Block *block, IrInstIndex index)
{
	for (ControlTarget *target = block->control; target != NULL; target = target->parent) {
		if (target->ir_index == index)
			return target;
	}
	return NULL;
}

/* Break operands are coerced after all exits establish the block's peer type. */
static void
analyze_break(Block *block, const IrInst *inst)
{
	ControlTarget *target = find_control(block, inst->data.brk.block);
	if (target == NULL || target->is_loop) {
		sema_error(block, inst->src, "invalid break target");
		return;
	}
	Operand value = resolve_ref(block, inst->data.brk.operand);
	if (value.kind == OPERAND_NONE)
		return;
	if (target->count == target->capacity) {
		uint32_t capacity = target->capacity == 0 ? 4 : target->capacity * 2;
		target->exits = arena_grow_array(block->sema->arena, target->exits, target->count, capacity, sizeof(BreakExit), alignof(BreakExit));
		target->capacity = capacity;
	}
	BreakExit *exit = &target->exits[target->count++];
	exit->value = value;
	exit->src = inst->src;
	exit->wrapper = block->tir == NULL ? TIR_REF_NONE : emit_tir(block, TIR_BLOCK, T_NORETURN, inst->src);
}

static Operand
analyze_control_block(Block *block, IrInstIndex index)
{
	const IrInst *inst = &block->ir->insts[index];
	ControlTarget target;
	memset(&target, 0, sizeof(target));
	target.parent = block->control;
	target.ir_index = index;
	target.tir_ref = block->tir == NULL ? TIR_REF_NONE : emit_tir(block, TIR_BLOCK, T_VOID, inst->src);
	Block inner;
	block_init_child(&inner, block);
	inner.control = &target;
	analyze_body(&inner, inst->data.payload);
	if (block->tir == NULL) {
		if (target.count == 1)
			return target.exits[0].value;
		if (target.count > 1)
			sema_error(block, inst->src, "unable to evaluate comptime expression");
		return runtime_operand(T_NORETURN, TIR_REF_NONE);
	}
	TypeId type = T_NORETURN;
	if (target.count > 0)
		type = target.exits[0].value.type;
	for (uint32_t i = 1; i < target.count; i++) {
		Operand lhs = target.exits[0].value;
		lhs.type = type;
		if (!peer_type(block, lhs, target.exits[i].value, inst->src, &type))
			return operand_none;
	}
	if (target.count > 1 && is_comptime_only_type(block->sema, type)) {
		sema_error(block, inst->src, "value with comptime-only type '%s' depends on runtime control flow", sema_type_name(block->sema, type));
		return operand_none;
	}
	bool known = target.count == 1 && target.exits[0].value.kind == OPERAND_COMPTIME;
	TypeId merge_type = known ? T_VOID : type;
	block->tir->insts[target.tir_ref].type = merge_type;
	for (uint32_t i = 0; i < target.count; i++) {
		BreakExit exit = target.exits[i];
		Block branch;
		block_init_child(&branch, block);
		Operand value = coerce(&branch, exit.value, type, exit.src);
		if (value.kind == OPERAND_NONE)
			continue;
		TirRef operand = merge_type == T_VOID ? TIR_REF_NONE : materialize(&branch, value, exit.src);
		emit_tir_bin(&branch, TIR_BR, T_NORETURN, exit.src, target.tir_ref, operand);
		block->tir->insts[exit.wrapper].data.payload = block_finish(&branch);
	}
	block->tir->insts[target.tir_ref].data.payload = block_finish(&inner);
	if (known)
		return target.exits[0].value;
	if (type == T_VOID)
		return void_operand(block->sema);
	return runtime_operand(type, target.tir_ref);
}

static void
analyze_loop(Block *block, IrInstIndex index)
{
	const IrInst *inst = &block->ir->insts[index];
	if (!require_runtime(block, inst->src))
		return;
	ControlTarget target;
	memset(&target, 0, sizeof(target));
	target.parent = block->control;
	target.ir_index = index;
	target.is_loop = true;
	target.tir_ref = emit_tir(block, TIR_LOOP, T_NORETURN, inst->src);
	Block inner;
	block_init_child(&inner, block);
	inner.control = &target;
	analyze_body(&inner, inst->data.payload);
	block->tir->insts[target.tir_ref].data.payload = block_finish(&inner);
}

static void
analyze_condbr(Block *block, const IrInst *inst)
{
	IrCondbr payload;
	IR_READ(block->ir, inst->data.payload, &payload);
	Operand condition = coerce(block, resolve_ref(block, payload.condition), T_BOOL, inst->src);
	if (condition.kind == OPERAND_NONE)
		return;
	if (condition.kind == OPERAND_COMPTIME) {
		if (check_defined(block, condition, inst->src))
			analyze_body(block, operand_value(block, condition)->as.boolean ? payload.then_body : payload.else_body);
		return;
	}
	Block arms[2];
	IrBody bodies[2] = {payload.then_body, payload.else_body};
	for (uint32_t i = 0; i < 2; i++) {
		block_init_child(&arms[i], block);
		analyze_body(&arms[i], bodies[i]);
	}
	TirCondbr branch;
	branch.condition = condition.inst;
	branch.then_body = block_finish(&arms[0]);
	branch.else_body = block_finish(&arms[1]);
	TirRef ref = emit_tir(block, TIR_CONDBR, T_NORETURN, inst->src);
	block->tir->insts[ref].data.payload = tir_extra(block->tir, block->sema->arena, &branch, TIR_WORDS(TirCondbr));
}

/* ---------- Analysis: functions and calls ---------- */

/* A `func` instruction: creates the function and evaluates its signature. */
static Operand
analyze_func_inst(Block *block, IrInstIndex index)
{
	Sema *sema = block->sema;
	const IrFile *ir = block->ir;
	const IrInst *inst = &ir->insts[index];
	IrFuncHeader header;
	IR_READ(ir, inst->data.payload, &header);
	const IrInstIndex *params = &ir->extra[inst->data.payload + IR_WORDS(IrFuncHeader)];

	FuncId id = func_create(sema);
	SemaFunc *func = sema->funcs[id];
	func->namespace_scope = block->namespace_scope;
	func->ir = ir;
	func->func_inst = index;
	func->header = header;
	func->name = header.name;
	func->is_export = (header.flags & IR_FUNC_EXPORT) != 0;
	func->is_extern = (header.flags & IR_FUNC_EXTERN) != 0 || (header.flags & IR_FUNC_HAS_BODY) == 0;
	func->state = FUNC_DECLARED;
	func->symbol = func->is_export || func->is_extern ? intern_text(sema->interner, header.name) : func_symbol(sema, header.name, id);

	Type fn_type = make_type(TYPE_FN);
	fn_type.as.fn.is_var_args = (header.flags & IR_FUNC_VAR_ARGS) != 0;
	fn_type.as.fn.ret = T_VOID;
	for (uint32_t i = 0; i < header.param_count; i++) {
		const IrInst *param = &ir->insts[params[i]];
		if (param->op == IR_PARAM_ANYTYPE) {
			func->is_generic = true;
			continue;
		}
		IrParam payload;
		IR_READ(ir, param->data.payload, &payload);
		if (payload.flags & IR_PARAM_COMPTIME)
			func->is_generic = true;
	}
	if (header.callconv_body != 0) {
		sema_error(block, inst->src, "not supported by szc yet: callconv");
		return operand_none;
	}
	if (!func->is_generic) {
		TypeId *param_types = arena_alloc(sema->arena, (header.param_count + 1) * sizeof(TypeId), alignof(TypeId));
		for (uint32_t i = 0; i < header.param_count; i++) {
			const IrInst *param = &ir->insts[params[i]];
			IrParam payload;
			IR_READ(ir, param->data.payload, &payload);
			BodyResult result = analyze_body(block, payload.type_body);
			if (result.end != BODY_BREAK_RESULT || !operand_to_type(block, result.result, param->src, &param_types[i]))
				return operand_none;
			if (is_comptime_only_type(sema, param_types[i]))
				func->is_generic = true;
		}
		fn_type.as.fn.params = param_types;
		fn_type.as.fn.param_count = header.param_count;
	}
	if (!func->is_generic) {
		BodyResult result = analyze_body(block, header.ret_type_body);
		if (result.end != BODY_BREAK_RESULT || !operand_to_type(block, result.result, inst->src, &fn_type.as.fn.ret))
			return operand_none;
		if (is_comptime_only_type(sema, fn_type.as.fn.ret))
			func->is_generic = true;
	}
	if (func->is_generic) {
		fn_type.as.fn.is_generic = true;
		fn_type.as.fn.params = NULL;
		fn_type.as.fn.param_count = 0;
		fn_type.as.fn.ret = T_VOID;
	}
	func->type = type_intern(sema, &fn_type);
	if (func->is_export && !func->is_generic)
		func_queue(sema, id);
	return comptime_operand(func->type, value_function(&sema->values, func->type, id));
}

/* `arg_type call, i`: the result type of argument i while the call is analysed. */
static Operand
analyze_arg_type(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	Operand call;
	if (!ir_ref_is_inst(inst->data.bin.lhs) || !inst_map_get(block->map, ir_ref_inst(inst->data.bin.lhs), &call) || call.kind != OPERAND_PENDING_CALL)
		return operand_none;
	TypeFn fn = sema->types[call.type].as.fn;
	uint32_t index = inst->data.bin.rhs;
	if (index >= fn.param_count) {
		Operand none = operand_none;
		none.kind = OPERAND_NO_RESULT_TYPE;
		return none;
	}
	return type_operand(sema, fn.params[index]);
}

static InstOutcome
analyze_call(Block *block, IrInstIndex index, Operand *out)
{
	Sema *sema = block->sema;
	const IrFile *ir = block->ir;
	const IrInst *inst = &ir->insts[index];
	uint32_t src = inst->src;
	IrCallHeader header;
	IR_READ(ir, inst->data.payload, &header);
	const IrBody *arg_bodies = &ir->extra[inst->data.payload + IR_WORDS(IrCallHeader)];
	*out = operand_none;

	Operand callee = resolve_ref(block, header.callee);
	if (callee.kind == OPERAND_NONE)
		return INST_CONTINUE;
	if (sema->types[callee.type].kind != TYPE_FN) {
		sema_error(block, src, "type '%s' not a function", sema_type_name(sema, callee.type));
		return INST_CONTINUE;
	}
	if (callee.kind != OPERAND_COMPTIME) {
		sema_error(block, src, "not supported by szc yet: calls through function pointers");
		return INST_CONTINUE;
	}
	FuncId id = operand_value(block, callee)->as.function;
	SemaFunc *func = sema->funcs[id];
	if (func->is_generic) {
		sema_error(block, src, "not supported by szc yet: calls of generic functions");
		return INST_CONTINUE;
	}
	TypeFn fn = sema->types[func->type].as.fn;
	bool count_ok = fn.is_var_args ? header.arg_count >= fn.param_count : header.arg_count == fn.param_count;
	if (!count_ok) {
		sema_error(block, src, "expected %s%u argument(s), found %u", fn.is_var_args ? "at least " : "", (unsigned)fn.param_count, (unsigned)header.arg_count);
		return INST_CONTINUE;
	}

	Operand pending = operand_none;
	pending.kind = OPERAND_PENDING_CALL;
	pending.type = func->type;
	inst_map_put(block->map, sema->arena, index, pending);
	Operand *args = arena_alloc(sema->arena, (header.arg_count + 1) * sizeof(Operand), alignof(Operand));
	bool failed = false;
	for (uint32_t i = 0; i < header.arg_count; i++) {
		BodyResult result = analyze_body(block, arg_bodies[i]);
		if (result.end == BODY_NORETURN)
			return INST_NORETURN;
		args[i] = result.result;
		if (result.end != BODY_BREAK_RESULT || args[i].kind == OPERAND_NONE)
			failed = true;
		if (!failed && is_comptime_only_type(sema, args[i].type)) {
			sema_error(block, src, "not supported by szc yet: variadic argument of type '%s'", sema_type_name(sema, args[i].type));
			failed = true;
		}
	}
	if (failed)
		return INST_CONTINUE;
	if (block->is_comptime || header.modifier == IR_CALL_COMPTIME) {
		sema_error(block, src, "not supported by szc yet: comptime function calls");
		return INST_CONTINUE;
	}
	func_queue(sema, id);
	TirRef *refs = arena_alloc(sema->arena, (header.arg_count + 1) * sizeof(TirRef), alignof(TirRef));
	for (uint32_t i = 0; i < header.arg_count; i++)
		refs[i] = materialize(block, args[i], src);
	TirCall call;
	call.callee = id;
	call.arg_count = header.arg_count;
	uint32_t payload = tir_extra(block->tir, sema->arena, &call, TIR_WORDS(TirCall));
	tir_extra(block->tir, sema->arena, refs, header.arg_count);
	TirRef ref = emit_tir(block, TIR_CALL, fn.ret, src);
	block->tir->insts[ref].data.payload = payload;
	if (fn.ret == T_NORETURN) {
		emit_tir(block, TIR_TRAP, T_NORETURN, src);
		return INST_NORETURN;
	}
	*out = runtime_operand(fn.ret, ref);
	return INST_CONTINUE;
}

/* ---------- Analysis: memory ---------- */

/* Known values have immutable backing objects; runtime values have anchored stack slots. */
static Operand
analyze_ref(Block *block, Operand value, uint32_t src, uint32_t identity)
{
	Sema *sema = block->sema;
	if (value.kind == OPERAND_NONE)
		return operand_none;
	TypeKind kind = sema->types[value.type].kind;
	if (kind == TYPE_FN) {
		sema_error(block, src, "not supported by szc yet: addresses of functions");
		return operand_none;
	}
	TypeId pointer = type_pointer(sema, value.type, POINTER_ONE, true);
	if (value.kind == OPERAND_COMPTIME)
		return comptime_operand(pointer, value_ref(&sema->values, pointer, value.value, identity));
	if (block->is_comptime || !require_runtime(block, src)) {
		sema_error(block, src, "unable to evaluate comptime expression");
		return operand_none;
	}
	if (kind != TYPE_INT && kind != TYPE_BOOL && kind != TYPE_POINTER && kind != TYPE_VOID) {
		sema_error(block, src, "not supported by szc yet: address of runtime '%s'", sema_type_name(sema, value.type));
		return operand_none;
	}
	TirRef slot = emit_tir(block, TIR_ALLOC, pointer, src);
	emit_tir_bin(block, TIR_STORE, T_VOID, src, slot, value.inst);
	return runtime_operand(pointer, slot);
}

static Operand
analyze_alloc(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	uint32_t src = inst->src;
	IrAlloc payload;
	IR_READ(block->ir, inst->data.payload, &payload);
	if (block->is_comptime || (payload.flags & IR_ALLOC_COMPTIME) != 0) {
		sema_error(block, src, "not supported by szc yet: comptime variables");
		return operand_none;
	}
	Operand init = resolve_ref(block, payload.init);
	if (init.kind == OPERAND_NONE)
		return operand_none;
	TypeId type = init.type;
	if (payload.type != IR_REF_NONE) {
		if (!operand_to_type(block, resolve_ref(block, payload.type), src, &type))
			return operand_none;
		init = coerce(block, init, type, src);
		if (init.kind == OPERAND_NONE)
			return operand_none;
	}
	if (is_comptime_only_type(sema, type)) {
		sema_error(block, src, "variable of type '%s' must be const or comptime", sema_type_name(sema, type));
		return operand_none;
	}
	if (sema->types[type].kind == TYPE_NORETURN || sema->types[type].kind == TYPE_STRUCT || sema->types[type].kind == TYPE_FN || sema->types[type].kind == TYPE_FLOAT) {
		sema_error(block, src, "not supported by szc yet: variables of type '%s'", sema_type_name(sema, type));
		return operand_none;
	}
	TypeId pointer = type_pointer(sema, type, POINTER_ONE, (payload.flags & IR_ALLOC_CONST) != 0);
	TirRef slot = emit_tir(block, TIR_ALLOC, pointer, src);
	if (!operand_is_undefined(block, init))
		emit_tir_bin(block, TIR_STORE, T_VOID, src, slot, materialize(block, init, src));
	return runtime_operand(pointer, slot);
}

static bool
pointer_type_operand(Block *block, Operand operand, uint32_t src)
{
	Sema *sema = block->sema;
	if (operand.kind == OPERAND_NONE)
		return false;
	if (sema->types[operand.type].kind != TYPE_POINTER) {
		sema_error(block, src, "expected pointer, found '%s'", sema_type_name(sema, operand.type));
		return false;
	}
	return true;
}

static bool
pointer_operand(Block *block, Operand operand, uint32_t src)
{
	if (!pointer_type_operand(block, operand, src))
		return false;
	if (operand.kind != OPERAND_RUNTIME) {
		sema_error(block, src, "not supported by szc yet: comptime pointers");
		return false;
	}
	return true;
}

/* Validate even &p.*: cancelling the address must not hide an invalid dereference. */
static Operand
analyze_deref(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	Operand pointer = resolve_ref(block, inst->data.un.operand);
	if (pointer.kind == OPERAND_NONE)
		return operand_none;
	if (sema->types[pointer.type].kind != TYPE_POINTER) {
		sema_error(block, inst->src, "cannot dereference non-pointer type '%s'", sema_type_name(sema, pointer.type));
		return operand_none;
	}
	TypePointer type = sema->types[pointer.type].as.pointer;
	if (type.size != POINTER_ONE) {
		sema_error(block, inst->src, "not supported by szc yet: non-single pointer dereference");
		return operand_none;
	}
	bool zero_int = is_int(sema, type.child) && sema->types[type.child].as.integer.bits == 0;
	if (operand_is_undefined(block, pointer) && type.child != T_VOID && !zero_int) {
		sema_error(block, inst->src, "cannot dereference undefined value");
		return operand_none;
	}
	if (inst->op == IR_REF_DEREF)
		return pointer;
	if (type.child == T_VOID)
		return void_operand(sema);
	if (zero_int)
		return comptime_operand(type.child, value_int_u64(&sema->values, type.child, 0));
	if (pointer.kind == OPERAND_COMPTIME && operand_value(block, pointer)->kind == VALUE_REF) {
		ConstObjectId object = operand_value(block, pointer)->as.object;
		ValueId pointee = sema->values.objects[object].value;
		TypeId stored_type = value_get(&sema->values, pointee)->type;
		return coerce(block, comptime_operand(stored_type, pointee), type.child, inst->src);
	}
	TypeKind child_kind = sema->types[type.child].kind;
	if (child_kind != TYPE_INT && child_kind != TYPE_BOOL && child_kind != TYPE_POINTER) {
		sema_error(block, inst->src, "not supported by szc yet: dereference of '%s'", sema_type_name(sema, type.child));
		return operand_none;
	}
	if (!pointer_operand(block, pointer, inst->src))
		return operand_none;
	return runtime_operand(type.child, emit_tir_un(block, TIR_LOAD, type.child, inst->src, pointer.inst));
}

/* ---------- Analysis: instructions ---------- */

static Operand
unsupported_inst(Block *block, const IrInst *inst)
{
	sema_error(block, inst->src, "not supported by szc yet: %s", ir_op_name(inst->op));
	return operand_none;
}

static InstOutcome
analyze_inst(Block *block, IrInstIndex index, Operand *break_value)
{
	Sema *sema = block->sema;
	const IrInst *inst = &block->ir->insts[index];
	uint32_t src = inst->src;
	Operand result = operand_none;
	switch (inst->op) {
	case IR_STRUCT_DECL: {
		IrContainerHeader header;
		IR_READ(block->ir, inst->data.payload, &header);
		if ((header.flags & IR_CONTAINER_KIND_MASK) != AST_CONTAINER_STRUCT || header.field_count > 0 || header.arg_body != 0) {
			result = unsupported_inst(block, inst);
			break;
		}
		SemaNamespace *ns = namespace_create(sema, block->namespace_scope, block->ir, index, block->decl_name);
		result = type_operand(sema, ns->type);
		break;
	}
	case IR_FUNC:
		result = analyze_func_inst(block, index);
		break;
	case IR_DECL_REF:
	case IR_DECL_VAL: {
		StringId name = inst->data.name.name;
		SemaDecl *decl = NULL;
		SemaNamespace *ns = block->namespace_scope;
		while (ns != NULL && decl == NULL) {
			decl = namespace_find(ns, name);
			if (decl == NULL)
				ns = ns->parent;
		}
		if (decl == NULL) {
			sema_error(block, src, "use of undeclared identifier '%s'", intern_text(sema->interner, name));
			break;
		}
		result = resolve_decl(block, src, ns, decl);
		if (inst->op == IR_DECL_REF) {
			if (decl->address_identity == 0)
				decl->address_identity = ++sema->next_address_identity;
			result = analyze_ref(block, result, src, decl->address_identity);
		}
		break;
	}
	case IR_INT:
		result = comptime_operand(T_COMPTIME_INT, value_int_u64(&sema->values, T_COMPTIME_INT, inst->data.integer));
		break;
	case IR_INT_BIG: {
		uint32_t payload = inst->data.payload;
		uint32_t limb_count = block->ir->extra[payload];
		BigInt value = bigint_from_limbs(sema->arena, &block->ir->extra[payload + 1], limb_count);
		result = comptime_operand(T_COMPTIME_INT, value_int(&sema->values, T_COMPTIME_INT, &value));
		break;
	}
	case IR_INT_TYPE: {
		bool is_signed = (inst->data.integer & 0x10000u) != 0;
		uint32_t bits = (uint32_t)(inst->data.integer & 0xffffu);
		if (is_signed && bits == 0) {
			sema_error(block, src, "signed integer cannot have bit width 0");
			break;
		}
		result = type_operand(sema, type_int(sema, is_signed, bits));
		break;
	}
	case IR_PTR_TYPE: {
		Operand child = resolve_ref(block, inst->data.bin.lhs);
		TypeId type;
		if (!operand_to_type(block, child, src, &type))
			break;
		if (type == T_NORETURN) {
			sema_error(block, src, "pointer to noreturn not allowed");
			break;
		}
		result = type_operand(sema, type_pointer(sema, type, POINTER_ONE, inst->data.bin.rhs == IR_REF_TRUE));
		break;
	}
	case IR_REF:
		result = analyze_ref(block, resolve_ref(block, inst->data.un.operand), src, 0);
		break;
	case IR_REF_TYPE: {
		Operand operand = resolve_ref(block, inst->data.un.operand);
		if (operand.kind == OPERAND_NO_RESULT_TYPE) {
			result = operand;
			break;
		}
		TypeId type;
		if (!operand_to_type(block, operand, src, &type))
			break;
		if (sema->types[type].kind != TYPE_POINTER) {
			sema_error(block, src, "expected type '%s', found pointer", sema_type_name(sema, type));
			break;
		}
		result = type_operand(sema, sema->types[type].as.pointer.child);
		break;
	}
	case IR_DEREF:
	case IR_REF_DEREF:
		result = analyze_deref(block, inst);
		break;
	case IR_AS:
	case IR_AS_RETURN: {
		Operand dest = resolve_ref(block, inst->data.bin.lhs);
		Operand value = resolve_ref(block, inst->data.bin.rhs);
		if (dest.kind == OPERAND_NO_RESULT_TYPE) {
			result = value;
			break;
		}
		TypeId type;
		if (value.kind == OPERAND_NONE || !operand_to_type(block, dest, src, &type))
			break;
		if (inst->op == IR_AS && type == T_NORETURN) {
			sema_error(block, src, "cannot cast to noreturn");
			break;
		}
		result = inst->op == IR_AS_RETURN ? coerce_return(block, value, type, src) : coerce(block, value, type, src);
		break;
	}
	case IR_STR:
		result = analyze_string(block, inst);
		break;
	case IR_PANIC:
		return analyze_panic(block, inst);
	case IR_DEFER: {
		/* Each exit gets fresh cleanup operands; captured outer refs remain visible. */
		InstMap map;
		inst_map_init(&map, sema->arena);
		InstMap *outer = block->map;
		map.parent = outer;
		block->map = &map;
		BodyResult cleanup = analyze_body(block, inst->data.payload);
		block->map = outer;
		if (cleanup.end == BODY_NORETURN)
			return INST_NORETURN;
		result = void_operand(sema);
		break;
	}
	case IR_ENSURE_RESULT: {
		Operand value = resolve_ref(block, inst->data.un.operand);
		if (value.kind != OPERAND_NONE && value.type != T_VOID && value.type != T_NORETURN)
			sema_error(block, src, "value of type '%s' ignored", sema_type_name(sema, value.type));
		result = void_operand(sema);
		break;
	}
	case IR_FOR_LEN:
		result = analyze_for_len(block, inst);
		break;
	case IR_INT_CAST:
	case IR_TRUNCATE:
		result = analyze_cast(block, inst);
		break;
	case IR_INT_FROM_BOOL:
		result = analyze_int_from_bool(block, inst);
		break;
	case IR_TYPEOF: {
		Operand operand = resolve_ref(block, inst->data.un.operand);
		if (operand.kind != OPERAND_NONE)
			result = type_operand(sema, operand.type);
		break;
	}
	case IR_ELEM_TYPE: {
		Operand pointer = resolve_ref(block, inst->data.un.operand);
		if (pointer_type_operand(block, pointer, src))
			result = type_operand(sema, sema->types[pointer.type].as.pointer.child);
		break;
	}
	case IR_LOG2_INT_TYPE: {
		Operand operand = resolve_ref(block, inst->data.un.operand);
		if (operand.kind == OPERAND_NONE)
			break;
		if (!is_int(sema, operand.type)) {
			sema_error(block, src, "expected integer type, found '%s'", sema_type_name(sema, operand.type));
			break;
		}
		result = type_operand(sema, type_int(sema, false, log2_int_bits(sema->types[operand.type].as.integer.bits)));
		break;
	}
	case IR_RET_TYPE:
		if (block->func == NULL) {
			sema_error(block, src, "'return' outside function scope");
			break;
		}
		result = type_operand(sema, sema->types[block->func->type].as.fn.ret);
		break;
	case IR_ARG_TYPE:
		result = analyze_arg_type(block, inst);
		break;
	case IR_ADD:
	case IR_SUB:
	case IR_MUL:
	case IR_DIV:
	case IR_MOD:
	case IR_DIV_TRUNC:
	case IR_DIV_FLOOR:
	case IR_DIV_CEIL:
	case IR_DIV_EXACT:
	case IR_REM:
	case IR_MOD_BUILTIN:
	case IR_ADD_WRAP:
	case IR_SUB_WRAP:
	case IR_MUL_WRAP:
	case IR_ADD_SAT:
	case IR_SUB_SAT:
	case IR_MUL_SAT:
	case IR_BIT_AND:
	case IR_BIT_OR:
	case IR_BIT_XOR:
		result = analyze_arith(block, inst);
		break;
	case IR_SHL:
	case IR_SHL_SAT:
	case IR_SHR:
		result = analyze_shift(block, inst);
		break;
	case IR_CMP_EQ:
	case IR_CMP_NEQ:
	case IR_CMP_LT:
	case IR_CMP_GT:
	case IR_CMP_LTE:
	case IR_CMP_GTE:
		result = analyze_compare(block, inst);
		break;
	case IR_NEGATE:
	case IR_NEGATE_WRAP:
	case IR_BIT_NOT:
	case IR_BOOL_NOT:
		result = analyze_unary(block, inst);
		break;
	case IR_BOOL_AND:
	case IR_BOOL_OR:
		result = analyze_bool_br(block, inst);
		if (result.type == T_NORETURN)
			return INST_NORETURN;
		break;
	case IR_ALLOC:
		result = analyze_alloc(block, inst);
		break;
	case IR_LOAD: {
		Operand pointer = resolve_ref(block, inst->data.un.operand);
		if (!pointer_operand(block, pointer, src))
			break;
		TypeId child = sema->types[pointer.type].as.pointer.child;
		result = runtime_operand(child, emit_tir_un(block, TIR_LOAD, child, src, pointer.inst));
		break;
	}
	case IR_STORE: {
		Operand pointer = resolve_ref(block, inst->data.bin.lhs);
		Operand value = resolve_ref(block, inst->data.bin.rhs);
		if (!pointer_type_operand(block, pointer, src) || value.kind == OPERAND_NONE)
			break;
		if (sema->types[pointer.type].as.pointer.is_const) {
			sema_error(block, src, "cannot assign to constant");
			break;
		}
		TypeId child = sema->types[pointer.type].as.pointer.child;
		value = coerce(block, value, child, src);
		bool no_storage = child == T_VOID || (is_int(sema, child) && sema->types[child].as.integer.bits == 0);
		if (value.kind != OPERAND_NONE && !no_storage && !operand_is_undefined(block, value) && pointer_operand(block, pointer, src))
			emit_tir_bin(block, TIR_STORE, T_VOID, src, pointer.inst, materialize(block, value, src));
		result = void_operand(sema);
		break;
	}
	case IR_CALL:
		if (analyze_call(block, index, &result) == INST_NORETURN)
			return INST_NORETURN;
		break;
	case IR_IF:
		result = analyze_if(block, inst);
		if (result.type == T_NORETURN)
			return INST_NORETURN;
		break;
	case IR_BLOCK:
		result = analyze_control_block(block, index);
		if (result.type == T_NORETURN)
			return INST_NORETURN;
		break;
	case IR_BREAK:
		analyze_break(block, inst);
		return INST_NORETURN;
	case IR_CONDBR:
		analyze_condbr(block, inst);
		return INST_NORETURN;
	case IR_LOOP:
		analyze_loop(block, index);
		return INST_NORETURN;
	case IR_REPEAT: {
		ControlTarget *target = ir_ref_is_inst(inst->data.un.operand) ? find_control(block, ir_ref_inst(inst->data.un.operand)) : NULL;
		if (target == NULL || !target->is_loop)
			sema_error(block, src, "invalid repeat target");
		else
			emit_tir_un(block, TIR_REPEAT, T_NORETURN, src, target->tir_ref);
		return INST_NORETURN;
	}
	case IR_BREAK_INLINE:
		if (inst->data.brk.block == IR_BODY_RESULT) {
			*break_value = resolve_ref(block, inst->data.brk.operand);
			return INST_BREAK;
		}
		unsupported_inst(block, inst);
		return INST_NORETURN;
	case IR_RET: {
		if (block->func == NULL || block->tir == NULL) {
			sema_error(block, src, "'return' outside function scope");
			return INST_NORETURN;
		}
		Operand value = resolve_ref(block, inst->data.un.operand);
		TypeId ret = sema->types[block->func->type].as.fn.ret;
		value = coerce_return(block, value, ret, src);
		if (value.kind == OPERAND_NONE)
			return INST_NORETURN;
		TirRef operand = ret == T_VOID ? TIR_REF_NONE : materialize(block, value, src);
		emit_tir_un(block, TIR_RET, T_NORETURN, src, operand);
		return INST_NORETURN;
	}
	case IR_RET_IMPLICIT: {
		if (block->func == NULL || block->tir == NULL)
			return INST_NORETURN;
		TypeId ret = sema->types[block->func->type].as.fn.ret;
		if (ret == T_NORETURN) {
			IrBody type_body = block->func->header.ret_type_body;
			uint32_t type_count = ir_body_count(block->ir, type_body);
			uint32_t type_src = type_count == 0 ? src : block->ir->insts[ir_body_insts(block->ir, type_body)[type_count - 1]].src;
			sema_error(block, type_src, "function declared 'noreturn' implicitly returns");
		}
		else if (ret != T_VOID)
			sema_error(block, src, "function with non-void return type '%s' implicitly returns", sema_type_name(sema, ret));
		emit_tir_un(block, TIR_RET, T_NORETURN, src, TIR_REF_NONE);
		return INST_NORETURN;
	}
	case IR_UNREACHABLE:
		if (block->tir == NULL) {
			sema_error(block, src, "reached unreachable code");
			return INST_NORETURN;
		}
		emit_tir(block, TIR_UNREACHABLE, T_NORETURN, src);
		return INST_NORETURN;
	case IR_PARAM:
	case IR_PARAM_ANYTYPE:
		/* Parameters are bound before the function body is analysed. */
		break;
	case IR_FLOAT:
	case IR_ENUM_LITERAL:
		result = unsupported_inst(block, inst);
		break;
	case IR_OP_COUNT:
		break;
	}
	inst_map_put(block->map, sema->arena, index, result);
	return INST_CONTINUE;
}

static BodyResult
analyze_body(Block *block, IrBody body)
{
	BodyResult result;
	result.end = BODY_FALLTHROUGH;
	result.result = operand_none;
	uint32_t count = ir_body_count(block->ir, body);
	const IrInstIndex *insts = ir_body_insts(block->ir, body);
	for (uint32_t i = 0; i < count; i++) {
		Operand value = operand_none;
		InstOutcome outcome = analyze_inst(block, insts[i], &value);
		if (outcome == INST_BREAK) {
			result.end = BODY_BREAK_RESULT;
			result.result = value;
			return result;
		}
		if (outcome == INST_NORETURN) {
			result.end = BODY_NORETURN;
			return result;
		}
	}
	return result;
}

/* Analyses the body of a queued function into its typed IR. */
static void
analyze_func_body(Sema *sema, FuncId id)
{
	SemaFunc *func = sema->funcs[id];
	func->state = FUNC_IN_PROGRESS;
	func->tir.func = id;
	InstMap map;
	inst_map_init(&map, sema->arena);
	Block block;
	block_init_comptime(&block, sema, func->namespace_scope, &map, func->name);
	block.ir = func->ir;
	block.is_comptime = false;
	block.func = func;
	block.tir = &func->tir;
	TypeFn fn = sema->types[func->type].as.fn;
	const IrInstIndex *params = &func->ir->extra[func->ir->insts[func->func_inst].data.payload + IR_WORDS(IrFuncHeader)];
	for (uint32_t i = 0; i < fn.param_count; i++) {
		TirRef arg = emit_tir(&block, TIR_ARG, fn.params[i], func->ir->insts[params[i]].src);
		func->tir.insts[arg].data.index = i;
		inst_map_put(&map, sema->arena, params[i], runtime_operand(fn.params[i], arg));
	}
	BodyResult result = analyze_body(&block, func->header.body);
	if (result.end != BODY_NORETURN)
		emit_tir(&block, TIR_TRAP, T_NORETURN, func->ir->insts[func->func_inst].src);
	func->tir.body = block_finish(&block);
	func->state = FUNC_ANALYSED;
}

/* ---------- Entry points ---------- */

Sema *
sema_create(Arena *arena, Diag *diag, Interner *interner, SourceTable *sources)
{
	Sema *sema = ARENA_NEW(arena, Sema);
	sema->arena = arena;
	sema->diag = diag;
	sema->interner = interner;
	sema->sources = sources;
	sema->type_capacity = 128;
	sema->types = ARENA_ARRAY(arena, Type, sema->type_capacity);
	sema->type_hashes = ARENA_ARRAY(arena, uint32_t, sema->type_capacity);
	sema->type_slot_count = 256;
	sema->type_slots = ARENA_ARRAY(arena, uint32_t, sema->type_slot_count);
	init_builtin_types(sema);
	value_table_init(&sema->values, arena);
	sema->func_capacity = 16;
	sema->funcs = ARENA_ARRAY(arena, SemaFunc *, sema->func_capacity);
	sema->func_count = 1;	/* FuncId 0 is "none" */
	sema->queue_capacity = 16;
	sema->queue = ARENA_ARRAY(arena, FuncId, sema->queue_capacity);
	return sema;
}

/* The file name without directories and `.zig`: the name of the file's struct. */
static StringId
file_struct_name(Sema *sema, FileId file)
{
	const char *path = source_get(sema->sources, file)->path;
	const char *base = strrchr(path, '/');
	base = base == NULL ? path : base + 1;
	size_t length = strlen(base);
	if (length > 4 && strcmp(base + length - 4, ".zig") == 0)
		length = length - 4;
	return intern(sema->interner, base, (uint32_t)length);
}

bool
sema_analyze_executable(Sema *sema, FileId file, const IrFile *ir)
{
	uint32_t errors_before = sema->diag->error_count;
	StringId root_name = file_struct_name(sema, file);
	SemaNamespace *root = namespace_create(sema, NULL, ir, ir->root, root_name);
	InstMap map;
	inst_map_init(&map, sema->arena);
	Block block;
	block_init_comptime(&block, sema, root, &map, root_name);
	uint32_t root_src = ir->insts[ir->root].src;

	/* Exported functions are analysed even if nothing references them. */
	for (uint32_t i = 0; i < root->decl_count; i++) {
		if (root->decls[i].ir.flags & IR_DECL_EXPORT)
			resolve_decl(&block, root->decls[i].ir.src, root, &root->decls[i]);
	}

	StringId main_name = intern_cstr(sema->interner, "main");
	SemaDecl *main_decl = namespace_find(root, main_name);
	if (main_decl == NULL) {
		sema_error(&block, root_src, "root source file struct '%s' has no member named 'main'", intern_text(sema->interner, root_name));
	} else if ((main_decl->ir.flags & IR_DECL_PUB) == 0) {
		sema_error(&block, main_decl->ir.src, "'main' is not marked 'pub'");
	} else {
		Operand main = resolve_decl(&block, main_decl->ir.src, root, main_decl);
		if (main.kind == OPERAND_COMPTIME && sema->types[main.type].kind == TYPE_FN) {
			FuncId id = operand_value(&block, main)->as.function;
			TypeFn fn = sema->types[main.type].as.fn;
			bool ret_ok = fn.ret == T_VOID || fn.ret == T_NORETURN || fn.ret == type_id_of_well_known(IR_REF_TYPE_U8);
			if (fn.is_generic || fn.param_count != 0) {
				sema_error(&block, main_decl->ir.src, "not supported by szc yet: 'main' with parameters");
			} else if (!ret_ok) {
				sema_error(&block, main_decl->ir.src, "expected return type of main to be 'void', '!void', 'noreturn', 'u8', or '!u8'");
			} else {
				sema->main_func = id;
				func_queue(sema, id);
			}
		} else if (main.kind != OPERAND_NONE) {
			sema_error(&block, main_decl->ir.src, "'main' is not a function");
		}
	}

	for (uint32_t i = 0; i < sema->queue_count; i++)
		analyze_func_body(sema, sema->queue[i]);
	return sema->diag->error_count == errors_before;
}

uint32_t
sema_func_count(const Sema *sema)
{
	return sema->func_count;
}

const SemaFunc *
sema_func(const Sema *sema, FuncId func)
{
	return sema->funcs[func];
}

FuncId
sema_main_func(const Sema *sema)
{
	return sema->main_func;
}

const ValueTable *
sema_values(const Sema *sema)
{
	return &sema->values;
}

const Interner *
sema_interner(const Sema *sema)
{
	return sema->interner;
}

/* ---------- Typed IR dump ---------- */

const char *
tir_op_name(TirOp op)
{
	switch (op) {
#define X(name, text) \
	case TIR_##name: \
		return text;
		TIR_OP_LIST(X)
#undef X
	case TIR_OP_COUNT:
		break;
	}
	return "?";
}

static const char *
value_text(Sema *sema, ValueId id)
{
	const Value *value = value_get(&sema->values, id);
	switch (value->kind) {
	case VALUE_VOID:
		return "{}";
	case VALUE_BOOL:
		return value->as.boolean ? "true" : "false";
	case VALUE_INT:
		return bigint_to_string(sema->arena, &value->as.integer);
	case VALUE_TYPE:
		return sema_type_name(sema, value->as.type_value);
	case VALUE_FUNCTION:
		return sema->funcs[value->as.function]->symbol;
	case VALUE_REF: {
		char *text = arena_alloc(sema->arena, 32, 1);
		snprintf(text, 32, "ref(object.%u)", (unsigned)value->as.object);
		return text;
	}
	case VALUE_BYTES: {
		ByteString bytes = value->as.bytes;
		char *text = arena_alloc(sema->arena, (size_t)bytes.length * 4 + 3, 1);
		char *next = text;
		*next++ = '"';
		for (uint32_t i = 0; i < bytes.length; i++) {
			uint8_t c = bytes.data[i];
			if (c == '"' || c == '\\') {
				*next++ = '\\';
				*next++ = (char)c;
			} else if (c >= 0x20 && c < 0x7f) {
				*next++ = (char)c;
			} else {
				snprintf(next, 5, "\\x%02x", (unsigned)c);
				next += 4;
			}
		}
		*next++ = '"';
		*next = '\0';
		return text;
	}
	case VALUE_UNDEFINED:
		return "undefined";
	}
	return "?";
}

static void dump_tir_body(FILE *out, Sema *sema, const TirFunc *tir, TirBody body, uint32_t depth);

static void
dump_tir_inst(FILE *out, Sema *sema, const TirFunc *tir, TirRef ref, uint32_t depth)
{
	const TirInst *inst = &tir->insts[ref];
	for (uint32_t i = 0; i < depth; i++)
		fputs("  ", out);
	fprintf(out, "%%%u: %s = %s", (unsigned)ref, sema_type_name(sema, inst->type), tir_op_name(inst->op));
	switch (inst->op) {
	case TIR_ARG:
		fprintf(out, " %u\n", (unsigned)inst->data.index);
		return;
	case TIR_CONST:
		fprintf(out, " %s\n", value_text(sema, inst->data.value));
		return;
	case TIR_NOT:
	case TIR_NEGATE:
	case TIR_NEGATE_WRAP:
	case TIR_PTR_CAST:
	case TIR_INT_CAST:
	case TIR_TRUNCATE:
	case TIR_INT_FROM_BOOL:
	case TIR_LOAD:
	case TIR_REPEAT:
		fprintf(out, " %%%u\n", (unsigned)inst->data.un.operand);
		return;
	case TIR_RET:
		if (inst->data.un.operand == TIR_REF_NONE)
			fputs("\n", out);
		else
			fprintf(out, " %%%u\n", (unsigned)inst->data.un.operand);
		return;
	case TIR_BR:
		fprintf(out, " %%%u", (unsigned)inst->data.bin.lhs);
		if (inst->data.bin.rhs != TIR_REF_NONE)
			fprintf(out, ", %%%u", (unsigned)inst->data.bin.rhs);
		fputs("\n", out);
		return;
	case TIR_CALL: {
		TirCall call;
		memcpy(&call, &tir->extra[inst->data.payload], sizeof(call));
		fprintf(out, " %s(", sema->funcs[call.callee]->symbol);
		for (uint32_t i = 0; i < call.arg_count; i++)
			fprintf(out, "%s%%%u", i == 0 ? "" : ", ", (unsigned)tir->extra[inst->data.payload + TIR_WORDS(TirCall) + i]);
		fputs(")\n", out);
		return;
	}
	case TIR_BLOCK:
	case TIR_LOOP:
		fputs(" {\n", out);
		dump_tir_body(out, sema, tir, inst->data.payload, depth + 1);
		break;
	case TIR_CONDBR: {
		TirCondbr condbr;
		memcpy(&condbr, &tir->extra[inst->data.payload], sizeof(condbr));
		fprintf(out, " %%%u then {\n", (unsigned)condbr.condition);
		dump_tir_body(out, sema, tir, condbr.then_body, depth + 1);
		for (uint32_t i = 0; i < depth; i++)
			fputs("  ", out);
		fputs("} else {\n", out);
		dump_tir_body(out, sema, tir, condbr.else_body, depth + 1);
		break;
	}
	case TIR_ALLOC:
	case TIR_TRAP:
	case TIR_UNREACHABLE:
		fputs("\n", out);
		return;
	default:
		fprintf(out, " %%%u, %%%u\n", (unsigned)inst->data.bin.lhs, (unsigned)inst->data.bin.rhs);
		return;
	}
	for (uint32_t i = 0; i < depth; i++)
		fputs("  ", out);
	fputs("}\n", out);
}

static void
dump_tir_body(FILE *out, Sema *sema, const TirFunc *tir, TirBody body, uint32_t depth)
{
	uint32_t count = tir->extra[body];
	for (uint32_t i = 0; i < count; i++)
		dump_tir_inst(out, sema, tir, tir->extra[body + 1 + i], depth);
}

void
sema_dump(FILE *out, Sema *sema)
{
	for (FuncId id = 1; id < sema->func_count; id++) {
		const SemaFunc *func = sema->funcs[id];
		if (func->state != FUNC_ANALYSED && !(func->is_extern && func->state == FUNC_QUEUED))
			continue;
		fprintf(out, "%sfn %s: %s", func->is_extern ? "extern " : func->is_export ? "export " : "", func->symbol, sema_type_name(sema, func->type));
		if (func->is_extern) {
			fputs("\n", out);
			continue;
		}
		fputs(" {\n", out);
		dump_tir_body(out, sema, &func->tir, func->tir.body, 1);
		fputs("}\n", out);
	}
}
