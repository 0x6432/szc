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
	DECL_TYPED,	/* mutable global address is known; initializer is still pending */
	DECL_RESOLVED,
	DECL_FAILED,
} DeclState;

typedef enum OperandKind {
	OPERAND_NONE,	/* an error was reported; use as "poison" */
	OPERAND_COMPTIME,	/* value is known */
	OPERAND_RUNTIME,	/* inst is a typed-IR instruction */
	OPERAND_AGGREGATE,	/* array value owned by a private snapshot; inst is its pointer */
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
	TypeId declared_type;	/* published only after both type and alignment resolve */
	uint32_t address_identity;
	uint32_t alignment;	/* immutable declaration storage; 0 means implicit ABI */
	GlobalId global;
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

/* Stable output and private initializer ownership for each addressable global declaration. */
typedef struct Global {
	SemaGlobal result;
	SemaNamespace *namespace_scope;
	SemaDecl *decl;
} Global;

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
	bool *queued_values;	/* immutable value DAG reachability, visited once */
	uint32_t queued_value_capacity;
	uint32_t array_elements;	/* work/storage charged by dense constant construction */

	SemaFunc   **funcs;	/* funcs[0] is unused; each SemaFunc stays at its address */
	uint32_t   func_count;
	uint32_t   func_capacity;
	FuncId     *queue;
	uint32_t   queue_count;
	uint32_t   queue_capacity;
	FuncId     main_func;
	uint32_t next_address_identity;
	Global **globals;	/* stable records, GlobalId 0 unused */
	uint32_t global_count;
	uint32_t global_capacity;
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
	TypeId declaration_type;	/* explicit mutable global initializer context */
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
		hash = hash_word(hash, (uint32_t)type->as.array.length);
		hash = hash_word(hash, (uint32_t)(type->as.array.length >> 32));
		hash = hash_word(hash, type->as.array.sentinel_zero ? 1u : 0u);
		break;
	case TYPE_POINTER:
		hash = hash_word(hash, type->as.pointer.child);
		hash = hash_word(hash, (uint32_t)type->as.pointer.size);
		hash = hash_word(hash, type->as.pointer.alignment);
		hash = hash_word(hash, type->as.pointer.is_const ? 1u : 0u);
		hash = hash_word(hash, type->as.pointer.is_volatile ? 1u : 0u);
		hash = hash_word(hash, type->as.pointer.is_allowzero ? 1u : 0u);
		break;
	case TYPE_FN:
		hash = hash_word(hash, type->as.fn.ret);
		hash = hash_word(hash, (uint32_t)type->as.fn.convention);
		hash = hash_word(hash, type->as.fn.is_var_args ? 1u : 0u);
		hash = hash_word(hash, type->as.fn.is_generic ? 1u : 0u);
		for (uint32_t i = 0; i < type->as.fn.param_count; i++) {
			hash = hash_word(hash, type->as.fn.params[i]);
			hash = hash_word(hash, type->as.fn.noalias[i] ? 1u : 0u);
		}
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
		return a->as.pointer.child == b->as.pointer.child && a->as.pointer.size == b->as.pointer.size && a->as.pointer.alignment == b->as.pointer.alignment && a->as.pointer.is_const == b->as.pointer.is_const && a->as.pointer.is_volatile == b->as.pointer.is_volatile && a->as.pointer.is_allowzero == b->as.pointer.is_allowzero;
	case TYPE_FN:
		if (a->as.fn.convention != b->as.fn.convention || a->as.fn.ret != b->as.fn.ret || a->as.fn.is_var_args != b->as.fn.is_var_args || a->as.fn.is_generic != b->as.fn.is_generic || a->as.fn.param_count != b->as.fn.param_count)
			return false;
		for (uint32_t i = 0; i < a->as.fn.param_count; i++) {
			if (a->as.fn.params[i] != b->as.fn.params[i] || a->as.fn.noalias[i] != b->as.fn.noalias[i])
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
		copy.as.fn.noalias = ARENA_ARRAY(sema->arena, bool, copy.as.fn.param_count);
		memcpy(copy.as.fn.noalias, type->as.fn.noalias, copy.as.fn.param_count * sizeof(bool));
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
type_pointer(Sema *sema, TypeId child, PointerSize size, bool is_const, bool is_allowzero, uint32_t alignment)
{
	Type type = make_type(TYPE_POINTER);
	type.as.pointer.child = child;
	type.as.pointer.size = size;
	type.as.pointer.is_const = is_const;
	type.as.pointer.is_allowzero = is_allowzero;
	type.as.pointer.alignment = alignment;
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
	case TYPE_FN:
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
		return arena_sprintf(sema->arena, "[%" PRIu64 "%s]%s", type->as.array.length, type->as.array.sentinel_zero ? ":0" : "", sema_type_name(sema, type->as.array.child));
	case TYPE_POINTER: {
		const char *prefix = type->as.pointer.size == POINTER_ONE ? "*" : type->as.pointer.size == POINTER_MANY ? "[*]" : type->as.pointer.size == POINTER_SLICE ? "[]" : "[*c]";
		const char *alignment = type->as.pointer.alignment == 0 ? "" : arena_sprintf(sema->arena, "align(%u) ", (unsigned)type->as.pointer.alignment);
		return arena_sprintf(sema->arena, "%s%s%s%s%s%s", prefix, alignment, type->as.pointer.is_allowzero ? "allowzero " : "", type->as.pointer.is_const ? "const " : "", type->as.pointer.is_volatile ? "volatile " : "", sema_type_name(sema, type->as.pointer.child));
	}
	case TYPE_FN: {
		const char *text = "fn (";
		for (uint32_t i = 0; i < type->as.fn.param_count; i++)
			text = arena_sprintf(sema->arena, "%s%s%s%s", text, i == 0 ? "" : ", ", type->as.fn.noalias[i] ? "noalias " : "", sema_type_name(sema, type->as.fn.params[i]));
		const char *convention = type->as.fn.convention == FUNCTION_C ? " callconv(.c)" : type->as.fn.convention == FUNCTION_INLINE ? " callconv(.@\"inline\")" : "";
		return arena_sprintf(sema->arena, "%s%s)%s %s", text, type->as.fn.is_var_args ? (type->as.fn.param_count == 0 ? "..." : ", ...") : "", convention, sema_type_name(sema, type->as.fn.ret));
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

/* Layout overflow is reported to the caller, never replaced by a wrapped size. */
bool
sema_type_size(const Sema *sema, TypeId type_id, uint64_t *out)
{
	const Type *type = &sema->types[type_id];
	uint64_t size = 0;
	switch (type->kind) {
	case TYPE_BOOL:
		size = 1;
		break;
	case TYPE_INT: {
		uint32_t bits = type->as.integer.bits;
		if (bits != 0) {
			size = 1;
			while (size * 8 < bits)
				size *= 2;
		}
		break;
	}
	case TYPE_ARRAY: {
		uint64_t child;
		if (!sema_type_size(sema, type->as.array.child, &child))
			return false;
		uint64_t count = type->as.array.length;
		/* Even maximal zero-bit array types occupy zero bytes. */
		if (child == 0)
			break;
		if (type->as.array.sentinel_zero) {
			if (count == UINT64_MAX)
				return false;
			count++;
		}
		if (count > UINT64_MAX / child)
			return false;
		size = count * child;
		break;
	}
	case TYPE_FLOAT:
		size = type->as.floating.bits / 8;
		break;
	case TYPE_POINTER:
		size = type->as.pointer.size == POINTER_SLICE ? 16 : 8;
		break;
	default:
		break;
	}
	*out = size;
	return true;
}

uint32_t
sema_type_align(const Sema *sema, TypeId type_id)
{
	if (sema->types[type_id].kind == TYPE_ARRAY)
		return sema_type_align(sema, sema->types[type_id].as.array.child);
	uint64_t size;
	if (!sema_type_size(sema, type_id, &size))
		return 1;
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

static void func_queue(Sema *sema, FuncId id);

static void
queue_value_functions(Block *block, ValueId id, uint32_t src)
{
	Sema *sema = block->sema;
	if (id >= sema->queued_value_capacity) {
		uint32_t capacity = sema->values.capacity;
		sema->queued_values = arena_grow_array(sema->arena, sema->queued_values,
			sema->queued_value_capacity, capacity, sizeof(bool), alignof(bool));
		memset(sema->queued_values + sema->queued_value_capacity, 0,
			(capacity - sema->queued_value_capacity) * sizeof(bool));
		sema->queued_value_capacity = capacity;
	}
	if (sema->queued_values[id])
		return;
	sema->queued_values[id] = true;
	const Value *value = value_get(&sema->values, id);
	while (value->kind == VALUE_REF || value->kind == VALUE_ELEMENT_PTR || value->kind == VALUE_PTR_CAST) {
		id = value->kind == VALUE_REF ? sema->values.objects[value->as.object].value
			: value->kind == VALUE_ELEMENT_PTR ? value->as.element.base : value->as.pointer;
		value = value_get(&sema->values, id);
	}
	if (value->kind == VALUE_ARRAY) {
		ArrayValue array = value->as.array;
		for (uint32_t i = 0; i < array.count; i++)
			queue_value_functions(block, array.items[i], src);
		return;
	}
	if (value->kind != VALUE_FUNCTION)
		return;
	if (sema->funcs[value->as.function]->is_generic) {
		sema_error(block, src, "not supported by szc yet: runtime addresses of generic functions");
		return;
	}
	func_queue(sema, value->as.function);
}

/* Turns an operand into a typed-IR instruction (materializing constants). */
static TirRef
materialize(Block *block, Operand operand, uint32_t src)
{
	if (operand.kind == OPERAND_AGGREGATE || operand.kind == OPERAND_NONE
		|| block->sema->types[operand.type].kind == TYPE_ARRAY) {
		sema_error(block, src, "not supported by szc yet: array value in scalar runtime context");
		return TIR_REF_NONE;
	}
	if (operand.kind == OPERAND_RUNTIME)
		return operand.inst;
	queue_value_functions(block, operand.value, src);
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
	/* Like Zig, only validate the machine ABI when a declaration is exported or
	 * used at runtime. A discarded known address need not make an extern live. */
	TypeFn fn = sema->types[func->type].as.fn;
	if (fn.convention == FUNCTION_C) {
		const IrInstIndex *params = &func->ir->extra[func->ir->insts[func->func_inst].data.payload + IR_WORDS(IrFuncHeader)];
		for (uint32_t i = 0; i < fn.param_count; i++) {
			if (sema->types[fn.params[i]].kind == TYPE_ARRAY) {
				diag_report(sema->diag, DIAG_ERROR, func->ir->file, func->ir->insts[params[i]].src,
					"parameter of type '%s' not allowed in function with calling convention 'x86_64_sysv'", sema_type_name(sema, fn.params[i]));
				func->state = FUNC_FAILED;
				return;
			}
		}
		if (sema->types[fn.ret].kind == TYPE_ARRAY) {
			diag_report(sema->diag, DIAG_ERROR, func->ir->file, func->ir->insts[func->func_inst].src,
				"return type '%s' not allowed in function with calling convention 'x86_64_sysv'", sema_type_name(sema, fn.ret));
			func->state = FUNC_FAILED;
			return;
		}
	}
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

static bool operand_to_type(Block *block, Operand operand, uint32_t src, TypeId *out);
static bool require_runtime(Block *block, uint32_t src);
static bool analyze_alignment(Block *block, IrBody body, uint32_t src, uint32_t *out);

static TirRef array_slot(Block *block, TypeId pointer, uint32_t src);
static Operand array_snapshot(Block *block, Operand source, uint32_t alignment, uint32_t src);
static Operand analyze_ref(Block *block, Operand value, uint32_t src, uint32_t identity, uint32_t alignment);
static bool store_array(Block *block, Operand pointer, Operand value, uint32_t src);
static bool array_storage_allowed(Sema *sema, TypeId type);

/* Until repeated/lazy aggregate values land, bound dense compile-time array work. */
static bool
reserve_array_elements(Block *block, uint32_t count, uint32_t src)
{
	enum { CONSTANT_ARRAY_ELEMENT_BUDGET = 1048576 };
	if (count > CONSTANT_ARRAY_ELEMENT_BUDGET - block->sema->array_elements) {
		sema_error(block, src, "not supported by szc yet: constant array element budget exceeded (1048576)");
		return false;
	}
	block->sema->array_elements += count;
	return true;
}

static bool
global_type_allowed(Block *block, TypeId type, uint32_t src)
{
	Sema *sema = block->sema;
	if (is_comptime_only_type(sema, type)) {
		sema_error(block, src, "variable of type '%s' must be const or comptime", sema_type_name(sema, type));
		return false;
	}
	TypeKind kind = sema->types[type].kind;
	if (kind == TYPE_ARRAY) {
		if (!array_storage_allowed(sema, type)) {
			sema_error(block, src, "not supported by szc yet: global array storage layout");
			return false;
		}
		uint64_t size;
		if (!sema_type_size(sema, type, &size)) {
			sema_error(block, src, "not supported by szc yet: array layout size exceeds 64-bit address space");
			return false;
		}
		return true;
	}
	if (kind != TYPE_INT && kind != TYPE_BOOL && kind != TYPE_POINTER && kind != TYPE_VOID) {
		sema_error(block, src, "not supported by szc yet: global variables of type '%s'", sema_type_name(sema, type));
		return false;
	}
	return true;
}

/* Publish a stable address after its type and alignment have resolved. */
static void
create_global(Sema *sema, SemaNamespace *ns, SemaDecl *decl, TypeId type, uint32_t alignment)
{
	if (sema->global_count == sema->global_capacity) {
		uint32_t capacity = sema->global_capacity * 2;
		sema->globals = arena_grow_array(sema->arena, sema->globals, sema->global_count, capacity, sizeof(Global *), alignof(Global *));
		sema->global_capacity = capacity;
	}
	GlobalId id = sema->global_count++;
	Global *entry = ARENA_NEW(sema->arena, Global);
	entry->decl = decl;
	entry->namespace_scope = ns;
	sema->globals[id] = entry;
	SemaGlobal *global = &entry->result;
	global->type = type;
	global->is_const = decl->ir.kind == IR_DECL_CONST;
	global->alignment = alignment;
	global->symbol = arena_sprintf(sema->arena, "szc.g.%u", (unsigned)id);
	global->src = decl->ir.src;
	global->file = ns->file;
	decl->global = id;
	if (!global->is_const) {
		TypeId pointer = type_pointer(sema, type, POINTER_ONE, false, false, alignment);
		decl->value = comptime_operand(pointer, value_global(&sema->values, pointer, id));
	}
}

/* Only these runtime storage types currently have scalar codegen. */
static bool
constant_storage_type(Sema *sema, TypeId type)
{
	TypeKind kind = sema->types[type].kind;
	return kind == TYPE_INT || kind == TYPE_BOOL || kind == TYPE_POINTER || kind == TYPE_VOID;
}

/* A declaration address is independent of its initializer, unlike an anonymous value ref. */
static Operand
constant_decl_address(Block *user, SemaNamespace *ns, SemaDecl *decl)
{
	Sema *sema = user->sema;
	if (decl->global == 0)
		create_global(sema, ns, decl, decl->declared_type, decl->alignment);
	SemaGlobal *global = &sema->globals[decl->global]->result;
	if (decl->state == DECL_RESOLVED) {
		global->initializer = decl->value.value;
		queue_value_functions(user, decl->value.value, decl->ir.src);
	}
	TypeId pointer = type_pointer(sema, global->type, POINTER_ONE, true, false, global->alignment);
	return comptime_operand(pointer, value_global(&sema->values, pointer, decl->global));
}

/* Analyses the value of a container-level declaration (once). */
static Operand
resolve_decl(Block *user, uint32_t src, SemaNamespace *ns, SemaDecl *decl, bool as_reference)
{
	Sema *sema = user->sema;
	if (as_reference && decl->state != DECL_FAILED && decl->ir.kind == IR_DECL_CONST
		&& decl->declared_type != 0 && constant_storage_type(sema, decl->declared_type))
		return constant_decl_address(user, ns, decl);
	switch (decl->state) {
	case DECL_RESOLVED:
		return decl->value;
	case DECL_TYPED:
		if (as_reference)
			return decl->value;
		break;
	case DECL_FAILED:
		return operand_none;
	case DECL_IN_PROGRESS:
		if (decl->ir.kind == IR_DECL_VAR) {
			if (as_reference && decl->global != 0)
				return decl->value;
			sema_error(user, src, "value of declaration '%s.%s' depends on itself here", sema_type_name(sema, ns->type), intern_text(sema->interner, decl->ir.name));
		} else {
			sema_error(user, src, "dependency loop detected");
		}
		return operand_none;
	case DECL_UNRESOLVED:
		break;
	}
	bool mutable = decl->ir.kind == IR_DECL_VAR;
	bool aligned_const = decl->ir.kind == IR_DECL_CONST && decl->ir.alignment_body != 0;
	if (mutable && (decl->ir.flags & (IR_DECL_THREADLOCAL | IR_DECL_EXPORT | IR_DECL_EXTERN)) != 0) {
		decl->state = DECL_FAILED;
		sema_error(user, src, "not supported by szc yet: threadlocal, exported, or extern global variables");
		return operand_none;
	}
	if (aligned_const && (decl->ir.flags & (IR_DECL_THREADLOCAL | IR_DECL_EXPORT | IR_DECL_EXTERN)) != 0) {
		decl->state = DECL_FAILED;
		sema_error(user, src, "not supported by szc yet: threadlocal, exported, or extern aligned global constants");
		return operand_none;
	}
	decl->state = DECL_IN_PROGRESS;
	InstMap map;
	inst_map_init(&map, sema->arena);
	Block block;
	block_init_comptime(&block, sema, ns, &map, decl->ir.name);
	uint32_t alignment = 0;
	if (decl->ir.kind == IR_DECL_CONST && decl->ir.type_body != 0 && decl->declared_type == 0) {
		BodyResult declared = analyze_body(&block, decl->ir.type_body);
		TypeId type;
		if (declared.end != BODY_BREAK_RESULT || !operand_to_type(&block, declared.result, decl->ir.src, &type)
			|| !analyze_alignment(&block, decl->ir.alignment_body, decl->ir.alignment_src, &alignment)) {
			decl->state = DECL_FAILED;
			return operand_none;
		}
		decl->declared_type = type;
		decl->alignment = alignment;
	}
	if (decl->ir.kind == IR_DECL_CONST && decl->declared_type != 0) {
		block.declaration_type = decl->declared_type;
		alignment = decl->alignment;
		if (as_reference && constant_storage_type(sema, decl->declared_type)) {
			decl->state = DECL_TYPED;
			return constant_decl_address(&block, ns, decl);
		}
	}
	if (mutable && decl->global == 0) {
		TypeId type = 0;
		if (decl->ir.type_body != 0) {
			BodyResult declared = analyze_body(&block, decl->ir.type_body);
			if (declared.end != BODY_BREAK_RESULT || !operand_to_type(&block, declared.result, decl->ir.src, &type) || !global_type_allowed(&block, type, decl->ir.src)) {
				decl->state = DECL_FAILED;
				return operand_none;
			}
		}
		if (type != 0) {
			if (!analyze_alignment(&block, decl->ir.alignment_body, decl->ir.alignment_src, &alignment)) {
				decl->state = DECL_FAILED;
				return operand_none;
			}
			create_global(sema, ns, decl, type, alignment);
		}
	}
	if (mutable && decl->global != 0) {
		block.declaration_type = sema->globals[decl->global]->result.type;
		if (as_reference) {
			decl->state = DECL_TYPED;
			return decl->value;
		}
	}
	BodyResult result = analyze_body(&block, decl->ir.value_body);
	if (result.end != BODY_BREAK_RESULT || result.result.kind != OPERAND_COMPTIME) {
		if (result.result.kind == OPERAND_RUNTIME)
			sema_error(&block, decl->ir.src, "unable to resolve comptime value");
		decl->state = DECL_FAILED;
		return operand_none;
	}
	if (mutable) {
		if (decl->global == 0) {
			if (!global_type_allowed(&block, result.result.type, decl->ir.src)) {
				decl->state = DECL_FAILED;
				return operand_none;
			}
			if (!analyze_alignment(&block, decl->ir.alignment_body, decl->ir.alignment_src, &alignment)) {
				decl->state = DECL_FAILED;
				return operand_none;
			}
			create_global(sema, ns, decl, result.result.type, alignment);
		}
		sema->globals[decl->global]->result.initializer = result.result.value;
		queue_value_functions(&block, result.result.value, decl->ir.src);
	} else {
		if (aligned_const) {
			if (decl->ir.type_body == 0 && !analyze_alignment(&block, decl->ir.alignment_body, decl->ir.alignment_src, &alignment)) {
				decl->state = DECL_FAILED;
				return operand_none;
			}
			if (is_comptime_only_type(sema, result.result.type)) {
				const char *reason = sema->types[result.result.type].kind == TYPE_FN ? "function alias" : "comptime-only type";
				sema_error(&block, decl->ir.alignment_src, "cannot specify alignment of %s", reason);
				decl->state = DECL_FAILED;
				return operand_none;
			}
			decl->alignment = alignment;
		}
		decl->value = result.result;
		if (decl->global != 0) {
			sema->globals[decl->global]->result.initializer = result.result.value;
			queue_value_functions(&block, result.result.value, decl->ir.src);
		}
	}
	decl->state = DECL_RESOLVED;
	if (as_reference && decl->ir.kind == IR_DECL_CONST && decl->declared_type != 0
		&& constant_storage_type(sema, decl->declared_type))
		return constant_decl_address(user, ns, decl);
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

static uint32_t
pointer_alignment(const Sema *sema, TypePointer pointer)
{
	return pointer.alignment != 0 ? pointer.alignment : sema_type_align(sema, pointer.child);
}

/* Memory-compatible types: mutable outer pointees require bidirectional coercions. */
static bool
memory_coercion_allowed(const Sema *sema, TypeId dest, TypeId source, bool dest_is_mut)
{
	if (dest == source)
		return true;
	if (sema->types[dest].kind == TYPE_INT && sema->types[source].kind == TYPE_INT)
		return sema->types[dest].as.integer.bits == sema->types[source].as.integer.bits && sema->types[dest].as.integer.is_signed == sema->types[source].as.integer.is_signed;
	if (sema->types[dest].kind == TYPE_ARRAY && sema->types[source].kind == TYPE_ARRAY) {
		TypeArray to = sema->types[dest].as.array;
		TypeArray from = sema->types[source].as.array;
		return to.length == from.length && (!to.sentinel_zero || from.sentinel_zero)
			&& (!dest_is_mut || to.sentinel_zero == from.sentinel_zero)
			&& memory_coercion_allowed(sema, to.child, from.child, dest_is_mut);
	}
	if (sema->types[dest].kind == TYPE_FN && sema->types[source].kind == TYPE_FN) {
		TypeFn to = sema->types[dest].as.fn;
		TypeFn from = sema->types[source].as.fn;
		if (to.convention != from.convention || to.is_var_args != from.is_var_args || to.is_generic != from.is_generic || to.param_count != from.param_count)
			return false;
		if (!(from.ret == T_NORETURN && !dest_is_mut) && !memory_coercion_allowed(sema, to.ret, from.ret, dest_is_mut))
			return false;
		for (uint32_t i = 0; i < to.param_count; i++) {
			if (to.noalias[i] != from.noalias[i] || !memory_coercion_allowed(sema, from.params[i], to.params[i], dest_is_mut))
				return false;
		}
		return true;
	}
	if (sema->types[dest].kind != TYPE_POINTER || sema->types[source].kind != TYPE_POINTER)
		return false;
	TypePointer to = sema->types[dest].as.pointer;
	TypePointer from = sema->types[source].as.pointer;
	if (to.size != from.size) {
		if (dest_is_mut || to.size != POINTER_MANY || from.size != POINTER_ONE
			|| sema->types[from.child].kind != TYPE_ARRAY)
			return false;
		from.child = sema->types[from.child].as.array.child;
	}
	if (to.is_volatile != from.is_volatile && (dest_is_mut || !to.is_volatile))
		return false;
	if (to.is_allowzero != from.is_allowzero && (dest_is_mut || !to.is_allowzero))
		return false;
	if (to.is_const != from.is_const && (dest_is_mut || !to.is_const))
		return false;
	uint32_t to_align = pointer_alignment(sema, to);
	uint32_t from_align = pointer_alignment(sema, from);
	if (dest_is_mut ? to_align != from_align : to_align > from_align)
		return false;
	return memory_coercion_allowed(sema, to.child, from.child, dest_is_mut || !to.is_const);
}

static TypeId
callable_type(const Sema *sema, TypeId type)
{
	if (sema->types[type].kind == TYPE_FN)
		return type;
	if (sema->types[type].kind != TYPE_POINTER)
		return 0;
	TypePointer pointer = sema->types[type].as.pointer;
	return pointer.size == POINTER_ONE && sema->types[pointer.child].kind == TYPE_FN ? pointer.child : 0;
}

static Operand
function_address(Block *block, Operand value, uint32_t src)
{
	Sema *sema = block->sema;
	TypeId pointer = type_pointer(sema, value.type, POINTER_ONE, true, false, 0);
	if (operand_is_undefined(block, value))
		return comptime_operand(pointer, value_undefined(&sema->values, pointer));
	if (value.kind != OPERAND_COMPTIME || operand_value(block, value)->kind != VALUE_FUNCTION) {
		sema_error(block, src, "cannot load comptime-only type '%s'", sema_type_name(sema, value.type));
		return operand_none;
	}
	FuncId id = operand_value(block, value)->as.function;
	if ((sema->funcs[id]->header.flags & IR_FUNC_INLINE) != 0) {
		sema_error(block, src, "not supported by szc yet: addresses of inline functions");
		return operand_none;
	}
	pointer = type_pointer(sema, value.type, POINTER_ONE, true, false, sema->funcs[id]->alignment);
	return comptime_operand(pointer, value_function(&sema->values, pointer, id));
}

/* Coerces `operand` to type `dest` (Zig's implicit coercions that szc supports so far). */
static Operand
coerce(Block *block, Operand operand, TypeId dest, uint32_t src)
{
	Sema *sema = block->sema;
	if (operand.kind == OPERAND_NONE || operand.type == dest || operand.type == T_NORETURN)
		return operand;
	TypeKind dest_kind = sema->types[dest].kind;
	if (operand.kind == OPERAND_AGGREGATE && dest_kind == TYPE_ARRAY) {
		TypeArray target = sema->types[dest].as.array;
		TypeArray source = sema->types[operand.type].as.array;
		if (target.child == source.child && target.length == source.length
			&& (!target.sentinel_zero || source.sentinel_zero)) {
			TypeId pointer = type_pointer(sema, dest, POINTER_ONE, true, false, 0);
			operand.inst = emit_tir_un(block, TIR_PTR_CAST, pointer, src, operand.inst);
			operand.type = dest;
			return operand;
		}
	}
	if (dest_kind == TYPE_POINTER && sema->types[operand.type].kind == TYPE_FN) {
		operand = function_address(block, operand, src);
		if (operand.kind == OPERAND_NONE)
			return operand_none;
		if (operand.type == dest)
			return operand;
	}
	if (operand.kind == OPERAND_COMPTIME) {
		const Value *value = operand_value(block, operand);
		if (dest_kind == TYPE_ARRAY && sema->types[operand.type].kind == TYPE_ARRAY) {
			TypeArray target = sema->types[dest].as.array;
			TypeArray source = sema->types[operand.type].as.array;
			if (target.child == source.child && target.length == source.length
				&& (!target.sentinel_zero || source.sentinel_zero)) {
				if (value->kind == VALUE_UNDEFINED)
					return comptime_operand(dest, value_undefined(&sema->values, dest));
				if (value->kind == VALUE_ARRAY) {
					ArrayValue array = value->as.array;
					uint32_t count = array.count - (source.sentinel_zero && !target.sentinel_zero ? 1u : 0u);
					if (!reserve_array_elements(block, count, src))
						return operand_none;
					return comptime_operand(dest, value_array(&sema->values, dest, array.items, count));
				}
			}
		}
		if (value->kind == VALUE_UNDEFINED && (operand.type == T_UNDEFINED
			|| (dest_kind != TYPE_ARRAY && sema->types[operand.type].kind != TYPE_ARRAY))) {
			bool pointer_conversion = dest_kind == TYPE_POINTER || sema->types[operand.type].kind == TYPE_POINTER;
			if (!pointer_conversion || operand.type == type_id_of_well_known(IR_REF_TYPE_UNDEFINED) || memory_coercion_allowed(sema, dest, operand.type, false))
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
	if (operand.kind == OPERAND_COMPTIME && dest_kind == TYPE_FN && sema->types[operand.type].kind == TYPE_FN && memory_coercion_allowed(sema, dest, operand.type, false)) {
		Value value = *operand_value(block, operand);
		value.type = dest;
		return comptime_operand(dest, value_intern(&sema->values, &value));
	}
	if (dest_kind == TYPE_POINTER && memory_coercion_allowed(sema, dest, operand.type, false)) {
		if (operand.kind == OPERAND_RUNTIME) {
			if (!require_runtime(block, src))
				return operand_none;
			return runtime_operand(dest, emit_tir_un(block, TIR_PTR_CAST, dest, src, operand.inst));
		}
		const Value *known = operand_value(block, operand);
		if (known->kind == VALUE_BYTES || known->kind == VALUE_PTR_CAST
			|| sema->types[dest].as.pointer.size != sema->types[operand.type].as.pointer.size)
			return comptime_operand(dest, value_pointer_cast(&sema->values, dest, operand.value));
		Value value = *known;
		value.type = dest;
		return comptime_operand(dest, value_intern(&sema->values, &value));
	}
	if (operand.kind == OPERAND_RUNTIME && dest_kind == TYPE_INT && is_int(sema, operand.type) && int_type_contains(sema, dest, operand.type)) {
		if (!require_runtime(block, src))
			return operand_none;
		return runtime_operand(dest, emit_tir_un(block, TIR_INT_CAST, dest, src, operand.inst));
	}
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
		if (!require_runtime(block, src))
			return operand_none;
		return runtime_operand(type, emit_tir_un(block, TIR_TRUNCATE, type, src, operand.inst));
	}
	if (operand.kind == OPERAND_COMPTIME)
		return coerce(block, operand, type, src);
	if (type == T_COMPTIME_INT) {
		sema_error(block, src, "unable to cast runtime value to 'comptime_int'");
		return operand_none;
	}
	if (!require_runtime(block, src))
		return operand_none;
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
	if (!require_runtime(block, inst->src))
		return operand_none;
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
	if (ak == TYPE_FN && bk == TYPE_POINTER && sema->types[b].as.pointer.is_const && callable_type(sema, b) != 0) {
		Operand address = function_address(block, lhs, src);
		return address.kind != OPERAND_NONE && peer_type(block, address, rhs, src, out);
	}
	if (bk == TYPE_FN && ak == TYPE_POINTER && sema->types[a].as.pointer.is_const && callable_type(sema, a) != 0) {
		Operand address = function_address(block, rhs, src);
		return address.kind != OPERAND_NONE && peer_type(block, lhs, address, src, out);
	}
	if (ak == TYPE_FN && bk == TYPE_FN) {
		if (memory_coercion_allowed(sema, a, b, false)) {
			*out = a;
			return true;
		}
		if (memory_coercion_allowed(sema, b, a, false)) {
			*out = b;
			return true;
		}
	}
	if (ak == TYPE_POINTER && bk == TYPE_POINTER) {
		TypePointer ap = sema->types[a].as.pointer;
		TypePointer bp = sema->types[b].as.pointer;
		bool many_array = (ap.size == POINTER_MANY && bp.size == POINTER_ONE && sema->types[bp.child].kind == TYPE_ARRAY)
			|| (bp.size == POINTER_MANY && ap.size == POINTER_ONE && sema->types[ap.child].kind == TYPE_ARRAY);
		if (ap.size == bp.size || many_array) {
			Type candidate = make_type(TYPE_POINTER);
			candidate.as.pointer = ap;
			if (many_array) {
				candidate.as.pointer.size = POINTER_MANY;
				candidate.as.pointer.child = ap.size == POINTER_MANY ? ap.child : bp.child;
			}
			candidate.as.pointer.is_const = ap.is_const || bp.is_const;
			candidate.as.pointer.is_volatile = ap.is_volatile || bp.is_volatile;
			candidate.as.pointer.is_allowzero = ap.is_allowzero || bp.is_allowzero;
			if (ap.alignment != 0 || bp.alignment != 0) {
				uint32_t a_align = pointer_alignment(sema, ap);
				uint32_t b_align = pointer_alignment(sema, bp);
				candidate.as.pointer.alignment = a_align < b_align ? a_align : b_align;
			}
			TypeId merged = type_intern(sema, &candidate);
			if (memory_coercion_allowed(sema, merged, a, false) && memory_coercion_allowed(sema, merged, b, false)) {
				*out = merged;
				return true;
			}
			candidate.as.pointer.child = many_array ? candidate.as.pointer.child : bp.child;
			merged = type_intern(sema, &candidate);
			if (memory_coercion_allowed(sema, merged, a, false) && memory_coercion_allowed(sema, merged, b, false)) {
				*out = merged;
				return true;
			}
		}
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

/* Root identity is independent of pointer qualifiers and element paths. */
static bool
pointer_same_root(const Sema *sema, PointerAddress left, PointerAddress right)
{
	const Value *a = value_get(&sema->values, left.root);
	const Value *b = value_get(&sema->values, right.root);
	if (a->kind != b->kind)
		return false;
	switch (a->kind) {
	case VALUE_FUNCTION:
		return a->as.function == b->as.function;
	case VALUE_GLOBAL:
		return a->as.global == b->as.global;
	case VALUE_REF:
		return a->as.object == b->as.object;
	case VALUE_BYTES:
		return a->as.bytes.length == b->as.bytes.length
			&& (a->as.bytes.length == 0 || memcmp(a->as.bytes.data, b->as.bytes.data, a->as.bytes.length) == 0);
	default:
		return false;
	}
}

static bool check_defined(Block *block, Operand operand, uint32_t src);
static bool array_element_stride(Sema *sema, TypeId child, uint64_t *stride, bool *logical);
static TypeId array_element_pointer_type(Sema *sema, TypePointer pointer, TypeArray array, Operand index, uint64_t stride, bool logical);

/* Zig 0.17 subtracts single array pointers in units of their array elements. */
static TypeId
pointer_difference_child(const Sema *sema, TypePointer pointer)
{
	if (pointer.size == POINTER_ONE && sema->types[pointer.child].kind == TYPE_ARRAY)
		return sema->types[pointer.child].as.array.child;
	return pointer.child;
}

static Operand
analyze_pointer_difference(Block *block, Operand lhs, Operand rhs, uint32_t src)
{
	Sema *sema = block->sema;
	TypeId child = pointer_difference_child(sema, sema->types[lhs.type].as.pointer);
	TypeId other = pointer_difference_child(sema, sema->types[rhs.type].as.pointer);
	if (child != other) {
		sema_error(block, src, "incompatible pointer arithmetic operands '%s' and '%s'", sema_type_name(sema, lhs.type), sema_type_name(sema, rhs.type));
		return operand_none;
	}
	uint64_t stride;
	if (!sema_type_size(sema, child, &stride)) {
		sema_error(block, src, "not supported by szc yet: pointer subtraction with element type '%s'", sema_type_name(sema, child));
		return operand_none;
	}
	if (stride == 0) {
		sema_error(block, src, "pointer subtraction requires element type '%s' to have runtime bits", sema_type_name(sema, child));
		return operand_none;
	}
	if (!check_defined(block, lhs, src) || !check_defined(block, rhs, src))
		return operand_none;
	if (lhs.kind == OPERAND_COMPTIME && rhs.kind == OPERAND_COMPTIME) {
		PointerAddress left = value_pointer_address(&sema->values, lhs.value);
		PointerAddress right = value_pointer_address(&sema->values, rhs.value);
		if (pointer_same_root(sema, left, right)) {
			if (left.offset < right.offset) {
				sema_error(block, src, "operation results in overflow");
				return operand_none;
			}
			return comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, (left.offset - right.offset) / stride));
		}
	}
	if (!require_runtime(block, src))
		return operand_none;
	TirRef left = materialize(block, lhs, src);
	TirRef right = materialize(block, rhs, src);
	left = emit_tir_un(block, TIR_PTR_TO_INT, T_USIZE, src, left);
	right = emit_tir_un(block, TIR_PTR_TO_INT, T_USIZE, src, right);
	TirRef difference = emit_tir_bin(block, TIR_SUB_WRAP, T_USIZE, src, left, right);
	Operand size = comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, stride));
	TirRef divisor = materialize(block, size, src);
	return runtime_operand(T_USIZE, emit_tir_bin(block, TIR_DIV_EXACT, T_USIZE, src, difference, divisor));
}

/* Offsets are usize; arithmetic borrows a root and never reads its contents. */
static Operand
analyze_pointer_offset(Block *block, IrOp op, Operand base, Operand offset, uint32_t src)
{
	Sema *sema = block->sema;
	if (op != IR_ADD && op != IR_SUB) {
		sema_error(block, src, "invalid pointer-integer arithmetic operator");
		return operand_none;
	}
	offset = coerce(block, offset, T_USIZE, src);
	if (offset.kind == OPERAND_NONE || !check_defined(block, offset, src))
		return operand_none;
	TypePointer shape = sema->types[base.type].as.pointer;
	uint64_t stride;
	bool logical;
	if (!array_element_stride(sema, shape.child, &stride, &logical)) {
		sema_error(block, src, "not supported by szc yet: pointer element offset exceeds 64-bit address space");
		return operand_none;
	}
	if (stride == 0)
		return base;
	TypeArray element = {shape.child, 0, false};
	TypeId single = array_element_pointer_type(sema, shape, element, offset, stride, logical);
	Type result_type = sema->types[single];
	result_type.as.pointer.size = POINTER_MANY;
	TypeId type = type_intern(sema, &result_type);
	if (operand_is_undefined(block, base))
		return comptime_operand(type, value_undefined(&sema->values, type));
	if (base.kind == OPERAND_COMPTIME && offset.kind == OPERAND_COMPTIME) {
		uint64_t index = bigint_to_u64_bits(&operand_value(block, offset)->as.integer);
		if (index == 0)
			return base;
		PointerAddress address = value_pointer_address(&sema->values, base.value);
		/* Upstream subtraction uses ABI bytes, even for logical comptime pointers. */
		if (op == IR_SUB && logical)
			return comptime_operand(type, value_pointer_cast(&sema->values, type, base.value));
		if (index > UINT64_MAX / stride || (op == IR_ADD && index * stride > UINT64_MAX - address.offset)) {
			sema_error(block, src, "not supported by szc yet: pointer element offset exceeds 64-bit address space");
			return operand_none;
		}
		uint64_t delta = index * stride;
		if (op == IR_SUB && delta > address.offset) {
			sema_error(block, src, "pointer computation here causes illegal behavior");
			return operand_none;
		}
		uint64_t result = op == IR_SUB ? address.offset - delta : address.offset + delta;
		return comptime_operand(type, value_element_pointer(&sema->values, type, base.value, index, result, logical));
	}
	if (!require_runtime(block, src))
		return operand_none;
	if (logical) {
		sema_error(block, src, "not supported by szc yet: runtime arithmetic on comptime-only pointers");
		return operand_none;
	}
	TirRef pointer = materialize(block, base, src);
	TirRef index = materialize(block, offset, src);
	return runtime_operand(type, emit_tir_bin(block, op == IR_SUB ? TIR_PTR_SUB : TIR_PTR_ADD, type, src, pointer, index));
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
	if (sema->types[lhs.type].kind == TYPE_POINTER) {
		TypePointer pointer = sema->types[lhs.type].as.pointer;
		if (sema->types[rhs.type].kind == TYPE_POINTER
			&& pointer.size != POINTER_SLICE && sema->types[rhs.type].as.pointer.size != POINTER_SLICE) {
			if (op != IR_SUB) {
				sema_error(block, src, "invalid pointer-pointer arithmetic operator");
				return operand_none;
			}
			return analyze_pointer_difference(block, lhs, rhs, src);
		}
		if (pointer.size == POINTER_MANY)
			return analyze_pointer_offset(block, op, lhs, rhs, src);
	}
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
			PointerAddress left = value_pointer_address(&sema->values, lhs.value);
			PointerAddress right = value_pointer_address(&sema->values, rhs.value);
			bool equal = pointer_same_root(sema, left, right) && left.offset == right.offset
				&& (left.offset == 0 || left.logical == right.logical);
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
		if (!require_runtime(block, src))
			return operand_none;
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
		if (!require_runtime(block, src))
			return operand_none;
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
	if (!require_runtime(block, src))
		return operand_none;
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
	TypeId pointer = type_pointer(sema, child, POINTER_ONE, true, false, 0);
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
	uint64_t length = array->as.array.length;
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
	if (!require_runtime(block, src))
		return operand_none;
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
	bool array = !known && sema->types[type].kind == TYPE_ARRAY;
	TirRef array_result = TIR_REF_NONE;
	if (array) {
		TypeId pointer = type_pointer(sema, type, POINTER_ONE, true, false, 0);
		array_result = array_slot(block, pointer, src);
		if (array_result == TIR_REF_NONE)
			return operand_none;
	}
	TypeId merge_type = known || array ? T_VOID : type;
	block->tir->insts[merge].type = merge_type;
	for (uint32_t i = 0; i < 2; i++) {
		if (exits[i].end != BODY_BREAK_RESULT)
			continue;
		Operand operand = coerce(&arms[i], exits[i].result, type, src);
		if (operand.kind == OPERAND_NONE)
			return operand_none;
		if (array) {
			Operand destination = runtime_operand(block->tir->insts[array_result].type, array_result);
			if (!store_array(&arms[i], destination, operand, src))
				return operand_none;
		}
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
	if (array) {
		Operand result = runtime_operand(type, array_result);
		result.kind = OPERAND_AGGREGATE;
		return result;
	}
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
	bool array = !known && block->sema->types[type].kind == TYPE_ARRAY;
	TirRef array_result = TIR_REF_NONE;
	if (array) {
		TypeId pointer = type_pointer(block->sema, type, POINTER_ONE, true, false, 0);
		array_result = array_slot(block, pointer, inst->src);
		if (array_result == TIR_REF_NONE)
			return operand_none;
	}
	TypeId merge_type = known || array ? T_VOID : type;
	block->tir->insts[target.tir_ref].type = merge_type;
	for (uint32_t i = 0; i < target.count; i++) {
		BreakExit exit = target.exits[i];
		Block branch;
		block_init_child(&branch, block);
		Operand value = coerce(&branch, exit.value, type, exit.src);
		if (value.kind == OPERAND_NONE)
			continue;
		if (array) {
			Operand destination = runtime_operand(block->tir->insts[array_result].type, array_result);
			if (!store_array(&branch, destination, value, exit.src))
				return operand_none;
		}
		TirRef operand = merge_type == T_VOID ? TIR_REF_NONE : materialize(&branch, value, exit.src);
		emit_tir_bin(&branch, TIR_BR, T_NORETURN, exit.src, target.tir_ref, operand);
		block->tir->insts[exit.wrapper].data.payload = block_finish(&branch);
	}
	block->tir->insts[target.tir_ref].data.payload = block_finish(&inner);
	if (known)
		return target.exits[0].value;
	if (array) {
		Operand result = runtime_operand(type, array_result);
		result.kind = OPERAND_AGGREGATE;
		return result;
	}
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
	if (!require_runtime(block, inst->src))
		return;
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

/* Evaluate a signature; declarations also create a function, type expressions do not. */
static Operand
analyze_func_inst(Block *block, IrInstIndex index)
{
	Sema *sema = block->sema;
	const IrFile *ir = block->ir;
	const IrInst *inst = &ir->insts[index];
	IrFuncHeader header;
	IR_READ(ir, inst->data.payload, &header);
	const IrInstIndex *params = &ir->extra[inst->data.payload + IR_WORDS(IrFuncHeader)];

	bool generic = false;
	Type fn_type = make_type(TYPE_FN);
	fn_type.as.fn.is_var_args = (header.flags & IR_FUNC_VAR_ARGS) != 0;
	if ((header.flags & IR_FUNC_INLINE) != 0)
		fn_type.as.fn.convention = FUNCTION_INLINE;
	else if (inst->op == IR_FUNC && ((header.flags & (IR_FUNC_EXTERN | IR_FUNC_EXPORT)) != 0 || (header.flags & IR_FUNC_HAS_BODY) == 0))
		fn_type.as.fn.convention = FUNCTION_C;
	fn_type.as.fn.ret = T_VOID;
	for (uint32_t i = 0; i < header.param_count; i++) {
		const IrInst *param = &ir->insts[params[i]];
		if (param->op == IR_PARAM_ANYTYPE) {
			generic = true;
			continue;
		}
		IrParam payload;
		IR_READ(ir, param->data.payload, &payload);
		if (payload.flags & IR_PARAM_COMPTIME)
			generic = true;
	}
	if (header.callconv_body != 0) {
		sema_error(block, inst->src, "not supported by szc yet: callconv");
		return operand_none;
	}
	if (fn_type.as.fn.is_var_args && fn_type.as.fn.convention != FUNCTION_C) {
		sema_error(block, inst->src, "variadic function does not support 'auto' calling convention");
		return operand_none;
	}
	if (!generic) {
		TypeId *param_types = arena_alloc(sema->arena, (header.param_count + 1) * sizeof(TypeId), alignof(TypeId));
		fn_type.as.fn.noalias = ARENA_ARRAY(sema->arena, bool, header.param_count + 1);
		for (uint32_t i = 0; i < header.param_count; i++) {
			const IrInst *param = &ir->insts[params[i]];
			IrParam payload;
			IR_READ(ir, param->data.payload, &payload);
			fn_type.as.fn.noalias[i] = (payload.flags & IR_PARAM_NOALIAS) != 0;
			BodyResult result = analyze_body(block, payload.type_body);
			if (result.end != BODY_BREAK_RESULT || !operand_to_type(block, result.result, param->src, &param_types[i]))
				return operand_none;
			if (fn_type.as.fn.noalias[i] && sema->types[param_types[i]].kind != TYPE_POINTER) {
				sema_error(block, param->src, "non-pointer parameter declared noalias");
				return operand_none;
			}
			if (is_comptime_only_type(sema, param_types[i]))
				generic = true;
		}
		fn_type.as.fn.params = param_types;
		fn_type.as.fn.param_count = header.param_count;
	}
	if (!generic) {
		BodyResult result = analyze_body(block, header.ret_type_body);
		if (result.end != BODY_BREAK_RESULT || !operand_to_type(block, result.result, inst->src, &fn_type.as.fn.ret))
			return operand_none;
		if (is_comptime_only_type(sema, fn_type.as.fn.ret))
			generic = true;
	}
	if (generic) {
		fn_type.as.fn.is_generic = true;
		fn_type.as.fn.params = NULL;
		fn_type.as.fn.param_count = 0;
		fn_type.as.fn.ret = T_VOID;
	}
	uint32_t alignment;
	if (!analyze_alignment(block, header.align_body, header.align_src, &alignment))
		return operand_none;
	TypeId type = type_intern(sema, &fn_type);
	if (inst->op == IR_FN_TYPE)
		return type_operand(sema, type);
	FuncId id = func_create(sema);
	SemaFunc *func = sema->funcs[id];
	func->namespace_scope = block->namespace_scope;
	func->ir = ir;
	func->func_inst = index;
	func->header = header;
	func->alignment = alignment;
	func->name = header.name;
	func->is_export = (header.flags & IR_FUNC_EXPORT) != 0;
	func->is_extern = (header.flags & IR_FUNC_EXTERN) != 0 || (header.flags & IR_FUNC_HAS_BODY) == 0;
	func->state = FUNC_DECLARED;
	func->symbol = func->is_export || func->is_extern ? intern_text(sema->interner, header.name) : func_symbol(sema, header.name, id);

	func->is_generic = generic;
	func->type = type;
	if (func->is_export && !generic)
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
	TypeId type = callable_type(sema, callee.type);
	if (type == 0) {
		sema_error(block, src, "type '%s' not a function", sema_type_name(sema, callee.type));
		return INST_CONTINUE;
	}
	TypeFn fn = sema->types[type].as.fn;
	if (fn.is_generic) {
		sema_error(block, src, "not supported by szc yet: calls of generic functions");
		return INST_CONTINUE;
	}
	FuncId id = 0;
	if (callee.kind == OPERAND_COMPTIME && operand_value(block, callee)->kind == VALUE_FUNCTION)
		id = operand_value(block, callee)->as.function;
	bool count_ok = fn.is_var_args ? header.arg_count >= fn.param_count : header.arg_count == fn.param_count;
	if (!count_ok) {
		sema_error(block, src, "expected %s%u argument(s), found %u", fn.is_var_args ? "at least " : "", (unsigned)fn.param_count, (unsigned)header.arg_count);
		return INST_CONTINUE;
	}

	Operand pending = operand_none;
	pending.kind = OPERAND_PENDING_CALL;
	if (!check_defined(block, callee, src))
		return INST_CONTINUE;
	pending.type = type;
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
		if (!failed && i >= fn.param_count && sema->types[args[i].type].kind == TYPE_ARRAY) {
			sema_error(block, src, "not supported by szc yet: variadic array arguments");
			failed = true;
		}
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
	if (id != 0) {
		func_queue(sema, id);
		if (sema->funcs[id]->state == FUNC_FAILED)
			return INST_CONTINUE;
	}
	TirRef *refs = arena_alloc(sema->arena, (header.arg_count + 1) * sizeof(TirRef), alignof(TirRef));
	for (uint32_t i = 0; i < header.arg_count; i++) {
		Operand argument = args[i];
		if (sema->types[argument.type].kind == TYPE_ARRAY)
			argument = analyze_ref(block, argument, src, 0, 0);
		if (argument.kind == OPERAND_NONE)
			return INST_CONTINUE;
		refs[i] = materialize(block, argument, src);
	}
	bool array_return = sema->types[fn.ret].kind == TYPE_ARRAY;
	TirRef result_pointer = TIR_REF_NONE;
	if (array_return) {
		TypeId pointer = type_pointer(sema, fn.ret, POINTER_ONE, false, false, 0);
		result_pointer = array_slot(block, pointer, src);
		if (result_pointer == TIR_REF_NONE)
			return INST_CONTINUE;
		refs[header.arg_count] = result_pointer;
	}
	TirCall call;
	call.callee = id;
	call.indirect = id == 0 ? materialize(block, callee, src) : TIR_REF_NONE;
	call.arg_count = header.arg_count + (array_return ? 1u : 0u);
	uint32_t payload = tir_extra(block->tir, sema->arena, &call, TIR_WORDS(TirCall));
	tir_extra(block->tir, sema->arena, refs, call.arg_count);
	TirRef ref = emit_tir(block, TIR_CALL, array_return ? T_VOID : fn.ret, src);
	block->tir->insts[ref].data.payload = payload;
	if (fn.ret == T_NORETURN) {
		emit_tir(block, TIR_TRAP, T_NORETURN, src);
		return INST_NORETURN;
	}
	if (array_return) {
		/* Publish only immutable access to the caller-owned result object. */
		TypeId pointer = type_pointer(sema, fn.ret, POINTER_ONE, true, false, 0);
		TirRef immutable = emit_tir_un(block, TIR_PTR_CAST, pointer, src, result_pointer);
		*out = runtime_operand(fn.ret, immutable);
		out->kind = OPERAND_AGGREGATE;
	} else {
		*out = runtime_operand(fn.ret, ref);
	}
	return INST_CONTINUE;
}

/* Aggregate values never enter scalar QBE temporaries. Every runtime value owns
 * a snapshot; borrowed lvalues are represented only by ordinary pointer operands. */
static bool
array_storage_allowed(Sema *sema, TypeId type)
{
	while (sema->types[type].kind == TYPE_ARRAY)
		type = sema->types[type].as.array.child;
	TypeKind kind = sema->types[type].kind;
	return kind == TYPE_BOOL || kind == TYPE_POINTER || kind == TYPE_VOID
		|| (kind == TYPE_INT && sema->types[type].as.integer.bits <= 64);
}

static TirRef
array_slot(Block *block, TypeId pointer, uint32_t src)
{
	Sema *sema = block->sema;
	TypePointer shape = sema->types[pointer].as.pointer;
	uint64_t size;
	uint32_t alignment = pointer_alignment(sema, shape);
	if (!array_storage_allowed(sema, shape.child) || !sema_type_size(sema, shape.child, &size)) {
		sema_error(block, src, "not supported by szc yet: runtime array storage layout");
		return TIR_REF_NONE;
	}
	if (block->is_comptime) {
		sema_error(block, src, "unable to evaluate comptime expression");
		return TIR_REF_NONE;
	}
	if (!require_runtime(block, src))
		return TIR_REF_NONE;
	if (size == 0)
		size = 1;
	uint64_t padding = alignment > 16 ? alignment - 1 : 0;
	const uint64_t limit = 16777216;
	if (size > limit || padding > limit - size || block->func->array_stack_bytes > limit - size - padding) {
		sema_error(block, src, "not supported by szc yet: aggregate stack storage exceeds 16777216 bytes");
		return TIR_REF_NONE;
	}
	block->func->array_stack_bytes += size + padding;
	return emit_tir(block, TIR_ALLOC, pointer, src);
}

static bool
store_array(Block *block, Operand pointer, Operand value, uint32_t src)
{
	Sema *sema = block->sema;
	TypePointer shape = sema->types[pointer.type].as.pointer;
	if (shape.is_volatile) {
		sema_error(block, src, "not supported by szc yet: volatile whole-array copies");
		return false;
	}
	if (!array_storage_allowed(sema, shape.child)) {
		sema_error(block, src, "not supported by szc yet: runtime array storage layout");
		return false;
	}
	if (block->is_comptime) {
		sema_error(block, src, "unable to evaluate comptime expression");
		return false;
	}
	if (!require_runtime(block, src))
		return false;
	if (operand_is_undefined(block, value))
		return true;
	Operand source = analyze_ref(block, value, src, 0, 0);
	if (source.kind == OPERAND_NONE)
		return false;
	TirRef destination_ref = materialize(block, pointer, src);
	TirRef source_ref = materialize(block, source, src);
	emit_tir_bin(block, TIR_ARRAY_COPY, T_VOID, src, destination_ref, source_ref);
	return true;
}

static Operand
array_snapshot(Block *block, Operand source, uint32_t alignment, uint32_t src)
{
	Sema *sema = block->sema;
	TypeId type = sema->types[source.type].as.pointer.child;
	TypeId pointer = type_pointer(sema, type, POINTER_ONE, true, false, alignment);
	TirRef slot = array_slot(block, pointer, src);
	if (slot == TIR_REF_NONE)
		return operand_none;
	if (sema->types[source.type].as.pointer.is_volatile) {
		sema_error(block, src, "not supported by szc yet: volatile whole-array copies");
		return operand_none;
	}
	emit_tir_bin(block, TIR_ARRAY_COPY, T_VOID, src, slot, materialize(block, source, src));
	Operand result = runtime_operand(type, slot);
	result.kind = OPERAND_AGGREGATE;
	return result;
}

/* ---------- Analysis: memory ---------- */

/* Known values have immutable backing objects; runtime values have anchored stack slots. */
static Operand
analyze_ref(Block *block, Operand value, uint32_t src, uint32_t identity, uint32_t alignment)
{
	Sema *sema = block->sema;
	if (value.kind == OPERAND_NONE)
		return operand_none;
	TypeKind kind = sema->types[value.type].kind;
	if (kind == TYPE_FN && alignment != 0) {
		sema_error(block, src, "not supported by szc yet: aligned function body address");
		return operand_none;
	}
	if (kind == TYPE_FN)
		return function_address(block, value, src);
	TypeId pointer = type_pointer(sema, value.type, POINTER_ONE, true, false, alignment);
	if (value.kind == OPERAND_AGGREGATE) {
		TypePointer backing = sema->types[block->tir->insts[value.inst].type].as.pointer;
		if (alignment != 0 && alignment > pointer_alignment(sema, backing)) {
			Operand copy = array_snapshot(block, runtime_operand(block->tir->insts[value.inst].type, value.inst), alignment, src);
			if (copy.kind == OPERAND_NONE)
				return operand_none;
			return runtime_operand(pointer, copy.inst);
		}
		return runtime_operand(block->tir->insts[value.inst].type, value.inst);
	}
	if (value.kind == OPERAND_COMPTIME)
		return comptime_operand(pointer, value_ref(&sema->values, pointer, value.value, identity, alignment));
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
	uint32_t alignment = 0;
	if (payload.alignment != IR_REF_NONE) {
		Operand value = resolve_ref(block, payload.alignment);
		if (value.kind == OPERAND_NONE)
			return operand_none;
		if (value.kind != OPERAND_COMPTIME || !bigint_to_u32(&operand_value(block, value)->as.integer, &alignment)) {
			sema_error(block, src, "unable to resolve comptime value");
			return operand_none;
		}
	}
	TypeId pointer = type_pointer(sema, type, POINTER_ONE, (payload.flags & IR_ALLOC_CONST) != 0, false, alignment);
	if (sema->types[type].kind == TYPE_ARRAY) {
		TirRef slot = array_slot(block, pointer, src);
		if (slot == TIR_REF_NONE)
			return operand_none;
		Operand address = runtime_operand(pointer, slot);
		return store_array(block, address, init, src) ? address : operand_none;
	}
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

/* A known global address does not make its mutable contents comptime-known. */
static bool
mutable_global_pointer(Block *block, Operand pointer)
{
	if (pointer.kind != OPERAND_COMPTIME)
		return false;
	PointerAddress address = value_pointer_address(&block->sema->values, pointer.value);
	const Value *root = value_get(&block->sema->values, address.root);
	return root->kind == VALUE_GLOBAL && !block->sema->globals[root->as.global]->result.is_const;
}

static bool
pointer_operand(Block *block, Operand operand, uint32_t src)
{
	if (!pointer_type_operand(block, operand, src))
		return false;
	if (operand.kind == OPERAND_COMPTIME) {
		PointerAddress address = value_pointer_address(&block->sema->values, operand.value);
		if (value_get(&block->sema->values, address.root)->kind == VALUE_GLOBAL)
			return true;
	}
	if (operand.kind != OPERAND_RUNTIME) {
		sema_error(block, src, "not supported by szc yet: comptime pointers");
		return false;
	}
	return true;
}

static Operand constant_array_element(Block *block, Operand base, TypeArray array, uint64_t index, uint32_t src);
static Operand load_offset_pointer(Block *block, Operand pointer, uint32_t src);

/* Immutable declaration loads resolve their initializer; mutable contents need runtime. */
static Operand
load_pointer(Block *block, Operand pointer, uint32_t src)
{
	Sema *sema = block->sema;
	if (!pointer_type_operand(block, pointer, src))
		return operand_none;
	TypePointer pointer_type = sema->types[pointer.type].as.pointer;
	TypeId child = pointer_type.child;
	if (child == T_VOID)
		return void_operand(sema);
	if (is_int(sema, child) && sema->types[child].as.integer.bits == 0)
		return comptime_operand(child, value_int_u64(&sema->values, child, 0));
	bool mutable_global = mutable_global_pointer(block, pointer);
	if (mutable_global && block->is_comptime) {
		PointerAddress address = value_pointer_address(&sema->values, pointer.value);
		Global *global = sema->globals[value_get(&sema->values, address.root)->as.global];
		Operand resolved = resolve_decl(block, src, global->namespace_scope, global->decl, false);
		if (resolved.kind != OPERAND_NONE)
			sema_error(block, src, "unable to resolve comptime value");
		return operand_none;
	}
	if (pointer.kind == OPERAND_COMPTIME && !mutable_global) {
		const Value *value = operand_value(block, pointer);
		if (value->kind == VALUE_FUNCTION && sema->types[child].kind == TYPE_FN)
			return comptime_operand(child, value_function(&sema->values, child, value->as.function));
		PointerAddress address = value_pointer_address(&sema->values, pointer.value);
		const Value *root = value_get(&sema->values, address.root);
		if (root->kind == VALUE_BYTES && sema->types[child].kind == TYPE_ARRAY) {
			ByteString bytes = root->as.bytes;
			if (bytes.length == UINT32_MAX) {
				sema_error(block, src, "not supported by szc yet: array constant with this many elements");
				return operand_none;
			}
			uint32_t count = bytes.length + (sema->types[child].as.array.sentinel_zero ? 1u : 0u);
			if (!reserve_array_elements(block, count, src))
				return operand_none;
			ValueId *items = ARENA_ARRAY(sema->arena, ValueId, count);
			TypeId byte = sema->types[child].as.array.child;
			for (uint32_t i = 0; i < count; i++)
				items[i] = value_int_u64(&sema->values, byte, i < bytes.length ? bytes.data[i] : 0);
			return comptime_operand(child, value_array(&sema->values, child, items, count));
		}
		if (value->kind == VALUE_PTR_CAST) {
			ValueId parent = value->as.pointer;
			Operand stored = load_pointer(block, comptime_operand(value_get(&sema->values, parent)->type, parent), src);
			return coerce(block, stored, child, src);
		}
		if (value->kind == VALUE_ELEMENT_PTR) {
			ElementPointer element = value->as.element;
			TypeId base_type = value_get(&sema->values, element.base)->type;
			if (sema->types[base_type].as.pointer.size == POINTER_MANY)
				return load_offset_pointer(block, pointer, src);
			TypeId array_type = sema->types[base_type].as.pointer.child;
			Operand selected = constant_array_element(block, comptime_operand(base_type, element.base),
				sema->types[array_type].as.array, element.index, src);
			return coerce(block, selected, child, src);
		}
		if (value->kind == VALUE_REF) {
			ValueId stored = sema->values.objects[value->as.object].value;
			return coerce(block, comptime_operand(value_get(&sema->values, stored)->type, stored), child, src);
		}
		if (value->kind == VALUE_GLOBAL && (block->is_comptime || sema->globals[value->as.global]->result.is_const)) {
			Global *global = sema->globals[value->as.global];
			Operand resolved = resolve_decl(block, src, global->namespace_scope, global->decl, false);
			if (global->result.is_const)
				return coerce(block, resolved, child, src);
			if (resolved.kind != OPERAND_NONE)
				sema_error(block, src, "unable to resolve comptime value");
			return operand_none;
		}
	}
	if (is_comptime_only_type(sema, child)) {
		sema_error(block, src, "cannot load comptime-only type '%s'", sema_type_name(sema, child));
		return operand_none;
	}
	TypeKind kind = sema->types[child].kind;
	if (kind == TYPE_ARRAY) {
		if (!pointer_operand(block, pointer, src))
			return operand_none;
		return array_snapshot(block, pointer, 0, src);
	}
	if (kind != TYPE_INT && kind != TYPE_BOOL && kind != TYPE_POINTER) {
		sema_error(block, src, "not supported by szc yet: dereference of '%s'", sema_type_name(sema, child));
		return operand_none;
	}
	if (!pointer_operand(block, pointer, src) || !require_runtime(block, src))
		return operand_none;
	return runtime_operand(child, emit_tir_un(block, pointer_type.is_volatile ? TIR_LOAD_VOLATILE : TIR_LOAD, child, src, materialize(block, pointer, src)));
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
		sema_error(block, inst->src, "index syntax required for unknown-length pointer type '%s'", sema_type_name(sema, pointer.type));
		return operand_none;
	}
	bool zero_int = is_int(sema, type.child) && sema->types[type.child].as.integer.bits == 0;
	if (operand_is_undefined(block, pointer) && type.child != T_VOID && !zero_int) {
		sema_error(block, inst->src, "cannot dereference undefined value");
		return operand_none;
	}
	if (inst->op == IR_REF_DEREF)
		return pointer;
	return load_pointer(block, pointer, inst->src);
}

static bool
analyze_alignment(Block *block, IrBody body, uint32_t src, uint32_t *out)
{
	if (body == 0) {
		*out = 0;
		return true;
	}
	InstMap map;
	inst_map_init(&map, block->sema->arena);
	map.parent = block->map;
	Block inner;
	block_init_child(&inner, block);
	inner.map = &map;
	inner.is_comptime = true;
	inner.tir = NULL;
	inner.func = NULL;
	inner.control = NULL;
	BodyResult result = analyze_body(&inner, body);
	if (result.end != BODY_BREAK_RESULT)
		return false;
	Operand operand = coerce(&inner, result.result, type_id_of_well_known(IR_REF_TYPE_U29), src);
	if (operand.kind == OPERAND_NONE)
		return false;
	if (operand.kind != OPERAND_COMPTIME) {
		sema_error(block, src, "unable to resolve comptime value");
		return false;
	}
	if (!check_defined(block, operand, src))
		return false;
	uint32_t bytes;
	if (!bigint_to_u32(&operand_value(block, operand)->as.integer, &bytes)) {
		sema_error(block, src, "alignment value is too large");
		return false;
	}
	if (bytes == 0) {
		sema_error(block, src, "alignment must be >= 1");
		return false;
	}
	if ((bytes & (bytes - 1)) != 0) {
		sema_error(block, src, "alignment value '%u' is not a power of two", (unsigned)bytes);
		return false;
	}
	*out = bytes;
	return true;
}

/* ---------- Analysis: constant arrays ---------- */

static TypeId
array_type_id(Sema *sema, TypeId child, uint64_t length, bool sentinel_zero)
{
	Type type = make_type(TYPE_ARRAY);
	type.as.array.child = child;
	type.as.array.length = length;
	type.as.array.sentinel_zero = sentinel_zero;
	return type_intern(sema, &type);
}

/* Inspect a length without loading or evaluating the array's contents. */
static TypeId
indexable_array_type(Sema *sema, TypeId type)
{
	if (sema->types[type].kind == TYPE_POINTER) {
		TypePointer pointer = sema->types[type].as.pointer;
		if (pointer.size != POINTER_ONE)
			return 0;
		type = pointer.child;
	}
	return sema->types[type].kind == TYPE_ARRAY ? type : 0;
}

static Operand
analyze_array_type(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	Operand length = coerce(block, resolve_ref(block, inst->data.bin.lhs), T_USIZE, inst->src);
	TypeId child;
	if (length.kind == OPERAND_NONE || !operand_to_type(block, resolve_ref(block, inst->data.bin.rhs), inst->src, &child))
		return operand_none;
	if (length.kind != OPERAND_COMPTIME) {
		sema_error(block, inst->src, "unable to resolve comptime value");
		return operand_none;
	}
	if (!check_defined(block, length, inst->src))
		return operand_none;
	if (child == T_NORETURN || sema->types[child].kind == TYPE_ANYOPAQUE) {
		sema_error(block, inst->src, child == T_NORETURN ? "array of '%s' not allowed" : "array of opaque type '%s' not allowed", sema_type_name(sema, child));
		return operand_none;
	}
	uint64_t count = bigint_to_u64_bits(&operand_value(block, length)->as.integer);
	return type_operand(sema, array_type_id(sema, child, count, false));
}

static Operand
analyze_array_init(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	uint32_t payload = inst->data.payload;
	TypeId type;
	if (!operand_to_type(block, resolve_ref(block, block->ir->extra[payload]), inst->src, &type))
		return operand_none;
	if (sema->types[type].kind != TYPE_ARRAY) {
		sema_error(block, inst->src, "expected array type, found '%s'", sema_type_name(sema, type));
		return operand_none;
	}
	TypeArray array = sema->types[type].as.array;
	uint32_t count = block->ir->extra[payload + 1];
	if (array.length != count) {
		sema_error(block, inst->src, "expected %" PRIu64 " array elements; found %u", array.length, (unsigned)count);
		return operand_none;
	}
	if (array.sentinel_zero && count == UINT32_MAX) {
		sema_error(block, inst->src, "not supported by szc yet: array constant with this many elements");
		return operand_none;
	}
	uint32_t total = count + (array.sentinel_zero ? 1u : 0u);
	if (!reserve_array_elements(block, total, inst->src))
		return operand_none;
	ValueId *items = ARENA_ARRAY(sema->arena, ValueId, total);
	Operand *elements = ARENA_ARRAY(sema->arena, Operand, total);
	bool all_known = true;
	bool failed = false;
	for (uint32_t i = 0; i < count; i++) {
		Operand element = coerce(block, resolve_ref(block, block->ir->extra[payload + 2 + i]), array.child, inst->src);
		if (element.kind == OPERAND_NONE) {
			failed = true;
		} else {
			elements[i] = element;
			if (element.kind == OPERAND_COMPTIME)
				items[i] = element.value;
			else
				all_known = false;
		}
	}
	if (failed)
		return operand_none;
	if (array.sentinel_zero) {
		items[count] = value_int_u64(&sema->values, array.child, 0);
		elements[count] = comptime_operand(array.child, items[count]);
	}
	if (all_known)
		return comptime_operand(type, value_array(&sema->values, type, items, total));
	TypeId pointer = type_pointer(sema, type, POINTER_ONE, true, false, 0);
	TirRef slot = array_slot(block, pointer, inst->src);
	if (slot == TIR_REF_NONE)
		return operand_none;
	TypeId child_pointer = type_pointer(sema, array.child, POINTER_ONE, true, false, 0);
	for (uint32_t i = 0; i < total; i++) {
		Operand index = comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, i));
		TirRef address = emit_tir_bin(block, TIR_ARRAY_ELEM_PTR, child_pointer, inst->src, slot, materialize(block, index, inst->src));
		if (sema->types[array.child].kind == TYPE_ARRAY) {
			if (!store_array(block, runtime_operand(child_pointer, address), elements[i], inst->src))
				return operand_none;
		} else if (!operand_is_undefined(block, elements[i])) {
			emit_tir_bin(block, TIR_STORE, T_VOID, inst->src, address, materialize(block, elements[i], inst->src));
		}
	}
	Operand result = runtime_operand(type, slot);
	result.kind = OPERAND_AGGREGATE;
	return result;
}

/* Indexing constant backing never materializes an aggregate in the scalar backend. */
static Operand
constant_array_element(Block *block, Operand base, TypeArray array, uint64_t index, uint32_t src)
{
	Sema *sema = block->sema;
	if (base.kind != OPERAND_COMPTIME) {
		sema_error(block, src, "not supported by szc yet: runtime array indexing");
		return operand_none;
	}
	if (sema->types[base.type].kind == TYPE_POINTER) {
		if (!check_defined(block, base, src))
			return operand_none;
		PointerAddress address = value_pointer_address(&sema->values, base.value);
		const Value *root = value_get(&sema->values, address.root);
		if (root->kind == VALUE_BYTES) {
			ByteString bytes = root->as.bytes;
			uint64_t position = address.offset + index;
			return comptime_operand(array.child, value_int_u64(&sema->values, array.child, position < bytes.length ? bytes.data[position] : 0));
		}
		base = load_pointer(block, base, src);
		if (base.kind == OPERAND_NONE)
			return operand_none;
	}
	if (base.kind != OPERAND_COMPTIME) {
		sema_error(block, src, "not supported by szc yet: runtime array indexing");
		return operand_none;
	}
	if (operand_is_undefined(block, base))
		return comptime_operand(array.child, value_undefined(&sema->values, array.child));
	const Value *value = operand_value(block, base);
	if (value->kind != VALUE_ARRAY || index >= value->as.array.count) {
		sema_error(block, src, "not supported by szc yet: this constant array backing");
		return operand_none;
	}
	ValueId element = value->as.array.items[index];
	return comptime_operand(array.child, element);
}

/* Byte offsets for runtime-layout children; logical leaf offsets for comptime-only arrays. */
static bool
array_element_stride(Sema *sema, TypeId child, uint64_t *stride, bool *logical)
{
	uint64_t count = 1;
	TypeId leaf = child;
	while (sema->types[leaf].kind == TYPE_ARRAY)
		leaf = sema->types[leaf].as.array.child;
	*logical = is_comptime_only_type(sema, leaf);
	if (!*logical)
		return sema_type_size(sema, child, stride);
	while (sema->types[child].kind == TYPE_ARRAY) {
		TypeArray array = sema->types[child].as.array;
		if (array.length != 0 && count > UINT64_MAX / array.length)
			return false;
		count *= array.length;
		child = array.child;
	}
	*stride = count;
	return true;
}

static TypeId
array_element_pointer_type(Sema *sema, TypePointer pointer, TypeArray array, Operand index, uint64_t stride, bool logical)
{
	uint32_t alignment = pointer_alignment(sema, pointer);
	uint64_t item = index.kind == OPERAND_COMPTIME
		? bigint_to_u64_bits(&value_get(&sema->values, index.value)->as.integer) : 1;
	if (logical) {
		uint32_t child_alignment = sema_type_align(sema, array.child);
		if (alignment > child_alignment)
			alignment = child_alignment;
	} else if (stride != 0 && item != 0) {
		/* Only the low alignment bits matter; unsigned multiplication is intentional. */
		uint64_t offset = stride * item;
		while ((offset & (alignment - 1u)) != 0)
			alignment /= 2;
	}
	Type type = make_type(TYPE_POINTER);
	type.as.pointer = pointer;
	type.as.pointer.child = array.child;
	type.as.pointer.size = POINTER_ONE;
	type.as.pointer.alignment = alignment == sema_type_align(sema, array.child) ? 0 : alignment;
	type.as.pointer.is_allowzero = pointer.is_allowzero && (index.kind != OPERAND_COMPTIME || item == 0);
	return type_intern(sema, &type);
}

static Operand
load_offset_pointer(Block *block, Operand pointer, uint32_t src)
{
	Sema *sema = block->sema;
	PointerAddress address = value_pointer_address(&sema->values, pointer.value);
	const Value *root = value_get(&sema->values, address.root);
	TypeId child = sema->types[pointer.type].as.pointer.child;
	if (root->kind == VALUE_BYTES) {
		ByteString bytes = root->as.bytes;
		if (address.offset <= bytes.length)
			return comptime_operand(child, value_int_u64(&sema->values, child,
				address.offset < bytes.length ? bytes.data[address.offset] : 0));
	} else {
		Operand stored = load_pointer(block, comptime_operand(root->type, address.root), src);
		uint64_t offset = address.offset;
		while (stored.kind == OPERAND_COMPTIME) {
			if (offset == 0 && memory_coercion_allowed(sema, child, stored.type, false))
				return coerce(block, stored, child, src);
			if (sema->types[stored.type].kind != TYPE_ARRAY)
				break;
			TypeArray array = sema->types[stored.type].as.array;
			uint64_t stride;
			bool logical;
			if (!array_element_stride(sema, array.child, &stride, &logical))
				break;
			uint64_t index = stride == 0 ? 0 : offset / stride;
			if (index > array.length || (index == array.length && !array.sentinel_zero))
				break;
			stored = constant_array_element(block, stored, array, index, src);
			if (stride != 0)
				offset %= stride;
		}
		if (stored.kind == OPERAND_NONE)
			return operand_none;
	}
	sema_error(block, src, "dereference of '%s' exceeds bounds of containing decl", sema_type_name(sema, pointer.type));
	return operand_none;
}

static Operand
constant_array_pointer(Block *block, Operand base, Operand index, TypeArray array, uint32_t src)
{
	Sema *sema = block->sema;
	if (sema->types[base.type].kind == TYPE_ARRAY)
		base = analyze_ref(block, base, src, 0, 0);
	if (base.kind == OPERAND_NONE)
		return operand_none;
	uint64_t stride;
	bool logical;
	if (!array_element_stride(sema, array.child, &stride, &logical)) {
		sema_error(block, src, "not supported by szc yet: array element offset exceeds 64-bit address space");
		return operand_none;
	}
	TypeId type = array_element_pointer_type(sema, sema->types[base.type].as.pointer, array, index, stride, logical);
	if (operand_is_undefined(block, base))
		return comptime_operand(type, value_undefined(&sema->values, type));
	uint64_t size;
	if (!logical && !sema_type_size(sema, sema->types[base.type].as.pointer.child, &size)) {
		sema_error(block, src, "not supported by szc yet: array layout size exceeds 64-bit address space");
		return operand_none;
	}
	uint64_t item = bigint_to_u64_bits(&operand_value(block, index)->as.integer);
	PointerAddress address = value_pointer_address(&sema->values, base.value);
	if (stride != 0 && item > (UINT64_MAX - address.offset) / stride) {
		sema_error(block, src, "not supported by szc yet: array element offset exceeds 64-bit address space");
		return operand_none;
	}
	uint64_t offset = address.offset + item * stride;
	return comptime_operand(type, value_element_pointer(&sema->values, type, base.value, item, offset, logical));
}

/* Runtime reads use real backing storage and a checked element address. */
static Operand
runtime_array_element(Block *block, Operand base, Operand index, TypeArray array, uint32_t src, bool address_only)
{
	Sema *sema = block->sema;
	if (block->is_comptime) {
		sema_error(block, src, "unable to evaluate comptime expression");
		return operand_none;
	}
	TypeKind kind = sema->types[array.child].kind;
	if (kind != TYPE_INT && kind != TYPE_BOOL && kind != TYPE_POINTER && kind != TYPE_VOID && kind != TYPE_ARRAY) {
		sema_error(block, src, "not supported by szc yet: runtime array element type '%s'", sema_type_name(sema, array.child));
		return operand_none;
	}
	if (is_int(sema, array.child) && sema->types[array.child].as.integer.bits > 64) {
		sema_error(block, src, "not supported by szc yet: integers wider than 64 bits");
		return operand_none;
	}
	if (sema->types[base.type].kind == TYPE_ARRAY)
		base = analyze_ref(block, base, src, 0, 0);
	if (base.kind == OPERAND_NONE || !check_defined(block, base, src) || !require_runtime(block, src))
		return operand_none;
	TypePointer pointer = sema->types[base.type].as.pointer;
	uint64_t size;
	if (!sema_type_size(sema, pointer.child, &size)) {
		sema_error(block, src, "not supported by szc yet: array layout size exceeds 64-bit address space");
		return operand_none;
	}
	uint64_t stride;
	bool logical;
	if (!array_element_stride(sema, array.child, &stride, &logical)) {
		sema_error(block, src, "not supported by szc yet: array element offset exceeds 64-bit address space");
		return operand_none;
	}
	TypeId element_pointer = array_element_pointer_type(sema, pointer, array, index, stride, logical);
	TirRef base_ref = materialize(block, base, src);
	TirRef index_ref = materialize(block, index, src);
	TirRef address = emit_tir_bin(block, TIR_ARRAY_ELEM_PTR, element_pointer, src, base_ref, index_ref);
	Operand pointer_value = runtime_operand(element_pointer, address);
	return address_only ? pointer_value : load_pointer(block, pointer_value, src);
}

static Operand
analyze_many_index(Block *block, Operand base, Operand index, uint32_t src, bool address_only, bool borrow)
{
	Sema *sema = block->sema;
	if (!check_defined(block, base, src) || !check_defined(block, index, src))
		return operand_none;
	TypePointer shape = sema->types[base.type].as.pointer;
	TypeArray element_shape = {shape.child, 0, false};
	uint64_t stride;
	bool logical;
	if (!array_element_stride(sema, shape.child, &stride, &logical)) {
		sema_error(block, src, "not supported by szc yet: pointer element offset exceeds 64-bit address space");
		return operand_none;
	}
	TypeId pointer_type = array_element_pointer_type(sema, shape, element_shape, index, stride, logical);
	Operand pointer;
	if (base.kind == OPERAND_COMPTIME && index.kind == OPERAND_COMPTIME) {
		uint64_t item = bigint_to_u64_bits(&operand_value(block, index)->as.integer);
		PointerAddress address = value_pointer_address(&sema->values, base.value);
		if (stride != 0 && item > (UINT64_MAX - address.offset) / stride) {
			sema_error(block, src, "not supported by szc yet: pointer element offset exceeds 64-bit address space");
			return operand_none;
		}
		pointer = comptime_operand(pointer_type, value_element_pointer(&sema->values, pointer_type,
			base.value, item, address.offset + item * stride, logical));
	} else {
		if (!require_runtime(block, src))
			return operand_none;
		if (logical || !array_storage_allowed(sema, shape.child)) {
			sema_error(block, src, "not supported by szc yet: runtime many-pointer element type '%s'", sema_type_name(sema, shape.child));
			return operand_none;
		}
		TirRef base_ref = materialize(block, base, src);
		TirRef index_ref = materialize(block, index, src);
		pointer = runtime_operand(pointer_type, emit_tir_bin(block, TIR_PTR_ELEM_PTR, pointer_type, src, base_ref, index_ref));
	}
	return address_only || (borrow && sema->types[shape.child].kind == TYPE_ARRAY)
		? pointer : load_pointer(block, pointer, src);
}

static Operand
analyze_array_index(Block *block, const IrInst *inst, bool address_only, bool borrow)
{
	Sema *sema = block->sema;
	Operand base = resolve_ref(block, inst->data.bin.lhs);
	Operand index = coerce(block, resolve_ref(block, inst->data.bin.rhs), T_USIZE, inst->src);
	if (base.kind == OPERAND_NONE || index.kind == OPERAND_NONE)
		return operand_none;
	if (sema->types[base.type].kind == TYPE_POINTER && sema->types[base.type].as.pointer.size == POINTER_MANY)
		return analyze_many_index(block, base, index, inst->src, address_only, borrow);
	TypeId type = indexable_array_type(sema, base.type);
	if (type == 0) {
		sema_error(block, inst->src, "expected array or pointer to array, found '%s'", sema_type_name(sema, base.type));
		return operand_none;
	}
	TypeArray array = sema->types[type].as.array;
	if (!check_defined(block, index, inst->src))
		return operand_none;
	if (index.kind == OPERAND_COMPTIME) {
		uint64_t item = bigint_to_u64_bits(&operand_value(block, index)->as.integer);
		if (item > array.length || (item == array.length && !array.sentinel_zero)) {
			sema_error(block, inst->src, "index %" PRIu64 " outside array of length %" PRIu64 "%s", item, array.length, array.sentinel_zero ? " +1 (sentinel)" : "");
			return operand_none;
		}
		if (base.kind == OPERAND_COMPTIME) {
			if (address_only || (borrow && sema->types[array.child].kind == TYPE_ARRAY))
				return constant_array_pointer(block, base, index, array, inst->src);
			if (mutable_global_pointer(block, base)) {
				Operand pointer = constant_array_pointer(block, base, index, array, inst->src);
				return load_pointer(block, pointer, inst->src);
			}
			return constant_array_element(block, base, array, item, inst->src);
		}
	} else if (array.length == 0 && !array.sentinel_zero) {
		sema_error(block, inst->src, sema->types[base.type].kind == TYPE_POINTER
			? "cannot index into empty array" : "indexing into empty array is not allowed");
		return operand_none;
	}
	/* Zig folds known one-possible/wholly undefined array values before a dynamic
	 * bounds guard. Pointer-based accesses must still keep their guard. */
	if (!address_only && base.kind == OPERAND_COMPTIME && sema->types[base.type].kind == TYPE_ARRAY) {
		if (array.child == T_VOID)
			return void_operand(sema);
		if (is_int(sema, array.child) && sema->types[array.child].as.integer.bits == 0)
			return comptime_operand(array.child, value_int_u64(&sema->values, array.child, 0));
		const Value *value = operand_value(block, base);
		bool undefined = value->kind == VALUE_UNDEFINED;
		if (value->kind == VALUE_ARRAY) {
			ArrayValue elements = value->as.array;
			undefined = true;
			for (uint32_t i = 0; i < elements.count; i++)
				if (value_get(&sema->values, elements.items[i])->kind != VALUE_UNDEFINED) {
					undefined = false;
					break;
				}
		}
		if (undefined)
			return comptime_operand(array.child, value_undefined(&sema->values, array.child));
	}
	return runtime_array_element(block, base, index, array, inst->src, address_only || (borrow && sema->types[array.child].kind == TYPE_ARRAY));
}

static Operand
analyze_array_cat(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	Operand lhs = resolve_ref(block, inst->data.bin.lhs);
	Operand rhs = resolve_ref(block, inst->data.bin.rhs);
	if (lhs.kind == OPERAND_NONE || rhs.kind == OPERAND_NONE)
		return operand_none;
	TypeId a = indexable_array_type(sema, lhs.type);
	TypeId b = indexable_array_type(sema, rhs.type);
	if (a == 0 || b == 0) {
		sema_error(block, inst->src, "expected indexable; found '%s'", sema_type_name(sema, a == 0 ? lhs.type : rhs.type));
		return operand_none;
	}
	TypeArray left = sema->types[a].as.array;
	TypeArray right = sema->types[b].as.array;
	if (lhs.kind != OPERAND_COMPTIME || rhs.kind != OPERAND_COMPTIME) {
		sema_error(block, inst->src, "not supported by szc yet: runtime array concatenation");
		return operand_none;
	}
	TypeId child;
	if (left.child != right.child && (sema->types[left.child].kind == TYPE_FN || sema->types[right.child].kind == TYPE_FN)) {
		sema_error(block, inst->src, "not supported by szc yet: function-body element joins in array concatenation");
		return operand_none;
	}
	/* Like Zig's type-only bitcasts, peer operands carry no known element value. */
	if (!peer_type(block, runtime_operand(left.child, TIR_REF_NONE), runtime_operand(right.child, TIR_REF_NONE), inst->src, &child))
		return operand_none;
	bool sentinel = left.sentinel_zero || right.sentinel_zero;
	if (right.length > UINT64_MAX - left.length) {
		sema_error(block, inst->src, "concatenating arrays of length %" PRIu64 " and %" PRIu64 " produces an array too large for this compiler implementation to handle", left.length, right.length);
		return operand_none;
	}
	uint64_t count = left.length + right.length;
	if (count > UINT32_MAX - (sentinel ? 1u : 0u)) {
		sema_error(block, inst->src, "not supported by szc yet: array constant with this many elements");
		return operand_none;
	}
	uint32_t total = (uint32_t)count + (sentinel ? 1u : 0u);
	if (!reserve_array_elements(block, total, inst->src))
		return operand_none;
	ValueId *items = ARENA_ARRAY(sema->arena, ValueId, total);
	for (uint32_t i = 0; i < count; i++) {
		Operand element = i < left.length
			? constant_array_element(block, lhs, left, i, inst->src)
			: constant_array_element(block, rhs, right, i - left.length, inst->src);
		element = coerce(block, element, child, inst->src);
		if (element.kind == OPERAND_NONE)
			return operand_none;
		items[i] = element.value;
	}
	if (sentinel)
		items[count] = value_int_u64(&sema->values, child, 0);
	TypeId type = array_type_id(sema, child, count, sentinel);
	bool reference = sema->types[lhs.type].kind == TYPE_POINTER || sema->types[rhs.type].kind == TYPE_POINTER;
	if (!reference)
		return comptime_operand(type, value_array(&sema->values, type, items, total));
	TypeId pointer = type_pointer(sema, type, POINTER_ONE, true, false, 0);
	/* Byte arrays use the existing compact backing, including panic messages. */
	if (child == type_id_of_well_known(IR_REF_TYPE_U8)) {
		uint8_t *bytes = ARENA_ARRAY(sema->arena, uint8_t, (uint32_t)count);
		bool defined = true;
		for (uint32_t i = 0; i < count; i++) {
			const Value *value = value_get(&sema->values, items[i]);
			if (value->kind != VALUE_INT) {
				defined = false;
				break;
			}
			bytes[i] = (uint8_t)bigint_to_u64_bits(&value->as.integer);
		}
		if (defined)
			return comptime_operand(pointer, value_bytes(&sema->values, pointer, bytes, (uint32_t)count));
	}
	ValueId value = value_array(&sema->values, type, items, total);
	return comptime_operand(pointer, value_ref(&sema->values, pointer, value, 0, 0));
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
	case IR_ARRAY_TYPE:
		result = analyze_array_type(block, inst);
		break;
	case IR_ARRAY_ELEM_TYPE: {
		TypeId type;
		if (!operand_to_type(block, resolve_ref(block, inst->data.un.operand), src, &type))
			break;
		if (sema->types[type].kind != TYPE_ARRAY)
			sema_error(block, src, "expected array type, found '%s'", sema_type_name(sema, type));
		else
			result = type_operand(sema, sema->types[type].as.array.child);
		break;
	}
	case IR_ARRAY_INIT:
		result = analyze_array_init(block, inst);
		break;
	case IR_ARRAY_BASE_PTR: {
		Operand pointer = resolve_ref(block, inst->data.un.operand);
		if (!pointer_type_operand(block, pointer, src))
			break;
		TypeId child = sema->types[pointer.type].as.pointer.child;
		if (sema->types[child].kind == TYPE_POINTER)
			pointer = load_pointer(block, pointer, src);
		if (pointer.kind != OPERAND_NONE && (indexable_array_type(sema, pointer.type) != 0
			|| (sema->types[pointer.type].kind == TYPE_POINTER && sema->types[pointer.type].as.pointer.size == POINTER_MANY)))
			result = pointer;
		else if (pointer.kind != OPERAND_NONE)
			sema_error(block, src, "expected array or pointer to array, found '%s'", sema_type_name(sema, child));
		break;
	}
	case IR_ARRAY_INIT_PTR: {
		Operand pointer = resolve_ref(block, inst->data.bin.lhs);
		Operand count = resolve_ref(block, inst->data.bin.rhs);
		if (!pointer_type_operand(block, pointer, src) || count.kind == OPERAND_NONE)
			break;
		TypePointer shape = sema->types[pointer.type].as.pointer;
		if (sema->types[shape.child].kind != TYPE_ARRAY) {
			sema_error(block, src, "expected array type, found '%s'", sema_type_name(sema, shape.child));
			break;
		}
		TypeArray array = sema->types[shape.child].as.array;
		uint64_t length = bigint_to_u64_bits(&operand_value(block, count)->as.integer);
		if (length != array.length)
			sema_error(block, src, "expected %" PRIu64 " array elements; found %" PRIu64, array.length, length);
		else if (shape.is_const)
			sema_error(block, src, "cannot assign to constant");
		else if (shape.is_volatile || array.sentinel_zero)
			sema_error(block, src, "not supported by szc yet: volatile or sentinel in-place array initialization");
		else
			result = pointer;
		break;
	}
	case IR_ARRAY_INDEX_PTR:
		result = analyze_array_index(block, inst, true, false);
		break;
	case IR_ARRAY_INDEX_BASE:
		result = analyze_array_index(block, inst, false, true);
		break;
	case IR_ARRAY_INDEX:
		result = analyze_array_index(block, inst, false, false);
		break;
	case IR_ARRAY_CAT:
		result = analyze_array_cat(block, inst);
		break;
	case IR_FIELD_REF:
	case IR_FIELD_VAL: {
		Operand base = resolve_ref(block, inst->data.name.operand);
		if (base.kind == OPERAND_NONE)
			break;
		TypeId base_type = base.type;
		if (inst->op == IR_FIELD_REF) {
			if (!pointer_type_operand(block, base, src))
				break;
			base_type = sema->types[base.type].as.pointer.child;
		}
		TypeId type = indexable_array_type(sema, base_type);
		if (type != 0 && inst->data.name.name == intern_cstr(sema->interner, "len"))
			result = comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, sema->types[type].as.array.length));
		else
			result = unsupported_inst(block, inst);
		break;
	}
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
	case IR_FN_TYPE:
		result = analyze_func_inst(block, index);
		break;
	case IR_DECL_TYPE:
		if (block->declaration_type != 0)
			result = type_operand(sema, block->declaration_type);
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
		result = resolve_decl(block, src, ns, decl, inst->op == IR_DECL_REF);
		if (decl->ir.kind == IR_DECL_VAR) {
			if (inst->op == IR_DECL_VAL)
				result = load_pointer(block, result, src);
		} else if (inst->op == IR_DECL_REF && decl->global == 0) {
			if (decl->address_identity == 0)
				decl->address_identity = ++sema->next_address_identity;
			result = analyze_ref(block, result, src, decl->address_identity, decl->alignment);
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
	case IR_ALIGN: {
		uint32_t alignment;
		if (analyze_alignment(block, inst->data.payload, src, &alignment)) {
			TypeId type = type_id_of_well_known(IR_REF_TYPE_U29);
			result = comptime_operand(type, value_int_u64(&sema->values, type, alignment));
		}
		break;
	}
	case IR_PTR_TYPE: {
		IrPointerType payload;
		IR_READ(block->ir, inst->data.payload, &payload);
		Operand child = resolve_ref(block, payload.child);
		TypeId type;
		if (!operand_to_type(block, child, src, &type))
			break;
		if (type == T_NORETURN) {
			sema_error(block, src, "pointer to noreturn not allowed");
			break;
		}
		Type pointer = make_type(TYPE_POINTER);
		pointer.as.pointer.child = type;
		pointer.as.pointer.size = (payload.flags & IR_POINTER_MANY) != 0 ? POINTER_MANY : POINTER_ONE;
		pointer.as.pointer.is_const = (payload.flags & IR_POINTER_CONST) != 0;
		pointer.as.pointer.is_allowzero = (payload.flags & IR_POINTER_ALLOWZERO) != 0;
		pointer.as.pointer.is_volatile = (payload.flags & IR_POINTER_VOLATILE) != 0;
		if (!analyze_alignment(block, payload.alignment_body, payload.alignment_src, &pointer.as.pointer.alignment))
			break;
		result = type_operand(sema, type_intern(sema, &pointer));
		break;
	}
	case IR_CONST_BIND: {
		uint32_t alignment = 0;
		if (inst->data.bin.rhs != IR_REF_NONE) {
			Operand value = resolve_ref(block, inst->data.bin.rhs);
			if (value.kind == OPERAND_NONE)
				break;
			if (value.kind != OPERAND_COMPTIME || !bigint_to_u32(&operand_value(block, value)->as.integer, &alignment)) {
				sema_error(block, src, "unable to resolve comptime value");
				break;
			}
		}
		result = resolve_ref(block, inst->data.bin.lhs);
		if (result.kind == OPERAND_AGGREGATE) {
			Operand pointer = analyze_ref(block, result, src, 0, 0);
			result = array_snapshot(block, pointer, alignment, src);
		}
		break;
	}
	case IR_REF: {
		IrRef operand = inst->data.un.operand;
		uint32_t alignment = 0;
		if (ir_ref_is_inst(operand)) {
			const IrInst *definition = &block->ir->insts[ir_ref_inst(operand)];
			if (definition->op == IR_CONST_BIND && definition->data.bin.rhs != IR_REF_NONE) {
				Operand value = resolve_ref(block, definition->data.bin.rhs);
				if (value.kind == OPERAND_NONE)
					break;
				if (value.kind != OPERAND_COMPTIME || !bigint_to_u32(&operand_value(block, value)->as.integer, &alignment)) {
					sema_error(block, src, "unable to resolve comptime value");
					break;
				}
			}
		}
		result = analyze_ref(block, resolve_ref(block, operand), src, 0, alignment);
		break;
	}
	case IR_REF_ARRAY_TYPE:
	case IR_REF_TYPE: {
		Operand operand = resolve_ref(block, inst->op == IR_REF_ARRAY_TYPE ? inst->data.bin.lhs : inst->data.un.operand);
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
		TypePointer pointer = sema->types[type].as.pointer;
		TypeId child = pointer.child;
		if (inst->op == IR_REF_ARRAY_TYPE && pointer.size == POINTER_MANY) {
			Operand length = resolve_ref(block, inst->data.bin.rhs);
			if (length.kind != OPERAND_COMPTIME)
				break;
			child = array_type_id(sema, child, bigint_to_u64_bits(&operand_value(block, length)->as.integer), false);
		}
		result = type_operand(sema, child);
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
	case IR_ADD_SUB_RHS_TYPE: {
		Operand operand = resolve_ref(block, inst->data.un.operand);
		if (operand.kind != OPERAND_NONE) {
			TypeId type = operand.type;
			if (sema->types[type].kind == TYPE_POINTER && sema->types[type].as.pointer.size == POINTER_MANY)
				type = T_USIZE;
			result = type_operand(sema, type);
		}
		break;
	}
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
	case IR_LOAD:
		result = load_pointer(block, resolve_ref(block, inst->data.un.operand), src);
		break;
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
		if (value.kind != OPERAND_NONE && sema->types[child].kind == TYPE_ARRAY) {
			if (pointer_operand(block, pointer, src) && store_array(block, pointer, value, src))
				result = void_operand(sema);
			break;
		}
		bool no_storage = child == T_VOID || (is_int(sema, child) && sema->types[child].as.integer.bits == 0);
		if (value.kind != OPERAND_NONE && !no_storage) {
			if (block->is_comptime && mutable_global_pointer(block, pointer)) {
				sema_error(block, src, "unable to evaluate comptime expression");
				break;
			}
			if (!require_runtime(block, src))
				break;
			bool is_volatile = sema->types[pointer.type].as.pointer.is_volatile;
			if ((is_volatile || !operand_is_undefined(block, value)) && pointer_operand(block, pointer, src)) {
				TirRef address = materialize(block, pointer, src);
				emit_tir_bin(block, is_volatile ? TIR_STORE_VOLATILE : TIR_STORE, T_VOID, src, address, materialize(block, value, src));
			}
		}
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
		if (sema->types[ret].kind == TYPE_ARRAY) {
			TirRef pointer = block->func->return_pointer;
			Operand destination = runtime_operand(block->tir->insts[pointer].type, pointer);
			if (!store_array(block, destination, value, src))
				return INST_NORETURN;
			emit_tir_un(block, TIR_RET, T_NORETURN, src, TIR_REF_NONE);
		} else {
			TirRef operand = ret == T_VOID ? TIR_REF_NONE : materialize(block, value, src);
			emit_tir_un(block, TIR_RET, T_NORETURN, src, operand);
		}
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
	func->return_pointer = TIR_REF_NONE;
	const IrInstIndex *params = &func->ir->extra[func->ir->insts[func->func_inst].data.payload + IR_WORDS(IrFuncHeader)];
	for (uint32_t i = 0; i < fn.param_count; i++) {
		uint32_t src = func->ir->insts[params[i]].src;
		bool array = sema->types[fn.params[i]].kind == TYPE_ARRAY;
		/* Transport pointers promise only byte alignment; the private parameter
		 * snapshot restores ABI alignment independently of the caller's storage. */
		TypeId type = array ? type_pointer(sema, fn.params[i], POINTER_ONE, true, false, 1) : fn.params[i];
		TirRef arg = emit_tir(&block, TIR_ARG, type, src);
		func->tir.insts[arg].data.index = i;
		Operand value = runtime_operand(type, arg);
		if (array)
			value = array_snapshot(&block, value, 0, src);
		inst_map_put(&map, sema->arena, params[i], value);
	}
	if (sema->types[fn.ret].kind == TYPE_ARRAY) {
		TypeId pointer = type_pointer(sema, fn.ret, POINTER_ONE, false, false, 0);
		TirRef arg = emit_tir(&block, TIR_ARG, pointer, func->ir->insts[func->func_inst].src);
		func->tir.insts[arg].data.index = fn.param_count;
		func->return_pointer = arg;
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
	sema->global_capacity = 16;
	sema->global_count = 1;
	sema->globals = ARENA_ARRAY(arena, Global *, sema->global_capacity);
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

	/* Exported declarations are analysed even if nothing references them. */
	for (uint32_t i = 0; i < root->decl_count; i++) {
		if (root->decls[i].ir.flags & IR_DECL_EXPORT)
			resolve_decl(&block, root->decls[i].ir.src, root, &root->decls[i], false);
	}

	StringId main_name = intern_cstr(sema->interner, "main");
	SemaDecl *main_decl = namespace_find(root, main_name);
	if (main_decl == NULL) {
		sema_error(&block, root_src, "root source file struct '%s' has no member named 'main'", intern_text(sema->interner, root_name));
	} else if ((main_decl->ir.flags & IR_DECL_PUB) == 0) {
		sema_error(&block, main_decl->ir.src, "'main' is not marked 'pub'");
	} else {
		Operand main = resolve_decl(&block, main_decl->ir.src, root, main_decl, false);
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

	uint32_t function_index = 0;
	GlobalId global_index = 1;
	while (function_index < sema->queue_count || global_index < sema->global_count) {
		while (function_index < sema->queue_count)
			analyze_func_body(sema, sema->queue[function_index++]);
		while (global_index < sema->global_count) {
			Global *global = sema->globals[global_index++];
			if (global->decl->state == DECL_TYPED)
				resolve_decl(&block, global->result.src, global->namespace_scope, global->decl, false);
		}
	}
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

uint32_t
sema_global_count(const Sema *sema)
{
	return sema->global_count;
}

const SemaGlobal *
sema_global(const Sema *sema, GlobalId global)
{
	return &sema->globals[global]->result;
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
	case VALUE_GLOBAL:
		return sema->globals[value->as.global]->result.symbol;
	case VALUE_REF: {
		char *text = arena_alloc(sema->arena, 32, 1);
		snprintf(text, 32, "ref(object.%u)", (unsigned)value->as.object);
		return text;
	}
	case VALUE_PTR_CAST:
		return arena_sprintf(sema->arena, "ptr_cast(value.%u)", (unsigned)value->as.pointer);
	case VALUE_ELEMENT_PTR:
		return arena_sprintf(sema->arena, "elem(base.%u, index.%" PRIu64 ", %s.%" PRIu64 ")",
			(unsigned)value->as.element.base, value->as.element.index,
			value->as.element.logical ? "leaf" : "offset", value->as.element.offset);
	case VALUE_ARRAY:
		return arena_sprintf(sema->arena, "array(%u elements)", (unsigned)value->as.array.count);
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
	case TIR_PTR_TO_INT:
	case TIR_PTR_CAST:
	case TIR_INT_CAST:
	case TIR_TRUNCATE:
	case TIR_INT_FROM_BOOL:
	case TIR_LOAD:
	case TIR_LOAD_VOLATILE:
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
		if (call.callee != 0)
			fprintf(out, " %s(", sema->funcs[call.callee]->symbol);
		else
			fprintf(out, " %%%u(", (unsigned)call.indirect);
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
	for (GlobalId id = 1; id < sema->global_count; id++) {
		const SemaGlobal *global = &sema->globals[id]->result;
		if (global->initializer != VALUE_NONE) {
			fprintf(out, "%s %s: %s", global->is_const ? "constant" : "global", global->symbol, sema_type_name(sema, global->type));
			if (global->alignment != 0)
				fprintf(out, " align(%u)", (unsigned)global->alignment);
			fprintf(out, " = %s\n", value_text(sema, global->initializer));
		}
	}
	for (FuncId id = 1; id < sema->func_count; id++) {
		const SemaFunc *func = sema->funcs[id];
		if (func->state != FUNC_ANALYSED && !(func->is_extern && func->state == FUNC_QUEUED))
			continue;
		fprintf(out, "%sfn %s: %s", func->is_extern ? "extern " : func->is_export ? "export " : "", func->symbol, sema_type_name(sema, func->type));
		if (func->alignment != 0)
			fprintf(out, " align(%u)", (unsigned)func->alignment);
		if (func->is_extern) {
			fputs("\n", out);
			continue;
		}
		fputs(" {\n", out);
		dump_tir_body(out, sema, &func->tir, func->tir.body, 1);
		fputs("}\n", out);
	}
}
