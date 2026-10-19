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
#include "struct_layout.h"

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
	OPERAND_AGGREGATE,	/* array/slice value owned by a private snapshot; inst is its pointer */
	OPERAND_PENDING_CALL,	/* arguments being analysed; type = callee function type */
	OPERAND_PENDING_METHOD_CALL,	/* same, with an already-bound argument zero */
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

typedef enum StructState {
	STRUCT_UNRESOLVED,
	STRUCT_IN_PROGRESS,
	STRUCT_RESOLVED,
	STRUCT_FAILED,
} StructState;

typedef enum StructDefaultsState {
	DEFAULTS_UNRESOLVED,
	DEFAULTS_QUEUED,
	DEFAULTS_IN_PROGRESS,
	DEFAULTS_RESOLVED,
	DEFAULTS_FAILED,
} StructDefaultsState;

typedef struct SemaField {
	IrField ir;
	TypeId type;
	ValueId default_value;	/* VALUE_NONE is absent; typed undefined is a value */
	uint64_t size;
	uint64_t offset;
	uint32_t alignment;
} SemaField;

struct SemaNamespace {
	SemaNamespace *parent;
	const IrFile  *ir;
	FileId        file;
	IrInstIndex   inst;
	TypeId        type;
	TypeId enum_tag_type;
	ValueId *enum_tags;	/* atomically published backing integer values */
	SemaDecl      *decls;
	uint32_t      decl_count;
	bool					exports_resolved;
	const struct InstMap *captures;
	SemaField *fields;
	uint32_t *field_order;
	uint32_t field_count;
	StructState layout_state;
	StructDefaultsState defaults_state;
	uint64_t size;
	uint32_t alignment;
};

/* Stable output and private initializer ownership for each addressable global declaration. */
typedef struct Global {
	SemaGlobal result;
	SemaNamespace *namespace_scope;
	SemaDecl *decl;
} Global;

typedef enum RuntimeStorageState {
	RUNTIME_STORAGE_UNKNOWN,
	RUNTIME_STORAGE_PENDING,
	RUNTIME_STORAGE_ALLOWED,
	RUNTIME_STORAGE_FORBIDDEN,
} RuntimeStorageState;

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
	RuntimeStorageState *runtime_storage_states;
	ValueId *runtime_storage_queue;
	uint32_t runtime_storage_capacity;
	uint32_t runtime_storage_work;
	uint32_t array_elements;	/* work/storage charged by dense constant construction */
	uint32_t layout_depth;
	uint32_t enum_work;
	uint32_t switch_work; /* cumulative, including comparisons and dispatch construction */
	uint32_t switch_depth;
	SemaNamespace **defaults_queue; /* stable namespaces, never borrowed Type entries */
	uint32_t defaults_queue_count;
	uint32_t defaults_queue_capacity;
	uint32_t defaults_depth;
	uint32_t defaults_work;
	uint32_t bit_cast_words;	/* bounded packed constant work/storage */

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

typedef struct SwitchContext {
	struct SwitchContext *parent;
	IrInstIndex owner;
	Operand operand;
	Operand capture;
	bool capture_active;
} SwitchContext;

typedef struct Block {
	Sema          *sema;
	SemaNamespace *namespace_scope;
	const IrFile  *ir;
	InstMap       *map;
	bool          is_comptime;
	bool          is_typeof;	/* runtime analysis whose emitted body is discarded */
	bool comptime_value_required;	/* a builtin argument must be resolved, not emitted */
	StringId      decl_name;	/* declaration whose value is analysed (names structs) */
	SemaFunc      *func;	/* function whose body is analysed, or NULL */
	TirFunc       *tir;	/* where runtime instructions go, or NULL */
	TirRef        *insts;	/* typed-IR body being built */
	uint32_t      count;
	uint32_t      capacity;
	ControlTarget *control;
	SwitchContext *switch_context;
	uint32_t switch_dispatch_depth; /* conservative switch-induced backend nesting */
	TypeId declaration_type;	/* explicit mutable global initializer context */
	TypeId enum_tag_type;	/* resolved backing type, never the enum under construction */
	TypeId field_default_type;	/* already resolved definition-context field type */
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
		hash = hash_word(hash, type->as.array.sentinel);
		break;
	case TYPE_POINTER:
		hash = hash_word(hash, type->as.pointer.child);
		hash = hash_word(hash, (uint32_t)type->as.pointer.size);
		hash = hash_word(hash, type->as.pointer.sentinel);
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
		return a->as.array.child == b->as.array.child && a->as.array.length == b->as.array.length && a->as.array.sentinel == b->as.array.sentinel;
	case TYPE_POINTER:
		return a->as.pointer.child == b->as.pointer.child && a->as.pointer.size == b->as.pointer.size && a->as.pointer.sentinel == b->as.pointer.sentinel && a->as.pointer.alignment == b->as.pointer.alignment && a->as.pointer.is_const == b->as.pointer.is_const && a->as.pointer.is_volatile == b->as.pointer.is_volatile && a->as.pointer.is_allowzero == b->as.pointer.is_allowzero;
	case TYPE_FN:
		if (a->as.fn.convention != b->as.fn.convention || a->as.fn.ret != b->as.fn.ret || a->as.fn.is_var_args != b->as.fn.is_var_args || a->as.fn.is_generic != b->as.fn.is_generic || a->as.fn.param_count != b->as.fn.param_count)
			return false;
		for (uint32_t i = 0; i < a->as.fn.param_count; i++) {
			if (a->as.fn.params[i] != b->as.fn.params[i] || a->as.fn.noalias[i] != b->as.fn.noalias[i])
				return false;
		}
		return true;
	case TYPE_STRUCT:
	case TYPE_ENUM:
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
#define T_ENUM_LITERAL type_id_of_well_known(IR_REF_TYPE_ENUM_LITERAL)

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

static const char *value_text(Sema *sema, ValueId id);
static const char *namespace_name(Sema *sema, SemaNamespace *ns);

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
		return arena_sprintf(sema->arena, "[%" PRIu64 "%s]%s", type->as.array.length, type->as.array.sentinel == VALUE_NONE ? "" : arena_sprintf(sema->arena, ":%s", value_text(sema, type->as.array.sentinel)), sema_type_name(sema, type->as.array.child));
	case TYPE_POINTER: {
		const char *prefix = type->as.pointer.size == POINTER_ONE ? "*" : type->as.pointer.size == POINTER_MANY ? "[*]" : type->as.pointer.size == POINTER_SLICE ? "[]" : "[*c]";
		if (type->as.pointer.sentinel != VALUE_NONE) {
			const Value *sentinel = value_get(&sema->values, type->as.pointer.sentinel);
			const char *text = sentinel->kind == VALUE_BOOL ? (sentinel->as.boolean ? "true" : "false")
				: bigint_to_string(sema->arena, &sentinel->as.integer);
			prefix = arena_sprintf(sema->arena, type->as.pointer.size == POINTER_SLICE ? "[:%s]" : "[*:%s]", text);
		}
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
	case TYPE_ENUM:
		return namespace_name(sema, type->as.structure);
	case TYPE_ANYOPAQUE:
		return "anyopaque";
	case TYPE_ANYERROR:
		return "anyerror";
	case TYPE_NULL:
		return "@TypeOf(null)";
	case TYPE_UNDEFINED:
		return "@TypeOf(undefined)";
	case TYPE_ENUM_LITERAL:
		return "@EnumLiteral()";
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
			uint64_t bytes = ((uint64_t)bits + 7) / 8;
			uint64_t alignment = 1;
			while (alignment < bytes && alignment < 16)
				alignment *= 2;
			size = ((bytes + alignment - 1) / alignment) * alignment;
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
		if (type->as.array.sentinel != VALUE_NONE) {
			if (count == UINT64_MAX)
				return false;
			count++;
		}
		if (count > UINT64_MAX / child)
			return false;
		size = count * child;
		break;
	}
	case TYPE_ENUM:
		if (type->as.structure->layout_state != STRUCT_RESOLVED
			|| type->as.structure->enum_tag_type == T_NORETURN)
			return false;
		return sema_type_size(sema, type->as.structure->enum_tag_type, out);
	case TYPE_STRUCT:
		if (type->as.structure->layout_state != STRUCT_RESOLVED)
			return false;
		size = type->as.structure->size;
		break;
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
	if (sema->types[type_id].kind == TYPE_STRUCT)
		return sema->types[type_id].as.structure->alignment;
	if (sema->types[type_id].kind == TYPE_ENUM) {
		const SemaNamespace *ns = sema->types[type_id].as.structure;
		return ns->layout_state == STRUCT_RESOLVED
			? sema_type_align(sema, ns->enum_tag_type) : 0;
	}
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

static bool
is_slice(const Sema *sema, TypeId type)
{
	return sema->types[type].kind == TYPE_POINTER && sema->types[type].as.pointer.size == POINTER_SLICE;
}

TypeId
sema_enum_tag_type(const Sema *sema, TypeId type)
{
	if (sema->types[type].kind != TYPE_ENUM)
		return 0;
	const SemaNamespace *ns = sema->types[type].as.structure;
	return ns->layout_state == STRUCT_RESOLVED ? ns->enum_tag_type : 0;
}

bool
sema_enum_tags(const Sema *sema, TypeId type, const ValueId **tags, uint32_t *count)
{
	if (sema_enum_tag_type(sema, type) == 0)
		return false;
	const SemaNamespace *ns = sema->types[type].as.structure;
	*tags = ns->enum_tags;
	*count = ns->field_count;
	return true;
}

bool
sema_struct_layout(const Sema *sema, TypeId type, const uint32_t **order, uint32_t *count)
{
	if (sema->types[type].kind != TYPE_STRUCT)
		return false;
	const SemaNamespace *ns = sema->types[type].as.structure;
	if (ns->layout_state != STRUCT_RESOLVED)
		return false;
	*order = ns->field_order;
	*count = ns->field_count;
	return true;
}

bool
sema_struct_field_layout(const Sema *sema, TypeId type, uint32_t index, SemaStructFieldLayout *out)
{
	if (sema->types[type].kind != TYPE_STRUCT)
		return false;
	const SemaNamespace *ns = sema->types[type].as.structure;
	if (ns->layout_state != STRUCT_RESOLVED || index >= ns->field_count)
		return false;
	const SemaField *field = &ns->fields[index];
	*out = (SemaStructFieldLayout){field->ir.name, field->type, field->offset};
	return true;
}

bool
sema_type_is_aggregate(const Sema *sema, TypeId type)
{
	return sema->types[type].kind == TYPE_ARRAY || sema->types[type].kind == TYPE_STRUCT || is_slice(sema, type);
}

static TypeId
slice_pointer_type(Sema *sema, TypeId type)
{
	Type pointer = sema->types[type];
	pointer.as.pointer.size = POINTER_MANY;
	return type_intern(sema, &pointer);
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
	while (value->kind == VALUE_REF || value->kind == VALUE_ELEMENT_PTR || value->kind == VALUE_PTR_CAST
		|| value->kind == VALUE_REINTERPRET_PTR || value->kind == VALUE_SLICE_FIELD_PTR
		|| value->kind == VALUE_STRUCT_FIELD_PTR) {
		id = value->kind == VALUE_REF ? sema->values.objects[value->as.object].value
			: value->kind == VALUE_ELEMENT_PTR ? value->as.element.base
			: value->kind == VALUE_SLICE_FIELD_PTR ? value->as.field.base
			: value->kind == VALUE_STRUCT_FIELD_PTR ? value->as.struct_field.base : value->as.pointer;
		value = value_get(&sema->values, id);
	}
	if (value->kind == VALUE_SLICE) {
		SliceValue slice = value->as.slice;
		queue_value_functions(block, slice.pointer, src);
		queue_value_functions(block, slice.length, src);
		return;
	}
	if (value->kind == VALUE_ARRAY) {
		ArrayValue array = value->as.array;
		for (uint32_t i = 0; i < array.count; i++)
			queue_value_functions(block, array.items[i], src);
		return;
	}
	if (value->kind == VALUE_STRUCT) {
		StructValue structure = value->as.structure;
		for (uint32_t i = 0; i < structure.count; i++)
			queue_value_functions(block, structure.fields[i], src);
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

/* A function value may be discarded without analyzing its body. An explicit
 * or coerced code address makes an internal, non-generic body reachable even
 * when the pointer is discarded. Extern ABI liveness still waits for use. */
static void
queue_function_address(Block *block, Operand operand)
{
	Sema *sema = block->sema;
	if (operand.kind != OPERAND_COMPTIME || sema->types[operand.type].kind != TYPE_POINTER)
		return;
	const Value *value = operand_value(block, operand);
	if (value->kind != VALUE_FUNCTION)
		return;
	SemaFunc *func = sema->funcs[value->as.function];
	if (!func->is_extern && !func->is_generic)
		func_queue(sema, value->as.function);
}

/* Validate compiler-owned immutable backing, including relocations hidden in
 * descriptor/array/struct storage. Static admission does not enable runtime
 * aggregate values. Layout is established when roots are constructed, never
 * effectfully resolved while these cached queue/state arrays are borrowed. */
static bool enum_runtime_allowed(const Sema *sema, TypeId type);

static bool
runtime_constant_allowed(Block *block, ValueId root, uint32_t src)
{
	Sema *sema = block->sema;
	uint32_t capacity = sema->values.capacity;
	if (capacity > sema->runtime_storage_capacity) {
		uint32_t old = sema->runtime_storage_capacity;
		sema->runtime_storage_states = arena_grow_array(sema->arena, sema->runtime_storage_states,
			old, capacity, sizeof(RuntimeStorageState), alignof(RuntimeStorageState));
		memset(sema->runtime_storage_states + old, 0, (capacity - old) * sizeof(RuntimeStorageState));
		sema->runtime_storage_queue = arena_grow_array(sema->arena, sema->runtime_storage_queue,
			0, capacity, sizeof(ValueId), alignof(ValueId));
		sema->runtime_storage_capacity = capacity;
	}
	RuntimeStorageState *states = sema->runtime_storage_states;
	if (states[root] == RUNTIME_STORAGE_ALLOWED)
		return true;
	uint32_t count = 1;
	sema->runtime_storage_queue[0] = root;
	bool allowed = states[root] != RUNTIME_STORAGE_FORBIDDEN;
	bool exhausted = false;
	states[root] = RUNTIME_STORAGE_PENDING;
	for (uint32_t cursor = 0; cursor < count && allowed; cursor++) {
		if (sema->runtime_storage_work >= 1048576) {
			exhausted = true;
			allowed = false;
			break;
		}
		sema->runtime_storage_work++;
		ValueId id = sema->runtime_storage_queue[cursor];
		const Value *value = value_get(&sema->values, id);
		ValueId one[2];
		const ValueId *children = one;
		uint32_t child_count = 0;
		switch (value->kind) {
		case VALUE_ENUM:
			allowed = enum_runtime_allowed(sema, value->type);
			one[0] = value->as.enum_tag;
			child_count = 1;
			break;
		case VALUE_STRUCT:
			allowed = sema_type_static_storage_allowed(sema, value->type);
			children = value->as.structure.fields;
			child_count = value->as.structure.count;
			break;
		case VALUE_REF: {
			one[0] = sema->values.objects[value->as.object].value;
			TypeId type = value_get(&sema->values, one[0])->type;
			TypeId leaf = type;
			while (sema->types[leaf].kind == TYPE_ARRAY)
				leaf = sema->types[leaf].as.array.child;
			if (sema->types[leaf].kind == TYPE_STRUCT || sema->types[leaf].kind == TYPE_ENUM)
				allowed = sema_type_static_storage_allowed(sema, type);
			child_count = 1;
			break;
		}
		case VALUE_ELEMENT_PTR:
			one[0] = value->as.element.base;
			child_count = 1;
			break;
		case VALUE_STRUCT_FIELD_PTR:
			one[0] = value->as.struct_field.base;
			child_count = 1;
			break;
		case VALUE_SLICE_FIELD_PTR:
			one[0] = value->as.field.base;
			child_count = 1;
			break;
		case VALUE_PTR_CAST:
		case VALUE_REINTERPRET_PTR:
			one[0] = value->as.pointer;
			child_count = 1;
			break;
		case VALUE_SLICE:
			one[0] = value->as.slice.pointer;
			one[1] = value->as.slice.length;
			child_count = 2;
			break;
		case VALUE_ARRAY:
			children = value->as.array.items;
			child_count = value->as.array.count;
			break;
		default:
			break;
		}
		for (uint32_t i = 0; i < child_count && allowed; i++) {
			if (sema->runtime_storage_work >= 1048576) {
				exhausted = true;
				allowed = false;
				break;
			}
			sema->runtime_storage_work++;
			ValueId child = children[i];
			if (states[child] == RUNTIME_STORAGE_FORBIDDEN)
				allowed = false;
			else if (states[child] == RUNTIME_STORAGE_UNKNOWN) {
				states[child] = RUNTIME_STORAGE_PENDING;
				sema->runtime_storage_queue[count++] = child;
			}
		}
	}
	for (uint32_t i = 0; i < count; i++)
		states[sema->runtime_storage_queue[i]] = allowed ? RUNTIME_STORAGE_ALLOWED : RUNTIME_STORAGE_UNKNOWN;
	if (!allowed) {
		if (!exhausted)
			states[root] = RUNTIME_STORAGE_FORBIDDEN;
		sema_error(block, src, exhausted
			? "not supported by szc yet: constant runtime-backing work budget exceeded (1048576)"
			: "not supported by szc yet: unsupported immutable struct static backing");
	}
	return allowed;
}

static bool enum_runtime_allowed(const Sema *sema, TypeId type);

/* Turns an operand into a typed-IR instruction (materializing constants). */
static TirRef
materialize(Block *block, Operand operand, uint32_t src)
{
	if (operand.kind == OPERAND_AGGREGATE || operand.kind == OPERAND_NONE
		|| sema_type_is_aggregate(block->sema, operand.type)) {
		sema_error(block, src, "not supported by szc yet: array value in scalar runtime context");
		return TIR_REF_NONE;
	}
	if (block->sema->types[operand.type].kind == TYPE_STRUCT) {
		sema_error(block, src, "not supported by szc yet: runtime struct values");
		return TIR_REF_NONE;
	}
	if (block->sema->types[operand.type].kind == TYPE_ENUM
		&& !enum_runtime_allowed(block->sema, operand.type)) {
		sema_error(block, src, "not supported by szc yet: zero-width or unresolved runtime enum");
		return TIR_REF_NONE;
	}
	if (block->sema->types[operand.type].kind == TYPE_ENUM_LITERAL) {
		sema_error(block, src, "not supported by szc yet: runtime enum literal");
		return TIR_REF_NONE;
	}
	if (operand.kind == OPERAND_RUNTIME)
		return operand.inst;
	if (!runtime_constant_allowed(block, operand.value, src))
		return TIR_REF_NONE;
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

static bool
type_has_struct_value(const Sema *sema, TypeId type)
{
	while (sema->types[type].kind == TYPE_ARRAY)
		type = sema->types[type].as.array.child;
	return sema->types[type].kind == TYPE_STRUCT;
}

static bool
type_has_enum_value(const Sema *sema, TypeId type)
{
	while (sema->types[type].kind == TYPE_ARRAY)
		type = sema->types[type].as.array.child;
	return sema->types[type].kind == TYPE_ENUM;
}

static bool
enum_runtime_allowed(const Sema *sema, TypeId type)
{
	TypeId tag = sema_enum_tag_type(sema, type);
	return tag != 0 && sema->types[tag].kind == TYPE_INT
		&& !sema->types[tag].as.integer.is_signed
		&& sema->types[tag].as.integer.bits > 0 && sema->types[tag].as.integer.bits <= 64;
}

static bool func_validate_internal_signature(Sema *sema, FuncId id);

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
	bool struct_value = type_has_struct_value(sema, fn.ret);
	for (uint32_t i = 0; i < fn.param_count; i++)
		struct_value |= type_has_struct_value(sema, fn.params[i]);
	bool enum_value = type_has_enum_value(sema, fn.ret);
	for (uint32_t i = 0; i < fn.param_count; i++)
		enum_value |= type_has_enum_value(sema, fn.params[i]);
	if (enum_value && fn.convention == FUNCTION_C) {
		diag_report(sema->diag, DIAG_ERROR, func->ir->file, func->ir->insts[func->func_inst].src,
			"not supported by szc yet: enum values in C function signatures");
		func->state = FUNC_FAILED;
		return;
	}
	if (struct_value && fn.convention == FUNCTION_C) {
		diag_report(sema->diag, DIAG_ERROR, func->ir->file, func->ir->insts[func->func_inst].src,
			"not supported by szc yet: struct values in runtime function signatures");
		func->state = FUNC_FAILED;
		return;
	}
	if (fn.convention == FUNCTION_C) {
		const IrInstIndex *params = &func->ir->extra[func->ir->insts[func->func_inst].data.payload + IR_WORDS(IrFuncHeader)];
		for (uint32_t i = 0; i < fn.param_count; i++) {
			if (sema_type_is_aggregate(sema, fn.params[i])) {
				diag_report(sema->diag, DIAG_ERROR, func->ir->file, func->ir->insts[params[i]].src,
					"parameter of type '%s' not allowed in function with calling convention 'x86_64_sysv'", sema_type_name(sema, fn.params[i]));
				func->state = FUNC_FAILED;
				return;
			}
		}
		if (sema_type_is_aggregate(sema, fn.ret)) {
			diag_report(sema->diag, DIAG_ERROR, func->ir->file, func->ir->insts[func->func_inst].src,
				"return type '%s' not allowed in function with calling convention 'x86_64_sysv'", sema_type_name(sema, fn.ret));
			func->state = FUNC_FAILED;
			return;
		}
	}
	if (func->is_extern)
		return;
	if ((struct_value || enum_value) && !func_validate_internal_signature(sema, id)) {
		func->state = FUNC_FAILED;
		return;
	}
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

/* Deferred field bodies retain lexical operands, never a pointer to a stack map. */
static const InstMap *
inst_map_capture(Sema *sema, const InstMap *map)
{
	if (map == NULL)
		return NULL;
	InstMap *copy = ARENA_NEW(sema->arena, InstMap);
	*copy = *map;
	copy->entries = ARENA_ARRAY(sema->arena, InstMapEntry, map->capacity);
	memcpy(copy->entries, map->entries, map->capacity * sizeof(InstMapEntry));
	copy->parent = inst_map_capture(sema, map->parent);
	return copy;
}

static SemaNamespace *
namespace_create(Sema *sema, SemaNamespace *parent, const IrFile *ir, IrInstIndex inst, StringId name, const InstMap *captures)
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
	bool is_enum = ir->insts[inst].op == IR_ENUM_DECL;
	ns->captures = header.field_count == 0 && !is_enum ? NULL : inst_map_capture(sema, captures);
	ns->field_count = header.field_count;
	ns->fields = ARENA_ARRAY(sema->arena, SemaField, header.field_count);
	for (uint32_t i = 0; i < header.field_count; i++)
		IR_READ(ir, payload + IR_WORDS(IrContainerHeader) + i * IR_WORDS(IrField), &ns->fields[i].ir);
	ns->defaults_state = DEFAULTS_RESOLVED;
	for (uint32_t i = 0; i < header.field_count; i++)
		if (!is_enum && ns->fields[i].ir.default_body != 0)
			ns->defaults_state = DEFAULTS_UNRESOLVED;
	ns->layout_state = header.field_count == 0 && !is_enum ? STRUCT_RESOLVED : STRUCT_UNRESOLVED;
	ns->alignment = header.field_count == 0 ? 1 : 0;
	ns->decl_count = header.decl_count;
	ns->decls = ARENA_ARRAY(sema->arena, SemaDecl, header.decl_count + 1);
	for (uint32_t i = 0; i < header.decl_count; i++) {
		IR_READ(ir, decls + i * IR_WORDS(IrDecl), &ns->decls[i].ir);
		ns->decls[i].state = DECL_UNRESOLVED;
		ns->decls[i].value = operand_none;
	}
	Type type = make_type(is_enum ? TYPE_ENUM : TYPE_STRUCT);
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
static bool ensure_type_layout(Block *block, TypeId type, uint32_t src);
static bool ensure_enum_layout(Block *block, TypeId type, uint32_t src, const char *reason);
static Operand enum_field_value(Block *block, TypeId type, StringId name, uint32_t src);
static bool ensure_struct_defaults(Block *block, SemaNamespace *ns, uint32_t src);
static bool validate_internal_value_type(Block *block, TypeId type, uint32_t src);
static bool require_runtime(Block *block, uint32_t src);
static bool analyze_alignment(Block *block, IrBody body, uint32_t src, uint32_t *out);

static TirRef aggregate_slot(Block *block, TypeId pointer, uint32_t src);
static Operand aggregate_snapshot(Block *block, Operand source, uint32_t alignment, uint32_t src);
static Operand analyze_ref(Block *block, Operand value, uint32_t src, uint32_t identity, uint32_t alignment);
static bool store_aggregate(Block *block, Operand pointer, Operand value, uint32_t src);

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
	/* Stable mutable symbols publish only complete, checked value layouts.
	 * Pointer-only types stay lazy and legacy array admission stays unchanged. */
	if (type_has_struct_value(sema, type) || type_has_enum_value(sema, type)) {
		if (!ensure_type_layout(block, type, src))
			return false;
		if (!sema_type_runtime_storage_allowed(sema, type)) {
			sema_error(block, src, "not supported by szc yet: global struct storage layout");
			return false;
		}
		uint64_t size;
		if (!sema_type_size(sema, type, &size)) {
			sema_error(block, src, "not supported by szc yet: array layout size exceeds 64-bit address space");
			return false;
		}
		return true;
	}
	TypeKind kind = sema->types[type].kind;
	if (kind == TYPE_ARRAY) {
		if (!sema_type_array_storage_allowed(sema, type)) {
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
	return kind == TYPE_INT || kind == TYPE_BOOL || kind == TYPE_POINTER || kind == TYPE_VOID
		|| (kind == TYPE_ENUM && enum_runtime_allowed(sema, type));
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
		if (!runtime_constant_allowed(user, decl->value.value, decl->ir.src))
			return operand_none;
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
		if (decl->ir.kind == IR_DECL_VAR || (decl->declared_type != 0
			&& user->namespace_scope == ns && user->decl_name == decl->ir.name)) {
			if (as_reference && decl->global != 0)
				return decl->value;
			sema_error(user, src, "value of declaration '%s.%s' depends on itself here", namespace_name(sema, ns), intern_text(sema->interner, decl->ir.name));
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
	if ((mutable || decl->global != 0) && !runtime_constant_allowed(&block, result.result.value, decl->ir.src)) {
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
	case TYPE_ENUM:
		return "enum";
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
pointer_alignment(Block *block, TypePointer pointer, uint32_t src)
{
	if (pointer.alignment != 0)
		return pointer.alignment;
	if (!ensure_type_layout(block, pointer.child, src))
		return 1;	/* An error was reported; keep dependent recovery arithmetic bounded. */
	return sema_type_align(block->sema, pointer.child);
}

static bool
sentinel_is_zero(const Sema *sema, ValueId sentinel)
{
	if (sentinel == VALUE_NONE)
		return false;
	const Value *value = value_get(&sema->values, sentinel);
	return value->kind == VALUE_INT && bigint_is_zero(&value->as.integer);
}

/* Memory-compatible types: mutable outer pointees require bidirectional coercions. */
static bool
memory_coercion_allowed(Block *block, TypeId dest, TypeId source, bool dest_is_mut, uint32_t src)
{
	Sema *sema = block->sema;
	if (dest == source)
		return true;
	if (sema->types[dest].kind == TYPE_INT && sema->types[source].kind == TYPE_INT)
		return sema->types[dest].as.integer.bits == sema->types[source].as.integer.bits && sema->types[dest].as.integer.is_signed == sema->types[source].as.integer.is_signed;
	if (sema->types[dest].kind == TYPE_ARRAY && sema->types[source].kind == TYPE_ARRAY) {
		TypeArray to = sema->types[dest].as.array;
		TypeArray from = sema->types[source].as.array;
		return to.length == from.length && to.sentinel == from.sentinel
			&& memory_coercion_allowed(block, to.child, from.child, dest_is_mut, src);
	}
	if (sema->types[dest].kind == TYPE_FN && sema->types[source].kind == TYPE_FN) {
		TypeFn to = sema->types[dest].as.fn;
		TypeFn from = sema->types[source].as.fn;
		if (to.convention != from.convention || to.is_var_args != from.is_var_args || to.is_generic != from.is_generic || to.param_count != from.param_count)
			return false;
		if (!(from.ret == T_NORETURN && !dest_is_mut) && !memory_coercion_allowed(block, to.ret, from.ret, dest_is_mut, src))
			return false;
		for (uint32_t i = 0; i < to.param_count; i++) {
			if (to.noalias[i] != from.noalias[i] || !memory_coercion_allowed(block, from.params[i], to.params[i], dest_is_mut, src))
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
		TypeArray array = sema->types[from.child].as.array;
		if (to.sentinel != VALUE_NONE) {
			if (array.sentinel != to.sentinel)
				return false;
			from.sentinel = to.sentinel;
		}
		from.child = array.child;
	}
	if (to.sentinel != from.sentinel && (dest_is_mut || to.sentinel != VALUE_NONE))
		return false;
	if (to.is_volatile != from.is_volatile && (dest_is_mut || !to.is_volatile))
		return false;
	if (to.is_allowzero != from.is_allowzero && (dest_is_mut || !to.is_allowzero))
		return false;
	if (to.is_const != from.is_const && (dest_is_mut || !to.is_const))
		return false;
	/* Equal pointees and annotations already imply equal alignment. Resolving
	 * their ABI here would turn recursive pointer qualifier coercions into
	 * false by-value layout cycles while a field type is being analyzed. */
	if (to.child != from.child || to.alignment != from.alignment) {
		uint32_t to_align = pointer_alignment(block, to, src);
		uint32_t from_align = pointer_alignment(block, from, src);
		if (dest_is_mut ? to_align != from_align : to_align > from_align)
			return false;
	}
	if (memory_coercion_allowed(block, to.child, from.child, dest_is_mut || !to.is_const, src))
		return true;
	/* Zig's pointer-value exception drops a trailing array sentinel, but not
	 * through a writable pointer slot. Nested array layouts still must match. */
	if (!dest_is_mut && sema->types[to.child].kind == TYPE_ARRAY && sema->types[from.child].kind == TYPE_ARRAY) {
		TypeArray target = sema->types[to.child].as.array;
		TypeArray source_array = sema->types[from.child].as.array;
		if (target.length == source_array.length && target.sentinel == VALUE_NONE && source_array.sentinel != VALUE_NONE)
			return memory_coercion_allowed(block, target.child, source_array.child, !to.is_const, src);
	}
	return false;
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

static Operand coerce(Block *block, Operand operand, TypeId dest, uint32_t src);
static Operand load_pointer(Block *block, Operand pointer, uint32_t src);
static bool struct_pointer_view(Block *block, ValueId id, ValueId *field, PointerAddress *address, uint32_t *work, uint32_t src);

/* Slice values borrow elements; only the two-word descriptor is privately owned. */
static Operand
make_slice(Block *block, TypeId type, Operand pointer, Operand length, uint32_t src)
{
	Sema *sema = block->sema;
	if (pointer.kind == OPERAND_NONE || length.kind == OPERAND_NONE)
		return operand_none;
	if (pointer.kind == OPERAND_COMPTIME && length.kind == OPERAND_COMPTIME)
		return comptime_operand(type, value_slice(&sema->values, type, pointer.value, length.value));
	TypeId storage = type_pointer(sema, type, POINTER_ONE, true, false, 0);
	TirRef slot = aggregate_slot(block, storage, src);
	if (slot == TIR_REF_NONE)
		return operand_none;
	TypeId pointer_field = type_pointer(sema, slice_pointer_type(sema, type), POINTER_ONE, false, false, 0);
	TirRef data_address = emit_tir_un(block, TIR_PTR_CAST, pointer_field, src, slot);
	TypeId length_field = type_pointer(sema, T_USIZE, POINTER_ONE, false, false, 0);
	TirRef length_address = emit_tir_un(block, TIR_SLICE_LEN_PTR, length_field, src, slot);
	emit_tir_bin(block, TIR_STORE, T_VOID, src, data_address, materialize(block, pointer, src));
	emit_tir_bin(block, TIR_STORE, T_VOID, src, length_address, materialize(block, length, src));
	Operand result = runtime_operand(type, slot);
	result.kind = OPERAND_AGGREGATE;
	return result;
}

static Operand
slice_field(Block *block, Operand slice, bool length, uint32_t src)
{
	Sema *sema = block->sema;
	TypeId type = length ? T_USIZE : slice_pointer_type(sema, slice.type);
	if (operand_is_undefined(block, slice))
		return comptime_operand(type, value_undefined(&sema->values, type));
	if (slice.kind == OPERAND_COMPTIME) {
		SliceValue pair = operand_value(block, slice)->as.slice;
		return comptime_operand(type, length ? pair.length : pair.pointer);
	}
	if (!require_runtime(block, src))
		return operand_none;
	Operand backing = analyze_ref(block, slice, src, 0, 0);
	if (backing.kind == OPERAND_NONE)
		return operand_none;
	TypeId pointer = type_pointer(sema, type, POINTER_ONE, true, false, 0);
	TirRef address = emit_tir_un(block, length ? TIR_SLICE_LEN_PTR : TIR_PTR_CAST,
		pointer, src, materialize(block, backing, src));
	return runtime_operand(type, emit_tir_un(block, TIR_LOAD, type, src, address));
}

/* Same-shape pointer changes preserve identity and never copy pointee memory. */
static Operand
retag_pointer(Block *block, Operand operand, TypeId type, uint32_t src)
{
	Sema *sema = block->sema;
	if (type == operand.type)
		return operand;
	if (operand_is_undefined(block, operand))
		return comptime_operand(type, value_undefined(&sema->values, type));
	if (sema->types[type].as.pointer.size == POINTER_SLICE) {
		Operand data = slice_field(block, operand, false, src);
		Operand length = slice_field(block, operand, true, src);
		if (data.kind == OPERAND_NONE || length.kind == OPERAND_NONE)
			return operand_none;
		data = retag_pointer(block, data, slice_pointer_type(sema, type), src);
		return data.kind == OPERAND_NONE ? operand_none : make_slice(block, type, data, length, src);
	}
	if (operand.kind == OPERAND_COMPTIME) {
		const Value *value = operand_value(block, operand);
		if (value->kind == VALUE_BYTES || value->kind == VALUE_PTR_CAST || value->kind == VALUE_REINTERPRET_PTR
			|| value->kind == VALUE_ELEMENT_PTR || value->kind == VALUE_SLICE_FIELD_PTR
			|| value->kind == VALUE_STRUCT_FIELD_PTR)
			return comptime_operand(type, value_pointer_cast(&sema->values, type, operand.value));
		Value cast = *value;
		cast.type = type;
		return comptime_operand(type, value_intern(&sema->values, &cast));
	}
	if (!require_runtime(block, src))
		return operand_none;
	return runtime_operand(type, emit_tir_un(block, TIR_PTR_CAST, type, src, operand.inst));
}

/* Explicit qualifier casts remove only the pointee's outer qualifier. */
static Operand
cast_pointer_qualifier(Block *block, Operand operand, IrOp op, uint32_t src)
{
	Sema *sema = block->sema;
	Type result = sema->types[operand.type];
	if (result.kind != TYPE_POINTER) {
		sema_error(block, src, "expected pointer, found '%s'", sema_type_name(sema, operand.type));
		return operand_none;
	}
	if (op == IR_CONST_CAST)
		result.as.pointer.is_const = false;
	else
		result.as.pointer.is_volatile = false;
	return retag_pointer(block, operand, type_intern(sema, &result), src);
}

/* This shape-changing coercion is not an in-memory pointer compatibility rule. */
static bool
array_slice_allowed(Block *block, TypeId dest, TypeId source, uint32_t src)
{
	Sema *sema = block->sema;
	TypePointer to = sema->types[dest].as.pointer;
	TypePointer from = sema->types[source].as.pointer;
	if (to.size != POINTER_SLICE || from.size != POINTER_ONE || sema->types[from.child].kind != TYPE_ARRAY)
		return false;
	Type adjusted = sema->types[source];
	/* Zig's array-pointer conversion checks const/volatile/alignment attributes,
	 * but does not use the ordinary pointer allowzero compatibility rule. */
	adjusted.as.pointer.is_allowzero = to.is_allowzero;
	TypeArray array = sema->types[from.child].as.array;
	bool empty = array.length + (array.sentinel != VALUE_NONE ? 1u : 0u) == 0
		|| (array.length == 0 && to.sentinel == VALUE_NONE);
	if (empty) {
		adjusted.as.pointer.is_const = to.is_const;
		adjusted.as.pointer.alignment = pointer_alignment(block, to, src);
	}
	TypeId source_type = type_intern(sema, &adjusted);
	TypeId data_type = slice_pointer_type(sema, dest);
	return memory_coercion_allowed(block, data_type, source_type, false, src);
}

/* Coerces `operand` to type `dest` (Zig's implicit coercions that szc supports so far). */
static Operand
coerce(Block *block, Operand operand, TypeId dest, uint32_t src)
{
	Sema *sema = block->sema;
	if (operand.kind == OPERAND_NONE)
		return operand;
	TypeId leaf = dest;
	while (sema->types[leaf].kind == TYPE_ARRAY)
		leaf = sema->types[leaf].as.array.child;
	/* A by-value destination demands layout before same-type/undefined fast
	 * paths. Pointer types themselves must not demand their child's layout. */
	if ((sema->types[leaf].kind == TYPE_STRUCT || sema->types[leaf].kind == TYPE_ENUM)
		&& !ensure_type_layout(block, dest, src))
		return operand_none;
	if (operand.type == dest || operand.type == T_NORETURN)
		return operand;
	TypeKind dest_kind = sema->types[dest].kind;
	if (operand.kind == OPERAND_AGGREGATE && dest_kind == TYPE_ARRAY
		&& sema->types[operand.type].kind == TYPE_ARRAY) {
		TypeArray target = sema->types[dest].as.array;
		TypeArray source = sema->types[operand.type].as.array;
		if (target.child == source.child && target.length == source.length
			&& (target.sentinel == VALUE_NONE || target.sentinel == source.sentinel)) {
			TypeId pointer = type_pointer(sema, dest, POINTER_ONE, true, false, 0);
			if (!require_runtime(block, src))
				return operand_none;
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
	if (dest_kind == TYPE_POINTER && sema->types[dest].as.pointer.size == POINTER_SLICE
		&& sema->types[operand.type].kind == TYPE_POINTER) {
		TypePointer source = sema->types[operand.type].as.pointer;
		TypeId data_type = slice_pointer_type(sema, dest);
		if (array_slice_allowed(block, dest, operand.type, src)) {
			uint64_t count = sema->types[source.child].as.array.length;
			Operand data;
			if (operand_is_undefined(block, operand))
				data = comptime_operand(data_type, value_undefined(&sema->values, data_type));
			else if (operand.kind == OPERAND_COMPTIME)
				data = comptime_operand(data_type, value_pointer_cast(&sema->values, data_type, operand.value));
			else {
				if (!require_runtime(block, src))
					return operand_none;
				data = runtime_operand(data_type, emit_tir_un(block, TIR_PTR_CAST, data_type, src, operand.inst));
			}
			Operand length = comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, count));
			return make_slice(block, dest, data, length, src);
		}
		if (source.size == POINTER_SLICE && memory_coercion_allowed(block, dest, operand.type, false, src)) {
			if (operand_is_undefined(block, operand))
				return comptime_operand(dest, value_undefined(&sema->values, dest));
			if (operand.kind == OPERAND_COMPTIME) {
				Operand data = coerce(block, slice_field(block, operand, false, src), data_type, src);
				return make_slice(block, dest, data, slice_field(block, operand, true, src), src);
			}
			TypeId pointer = type_pointer(sema, dest, POINTER_ONE, true, false, 0);
			if (!require_runtime(block, src))
				return operand_none;
			operand.inst = emit_tir_un(block, TIR_PTR_CAST, pointer, src, operand.inst);
			operand.type = dest;
			return operand;
		}
	}
	if (operand.kind == OPERAND_COMPTIME) {
		/* Pointer compatibility may resolve fields and grow the value table. */
		Value value = *operand_value(block, operand);
		if (dest_kind == TYPE_ENUM && value.kind == VALUE_ENUM_LITERAL)
			return enum_field_value(block, dest, value.as.enum_literal, src);
		if (dest_kind == TYPE_ARRAY && sema->types[operand.type].kind == TYPE_ARRAY) {
			TypeArray target = sema->types[dest].as.array;
			TypeArray source = sema->types[operand.type].as.array;
			if (target.child == source.child && target.length == source.length
				&& (target.sentinel == VALUE_NONE || target.sentinel == source.sentinel)) {
				if (value.kind == VALUE_UNDEFINED)
					return comptime_operand(dest, value_undefined(&sema->values, dest));
				if (value.kind == VALUE_ARRAY) {
					ArrayValue array = value.as.array;
					uint32_t count = array.count - (source.sentinel != VALUE_NONE && target.sentinel == VALUE_NONE ? 1u : 0u);
					if (!reserve_array_elements(block, count, src))
						return operand_none;
					return comptime_operand(dest, value_array(&sema->values, dest, array.items, count));
				}
			}
		}
		if (value.kind == VALUE_UNDEFINED && (operand.type == T_UNDEFINED
			|| (dest_kind != TYPE_ARRAY && sema->types[operand.type].kind != TYPE_ARRAY))) {
			bool pointer_conversion = dest_kind == TYPE_POINTER || sema->types[operand.type].kind == TYPE_POINTER;
			if (!pointer_conversion || operand.type == type_id_of_well_known(IR_REF_TYPE_UNDEFINED) || memory_coercion_allowed(block, dest, operand.type, false, src))
				return comptime_operand(dest, value_undefined(&sema->values, dest));
		}
		if (value.kind == VALUE_INT && (dest_kind == TYPE_INT || dest_kind == TYPE_COMPTIME_INT)) {
			BigInt integer = value.as.integer;
			if (dest_kind == TYPE_INT && !bigint_fits(&integer, sema->types[dest].as.integer.is_signed, sema->types[dest].as.integer.bits)) {
				sema_error(block, src, "type '%s' cannot represent integer value '%s'", sema_type_name(sema, dest), bigint_to_string(sema->arena, &integer));
				return operand_none;
			}
			return comptime_operand(dest, value_int(&sema->values, dest, &integer));
		}
	}
	if (operand.kind == OPERAND_COMPTIME && dest_kind == TYPE_FN && sema->types[operand.type].kind == TYPE_FN && memory_coercion_allowed(block, dest, operand.type, false, src)) {
		Value value = *operand_value(block, operand);
		value.type = dest;
		return comptime_operand(dest, value_intern(&sema->values, &value));
	}
	if (dest_kind == TYPE_POINTER && memory_coercion_allowed(block, dest, operand.type, false, src)) {
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
	if (dest_kind == TYPE_POINTER && sema->types[dest].as.pointer.size == POINTER_SLICE
		&& sema->types[operand.type].kind == TYPE_ARRAY)
		sema_error(block, src, "array literal requires address-of operator (&) to coerce to slice type '%s'", sema_type_name(sema, dest));
	else
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

/* Packed bit representations use logical element order, never ABI padding. */
static bool
bit_cast_width(Block *block, TypeId type, bool destination, uint32_t src, uint32_t *out)
{
	Sema *sema = block->sema;
	TypeId leaf = type;
	bool empty = false;
	while (sema->types[leaf].kind == TYPE_ARRAY) {
		TypeArray array = sema->types[leaf].as.array;
		empty |= array.length == 0 && array.sentinel == VALUE_NONE;
		leaf = array.child;
	}
	TypeKind kind = sema->types[leaf].kind;
	if (kind != TYPE_INT && kind != TYPE_BOOL && kind != TYPE_VOID) {
		const char *message = destination ? "cannot @bitCast to '%s'" : "cannot @bitCast from '%s'";
		if (kind == TYPE_FLOAT || kind == TYPE_ENUM)
			message = destination ? "not supported by szc yet: @bitCast to '%s'" : "not supported by szc yet: @bitCast from '%s'";
		sema_error(block, src, message, sema_type_name(sema, type));
		return false;
	}
	uint32_t bits = empty ? 0u : kind == TYPE_INT ? sema->types[leaf].as.integer.bits : kind == TYPE_BOOL ? 1u : 0u;
	for (TypeId current = type; sema->types[current].kind == TYPE_ARRAY; current = sema->types[current].as.array.child) {
		TypeArray array = sema->types[current].as.array;
		if (bits == 0)
			break;
		if ((array.sentinel != VALUE_NONE && array.length == UINT64_MAX)
			|| array.length + (array.sentinel != VALUE_NONE ? 1u : 0u) > UINT32_MAX / bits) {
			sema_error(block, src, "not supported by szc yet: @bitCast representation exceeds 32-bit bit count");
			return false;
		}
		bits *= (uint32_t)(array.length + (array.sentinel != VALUE_NONE ? 1u : 0u));
	}
	*out = bits;
	return true;
}

/* Canonicalize all-undefined aggregates, including any stored sentinel. */
static bool
bit_cast_value_undefined(Sema *sema, ValueId id)
{
	const Value *value = value_get(&sema->values, id);
	if (value->kind == VALUE_UNDEFINED)
		return true;
	if (value->kind != VALUE_ARRAY)
		return false;
	uint32_t length = value->as.array.count;
	if (length == 0)
		return false;
	for (uint32_t i = 0; i < length; i++)
		if (!bit_cast_value_undefined(sema, value->as.array.items[i]))
			return false;
	return true;
}

/* Zig's packed constant writer chooses zero for undefined subvalues. A whole
 * undefined source is preserved before this helper is entered. */
static bool
pack_bit_cast(Block *block, ValueId id, PackedBits *bits, uint32_t offset, uint32_t src)
{
	Sema *sema = block->sema;
	const Value *value = value_get(&sema->values, id);
	TypeId type = value->type;
	TypeKind kind = sema->types[type].kind;
	if (bit_cast_value_undefined(sema, id) || kind == TYPE_VOID)
		return true;
	if (kind == TYPE_BOOL || kind == TYPE_INT) {
		uint32_t width = kind == TYPE_BOOL ? 1u : sema->types[type].as.integer.bits;
		BigInt integer = kind == TYPE_BOOL ? bigint_from_u64(sema->arena, value->as.boolean ? 1u : 0u) : value->as.integer;
		if (packed_bits_write(sema->arena, bits, offset, width, &integer))
			return true;
		sema_error(block, src, "internal error: invalid @bitCast packed write bounds");
		return false;
	}
	if (value->kind != VALUE_ARRAY) {
		sema_error(block, src, "not supported by szc yet: this @bitCast constant backing");
		return false;
	}
	ArrayValue items = value->as.array;
	if (items.count == 0)
		return true;
	TypeArray array = sema->types[type].as.array;
	uint32_t element_bits;
	if (!bit_cast_width(block, array.child, false, src, &element_bits))
		return false;
	for (uint32_t i = 0; i < items.count; i++)
		if (!pack_bit_cast(block, items.items[i], bits, offset + i * element_bits, src))
			return false;
	return true;
}

static ValueId
unpack_bit_cast(Block *block, TypeId type, const PackedBits *bits, uint32_t offset, uint32_t src)
{
	Sema *sema = block->sema;
	TypeKind kind = sema->types[type].kind;
	if (kind == TYPE_VOID)
		return value_void(&sema->values, T_VOID);
	if (kind != TYPE_ARRAY) {
		uint32_t width = kind == TYPE_BOOL ? 1u : sema->types[type].as.integer.bits;
		bool is_signed = kind == TYPE_INT && sema->types[type].as.integer.is_signed;
		BigInt result;
		if (!packed_bits_read(sema->arena, bits, offset, width, is_signed, &result)) {
			sema_error(block, src, "internal error: invalid @bitCast packed read bounds");
			return VALUE_NONE;
		}
		return kind == TYPE_BOOL ? value_bool(&sema->values, type, !bigint_is_zero(&result))
			: value_int(&sema->values, type, &result);
	}
	TypeArray array = sema->types[type].as.array;
	uint32_t extra = array.sentinel != VALUE_NONE ? 1u : 0u;
	if (array.length > UINT32_MAX - extra) {
		sema_error(block, src, "not supported by szc yet: @bitCast array with this many elements");
		return VALUE_NONE;
	}
	uint32_t count = (uint32_t)array.length + extra;
	if (count == 0)
		return value_array(&sema->values, type, NULL, 0);
	if (!reserve_array_elements(block, count, src))
		return VALUE_NONE;
	uint32_t element_bits;
	if (!bit_cast_width(block, array.child, true, src, &element_bits))
		return VALUE_NONE;
	ValueId *items = ARENA_ARRAY(sema->arena, ValueId, count);
	for (uint32_t i = 0; i < (uint32_t)array.length; i++) {
		items[i] = unpack_bit_cast(block, array.child, bits, offset + i * element_bits, src);
		if (items[i] == VALUE_NONE)
			return VALUE_NONE;
	}
	if (extra != 0)
		items[count - 1] = array.sentinel;
	return value_array(&sema->values, type, items, count);
}

/* Flatten runtime array values through typed element loads, not ABI bytes. */
static TirRef
pack_runtime_bit_cast(Block *block, TypeId type, TirRef operand, uint32_t src)
{
	Sema *sema = block->sema;
	TypeId word_type = type_int(sema, false, 64);
	TypeId shift_type = type_int(sema, false, 6);
	TypeKind kind = sema->types[type].kind;
	if (kind != TYPE_ARRAY) {
		uint32_t width = kind == TYPE_BOOL ? 1u : sema->types[type].as.integer.bits;
		TypeId raw_type = type_int(sema, false, width);
		TirRef raw = emit_tir_un(block, kind == TYPE_BOOL ? TIR_INT_FROM_BOOL : TIR_BIT_CAST, raw_type, src, operand);
		return emit_tir_un(block, TIR_INT_CAST, word_type, src, raw);
	}
	TypeArray array = sema->types[type].as.array;
	uint32_t element_bits;
	if (!bit_cast_width(block, array.child, false, src, &element_bits))
		return TIR_REF_NONE;
	TirRef result = materialize(block, comptime_operand(word_type, value_int_u64(&sema->values, word_type, 0)), src);
	if (element_bits == 0)
		return result;
	TypeId pointer = type_pointer(sema, array.child, POINTER_ONE, true, false, 0);
	uint32_t count = (uint32_t)array.length + (array.sentinel != VALUE_NONE ? 1u : 0u);
	for (uint32_t i = 0; i < count; i++) {
		TirRef index = materialize(block, comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, i)), src);
		TirRef address = emit_tir_bin(block, TIR_ARRAY_ELEM_PTR, pointer, src, operand, index);
		TirRef element = sema->types[array.child].kind == TYPE_ARRAY ? address : emit_tir_un(block, TIR_LOAD, array.child, src, address);
		TirRef packed = pack_runtime_bit_cast(block, array.child, element, src);
		if (packed == TIR_REF_NONE)
			return TIR_REF_NONE;
		if (i != 0) {
			TirRef shift = materialize(block, comptime_operand(shift_type, value_int_u64(&sema->values, shift_type, i * element_bits)), src);
			packed = emit_tir_bin(block, TIR_SHL, word_type, src, packed, shift);
		}
		result = emit_tir_bin(block, TIR_BIT_OR, word_type, src, result, packed);
	}
	return result;
}

/* Scalar extraction normalizes destination values before storing ABI elements. */
static Operand
unpack_runtime_bit_cast(Block *block, TypeId type, TirRef bits, uint32_t offset, uint32_t src)
{
	Sema *sema = block->sema;
	TypeId word_type = type_int(sema, false, 64);
	TypeId shift_type = type_int(sema, false, 6);
	if (sema->types[type].kind != TYPE_ARRAY) {
		uint32_t width = sema->types[type].kind == TYPE_BOOL ? 1u : sema->types[type].as.integer.bits;
		TirRef raw = bits;
		if (offset != 0) {
			TirRef shift = materialize(block, comptime_operand(shift_type, value_int_u64(&sema->values, shift_type, offset)), src);
			raw = emit_tir_bin(block, TIR_SHR, word_type, src, raw, shift);
		}
		TypeId raw_type = type_int(sema, false, width);
		raw = emit_tir_un(block, TIR_TRUNCATE, raw_type, src, raw);
		return runtime_operand(type, emit_tir_un(block, TIR_BIT_CAST, type, src, raw));
	}
	TypeArray array = sema->types[type].as.array;
	uint32_t element_bits;
	if (!bit_cast_width(block, array.child, true, src, &element_bits))
		return operand_none;
	TypeId pointer = type_pointer(sema, type, POINTER_ONE, true, false, 0);
	TirRef slot = aggregate_slot(block, pointer, src);
	if (slot == TIR_REF_NONE)
		return operand_none;
	TypeId child_pointer = type_pointer(sema, array.child, POINTER_ONE, true, false, 0);
	uint32_t count = (uint32_t)array.length + (array.sentinel != VALUE_NONE ? 1u : 0u);
	for (uint32_t i = 0; i < count; i++) {
		Operand element = unpack_runtime_bit_cast(block, array.child, bits, offset + i * element_bits, src);
		if (element.kind == OPERAND_NONE)
			return operand_none;
		TirRef index = materialize(block, comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, i)), src);
		TirRef address = emit_tir_bin(block, TIR_ARRAY_ELEM_PTR, child_pointer, src, slot, index);
		if (sema->types[array.child].kind == TYPE_ARRAY) {
			if (!store_aggregate(block, runtime_operand(child_pointer, address), element, src))
				return operand_none;
		} else {
			emit_tir_bin(block, TIR_STORE, T_VOID, src, address, materialize(block, element, src));
		}
	}
	Operand result = runtime_operand(type, slot);
	result.kind = OPERAND_AGGREGATE;
	return result;
}

/* Runtime aggregate packing is bounded by the native scalar representation. */
static Operand
analyze_bit_cast(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	Operand dest = resolve_ref(block, inst->data.bin.lhs);
	Operand operand = resolve_ref(block, inst->data.bin.rhs);
	TypeId type;
	if (operand.kind == OPERAND_NONE || !operand_to_type(block, dest, inst->src, &type))
		return operand_none;
	uint32_t to_bits;
	uint32_t from_bits;
	if (!bit_cast_width(block, type, true, inst->src, &to_bits)
		|| !bit_cast_width(block, operand.type, false, inst->src, &from_bits))
		return operand_none;
	if (to_bits != from_bits) {
		sema_error(block, inst->src, "@bitCast size mismatch: destination type '%s' has %u bits but source type '%s' has %u bits",
			sema_type_name(sema, type), (unsigned)to_bits, sema_type_name(sema, operand.type), (unsigned)from_bits);
		return operand_none;
	}
	if (sema->types[type].kind == TYPE_VOID)
		return void_operand(sema);
	if (to_bits == 0 && sema->types[type].kind == TYPE_INT)
		return comptime_operand(type, value_int_u64(&sema->values, type, 0));
	if (to_bits == 0) {
		PackedBits bits = packed_bits_init(sema->arena, 0);
		ValueId value = unpack_bit_cast(block, type, &bits, 0, inst->src);
		return value == VALUE_NONE ? operand_none : comptime_operand(type, value);
	}
	if (operand.kind == OPERAND_COMPTIME && bit_cast_value_undefined(sema, operand.value))
		return comptime_operand(type, value_undefined(&sema->values, type));
	if (operand.kind == OPERAND_COMPTIME) {
		enum { BIT_CAST_WORD_BUDGET = 1048576 };
		uint32_t words = to_bits / 32 + (to_bits % 32 != 0 ? 1u : 0u);
		if (words > BIT_CAST_WORD_BUDGET - sema->bit_cast_words) {
			sema_error(block, inst->src, "not supported by szc yet: packed constant word budget exceeded (1048576)");
			return operand_none;
		}
		sema->bit_cast_words += words;
		PackedBits bits = packed_bits_init(sema->arena, to_bits);
		if (!pack_bit_cast(block, operand.value, &bits, 0, inst->src))
			return operand_none;
		ValueId result = unpack_bit_cast(block, type, &bits, 0, inst->src);
		return result == VALUE_NONE ? operand_none : comptime_operand(type, result);
	}
	if (sema->types[type].kind == TYPE_ARRAY || sema->types[operand.type].kind == TYPE_ARRAY) {
		if (to_bits > 64) {
			sema_error(block, inst->src, "not supported by szc yet: runtime aggregate @bitCast wider than 64 bits");
			return operand_none;
		}
		if (!require_runtime(block, inst->src))
			return operand_none;
		Operand source = sema->types[operand.type].kind == TYPE_ARRAY ? analyze_ref(block, operand, inst->src, 0, 0) : operand;
		if (source.kind == OPERAND_NONE)
			return operand_none;
		TirRef bits = pack_runtime_bit_cast(block, operand.type, materialize(block, source, inst->src), inst->src);
		return bits == TIR_REF_NONE ? operand_none : unpack_runtime_bit_cast(block, type, bits, 0, inst->src);
	}
	if (!require_runtime(block, inst->src))
		return operand_none;
	return runtime_operand(type, emit_tir_un(block, TIR_BIT_CAST, type, inst->src, operand.inst));
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

static bool check_defined(Block *block, Operand operand, uint32_t src);

/* Sentinels are type data: coerce, resolve and validate before interning. */
static bool
analyze_scalar_sentinel(Block *block, Operand operand, TypeId child, uint32_t src, ValueId *out)
{
	Operand sentinel = coerce(block, operand, child, src);
	if (sentinel.kind == OPERAND_NONE)
		return false;
	if (sentinel.kind != OPERAND_COMPTIME) {
		sema_error(block, src, "unable to resolve comptime value");
		return false;
	}
	if (!check_defined(block, sentinel, src))
		return false;
	const Value *value = operand_value(block, sentinel);
	if (value->kind != VALUE_INT && value->kind != VALUE_BOOL) {
		sema_error(block, src, "not supported by szc yet: sentinel type '%s'", sema_type_name(block->sema, child));
		return false;
	}
	*out = sentinel.value;
	return true;
}

/* Numeric addresses have no compiler-owned contents or symbolic relocation. */
static bool
integer_pointer(Block *block, Operand pointer)
{
	return pointer.kind == OPERAND_COMPTIME && operand_value(block, pointer)->kind == VALUE_INT_PTR;
}

/* Unlike integer_pointer, this classification includes typed field paths and
 * qualifier wrappers. Callers must not read their outer union as an address. */
static bool
absolute_pointer(Block *block, Operand pointer)
{
	if (pointer.kind != OPERAND_COMPTIME)
		return false;
	PointerAddress address = value_pointer_address(&block->sema->values, pointer.value);
	return value_get(&block->sema->values, address.root)->kind == VALUE_INT_PTR;
}

/* Path payloads store displacement from a symbolic/numeric root. The direct
 * integer constructors instead consume a complete address and canonicalize it.
 * Normalize composed numeric paths without erasing their typed field origin. */
static uint64_t
pointer_path_offset(const Sema *sema, ValueId base, uint64_t address)
{
	if (value_get(&sema->values, base)->kind == VALUE_INT_PTR)
		return address;
	PointerAddress root = value_pointer_address(&sema->values, base);
	const Value *value = value_get(&sema->values, root.root);
	return value->kind == VALUE_INT_PTR ? address - value->as.address : address;
}

/* A physical view may occur below qualifier wrappers, indexing or slicing.
 * Such reads must use real storage until physical comptime memory is implemented. */
static bool
runtime_pointer_view(Block *block, Operand pointer)
{
	if (pointer.kind != OPERAND_COMPTIME || block->sema->types[pointer.type].kind != TYPE_POINTER)
		return false;
	const Value *value = operand_value(block, pointer);
	for (;;) {
		ValueId parent;
		switch (value->kind) {
		case VALUE_REINTERPRET_PTR:
			return true;
		case VALUE_PTR_CAST:
			parent = value->as.pointer;
			break;
		case VALUE_ELEMENT_PTR:
			parent = value->as.element.base;
			break;
		case VALUE_STRUCT_FIELD_PTR:
			parent = value->as.struct_field.base;
			break;
		case VALUE_SLICE_FIELD_PTR:
			parent = value->as.field.base;
			break;
		default:
			return false;
		}
		value = value_get(&block->sema->values, parent);
	}
}

static Operand
analyze_ptr_from_int(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	TypeId type;
	if (!operand_to_type(block, resolve_ref(block, inst->data.bin.lhs), inst->src, &type))
		return operand_none;
	Operand operand = coerce(block, resolve_ref(block, inst->data.bin.rhs), T_USIZE, inst->src);
	if (operand.kind == OPERAND_NONE)
		return operand_none;
	if (sema->types[type].kind != TYPE_POINTER) {
		sema_error(block, inst->src, "expected pointer type, found '%s'", sema_type_name(sema, type));
		return operand_none;
	}
	TypePointer shape = sema->types[type].as.pointer;
	if (shape.size == POINTER_SLICE) {
		sema_error(block, inst->src, "integer cannot be converted to slice type '%s'", sema_type_name(sema, type));
		return operand_none;
	}
	if (!check_defined(block, operand, inst->src))
		return operand_none;
	uint32_t alignment = pointer_alignment(block, shape, inst->src);
	if (operand.kind == OPERAND_COMPTIME) {
		uint64_t address = bigint_to_u64_bits(&operand_value(block, operand)->as.integer);
		if (address == 0 && !shape.is_allowzero) {
			sema_error(block, inst->src, "pointer type '%s' does not allow address zero", sema_type_name(sema, type));
			return operand_none;
		}
		if (alignment > 1 && (address & (alignment - 1)) != 0) {
			sema_error(block, inst->src, "pointer type '%s' requires aligned address", sema_type_name(sema, type));
			return operand_none;
		}
		return comptime_operand(type, value_int_pointer(&sema->values, type, address));
	}
	if (!require_runtime(block, inst->src))
		return operand_none;
	return runtime_operand(type, emit_tir_un(block, TIR_PTR_FROM_INT, type, inst->src, operand.inst));
}

/* Length inference is separate from runtime evaluation so address guards execute
 * before a dynamic divisibility check, just as in the reference compiler. */
typedef struct SliceCastLength {
	Operand value;
	uint64_t multiplier;
	uint64_t divisor;
} SliceCastLength;

static bool
ptr_cast_slice_length(Block *block, Operand operand, TypeId destination, uint32_t src, SliceCastLength *result)
{
	Sema *sema = block->sema;
	TypePointer from = sema->types[operand.type].as.pointer;
	TypeId child = from.child;
	if (from.size == POINTER_MANY || from.size == POINTER_C) {
		sema_error(block, src, "cannot infer length of slice from %s",
			from.size == POINTER_MANY ? "many pointer" : "C pointer");
		return false;
	}
	uint64_t count = 1;
	if (from.size == POINTER_ONE && sema->types[child].kind == TYPE_ARRAY) {
		count = sema->types[child].as.array.length;
		child = sema->types[child].as.array.child;
	}
	Operand length = operand_none;
	if (from.size == POINTER_SLICE) {
		length = slice_field(block, operand, true, src);
		if (length.kind == OPERAND_NONE)
			return false;
	}
	result->value = length;
	TypeId target = sema->types[destination].as.pointer.child;
	if (child == target) {
		if (from.size != POINTER_SLICE)
			result->value = comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, count));
		return true;
	}
	if (!ensure_type_layout(block, child, src) || !ensure_type_layout(block, target, src))
		return false;
	uint64_t source_size, target_size;
	if (!sema_type_size(sema, child, &source_size) || !sema_type_size(sema, target, &target_size)) {
		sema_error(block, src, "not supported by szc yet: slice cast of type with unknown storage size");
		return false;
	}
	if (target_size == 0) {
		sema_error(block, src, "cannot infer length of slice of zero-bit '%s' from '%s'",
			sema_type_name(sema, target), sema_type_name(sema, operand.type));
		return false;
	}
	if (from.size != POINTER_SLICE || length.kind == OPERAND_COMPTIME) {
		if (from.size == POINTER_SLICE) {
			if (operand_is_undefined(block, length))
				return true;
			count = bigint_to_u64_bits(&operand_value(block, length)->as.integer);
		}
		if (source_size != 0 && count > UINT64_MAX / source_size) {
			sema_error(block, src, "not supported by szc yet: slice cast byte extent exceeds usize");
			return false;
		}
		uint64_t bytes = count * source_size;
		if (bytes % target_size != 0) {
			if (from.size == POINTER_SLICE)
				sema_error(block, src, "slice length '%" PRIu64 "' does not divide exactly into destination elements", count);
			else
				sema_error(block, src, "type '%s' does not divide exactly into destination elements",
					sema_type_name(sema, from.child));
			return false;
		}
		result->value = comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, bytes / target_size));
		return true;
	}
	/* Prefer the reference's integer-ratio fast paths. Its runtime multiply is
	 * unchecked usize arithmetic; do not introduce an unrelated overflow panic. */
	if (source_size % target_size == 0)
		result->multiplier = source_size / target_size;
	else if (source_size != 0 && target_size % source_size == 0)
		result->divisor = target_size / source_size;
	else {
		result->multiplier = source_size;
		result->divisor = target_size;
	}
	return true;
}

/* A slice owns only its descriptor; its data pointer retains the original backing. */
static Operand
finish_ptr_cast(Block *block, Operand pointer, TypeId type, SliceCastLength length, uint32_t src)
{
	if (length.value.kind == OPERAND_NONE)
		return pointer;
	Sema *sema = block->sema;
	Operand count = length.value;
	if (length.multiplier != 1) {
		Operand factor = comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, length.multiplier));
		count = runtime_operand(T_USIZE, emit_tir_bin(block, TIR_MUL_WRAP, T_USIZE, src,
			materialize(block, count, src), materialize(block, factor, src)));
	}
	if (length.divisor != 1) {
		Operand divisor = comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, length.divisor));
		TirRef lhs = materialize(block, count, src);
		TirRef rhs = materialize(block, divisor, src);
		emit_tir_bin(block, TIR_SLICE_CAST_LEN_CHECK, T_VOID, src, lhs, rhs);
		count = runtime_operand(T_USIZE, emit_tir_bin(block, TIR_DIV_TRUNC, T_USIZE, src, lhs, rhs));
	}
	return make_slice(block, type, pointer, count, src);
}

/* Only cast/qualifier wrappers preserve the exact subobject. Stop at indexing
 * or descriptor projections: equal numeric offsets alone do not prove a type. */
static Operand
restore_pointer_view(Block *block, Operand pointer, TypeId type, uint32_t src)
{
	Sema *sema = block->sema;
	TypePointer target = sema->types[type].as.pointer;
	ValueId candidate = pointer.value;
	ValueId restored = VALUE_NONE;
	for (;;) {
		Value value = *value_get(&sema->values, candidate);
		if (sema->types[value.type].kind == TYPE_POINTER) {
			TypePointer original = sema->types[value.type].as.pointer;
			if (original.child == target.child && original.size == target.size
				&& pointer_alignment(block, original, src) >= pointer_alignment(block, target, src))
				restored = candidate;
			/* An exact typed array-field decay is an element path, not a byte
			 * reinterpret load. Preserve its field origin and declared extent. */
			if (original.size == POINTER_ONE && target.size == POINTER_MANY
				&& sema->types[original.child].kind == TYPE_ARRAY
				&& sema->types[original.child].as.array.child == target.child
				&& pointer_alignment(block, original, src) >= pointer_alignment(block, target, src)) {
				ValueId field;
				PointerAddress address;
				uint32_t work = 0;
				if (!struct_pointer_view(block, candidate, &field, &address, &work, src))
					return operand_none;
				if (field != VALUE_NONE)
					restored = candidate;
			}
		}
		if (value.kind != VALUE_REINTERPRET_PTR && value.kind != VALUE_PTR_CAST)
			break;
		candidate = value.as.pointer;
	}
	if (restored == VALUE_NONE)
		return comptime_operand(type, value_pointer_reinterpret(&sema->values, type, pointer.value));
	const Value *value = value_get(&sema->values, restored);
	return retag_pointer(block, comptime_operand(value->type, restored), type, src);
}

/* Casts change address interpretation, not the contents of known backing. */
static Operand
analyze_ptr_cast(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	IrPointerCast cast;
	memcpy(&cast, &block->ir->extra[inst->data.payload], sizeof(cast));
	TypeId type;
	if (!operand_to_type(block, resolve_ref(block, cast.type), inst->src, &type))
		return operand_none;
	Operand operand = resolve_ref(block, cast.operand);
	if (operand.kind == OPERAND_NONE)
		return operand_none;
	if (sema->types[type].kind != TYPE_POINTER || sema->types[operand.type].kind != TYPE_POINTER) {
		sema_error(block, inst->src, "expected pointer, found '%s'",
			sema_type_name(sema, sema->types[type].kind != TYPE_POINTER ? type : operand.type));
		return operand_none;
	}
	TypePointer from = sema->types[operand.type].as.pointer;
	TypePointer to = sema->types[type].as.pointer;
	SliceCastLength length = {operand_none, 1, 1};
	TypeId pointer_type = type;
	if (to.size == POINTER_SLICE) {
		if (!ptr_cast_slice_length(block, operand, type, inst->src, &length))
			return operand_none;
		pointer_type = slice_pointer_type(sema, type);
		to = sema->types[pointer_type].as.pointer;
	}
	uint32_t source_alignment = pointer_alignment(block, from, inst->src);
	uint32_t target_alignment = pointer_alignment(block, to, inst->src);
	if (target_alignment > source_alignment && (cast.flags & IR_CAST_ALIGN) == 0) {
		sema_error(block, inst->src, "@ptrCast increases pointer alignment");
		return operand_none;
	}
	if (from.is_const && !to.is_const && (cast.flags & IR_CAST_CONST) == 0) {
		sema_error(block, inst->src, "@ptrCast discards const qualifier");
		return operand_none;
	}
	if (from.is_volatile && !to.is_volatile && (cast.flags & IR_CAST_VOLATILE) == 0) {
		sema_error(block, inst->src, "@ptrCast discards volatile qualifier");
		return operand_none;
	}
	if (operand_is_undefined(block, operand)) {
		if (!to.is_allowzero && !check_defined(block, operand, cast.operand_src))
			return operand_none;
		return comptime_operand(type, value_undefined(&sema->values, type));
	}
	/* Only runtime slice-to-slice casts exempt an empty source from address
	 * checks. Constant casts and thin results still validate their data address. */
	bool slice_to_slice = from.size == POINTER_SLICE && length.value.kind != OPERAND_NONE;
	bool source_slice = from.size == POINTER_SLICE;
	if (source_slice) {
		operand = slice_field(block, operand, false, inst->src);
		if (operand.kind == OPERAND_NONE)
			return operand_none;
		from.size = POINTER_MANY;
	}
	if (operand.kind == OPERAND_COMPTIME) {
		if (integer_pointer(block, operand)) {
			uint64_t address = operand_value(block, operand)->as.address;
			if (address == 0 && !to.is_allowzero && !source_slice) {
				sema_error(block, cast.operand_src, "null pointer casted to type '%s'", sema_type_name(sema, type));
				return operand_none;
			}
			if (target_alignment > source_alignment && (address & (target_alignment - 1)) != 0) {
				sema_error(block, cast.operand_src, "pointer address 0x%" PRIX64 " is not aligned to %u bytes",
					address, (unsigned)target_alignment);
				return operand_none;
			}
			Operand result = comptime_operand(pointer_type, value_int_pointer(&sema->values, pointer_type, address));
			return finish_ptr_cast(block, result, type, length, inst->src);
		}
		Operand result;
		if (from.child != to.child || from.size != to.size)
			result = restore_pointer_view(block, operand, pointer_type, inst->src);
		else
			result = retag_pointer(block, operand, pointer_type, inst->src);
		return finish_ptr_cast(block, result, type, length, inst->src);
	}
	if (!require_runtime(block, inst->src))
		return operand_none;
	Operand empty = operand_none;
	if (target_alignment > source_alignment || (slice_to_slice && from.is_allowzero && !to.is_allowzero)) {
		empty = comptime_operand(T_BOOL, value_bool(&sema->values, T_BOOL, false));
		if (slice_to_slice) {
			Operand zero = comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, 0));
			empty = runtime_operand(T_BOOL, emit_tir_bin(block, TIR_CMP_EQ, T_BOOL, inst->src,
				materialize(block, length.value, inst->src), materialize(block, zero, inst->src)));
		}
	}
	TirRef pointer = materialize(block, operand, inst->src);
	if (from.is_allowzero && !to.is_allowzero) {
		if (slice_to_slice)
			emit_tir_bin(block, TIR_SLICE_NULL_CHECK, T_VOID, inst->src, pointer, materialize(block, empty, inst->src));
		else
			emit_tir_un(block, TIR_NULL_CHECK, T_VOID, inst->src, pointer);
	}
	if (target_alignment > source_alignment) {
		emit_tir_bin(block, TIR_ALIGN_CHECK, pointer_type, inst->src, pointer, materialize(block, empty, inst->src));
	}
	Operand result = runtime_operand(pointer_type, emit_tir_un(block, TIR_PTR_CAST, pointer_type, inst->src, pointer));
	return finish_ptr_cast(block, result, type, length, inst->src);
}

/* @alignCast changes only alignment; nested qualifier flags are explicit permissions. */
static Operand
analyze_align_cast(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	IrPointerCast cast;
	memcpy(&cast, &block->ir->extra[inst->data.payload], sizeof(cast));
	TypeId type;
	if (!operand_to_type(block, resolve_ref(block, cast.type), inst->src, &type))
		return operand_none;
	Operand operand = resolve_ref(block, cast.operand);
	if (operand.kind == OPERAND_NONE)
		return operand_none;
	if (sema->types[type].kind != TYPE_POINTER || sema->types[operand.type].kind != TYPE_POINTER) {
		sema_error(block, inst->src, "expected pointer, found '%s'",
			sema_type_name(sema, sema->types[type].kind != TYPE_POINTER ? type : operand.type));
		return operand_none;
	}
	TypePointer from = sema->types[operand.type].as.pointer;
	TypePointer to = sema->types[type].as.pointer;
	bool array_to_slice = from.size == POINTER_ONE && to.size == POINTER_SLICE
		&& sema->types[from.child].kind == TYPE_ARRAY;
	if (from.size != to.size && !array_to_slice) {
		sema_error(block, inst->src, "expected type '%s', found '%s'", sema_type_name(sema, type), sema_type_name(sema, operand.type));
		return operand_none;
	}
	TypeId source_child = array_to_slice ? sema->types[from.child].as.array.child : from.child;
	if (!memory_coercion_allowed(block, to.child, source_child, !to.is_const, inst->src)) {
		sema_error(block, inst->src, "pointer element type '%s' cannot coerce into element type '%s'",
			sema_type_name(sema, source_child), sema_type_name(sema, to.child));
		return operand_none;
	}
	if (from.is_const && !to.is_const && (cast.flags & IR_CAST_CONST) == 0) {
		sema_error(block, inst->src, "@alignCast discards const qualifier");
		return operand_none;
	}
	if (from.is_volatile && !to.is_volatile && (cast.flags & IR_CAST_VOLATILE) == 0) {
		sema_error(block, inst->src, "@alignCast discards volatile qualifier");
		return operand_none;
	}
	if (from.is_allowzero && !to.is_allowzero) {
		sema_error(block, inst->src, "'%s' could have null values which are illegal in type '%s'",
			sema_type_name(sema, operand.type), sema_type_name(sema, type));
		return operand_none;
	}
	uint32_t source_alignment = pointer_alignment(block, from, inst->src);
	uint32_t target_alignment = pointer_alignment(block, to, inst->src);
	Type adjusted = sema->types[operand.type];
	adjusted.as.pointer.alignment = target_alignment;
	if ((cast.flags & IR_CAST_CONST) != 0)
		adjusted.as.pointer.is_const = false;
	if ((cast.flags & IR_CAST_VOLATILE) != 0)
		adjusted.as.pointer.is_volatile = false;
	TypeId adjusted_type = type_intern(sema, &adjusted);
	if (!array_slice_allowed(block, type, adjusted_type, inst->src)
		&& !memory_coercion_allowed(block, type, adjusted_type, false, inst->src)) {
		sema_error(block, inst->src, "expected type '%s', found '%s'", sema_type_name(sema, type), sema_type_name(sema, operand.type));
		return operand_none;
	}
	if (operand_is_undefined(block, operand)) {
		if (!to.is_allowzero && !check_defined(block, operand, cast.operand_src))
			return operand_none;
		return comptime_operand(type, value_undefined(&sema->values, type));
	}
	if (target_alignment > source_alignment) {
		Operand pointer = from.size == POINTER_SLICE ? slice_field(block, operand, false, inst->src) : operand;
		if (pointer.kind == OPERAND_NONE)
			return operand_none;
		if (pointer.kind == OPERAND_COMPTIME) {
			PointerAddress address = value_pointer_address(&sema->values, pointer.value);
			if (value_get(&sema->values, address.root)->kind == VALUE_INT_PTR
				&& (address.offset & (target_alignment - 1)) != 0) {
				sema_error(block, cast.operand_src, "pointer address 0x%" PRIX64 " is not aligned to %u bytes",
					address.offset, (unsigned)target_alignment);
				return operand_none;
			}
		}
		if (operand.kind != OPERAND_COMPTIME) {
			if (!require_runtime(block, inst->src))
				return operand_none;
			Operand empty = comptime_operand(T_BOOL, value_bool(&sema->values, T_BOOL, false));
			if (from.size == POINTER_SLICE && to.size == POINTER_SLICE) {
				Operand length = slice_field(block, operand, true, inst->src);
				if (length.kind == OPERAND_NONE)
					return operand_none;
				TirRef length_ref = materialize(block, length, inst->src);
				Operand zero = comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, 0));
				TirRef zero_ref = materialize(block, zero, inst->src);
				empty = runtime_operand(T_BOOL, emit_tir_bin(block, TIR_CMP_EQ, T_BOOL, inst->src, length_ref, zero_ref));
			}
			Type aligned_pointer = sema->types[pointer.type];
			aligned_pointer.as.pointer.alignment = target_alignment;
			TypeId checked_type = type_intern(sema, &aligned_pointer);
			TirRef pointer_ref = materialize(block, pointer, inst->src);
			TirRef empty_ref = materialize(block, empty, inst->src);
			emit_tir_bin(block, TIR_ALIGN_CHECK, checked_type, inst->src, pointer_ref, empty_ref);
		}
	}
	return coerce(block, retag_pointer(block, operand, adjusted_type, inst->src), type, inst->src);
}

/* Symbolic pointers have runtime addresses; undefined pointers retain undefined. */
static Operand
analyze_int_from_ptr(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	Operand pointer = resolve_ref(block, inst->data.un.operand);
	if (pointer.kind == OPERAND_NONE)
		return operand_none;
	if (sema->types[pointer.type].kind != TYPE_POINTER || sema->types[pointer.type].as.pointer.size == POINTER_SLICE) {
		sema_error(block, inst->src, "expected pointer, found '%s'", sema_type_name(sema, pointer.type));
		return operand_none;
	}
	if (operand_is_undefined(block, pointer))
		return comptime_operand(T_USIZE, value_undefined(&sema->values, T_USIZE));
	if (integer_pointer(block, pointer))
		return comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, operand_value(block, pointer)->as.address));
	if (pointer.kind == OPERAND_COMPTIME) {
		PointerAddress address = value_pointer_address(&sema->values, pointer.value);
		if (value_get(&sema->values, address.root)->kind == VALUE_INT_PTR)
			return comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, address.offset));
	}
	TypeId leaf = sema->types[pointer.type].as.pointer.child;
	while (sema->types[leaf].kind == TYPE_ARRAY)
		leaf = sema->types[leaf].as.array.child;
	if (pointer.kind == OPERAND_COMPTIME && is_comptime_only_type(sema, leaf) && sema->types[leaf].kind != TYPE_FN) {
		sema_error(block, inst->src, "not supported by szc yet: runtime address of comptime-only backing");
		return operand_none;
	}
	if (!require_runtime(block, inst->src))
		return operand_none;
	TirRef address = materialize(block, pointer, inst->src);
	return runtime_operand(T_USIZE, emit_tir_un(block, TIR_PTR_TO_INT, T_USIZE, inst->src, address));
}

/* Empty arrays are OPV even when their element type is comptime-only. */
static bool
layout_is_comptime_only(const Sema *sema, TypeId type)
{
	while (sema->types[type].kind == TYPE_ARRAY) {
		TypeArray array = sema->types[type].as.array;
		if (array.length == 0 && array.sentinel == VALUE_NONE)
			return false;
		type = array.child;
	}
	return is_comptime_only_type(sema, type);
}

/* Queueing layout obligations never runs user expressions. Defaults are separate
 * analysis units so an owner can report its own errors before the queue drains. */
static bool
charge_defaults_work(Block *block, uint32_t src, uint32_t count)
{
	const uint32_t limit = 1048576;
	if (count > limit - block->sema->defaults_work) {
		sema_error(block, src, "not supported by szc yet: struct default work exceeds 1048576");
		return false;
	}
	block->sema->defaults_work += count;
	return true;
}

static bool
queue_struct_defaults(Block *block, SemaNamespace *ns, uint32_t src)
{
	if (ns->defaults_state != DEFAULTS_UNRESOLVED)
		return true;
	Sema *sema = block->sema;
	if (!charge_defaults_work(block, src, 1)) {
		ns->defaults_state = DEFAULTS_FAILED;
		return false;
	}
	if (sema->defaults_queue_count == sema->defaults_queue_capacity) {
		uint32_t capacity = sema->defaults_queue_capacity == 0 ? 16 : sema->defaults_queue_capacity * 2;
		sema->defaults_queue = arena_grow_array(sema->arena, sema->defaults_queue,
			sema->defaults_queue_count, capacity, sizeof(SemaNamespace *), alignof(SemaNamespace *));
		sema->defaults_queue_capacity = capacity;
	}
	ns->defaults_state = DEFAULTS_QUEUED;
	sema->defaults_queue[sema->defaults_queue_count++] = ns;
	return true;
}

/* Tag expressions are independent definition-context bodies. Resolve every
 * explicit value before implicit numbering/duplicate checks; publish atomically. */
static bool
ensure_enum_layout(Block *block, TypeId type, uint32_t src, const char *reason)
{
	Sema *sema = block->sema;
	SemaNamespace *ns = sema->types[type].as.structure;
	if (ns->layout_state == STRUCT_RESOLVED)
		return true;
	if (ns->layout_state == STRUCT_FAILED)
		return false;
	if (ns->layout_state == STRUCT_IN_PROGRESS) {
		sema_error(block, src, "type '%s' depends on itself for %s here", sema_type_name(sema, type), reason);
		return false;
	}
	if (sema->layout_depth >= 256 || ns->field_count > 1048576 - sema->enum_work) {
		sema_error(block, src, "not supported by szc yet: enum layout depth/work limit exceeded");
		ns->layout_state = STRUCT_FAILED;
		return false;
	}
	sema->enum_work += ns->field_count;
	sema->layout_depth++;
	ns->layout_state = STRUCT_IN_PROGRESS;
	InstMap map;
	inst_map_init(&map, sema->arena);
	map.parent = ns->captures;
	Block fields;
	block_init_comptime(&fields, sema, ns, &map, STRING_NONE);
	fields.comptime_value_required = true;
	IrContainerHeader header;
	IR_READ(ns->ir, ns->ir->insts[ns->inst].data.payload, &header);
	TypeId tag;
	if (header.arg_body != 0) {
		BodyResult result = analyze_body(&fields, header.arg_body);
		if (result.end != BODY_BREAK_RESULT || !operand_to_type(&fields, result.result, src, &tag))
			goto fail;
		if (sema->types[tag].kind != TYPE_INT && tag != T_NORETURN) {
			sema_error(&fields, src, "expected integer tag type, found '%s'", sema_type_name(sema, tag));
			goto fail;
		}
		if (ns->field_count == 0 && tag != T_NORETURN) {
			sema_error(&fields, src, "empty exhaustive enums must be backed by 'noreturn'");
			goto fail;
		}
		if (ns->field_count != 0 && tag == T_NORETURN) {
			sema_error(&fields, src, "non-empty enums cannot be backed by 'noreturn'");
			goto fail;
		}
		if (tag != T_NORETURN && (sema->types[tag].as.integer.is_signed || sema->types[tag].as.integer.bits > 64)) {
			sema_error(&fields, src, "not supported by szc yet: signed or wide enum backing type");
			goto fail;
		}
	} else if (ns->field_count == 0) {
		tag = T_NORETURN;
	} else {
		uint16_t bits = 0;
		for (uint32_t highest = ns->field_count - 1; highest != 0; highest >>= 1)
			bits++;
		tag = type_int(sema, false, bits);
	}
	fields.enum_tag_type = tag;
	ValueId *values = ARENA_ARRAY(sema->arena, ValueId, ns->field_count);
	for (uint32_t i = 0; i < ns->field_count; i++) {
		values[i] = VALUE_NONE;
		SemaField *field = &ns->fields[i];
		if (field->ir.default_body == 0)
			continue;
		BodyResult result = analyze_body(&fields, field->ir.default_body);
		if (result.end != BODY_BREAK_RESULT || result.result.kind == OPERAND_NONE)
			goto fail;
		uint32_t value_src = ns->ir->insts[ir_body_insts(ns->ir, field->ir.default_body)
			[ir_body_count(ns->ir, field->ir.default_body) - 1]].src;
		Operand value = coerce(&fields, result.result, tag, value_src);
		if (value.kind == OPERAND_NONE || !check_defined(&fields, value, value_src))
			goto fail;
		if (value.kind != OPERAND_COMPTIME) {
			sema_error(&fields, value_src, "unable to resolve comptime value");
			goto fail;
		}
		values[i] = value.value;
	}
	uint32_t capacity = 1;
	while (capacity < ns->field_count * 2u)
		capacity *= 2;
	ValueId *seen = arena_zalloc(sema->arena, capacity * sizeof(ValueId), alignof(ValueId));
	BigInt zero = bigint_from_u64(sema->arena, 0);
	BigInt one = bigint_from_u64(sema->arena, 1);
	for (uint32_t i = 0; i < ns->field_count; i++) {
		SemaField *field = &ns->fields[i];
		if (values[i] == VALUE_NONE) {
			BigInt previous = i == 0 ? zero : value_get(&sema->values, values[i - 1])->as.integer;
			BigInt number = i == 0 ? zero : bigint_add(sema->arena, &previous, &one);
			if (!bigint_fits(&number, false, sema->types[tag].as.integer.bits)) {
				sema_error(&fields, field->ir.src, "enum tag value '%s' too large for type '%s'",
					bigint_to_string(sema->arena, &number), sema_type_name(sema, tag));
				goto fail;
			}
			values[i] = value_int(&sema->values, tag, &number);
		}
		uint32_t slot = hash_word(2166136261u, values[i]) & (capacity - 1);
		while (seen[slot] != VALUE_NONE && seen[slot] != values[i])
			slot = (slot + 1) & (capacity - 1);
		if (seen[slot] != VALUE_NONE) {
			BigInt number = value_get(&sema->values, values[i])->as.integer;
			sema_error(&fields, field->ir.src, "enum tag value '%s' for field '%s' already taken",
				bigint_to_string(sema->arena, &number), intern_text(sema->interner, field->ir.name));
			goto fail;
		}
		seen[slot] = values[i];
	}
	ns->enum_tag_type = tag;
	ns->enum_tags = values;
	ns->alignment = sema_type_align(sema, tag);
	ns->layout_state = STRUCT_RESOLVED;
	sema->layout_depth--;
	return true;
fail:
	ns->layout_state = STRUCT_FAILED;
	sema->layout_depth--;
	return false;
}

/* Layout is independent of runtime value support. Publish nominal identity before
 * entering field bodies so pointer recursion never becomes a by-value cycle. */
static bool
ensure_type_layout(Block *block, TypeId type, uint32_t src)
{
	Sema *sema = block->sema;
	while (sema->types[type].kind == TYPE_ARRAY)
		type = sema->types[type].as.array.child;
	if (sema->types[type].kind == TYPE_ENUM)
		return ensure_enum_layout(block, type, src, "size query");
	if (sema->types[type].kind != TYPE_STRUCT)
		return true;
	SemaNamespace *ns = sema->types[type].as.structure;
	if (ns->layout_state == STRUCT_RESOLVED)
		return true;
	if (ns->layout_state == STRUCT_FAILED)
		return false;
	if (ns->layout_state == STRUCT_IN_PROGRESS) {
		sema_error(block, src, "type '%s' depends on itself for field declared here", namespace_name(sema, ns));
		return false;
	}
	if (sema->layout_depth >= 256) {
		sema_error(block, src, "not supported by szc yet: struct layout dependency depth exceeds 256");
		return false;
	}
	sema->layout_depth++;
	ns->layout_state = STRUCT_IN_PROGRESS;
	InstMap map;
	inst_map_init(&map, sema->arena);
	map.parent = ns->captures;
	Block fields;
	block_init_comptime(&fields, sema, ns, &map, STRING_NONE);
	uint32_t alignment = 1;
	for (uint32_t i = 0; i < ns->field_count; i++) {
		SemaField *field = &ns->fields[i];
		if (field->ir.name == STRING_NONE || (field->ir.flags & IR_FIELD_COMPTIME) != 0
			|| field->ir.align_body != 0) {
			sema_error(&fields, field->ir.src, "not supported by szc yet: tuple, comptime or aligned struct fields");
			goto fail;
		}
		BodyResult result = analyze_body(&fields, field->ir.type_body);
		if (result.end != BODY_BREAK_RESULT
			|| !operand_to_type(&fields, result.result, field->ir.src, &field->type))
			goto fail;
		if (!ensure_type_layout(&fields, field->type, field->ir.src))
			goto fail;
		TypeId leaf = field->type;
		while (sema->types[leaf].kind == TYPE_ARRAY)
			leaf = sema->types[leaf].as.array.child;
		TypeKind kind = sema->types[leaf].kind;
		if (kind == TYPE_ANYOPAQUE) {
			sema_error(&fields, field->ir.src, "cannot directly embed opaque type 'anyopaque' in struct");
			goto fail;
		}
		if (is_comptime_only_type(sema, leaf) || kind == TYPE_NORETURN || kind == TYPE_ANYERROR
			|| (kind == TYPE_FLOAT && sema->types[leaf].as.floating.bits != 32 && sema->types[leaf].as.floating.bits != 64)) {
			sema_error(&fields, field->ir.src, "not supported by szc yet: struct field layout of type '%s'", sema_type_name(sema, field->type));
			goto fail;
		}
		if (!sema_type_size(sema, field->type, &field->size)) {
			sema_error(&fields, field->ir.src, "not supported by szc yet: struct field size exceeds 64-bit address space");
			goto fail;
		}
		field->alignment = sema_type_align(sema, field->type);
		if (field->alignment > alignment)
			alignment = field->alignment;
	}
	/* Declaration order owns field identities; only this separate permutation
	 * follows pinned Zig's exact unstable alignment sort. Offsets stay private
	 * until the entire checked layout succeeds. */
	uint32_t *order = ARENA_ARRAY(sema->arena, uint32_t, ns->field_count);
	uint32_t *alignments = ARENA_ARRAY(sema->arena, uint32_t, ns->field_count);
	for (uint32_t i = 0; i < ns->field_count; i++) {
		order[i] = i;
		alignments[i] = ns->fields[i].alignment;
	}
	struct_layout_order(order, alignments, ns->field_count);
	uint64_t size = 0;
	for (uint32_t i = 0; i < ns->field_count; i++) {
		SemaField *field = &ns->fields[order[i]];
		uint64_t padding = (0u - size) & (field->alignment - 1u);
		if (size > UINT64_MAX - padding || size + padding > UINT64_MAX - field->size) {
			sema_error(&fields, field->ir.src, "not supported by szc yet: struct layout size exceeds 64-bit address space");
			goto fail;
		}
		field->offset = size + padding;
		size = field->offset + field->size;
	}
	uint64_t padding = (0u - size) & (alignment - 1u);
	if (size > UINT64_MAX - padding) {
		sema_error(&fields, ns->ir->insts[ns->inst].src, "not supported by szc yet: struct layout size exceeds 64-bit address space");
		goto fail;
	}
	uint64_t total_size = size + padding;
	if (total_size > UINT32_MAX) {
		sema_error(&fields, ns->ir->insts[ns->inst].src,
			"struct layout requires size %" PRIu64 ", this compiler implementation supports up to %" PRIu32,
			total_size, UINT32_MAX);
		goto fail;
	}
	ns->size = total_size;
	ns->alignment = alignment;
	ns->field_order = order;
	ns->layout_state = STRUCT_RESOLVED;
	sema->layout_depth--;
	return queue_struct_defaults(block, ns, src);
fail:
	ns->layout_state = STRUCT_FAILED;
	sema->layout_depth--;
	return false;
}

static SemaField *
struct_find_field(SemaNamespace *ns, StringId name)
{
	for (uint32_t i = 0; i < ns->field_count; i++)
		if (ns->fields[i].ir.name == name)
			return &ns->fields[i];
	return NULL;
}

static Operand
analyze_type_layout(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	TypeId type;
	if (!operand_to_type(block, resolve_ref(block, inst->data.un.operand), inst->src, &type))
		return operand_none;
	bool size_of = inst->op == IR_SIZE_OF;
	const char *name = size_of ? "size" : "align";
	TypeId leaf = type;
	while (sema->types[leaf].kind == TYPE_ARRAY)
		leaf = sema->types[leaf].as.array.child;
	if (sema->types[leaf].kind == TYPE_NORETURN || sema->types[leaf].kind == TYPE_ANYOPAQUE) {
		sema_error(block, inst->src, "no %s available for uninstantiable type '%s'", name, sema_type_name(sema, type));
		return operand_none;
	}
	if (sema->types[leaf].kind == TYPE_ANYERROR) {
		sema_error(block, inst->src, "not supported by szc yet: layout of type '%s'", sema_type_name(sema, type));
		return operand_none;
	}
	if (size_of && layout_is_comptime_only(sema, type)) {
		sema_error(block, inst->src, "no size available for comptime-only type '%s'", sema_type_name(sema, type));
		return operand_none;
	}
	if (!ensure_type_layout(block, type, inst->src))
		return operand_none;
	if (sema->types[leaf].kind == TYPE_ENUM && sema_enum_tag_type(sema, leaf) == T_NORETURN) {
		sema_error(block, inst->src, "no %s available for uninstantiable type '%s'", name, sema_type_name(sema, type));
		return operand_none;
	}
	uint64_t result;
	if (!size_of) {
		result = sema_type_align(sema, type);
	} else if (!sema_type_size(sema, type, &result)) {
		sema_error(block, inst->src, "not supported by szc yet: array layout size exceeds 64-bit address space");
		return operand_none;
	}
	return comptime_operand(T_COMPTIME_INT, value_int_u64(&sema->values, T_COMPTIME_INT, result));
}

/* Representation bits exclude integer ABI padding, including inside arrays. */
static Operand
analyze_bit_size(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	TypeId type;
	if (!operand_to_type(block, resolve_ref(block, inst->data.un.operand), inst->src, &type))
		return operand_none;
	TypeId leaf = type;
	bool empty = false;
	while (sema->types[leaf].kind == TYPE_ARRAY) {
		TypeArray array = sema->types[leaf].as.array;
		empty |= array.length == 0 && array.sentinel == VALUE_NONE;
		leaf = array.child;
	}
	bool has_representation = true;
	uint64_t bits;
	switch (sema->types[leaf].kind) {
	case TYPE_VOID:
		bits = 0;
		break;
	case TYPE_BOOL:
		bits = 1;
		break;
	case TYPE_INT:
		bits = sema->types[leaf].as.integer.bits;
		break;
	case TYPE_FLOAT:
		bits = sema->types[leaf].as.floating.bits;
		break;
	case TYPE_POINTER:
		has_representation = sema->types[leaf].as.pointer.size != POINTER_SLICE;
		bits = 64;
		break;
	case TYPE_ENUM: {
		if (!ensure_enum_layout(block, leaf, inst->src, "size query"))
			return operand_none;
		TypeId tag = sema_enum_tag_type(sema, leaf);
		has_representation = tag != T_NORETURN;
		bits = has_representation ? sema->types[tag].as.integer.bits : 0;
		break;
	}
	case TYPE_STRUCT:
		/* Ordinary structs have no bit representation, even inside [0]S.
		 * Reject before demanding fields or layout, using the requested type. */
		has_representation = false;
		bits = 0;
		break;
	case TYPE_ANYERROR:
		sema_error(block, inst->src, "not supported by szc yet: bit size of error sets");
		return operand_none;
	default:
		has_representation = false;
		bits = 0;
		break;
	}
	if (!has_representation) {
		sema_error(block, inst->src, "no bit size available for type '%s'", sema_type_name(sema, type));
		return operand_none;
	}
	if (empty)
		bits = 0;
	for (TypeId current = type; sema->types[current].kind == TYPE_ARRAY; current = sema->types[current].as.array.child) {
		TypeArray array = sema->types[current].as.array;
		if (bits == 0)
			break;
		if ((array.sentinel != VALUE_NONE && array.length == UINT64_MAX) ||
			array.length + (array.sentinel != VALUE_NONE ? 1u : 0u) > UINT64_MAX / bits) {
			sema_error(block, inst->src, "not supported by szc yet: array bit size exceeds 64-bit range");
			return operand_none;
		}
		bits *= array.length + (array.sentinel != VALUE_NONE ? 1u : 0u);
	}
	return comptime_operand(T_COMPTIME_INT, value_int_u64(&sema->values, T_COMPTIME_INT, bits));
}

/* Common type for arithmetic operands and runtime control-flow exits. */
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
	if ((ak == TYPE_ENUM && bk == TYPE_ENUM_LITERAL) || (bk == TYPE_ENUM && ak == TYPE_ENUM_LITERAL)) {
		*out = ak == TYPE_ENUM ? a : b;
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
		if (memory_coercion_allowed(block, a, b, false, src)) {
			*out = a;
			return true;
		}
		if (memory_coercion_allowed(block, b, a, false, src)) {
			*out = b;
			return true;
		}
	}
	if (ak == TYPE_POINTER && bk == TYPE_POINTER) {
		TypePointer ap = sema->types[a].as.pointer;
		TypePointer bp = sema->types[b].as.pointer;
		bool many_array = (ap.size == POINTER_MANY && bp.size == POINTER_ONE && sema->types[bp.child].kind == TYPE_ARRAY)
			|| (bp.size == POINTER_MANY && ap.size == POINTER_ONE && sema->types[ap.child].kind == TYPE_ARRAY);
		bool slice_array = (ap.size == POINTER_SLICE && bp.size == POINTER_ONE && sema->types[bp.child].kind == TYPE_ARRAY)
			|| (bp.size == POINTER_SLICE && ap.size == POINTER_ONE && sema->types[ap.child].kind == TYPE_ARRAY);
		bool array_slice = ap.size == POINTER_ONE && bp.size == POINTER_ONE
			&& sema->types[ap.child].kind == TYPE_ARRAY && sema->types[bp.child].kind == TYPE_ARRAY
			&& sema->types[ap.child].as.array.length != sema->types[bp.child].as.array.length;
		if (ap.size == bp.size || many_array || slice_array) {
			Type candidate = make_type(TYPE_POINTER);
			candidate.as.pointer = ap;
			if (array_slice) {
				candidate.as.pointer.size = POINTER_SLICE;
				candidate.as.pointer.child = sema->types[ap.child].as.array.child;
			} else if (many_array || slice_array) {
				candidate.as.pointer.size = slice_array ? POINTER_SLICE : POINTER_MANY;
				candidate.as.pointer.child = ap.size != POINTER_ONE ? ap.child : bp.child;
			}
			if (!array_slice && ap.size == POINTER_ONE && bp.size == POINTER_ONE
				&& sema->types[ap.child].kind == TYPE_ARRAY && sema->types[bp.child].kind == TYPE_ARRAY
				&& sema->types[ap.child].as.array.length == sema->types[bp.child].as.array.length
				&& sema->types[ap.child].as.array.sentinel != sema->types[bp.child].as.array.sentinel) {
				Type array = sema->types[ap.child];
				array.as.array.sentinel = VALUE_NONE;
				candidate.as.pointer.child = type_intern(sema, &array);
			}
			candidate.as.pointer.sentinel = ap.sentinel == bp.sentinel ? ap.sentinel : VALUE_NONE;
			if (array_slice) {
				ValueId first = sema->types[ap.child].as.array.sentinel;
				ValueId second = sema->types[bp.child].as.array.sentinel;
				candidate.as.pointer.sentinel = first == second ? first : VALUE_NONE;
			}
			if (many_array || slice_array) {
				TypePointer many = ap.size != POINTER_ONE ? ap : bp;
				TypeArray array = sema->types[ap.size == POINTER_ONE ? ap.child : bp.child].as.array;
				candidate.as.pointer.sentinel = array.sentinel == many.sentinel ? many.sentinel : VALUE_NONE;
			}
			candidate.as.pointer.is_const = ap.is_const || bp.is_const;
			candidate.as.pointer.is_volatile = ap.is_volatile || bp.is_volatile;
			candidate.as.pointer.is_allowzero = ap.is_allowzero || bp.is_allowzero;
			if (ap.alignment != 0 || bp.alignment != 0) {
				uint32_t a_align = pointer_alignment(block, ap, src);
				uint32_t b_align = pointer_alignment(block, bp, src);
				candidate.as.pointer.alignment = a_align < b_align ? a_align : b_align;
			}
			TypeId merged = type_intern(sema, &candidate);
			if ((memory_coercion_allowed(block, merged, a, false, src) || array_slice_allowed(block, merged, a, src))
				&& (memory_coercion_allowed(block, merged, b, false, src) || array_slice_allowed(block, merged, b, src))) {
				*out = merged;
				return true;
			}
			candidate.as.pointer.child = array_slice ? sema->types[bp.child].as.array.child
				: (many_array || slice_array) ? candidate.as.pointer.child : bp.child;
			merged = type_intern(sema, &candidate);
			if ((memory_coercion_allowed(block, merged, a, false, src) || array_slice_allowed(block, merged, a, src))
				&& (memory_coercion_allowed(block, merged, b, false, src) || array_slice_allowed(block, merged, b, src))) {
				*out = merged;
				return true;
			}
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
	sema_error(block, src, block->comptime_value_required ? "unable to resolve comptime value" : "unable to evaluate comptime expression");
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
	case VALUE_INT_PTR:
		return true;
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

/* A field path is semantic provenance, not merely its physical byte offset.
 * Strip views/indexing only as far as the nearest field, retaining displacement
 * within that field. Array-only comparisons keep their established behavior. */
static bool
struct_pointer_view(Block *block, ValueId id, ValueId *field, PointerAddress *address, uint32_t *work, uint32_t src)
{
	Sema *sema = block->sema;
	*address = (PointerAddress){id, 0, false};
	*field = VALUE_NONE;
	uint64_t field_offset = 0;
	bool selected = false;
	for (;;) {
		if (*work >= 4096) {
			sema_error(block, src, "not supported by szc yet: pointer provenance comparison work budget exceeded (4096)");
			return false;
		}
		(*work)++;
		const Value *value = value_get(&sema->values, id);
		switch (value->kind) {
		case VALUE_STRUCT_FIELD_PTR:
			if (*field == VALUE_NONE) {
				*field = id;
				field_offset = value->as.struct_field.offset;
			}
			if (!selected) {
				address->offset = value->as.struct_field.offset;
				selected = true;
			}
			id = value->as.struct_field.base;
			break;
		case VALUE_PTR_CAST:
		case VALUE_REINTERPRET_PTR:
			id = value->as.pointer;
			break;
		case VALUE_ELEMENT_PTR:
			if (!selected) {
				address->offset = value->as.element.offset;
				address->logical = value->as.element.logical;
				selected = true;
			}
			id = value->as.element.base;
			break;
		case VALUE_SLICE_FIELD_PTR:
			if (!selected) {
				address->offset = value->as.field.offset;
				selected = true;
			}
			id = value->as.field.base;
			break;
		default:
			address->root = id;
			if (*field != VALUE_NONE)
				address->offset -= field_offset;
			else if (value->kind == VALUE_INT_PTR)
				address->offset += value->as.address;
			return true;
		}
	}
}

static bool
known_pointer_equal(Block *block, ValueId lhs, ValueId rhs, uint32_t src, bool *out)
{
	Sema *sema = block->sema;
	uint32_t work = 0;
	for (;;) {
		ValueId left_field;
		ValueId right_field;
		PointerAddress left;
		PointerAddress right;
		if (!struct_pointer_view(block, lhs, &left_field, &left, &work, src)
			|| !struct_pointer_view(block, rhs, &right_field, &right, &work, src))
			return false;
		if (left_field == VALUE_NONE || right_field == VALUE_NONE) {
			*out = left_field == right_field && pointer_same_root(sema, left, right)
				&& left.offset == right.offset && (left.offset == 0 || left.logical == right.logical);
			return true;
		}
		StructFieldPointer a = value_get(&sema->values, left_field)->as.struct_field;
		StructFieldPointer b = value_get(&sema->values, right_field)->as.struct_field;
		TypeId a_base = value_get(&sema->values, a.base)->type;
		TypeId b_base = value_get(&sema->values, b.base)->type;
		/* Canonical field bases erase qualifiers, not nominal aggregate type.
		 * Numeric roots of two distinct structs can share the same address. */
		if (sema->types[a_base].as.pointer.child != sema->types[b_base].as.pointer.child
			|| a.index != b.index || left.offset != right.offset
			|| (left.offset != 0 && left.logical != right.logical)) {
			*out = false;
			return true;
		}
		lhs = a.base;
		rhs = b.base;
	}
}

static bool array_element_stride(Block *block, TypeId child, uint64_t *stride, bool *logical, uint32_t src);
static TypeId array_element_pointer_type(Block *block, TypePointer pointer, TypeArray array, Operand index, uint64_t stride, bool logical, uint32_t src);

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
	if (!ensure_type_layout(block, child, src))
		return operand_none;
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
		ValueId left_field;
		ValueId right_field;
		PointerAddress left_view;
		PointerAddress right_view;
		uint32_t work = 0;
		if (!struct_pointer_view(block, lhs.value, &left_field, &left_view, &work, src)
			|| !struct_pointer_view(block, rhs.value, &right_field, &right_view, &work, src))
			return operand_none;
		PointerAddress left = left_view;
		PointerAddress right = right_view;
		bool same_base = left_field == VALUE_NONE && right_field == VALUE_NONE
			&& pointer_same_root(sema, left, right);
		if (left_field != VALUE_NONE && right_field != VALUE_NONE
			&& !known_pointer_equal(block, left_field, right_field, src, &same_base))
			return operand_none;
		/* Different field bases lower at runtime even if their byte addresses
		 * share a physical root. Only one canonical base permits constant math. */
		if (same_base) {
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
	if (!array_element_stride(block, shape.child, &stride, &logical, src)) {
		sema_error(block, src, "not supported by szc yet: pointer element offset exceeds 64-bit address space");
		return operand_none;
	}
	if (stride == 0)
		return base;
	TypeArray element = {shape.child, 0, VALUE_NONE};
	TypeId single = array_element_pointer_type(block, shape, element, offset, stride, logical, src);
	Type result_type = sema->types[single];
	result_type.as.pointer.size = POINTER_MANY;
	result_type.as.pointer.sentinel = shape.sentinel;
	TypeId type = type_intern(sema, &result_type);
	if (operand_is_undefined(block, base))
		return comptime_operand(type, value_undefined(&sema->values, type));
	if (base.kind == OPERAND_COMPTIME && offset.kind == OPERAND_COMPTIME) {
		uint64_t index = bigint_to_u64_bits(&operand_value(block, offset)->as.integer);
		if (index == 0)
			return base;
		ValueId field;
		PointerAddress address;
		uint32_t work = 0;
		if (!struct_pointer_view(block, base.value, &field, &address, &work, src))
			return operand_none;
		/* A typed field is its own containing value, even when the enclosing
		 * object has earlier bytes. Reject escape at pointer formation, not
		 * merely when a later load attempts to read those bytes. */
		if (field != VALUE_NONE) {
			if (op == IR_SUB && !logical
				&& (index > UINT64_MAX / stride || index * stride > address.offset)) {
				sema_error(block, src, "pointer computation here causes illegal behavior");
				return operand_none;
			}
			address.offset += value_get(&sema->values, field)->as.struct_field.offset;
		}
		if (integer_pointer(block, base)) {
			uint64_t delta = index * stride;
			if (op == IR_SUB && (index > UINT64_MAX / stride || delta > address.offset)) {
				sema_error(block, src, "pointer computation here causes illegal behavior");
				return operand_none;
			}
			uint64_t result = op == IR_SUB ? address.offset - delta : address.offset + delta;
			return comptime_operand(type, value_int_pointer(&sema->values, type, result));
		}
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

/* Scalar integer bit-count builtins use the smallest unsigned type holding width. */
static Operand
analyze_bit_count(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	Operand operand = resolve_ref(block, inst->data.un.operand);
	if (operand.kind == OPERAND_NONE)
		return operand_none;
	if (!is_int(sema, operand.type)) {
		sema_error(block, inst->src, "expected integer or vector, found '%s'", sema_type_name(sema, operand.type));
		return operand_none;
	}
	uint32_t bits = sema->types[operand.type].as.integer.bits;
	TypeId type = type_int(sema, false, log2_int_bits(bits + 1));
	if (bits == 0)
		return comptime_operand(type, value_int_u64(&sema->values, type, 0));
	if (operand_is_undefined(block, operand))
		return comptime_operand(type, value_undefined(&sema->values, type));
	if (operand.kind == OPERAND_COMPTIME) {
		BigIntCountOp op = inst->op == IR_CLZ ? BIGINT_CLZ : inst->op == IR_CTZ ? BIGINT_CTZ : BIGINT_POP_COUNT;
		uint32_t count = bigint_bit_count(sema->arena, &operand_value(block, operand)->as.integer, bits, op);
		return comptime_operand(type, value_int_u64(&sema->values, type, count));
	}
	if (!require_runtime(block, inst->src))
		return operand_none;
	TirOp op = inst->op == IR_CLZ ? TIR_CLZ : inst->op == IR_CTZ ? TIR_CTZ : TIR_POP_COUNT;
	return runtime_operand(type, emit_tir_un(block, op, type, inst->src, operand.inst));
}

/* Integer absolute values include signed minimum without overflow. */
static Operand
analyze_abs(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	Operand operand = resolve_ref(block, inst->data.un.operand);
	if (operand.kind == OPERAND_NONE)
		return operand_none;
	TypeId type = operand.type;
	TypeKind kind = sema->types[type].kind;
	if (kind == TYPE_FLOAT || kind == TYPE_COMPTIME_FLOAT) {
		if (operand_is_undefined(block, operand))
			return operand;
		sema_error(block, inst->src, "not supported by szc yet: floating-point @abs");
		return operand_none;
	}
	if (!is_int_or_comptime_int(sema, type)) {
		sema_error(block, inst->src, "expected integer, float, or vector of either integers or floats, found '%s'", sema_type_name(sema, type));
		return operand_none;
	}
	if (kind == TYPE_INT && !is_signed_type(sema, type)) {
		if (sema->types[type].as.integer.bits == 0)
			return comptime_operand(type, value_int_u64(&sema->values, type, 0));
		return operand;
	}
	if (kind == TYPE_INT)
		type = type_int(sema, false, sema->types[type].as.integer.bits);
	if (operand_is_undefined(block, operand))
		return comptime_operand(type, value_undefined(&sema->values, type));
	if (operand.kind == OPERAND_COMPTIME) {
		BigInt result = bigint_abs(sema->arena, &operand_value(block, operand)->as.integer);
		return comptime_operand(type, value_int(&sema->values, type, &result));
	}
	if (!require_runtime(block, inst->src))
		return operand_none;
	return runtime_operand(type, emit_tir_un(block, TIR_ABS, type, inst->src, operand.inst));
}

/* Width-preserving integer permutations; vectors are a separate milestone. */
static Operand
analyze_integer_transform(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	Operand operand = resolve_ref(block, inst->data.un.operand);
	if (operand.kind == OPERAND_NONE)
		return operand_none;
	TypeId type = operand.type;
	if (!is_int(sema, type)) {
		sema_error(block, inst->src, "expected integer or vector, found '%s'", sema_type_name(sema, type));
		return operand_none;
	}
	uint32_t bits = sema->types[type].as.integer.bits;
	if (inst->op == IR_BYTE_SWAP && bits % 8 != 0) {
		sema_error(block, inst->src, "@byteSwap requires the number of bits to be evenly divisible by 8, but %s has %u bits", sema_type_name(sema, type), bits);
		return operand_none;
	}
	if (bits == 0)
		return comptime_operand(type, value_int_u64(&sema->values, type, 0));
	if (operand_is_undefined(block, operand))
		return comptime_operand(type, value_undefined(&sema->values, type));
	if (operand.kind == OPERAND_COMPTIME) {
		BigIntTransformOp op = inst->op == IR_BIT_REVERSE ? BIGINT_BIT_REVERSE : BIGINT_BYTE_SWAP;
		BigInt result = bigint_transform(sema->arena, &operand_value(block, operand)->as.integer, is_signed_type(sema, type), bits, op);
		return comptime_operand(type, value_int(&sema->values, type, &result));
	}
	if (!require_runtime(block, inst->src))
		return operand_none;
	TirOp op = inst->op == IR_BIT_REVERSE ? TIR_BIT_REVERSE : TIR_BYTE_SWAP;
	return runtime_operand(type, emit_tir_un(block, op, type, inst->src, operand.inst));
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
	if (is_equality && ((lk == TYPE_ENUM && (rk == TYPE_ENUM || rk == TYPE_ENUM_LITERAL))
		|| (rk == TYPE_ENUM && lk == TYPE_ENUM_LITERAL))) {
		TypeId type;
		if (!peer_type(block, lhs, rhs, src, &type))
			return operand_none;
		lhs = coerce(block, lhs, type, src);
		rhs = coerce(block, rhs, type, src);
		if (lhs.kind == OPERAND_NONE || rhs.kind == OPERAND_NONE)
			return operand_none;
		if (operand_is_undefined(block, lhs) || operand_is_undefined(block, rhs))
			return comptime_operand(T_BOOL, value_undefined(&sema->values, T_BOOL));
		if (lhs.kind == OPERAND_COMPTIME && rhs.kind == OPERAND_COMPTIME) {
			bool equal = operand_value(block, lhs)->as.enum_tag == operand_value(block, rhs)->as.enum_tag;
			return bool_operand(sema, (op == IR_CMP_EQ) == equal);
		}
		if (!require_runtime(block, src))
			return operand_none;
		TirRef left = materialize(block, lhs, src);
		TirRef right = materialize(block, rhs, src);
		if (left == TIR_REF_NONE || right == TIR_REF_NONE)
			return operand_none;
		return runtime_operand(T_BOOL, emit_tir_bin(block, tir_arith_op(op), T_BOOL, src, left, right));
	}
	if (is_equality && lk == TYPE_ENUM_LITERAL && rk == TYPE_ENUM_LITERAL) {
		if (operand_is_undefined(block, lhs) || operand_is_undefined(block, rhs))
			return comptime_operand(T_BOOL, value_undefined(&sema->values, T_BOOL));
		bool equal = operand_value(block, lhs)->as.enum_literal == operand_value(block, rhs)->as.enum_literal;
		return bool_operand(sema, (op == IR_CMP_EQ) == equal);
	}
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
		if (is_slice(sema, type)) {
			sema_error(block, src, "operator %s not allowed for type '%s'", op == IR_CMP_EQ ? "==" : "!=", sema_type_name(sema, type));
			return operand_none;
		}
		lhs = coerce(block, lhs, type, src);
		rhs = coerce(block, rhs, type, src);
		if (operand_is_undefined(block, lhs) || operand_is_undefined(block, rhs))
			return comptime_operand(T_BOOL, value_undefined(&sema->values, T_BOOL));
		if (lhs.kind == OPERAND_COMPTIME && rhs.kind == OPERAND_COMPTIME) {
			bool equal;
			if (!known_pointer_equal(block, lhs.value, rhs.value, src, &equal))
				return operand_none;
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
	array.as.array.sentinel = value_int_u64(&sema->values, type_id_of_well_known(IR_REF_TYPE_U8), 0);
	TypeId child = type_intern(sema, &array);
	TypeId pointer = type_pointer(sema, child, POINTER_ONE, true, false, 0);
	return comptime_operand(pointer, value_bytes(&sema->values, pointer, (const uint8_t *)intern_text(sema->interner, id), length));
}

static Operand analyze_many_index(Block *block, Operand base, Operand index, uint32_t src, bool address_only, bool borrow);

/* Panic takes a byte slice, including dynamic lengths; it never scans for NUL. */
static InstOutcome
analyze_panic(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	IrPanic payload;
	IR_READ(block->ir, inst->data.payload, &payload);
	Operand message = resolve_ref(block, payload.message);
	if (message.kind == OPERAND_NONE)
		return INST_NORETURN;
	if (operand_is_undefined(block, message) && !block->is_comptime) {
		sema_error(block, payload.argument_src, "not supported by szc yet: undefined panic message");
		return INST_NORETURN;
	}
	TypeId type = type_pointer(sema, type_id_of_well_known(IR_REF_TYPE_U8), POINTER_SLICE, true, false, 0);
	message = coerce(block, message, type, payload.argument_src);
	if (message.kind == OPERAND_NONE || !check_defined(block, message, payload.argument_src))
		return INST_NORETURN;
	Operand data = slice_field(block, message, false, payload.argument_src);
	Operand size = slice_field(block, message, true, payload.argument_src);
	if (data.kind == OPERAND_NONE || size.kind == OPERAND_NONE)
		return INST_NORETURN;
	if (block->is_comptime) {
		if (size.kind != OPERAND_COMPTIME || !check_defined(block, size, payload.argument_src)) {
			if (size.kind != OPERAND_COMPTIME)
				sema_error(block, inst->src, "unable to evaluate comptime expression");
			return INST_NORETURN;
		}
		uint64_t length = bigint_to_u64_bits(&operand_value(block, size)->as.integer);
		if (length > 16777216) {
			sema_error(block, inst->src, "not supported by szc yet: comptime panic message exceeds 16777216 bytes");
			return INST_NORETURN;
		}
		static const char prefix[] = "encountered @panic at comptime: ";
		size_t total = sizeof(prefix) - 1 + (size_t)length;
		char *text = arena_alloc(sema->arena, total, 1);
		memcpy(text, prefix, sizeof(prefix) - 1);
		for (uint64_t i = 0; i < length; i++) {
			Operand index = comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, i));
			Operand byte = analyze_many_index(block, data, index, payload.argument_src, false, false);
			if (byte.kind == OPERAND_NONE || !check_defined(block, byte, payload.argument_src))
				return INST_NORETURN;
			if (byte.kind != OPERAND_COMPTIME) {
				sema_error(block, inst->src, "unable to evaluate comptime expression");
				return INST_NORETURN;
			}
			text[sizeof(prefix) - 1 + i] = (char)bigint_to_u64_bits(&operand_value(block, byte)->as.integer);
		}
		diag_report_bytes(sema->diag, DIAG_ERROR, block->ir->file, inst->src, text, total);
		return INST_NORETURN;
	}
	if (!require_runtime(block, inst->src))
		return INST_NORETURN;
	emit_tir_bin(block, TIR_PANIC, T_NORETURN, inst->src,
		materialize(block, data, inst->src), materialize(block, size, inst->src));
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
	bool array = !known && sema_type_is_aggregate(sema, type);
	TirRef array_result = TIR_REF_NONE;
	if (array) {
		TypeId pointer = type_pointer(sema, type, POINTER_ONE, true, false, 0);
		array_result = aggregate_slot(block, pointer, src);
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
			if (!store_aggregate(&arms[i], destination, operand, src))
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


/* Switch labels are normalized mathematical intervals. IDs, not borrowed Value
 * or Type pointers, survive recursive endpoint/arm analysis and interning. */
typedef struct SwitchCase {
	ValueId low;
	ValueId high;
	Operand capture;
	uint32_t prong;
	uint32_t src;
	bool range;
} SwitchCase;

typedef struct SwitchArm {
	IrSwitchProng ir;
	uint32_t first;
	Block block;
	BodyResult exit;
	TirBody body;
} SwitchArm;

static bool
switch_charge(Block *block, uint32_t src, uint64_t work)
{
	if (work > 1048576u - block->sema->switch_work) {
		sema_error(block, src, "not supported by szc yet: cumulative switch work limit exceeded");
		return false;
	}
	block->sema->switch_work += (uint32_t)work;
	return true;
}

static BigInt
switch_integer(Sema *sema, ValueId value)
{
	return value_get(&sema->values, value)->as.integer;
}

static bool
switch_compare(Block *block, ValueId a, ValueId b, uint32_t src, int *order)
{
	BigInt x = switch_integer(block->sema, a);
	BigInt y = switch_integer(block->sema, b);
	if (!switch_charge(block, src, 1ull + x.limb_count + y.limb_count))
		return false;
	*order = bigint_compare(&x, &y);
	return true;
}

/* Bottom-up stable merge sort: no input-sized C recursion or global comparator
 * state. The source table remains untouched for capture/prong provenance. */
static bool
switch_sort(Block *block, SwitchCase *cases, uint32_t count, uint32_t src)
{
	SwitchCase *scratch = ARENA_ARRAY(block->sema->arena, SwitchCase, count);
	for (uint32_t width = 1; width < count; width *= 2) {
		for (uint32_t first = 0; first < count; first += width * 2) {
			uint32_t middle = first + width < count ? first + width : count;
			uint32_t last = middle + width < count ? middle + width : count;
			uint32_t a = first, b = middle;
			for (uint32_t out = first; out < last; out++) {
				int order = 0;
				if (!switch_charge(block, src, 1))
					return false;
				if (a < middle && b < last && !switch_compare(block, cases[a].low, cases[b].low, src, &order))
					return false;
				scratch[out] = a < middle && (b == last || order <= 0) ? cases[a++] : cases[b++];
			}
		}
		memcpy(cases, scratch, (size_t)count * sizeof(*cases));
	}
	return true;
}

static bool
switch_read(Block *block, uint32_t *cursor, void *out, uint32_t words, uint32_t src)
{
	if (*cursor > block->ir->extra_count || words > block->ir->extra_count - *cursor) {
		sema_error(block, src, "invalid switch payload");
		return false;
	}
	memcpy(out, &block->ir->extra[*cursor], (size_t)words * sizeof(uint32_t));
	*cursor += words;
	return true;
}

static Operand
switch_endpoint(Block *block, IrBody body, TypeId type, uint32_t src)
{
	Block endpoint;
	block_init_child(&endpoint, block);
	endpoint.is_comptime = true;
	endpoint.comptime_value_required = true;
	endpoint.tir = NULL; /* Runtime label instructions cannot escape validation. */
	BodyResult result = analyze_body(&endpoint, body);
	if (result.end != BODY_BREAK_RESULT) {
		sema_error(block, src, "switch case must produce a comptime value");
		return operand_none;
	}
	Operand value = coerce(&endpoint, result.result, type, src);
	if (value.kind == OPERAND_NONE || !check_defined(&endpoint, value, src))
		return operand_none;
	if (value.kind != OPERAND_COMPTIME) {
		sema_error(block, src, "unable to resolve comptime value");
		return operand_none;
	}
	return value;
}

static ValueId
switch_tag(Block *block, Operand value, bool enumeration)
{
	return enumeration ? operand_value(block, value)->as.enum_tag : value.value;
}

static Operand
switch_capture(SwitchContext *context, SwitchArm *arm, SwitchCase *cases)
{
	if (context->operand.kind == OPERAND_COMPTIME)
		return context->operand;
	if (arm->ir.item_count == 1 && !cases[arm->first].range)
		return cases[arm->first].capture;
	return context->operand;
}

static Operand
analyze_switch_inner(Block *block, const IrInst *inst, IrSwitch payload, SwitchContext *context)
{
	Sema *sema = block->sema;
	uint32_t src = inst->src;
	Operand operand = context->operand;
	TypeId domain = operand.type;
	TypeKind kind = sema->types[domain].kind;
	bool enumeration = kind == TYPE_ENUM;
	if (kind == TYPE_FLOAT || kind == TYPE_COMPTIME_FLOAT) {
		sema_error(block, src, "switch on type '%s'", sema_type_name(sema, domain));
		return operand_none;
	}
	if (kind != TYPE_INT && kind != TYPE_COMPTIME_INT && !enumeration) {
		sema_error(block, src, "not supported by szc yet: switch operand type '%s'", sema_type_name(sema, domain));
		return operand_none;
	}
	if (!check_defined(block, operand, src))
		return operand_none;
	if (enumeration && !ensure_enum_layout(block, domain, src, "switch"))
		return operand_none;
	TypeId integer_type = enumeration ? sema_enum_tag_type(sema, domain) : domain;
	uint32_t enum_count = enumeration ? sema->types[domain].as.structure->field_count : 0;
	/* An empty exhaustive enum has no valid input and no continuation. */
	bool empty_enum = enumeration && enum_count == 0;
	if (operand.kind != OPERAND_COMPTIME && !empty_enum
		&& (integer_type == T_COMPTIME_INT || sema->types[integer_type].as.integer.bits == 0
			|| sema->types[integer_type].as.integer.bits > 64)) {
		sema_error(block, src, "not supported by szc yet: zero-width or wide runtime switch operand");
		return operand_none;
	}
	/* Each multi-item predicate is straight-line BIT_OR/BIT_AND, not nested
	 * branches. Charge conservative ancestor + current prong nesting before
	 * allocating arm state. Known switches add no runtime dispatch depth. */
	if (operand.kind != OPERAND_COMPTIME
		&& payload.prong_count > 256u - block->switch_dispatch_depth) {
		sema_error(block, src, "not supported by szc yet: runtime switch dispatch depth limit exceeded");
		return operand_none;
	}
	if (!switch_charge(block, src, 1ull + payload.prong_count * 16ull))
		return operand_none;
	SwitchArm *arms = ARENA_ARRAY(sema->arena, SwitchArm, payload.prong_count);
	uint32_t cursor = inst->data.payload + IR_WORDS(IrSwitch);
	uint32_t case_count = 0;
	uint32_t otherwise = UINT32_MAX;
	/* First walk checks word arithmetic and allocates a bounded flat table. */
	for (uint32_t i = 0; i < payload.prong_count; i++) {
		if (!switch_read(block, &cursor, &arms[i].ir, IR_WORDS(IrSwitchProng), src))
			return operand_none;
		IrSwitchProng prong = arms[i].ir;
		if ((prong.flags & ~(IR_SWITCH_ELSE | IR_SWITCH_INDEX_CAPTURE)) != 0 || ((prong.flags & IR_SWITCH_ELSE) != 0 ? prong.item_count != 0 : prong.item_count == 0)) {
			sema_error(block, prong.src, "invalid switch prong");
			return operand_none;
		}
		if ((prong.flags & IR_SWITCH_ELSE) != 0) {
			if (otherwise != UINT32_MAX) {
				sema_error(block, prong.src, "multiple else prongs in switch expression");
				return operand_none;
			}
			otherwise = i;
		}
		if (!switch_charge(block, prong.src, (uint64_t)prong.item_count * 16))
			return operand_none;
		arms[i].first = case_count;
		case_count += prong.item_count; /* bounded by the charge above */
		uint64_t words = (uint64_t)prong.item_count * IR_WORDS(IrSwitchItem);
		if (cursor > block->ir->extra_count || words > block->ir->extra_count - cursor) {
			sema_error(block, prong.src, "invalid switch payload");
			return operand_none;
		}
		cursor += (uint32_t)words;
	}
	SwitchCase *cases = ARENA_ARRAY(sema->arena, SwitchCase, case_count);
	cursor = inst->data.payload + IR_WORDS(IrSwitch);
	for (uint32_t i = 0; i < payload.prong_count; i++) {
		cursor += IR_WORDS(IrSwitchProng);
		for (uint32_t j = 0; j < arms[i].ir.item_count; j++) {
			IrSwitchItem item;
			if (!switch_read(block, &cursor, &item, IR_WORDS(IrSwitchItem), src))
				return operand_none;
			if (enumeration && item.high_body != 0) {
				sema_error(block, item.src, "ranges not allowed when switching on type '%s'", sema_type_name(sema, domain));
				return operand_none;
			}
			Operand low = switch_endpoint(block, item.low_body, domain, item.src);
			if (low.kind == OPERAND_NONE)
				return operand_none;
			Operand high = item.high_body == 0 ? low : switch_endpoint(block, item.high_body, domain, item.end_src);
			if (high.kind == OPERAND_NONE)
				return operand_none;
			SwitchCase *entry = &cases[arms[i].first + j];
			*entry = (SwitchCase){switch_tag(block, low, enumeration), switch_tag(block, high, enumeration), low, i, item.src, item.high_body != 0};
			int order;
			if (!switch_compare(block, entry->low, entry->high, item.src, &order))
				return operand_none;
			if (order > 0) {
				sema_error(block, item.src, "range start value is greater than the end value");
				return operand_none;
			}
		}
	}
	SwitchCase *sorted = ARENA_ARRAY(sema->arena, SwitchCase, case_count);
	memcpy(sorted, cases, (size_t)case_count * sizeof(*cases));
	if (!switch_sort(block, sorted, case_count, src))
		return operand_none;
	bool contiguous = case_count != 0;
	BigInt one = bigint_from_u64(sema->arena, 1);
	for (uint32_t i = 1; i < case_count; i++) {
		int order;
		if (!switch_compare(block, sorted[i].low, sorted[i - 1].high, sorted[i].src, &order))
			return operand_none;
		if (order <= 0) {
			BigInt duplicate = switch_integer(sema, sorted[i].low);
			sema_error(block, sorted[i].src, "duplicate switch value '%s'", bigint_to_string(sema->arena, &duplicate));
			diag_report(sema->diag, DIAG_NOTE, block->ir->file, sorted[i - 1].src,
				"previous value%s here", sorted[i - 1].range ? " inside range" : "");
			return operand_none;
		}
		if (!enumeration) {
			BigInt previous = switch_integer(sema, sorted[i - 1].high);
			BigInt next = switch_integer(sema, sorted[i].low);
			if (!switch_charge(block, src, 2ull + previous.limb_count + next.limb_count))
				return operand_none;
			BigInt successor = bigint_add(sema->arena, &previous, &one);
			if (bigint_compare(&successor, &next) != 0)
				contiguous = false;
		}
	}
	bool exhaustive = enumeration ? case_count == enum_count : false;
	if (kind == TYPE_INT && contiguous) {
		if (!switch_charge(block, src, 8ull + sema->types[domain].as.integer.bits / 8))
			return operand_none;
		BigInt min, max;
		int_type_bounds(sema, domain, &min, &max);
		BigInt low = switch_integer(sema, sorted[0].low);
		BigInt high = switch_integer(sema, sorted[case_count - 1].high);
		exhaustive = bigint_compare(&low, &min) == 0 && bigint_compare(&high, &max) == 0;
	}
	/* Every enum label was nominally coerced to a validated member. Distinct
	 * member count therefore proves coverage without prongs x tags scans. */
	if (exhaustive && otherwise != UINT32_MAX) {
		sema_error(block, arms[otherwise].ir.src, "unreachable else prong; all cases already handled");
		return operand_none;
	}
	if (!exhaustive && otherwise == UINT32_MAX) {
		sema_error(block, src, kind == TYPE_COMPTIME_INT ? "else prong required when switching on type 'comptime_int'" : "switch must handle all possibilities");
		return operand_none;
	}
	if (operand.kind == OPERAND_COMPTIME) {
		ValueId value = switch_tag(block, operand, enumeration);
		uint32_t selected = otherwise;
		for (uint32_t i = 0; i < case_count; i++) {
			int low, high;
			if (!switch_compare(block, value, sorted[i].low, src, &low)
				|| !switch_compare(block, value, sorted[i].high, src, &high))
				return operand_none;
			if (low >= 0 && high <= 0) {
				selected = sorted[i].prong;
				break;
			}
		}
		if (selected == UINT32_MAX) {
			sema_error(block, src, "switch operand has no matching case");
			return operand_none;
		}
		/* Arm-local type checks follow known selection; lexical generation
		 * still visits every body, including known-unselected captures. */
		if ((arms[selected].ir.flags & IR_SWITCH_INDEX_CAPTURE) != 0) {
			sema_error(block, arms[selected].ir.src, "cannot capture tag of non-union type '%s'", sema_type_name(sema, domain));
			return operand_none;
		}
		context->capture = operand;
		context->capture_active = true;
		uint32_t errors = sema->diag->error_count;
		BodyResult arm = analyze_body(block, arms[selected].ir.body);
		if (sema->diag->error_count != errors)
			return operand_none;
		if (arm.end == BODY_NORETURN)
			return runtime_operand(T_NORETURN, TIR_REF_NONE);
		if (arm.end != BODY_BREAK_RESULT) {
			sema_error(block, arms[selected].ir.src, "switch arm did not produce a result");
			return operand_none;
		}
		return arm.result;
	}
	if (!require_runtime(block, src))
		return operand_none;
	if (!switch_charge(block, src, 8ull * case_count + 8ull * payload.prong_count))
		return operand_none;
	uint32_t exit_count = 0;
	Operand value = operand_none;
	TypeId type = T_NORETURN;
	Operand peer = operand_none;
	for (uint32_t i = 0; i < payload.prong_count; i++) {
		if ((arms[i].ir.flags & IR_SWITCH_INDEX_CAPTURE) != 0) {
			sema_error(block, arms[i].ir.src, "cannot capture tag of non-union type '%s'", sema_type_name(sema, domain));
			return operand_none;
		}
		block_init_child(&arms[i].block, block);
		arms[i].block.switch_dispatch_depth += payload.prong_count;
		context->capture = switch_capture(context, &arms[i], cases);
		context->capture_active = true;
		uint32_t errors = sema->diag->error_count;
		arms[i].exit = analyze_body(&arms[i].block, arms[i].ir.body);
		context->capture_active = false;
		if (sema->diag->error_count != errors)
			return operand_none;
		if (arms[i].exit.end == BODY_NORETURN)
			continue;
		if (arms[i].exit.end != BODY_BREAK_RESULT) {
			sema_error(block, arms[i].ir.src, "switch arm did not produce a result");
			return operand_none;
		}
		Operand result = arms[i].exit.result;
		if (result.kind == OPERAND_NONE)
			return operand_none;
		if (exit_count == 0) {
			type = result.type;
			value = result;
			peer = result;
		} else {
			if (!peer_type(block, peer, result, src, &type))
				return operand_none;
			/* Keep a genuine operand whenever the common type is one of
			 * the inputs. Newly synthesized pointer peers need only their
			 * type: never relabel a constant ValueId or a runtime TirRef. */
			if (type == result.type)
				peer = result;
			else if (type != peer.type)
				peer = runtime_operand(type, TIR_REF_NONE);
		}
		exit_count++;
	}
	if (exit_count > 1 && is_comptime_only_type(sema, type)) {
		sema_error(block, src, "value with comptime-only type '%s' depends on runtime control flow", sema_type_name(sema, type));
		return operand_none;
	}
	bool known = exit_count == 1 && value.kind == OPERAND_COMPTIME;
	bool aggregate = !known && sema_type_is_aggregate(sema, type);
	TirRef slot = TIR_REF_NONE;
	if (aggregate) {
		TypeId pointer = type_pointer(sema, type, POINTER_ONE, true, false, 0);
		slot = aggregate_slot(block, pointer, src);
		if (slot == TIR_REF_NONE)
			return operand_none;
	}
	TypeId merge_type = known || aggregate ? T_VOID : type;
	TirRef merge = emit_tir(block, TIR_BLOCK, merge_type, src);
	for (uint32_t i = 0; i < payload.prong_count; i++) {
		if (arms[i].exit.end == BODY_BREAK_RESULT) {
			Operand result = coerce(&arms[i].block, arms[i].exit.result, type, arms[i].ir.src);
			if (result.kind == OPERAND_NONE)
				return operand_none;
			if (aggregate && !store_aggregate(&arms[i].block, runtime_operand(block->tir->insts[slot].type, slot), result, src))
				return operand_none;
			TirRef ref = merge_type == T_VOID ? TIR_REF_NONE : materialize(&arms[i].block, result, src);
			emit_tir_bin(&arms[i].block, TIR_BR, T_NORETURN, src, merge, ref);
		}
		arms[i].body = block_finish(&arms[i].block);
	}
	Block dispatch;
	block_init_child(&dispatch, block);
	TirRef test_operand = TIR_REF_NONE;
	if (!empty_enum) {
		test_operand = materialize(&dispatch, operand, src);
		if (enumeration)
			test_operand = emit_tir_un(&dispatch, TIR_INT_CAST, integer_type, src, test_operand);
	}
	Block fallback;
	block_init_child(&fallback, block);
	TirBody tail;
	if (otherwise != UINT32_MAX)
		tail = arms[otherwise].body;
	else {
		emit_tir(&fallback, TIR_TRAP, T_NORETURN, src);
		tail = block_finish(&fallback);
	}
	/* Build from the end without semantic recursion. Each source arm is
	 * serialized once, even when many distinct intervals select it. */
	for (uint32_t i = payload.prong_count; i-- > 0;) {
		if (i == otherwise)
			continue;
		Block test;
		block_init_child(&test, block);
		TirRef predicate = TIR_REF_NONE;
		for (uint32_t j = 0; j < arms[i].ir.item_count; j++) {
			SwitchCase entry = cases[arms[i].first + j];
			TirRef low = materialize(&test, comptime_operand(integer_type, entry.low), entry.src);
			TirRef match = emit_tir_bin(&test, entry.range ? TIR_CMP_GTE : TIR_CMP_EQ, T_BOOL, entry.src, test_operand, low);
			if (entry.range) {
				TirRef high = materialize(&test, comptime_operand(integer_type, entry.high), entry.src);
				TirRef upper = emit_tir_bin(&test, TIR_CMP_LTE, T_BOOL, entry.src, test_operand, high);
				match = emit_tir_bin(&test, TIR_BIT_AND, T_BOOL, entry.src, match, upper);
			}
			predicate = predicate == TIR_REF_NONE ? match : emit_tir_bin(&test, TIR_BIT_OR, T_BOOL, entry.src, predicate, match);
		}
		TirCondbr branch = {predicate, arms[i].body, tail};
		TirRef ref = emit_tir(&test, TIR_CONDBR, T_NORETURN, arms[i].ir.src);
		block->tir->insts[ref].data.payload = tir_extra(block->tir, sema->arena, &branch, TIR_WORDS(TirCondbr));
		tail = block_finish(&test);
	}
	/* Flatten the dispatch preamble plus the outermost body into the merge. */
	uint32_t tail_count = block->tir->extra[tail];
	for (uint32_t i = 0; i < tail_count; i++)
		block_push(&dispatch, block->tir->extra[tail + 1 + i]);
	block->tir->insts[merge].data.payload = block_finish(&dispatch);
	if (known)
		return value;
	if (aggregate) {
		Operand result = runtime_operand(type, slot);
		result.kind = OPERAND_AGGREGATE;
		return result;
	}
	return type == T_VOID ? void_operand(sema) : runtime_operand(type, merge);
}

static Operand
analyze_switch(Block *block, IrInstIndex index)
{
	const IrInst *inst = &block->ir->insts[index];
	uint32_t cursor = inst->data.payload;
	IrSwitch payload;
	if (!switch_read(block, &cursor, &payload, IR_WORDS(IrSwitch), inst->src))
		return operand_none;
	Operand operand = resolve_ref(block, payload.operand);
	if (operand.kind == OPERAND_NONE)
		return operand_none;
	if (block->sema->switch_depth >= 128) {
		sema_error(block, inst->src, "not supported by szc yet: nested switch analysis depth limit exceeded");
		return operand_none;
	}
	SwitchContext context = {block->switch_context, index, operand, operand_none, false};
	block->switch_context = &context;
	block->sema->switch_depth++;
	Operand result = analyze_switch_inner(block, inst, payload, &context);
	block->sema->switch_depth--;
	block->switch_context = context.parent;
	return result;
}

static Operand
analyze_switch_context(Block *block, const IrInst *inst)
{
	if (ir_ref_is_inst(inst->data.un.operand)) {
		IrInstIndex owner = ir_ref_inst(inst->data.un.operand);
		for (SwitchContext *context = block->switch_context; context != NULL; context = context->parent) {
			if (context->owner != owner)
				continue;
			if (inst->op == IR_SWITCH_TYPE)
				return type_operand(block->sema, context->operand.type);
			if (inst->op == IR_SWITCH_INDEX_VALUE) {
				/* No supported domain supplies a tag capture. This marker must
				 * fail closed even if a malformed IR prong omitted its flag. */
				sema_error(block, inst->src, "cannot capture tag of non-union type '%s'", sema_type_name(block->sema, context->operand.type));
				return operand_none;
			}
			if (inst->op == IR_SWITCH_UNDERSCORE) {
				/* Non-exhaustive enum types still have their explicit producer
				 * gate; none of the supported domains admits a bare '_' arm. */
				sema_error(block, inst->src, "'_' prong only allowed when switching on non-exhaustive enums");
				return operand_none;
			}
			if (context->capture_active)
				return context->capture;
			break;
		}
	}
	sema_error(block, inst->src, "invalid switch owner context");
	return operand_none;
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
	bool array = !known && sema_type_is_aggregate(block->sema, type);
	TirRef array_result = TIR_REF_NONE;
	if (array) {
		TypeId pointer = type_pointer(block->sema, type, POINTER_ONE, true, false, 0);
		array_result = aggregate_slot(block, pointer, inst->src);
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
			if (!store_aggregate(&branch, destination, value, exit.src))
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
	if (!ir_ref_is_inst(inst->data.bin.lhs) || !inst_map_get(block->map, ir_ref_inst(inst->data.bin.lhs), &call) || (call.kind != OPERAND_PENDING_CALL && call.kind != OPERAND_PENDING_METHOD_CALL))
		return operand_none;
	TypeFn fn = sema->types[call.type].as.fn;
	uint32_t index = inst->data.bin.rhs + (call.kind == OPERAND_PENDING_METHOD_CALL ? 1u : 0u);
	if (index >= fn.param_count) {
		Operand none = operand_none;
		none.kind = OPERAND_NO_RESULT_TYPE;
		return none;
	}
	return type_operand(sema, fn.params[index]);
}

static InstOutcome
analyze_call_core(Block *block, IrInstIndex index, Operand callee,
	IrCallModifier modifier, uint32_t explicit_count, const IrBody *arg_bodies,
	Operand receiver, Operand *out)
{
	Sema *sema = block->sema;
	uint32_t src = block->ir->insts[index].src;
	uint32_t bound = receiver.kind != OPERAND_NONE ? 1u : 0u;
	*out = operand_none;
	if (explicit_count > UINT32_MAX / sizeof(Operand) - 2) {
		sema_error(block, src, "not supported by szc yet: call argument storage exceeds limit");
		return INST_CONTINUE;
	}
	uint32_t arg_count = explicit_count + bound;

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
	bool struct_value = type_has_struct_value(sema, fn.ret);
	for (uint32_t i = 0; i < fn.param_count; i++)
		struct_value |= type_has_struct_value(sema, fn.params[i]);
	bool enum_value = type_has_enum_value(sema, fn.ret);
	for (uint32_t i = 0; i < fn.param_count; i++)
		enum_value |= type_has_enum_value(sema, fn.params[i]);
	if (enum_value && fn.convention == FUNCTION_C) {
		sema_error(block, src, "not supported by szc yet: enum values in C function signatures");
		return INST_CONTINUE;
	}
	if (struct_value && fn.convention == FUNCTION_C) {
		sema_error(block, src, "not supported by szc yet: struct values in runtime function signatures");
		return INST_CONTINUE;
	}
	FuncId id = 0;
	if (callee.kind == OPERAND_COMPTIME && operand_value(block, callee)->kind == VALUE_FUNCTION)
		id = operand_value(block, callee)->as.function;
	bool count_ok = fn.is_var_args ? arg_count >= fn.param_count : arg_count == fn.param_count;
	if (!count_ok) {
		sema_error(block, src, "%sexpected %s%u argument(s), found %u", bound ? "member function " : "", fn.is_var_args ? "at least " : "", (unsigned)(fn.param_count - bound), (unsigned)explicit_count);
		return INST_CONTINUE;
	}

	/* Typed indirect C calls have no FuncId and never visit func_queue. Do not
	 * silently apply our private aggregate pointer ABI to a public C signature. */
	if (fn.convention == FUNCTION_C && id == 0) {
		for (uint32_t i = 0; i < fn.param_count; i++) {
			if (sema_type_is_aggregate(sema, fn.params[i])) {
				sema_error(block, src, "parameter of type '%s' not allowed in function with calling convention 'x86_64_sysv'", sema_type_name(sema, fn.params[i]));
				return INST_CONTINUE;
			}
		}
		if (sema_type_is_aggregate(sema, fn.ret)) {
			sema_error(block, src, "return type '%s' not allowed in function with calling convention 'x86_64_sysv'", sema_type_name(sema, fn.ret));
			return INST_CONTINUE;
		}
	} else if (fn.convention != FUNCTION_C) {
		bool valid = true;
		for (uint32_t i = 0; i < fn.param_count; i++)
			valid &= validate_internal_value_type(block, fn.params[i], src);
		valid &= validate_internal_value_type(block, fn.ret, src);
		if (!valid)
			return INST_CONTINUE;
	}

	Operand pending = operand_none;
	pending.kind = bound ? OPERAND_PENDING_METHOD_CALL : OPERAND_PENDING_CALL;
	if (!check_defined(block, callee, src))
		return INST_CONTINUE;
	pending.type = type;
	inst_map_put(block->map, sema->arena, index, pending);
	Operand *args = arena_alloc(sema->arena, (arg_count + 1) * sizeof(Operand), alignof(Operand));
	bool failed = false;
	if (bound) {
		args[0] = coerce(block, receiver, fn.params[0], src);
		if (args[0].kind == OPERAND_NONE)
			return INST_CONTINUE;
	}
	for (uint32_t i = bound; i < arg_count; i++) {
		BodyResult result = analyze_body(block, arg_bodies[i - bound]);
		if (result.end == BODY_NORETURN)
			return INST_NORETURN;
		args[i] = result.result;
		if (result.end != BODY_BREAK_RESULT || args[i].kind == OPERAND_NONE)
			failed = true;
		if (!failed && i >= fn.param_count && type_has_enum_value(sema, args[i].type)) {
			sema_error(block, src, "not supported by szc yet: variadic enum arguments");
			failed = true;
		}
		if (!failed && i >= fn.param_count && sema_type_is_aggregate(sema, args[i].type)) {
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
	if (block->is_comptime || modifier == IR_CALL_COMPTIME) {
		sema_error(block, src, "not supported by szc yet: comptime function calls");
		return INST_CONTINUE;
	}
	if (id != 0) {
		func_queue(sema, id);
		if (sema->funcs[id]->state == FUNC_FAILED)
			return INST_CONTINUE;
	}
	TirRef *refs = arena_alloc(sema->arena, (arg_count + 1) * sizeof(TirRef), alignof(TirRef));
	for (uint32_t i = 0; i < arg_count; i++) {
		Operand argument = args[i];
		if (sema_type_is_aggregate(sema, argument.type))
			argument = analyze_ref(block, argument, src, 0, 0);
		if (argument.kind == OPERAND_NONE)
			return INST_CONTINUE;
		refs[i] = materialize(block, argument, src);
	}
	bool array_return = sema_type_is_aggregate(sema, fn.ret);
	TirRef result_pointer = TIR_REF_NONE;
	if (array_return) {
		TypeId pointer = type_pointer(sema, fn.ret, POINTER_ONE, false, false, 0);
		result_pointer = aggregate_slot(block, pointer, src);
		if (result_pointer == TIR_REF_NONE)
			return INST_CONTINUE;
		refs[arg_count] = result_pointer;
	}
	TirCall call;
	call.callee = id;
	call.indirect = id == 0 ? materialize(block, callee, src) : TIR_REF_NONE;
	call.arg_count = arg_count + (array_return ? 1u : 0u);
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

static InstOutcome
analyze_call(Block *block, IrInstIndex index, Operand *out)
{
	const IrInst *inst = &block->ir->insts[index];
	IrCallHeader header;
	IR_READ(block->ir, inst->data.payload, &header);
	const IrBody *bodies = &block->ir->extra[inst->data.payload + IR_WORDS(IrCallHeader)];
	Operand callee = resolve_ref(block, header.callee);
	return analyze_call_core(block, index, callee, header.modifier,
		header.arg_count, bodies, operand_none, out);
}

/* Aggregate values never enter scalar QBE temporaries. Every runtime value owns
 * a snapshot; borrowed lvalues are represented only by ordinary pointer operands. */
bool
sema_type_array_storage_allowed(const Sema *sema, TypeId type)
{
	bool empty = false;
	while (sema->types[type].kind == TYPE_ARRAY) {
		TypeArray array = sema->types[type].as.array;
		if (array.length == 0 && array.sentinel == VALUE_NONE)
			empty = true;
		type = array.child;
	}
	TypeKind kind = sema->types[type].kind;
	/* An empty dimension removes every leaf value, but not its ABI alignment.
	 * Admit only leaves whose layout is known, not unresolved structs/errors. */
	if (empty && (is_comptime_only_type(sema, type) || kind == TYPE_NORETURN))
		return true;
	if (empty && kind == TYPE_FLOAT)
		return sema->types[type].as.floating.bits == 32 || sema->types[type].as.floating.bits == 64;
	return kind == TYPE_BOOL || kind == TYPE_POINTER || kind == TYPE_VOID
		|| (kind == TYPE_INT && (empty || sema->types[type].as.integer.bits <= 64));
}

/* Ordinary layout recursion is already limited to 256 levels. This shared
 * read-only shape walk bounds repeated edges; callers choose their own domain. */
static bool
storage_type_allowed(const Sema *sema, TypeId id, uint32_t depth, uint32_t *work)
{
	if (depth >= 256 || *work >= 1048576)
		return false;
	(*work)++;
	if (sema_type_array_storage_allowed(sema, id))
		return true;
	const Type *type = &sema->types[id];
	if (type->kind == TYPE_ENUM)
		return enum_runtime_allowed(sema, id);
	if (type->kind == TYPE_ARRAY)
		return storage_type_allowed(sema, type->as.array.child, depth + 1, work);
	if (type->kind != TYPE_STRUCT || type->as.structure->layout_state != STRUCT_RESOLVED)
		return false;
	const SemaNamespace *ns = type->as.structure;
	for (uint32_t i = 0; i < ns->field_count; i++)
		if (!storage_type_allowed(sema, ns->fields[i].type, depth + 1, work))
			return false;
	return true;
}

bool
sema_type_static_storage_allowed(const Sema *sema, TypeId type)
{
	uint32_t work = 0;
	return storage_type_allowed(sema, type, 0, &work);
}

/* Storage shape is shared today, but callers choose admission deliberately.
 * Signature convention, globals and explicit memory builtins have separate gates. */
bool
sema_type_runtime_storage_allowed(const Sema *sema, TypeId type)
{
	uint32_t work = 0;
	return storage_type_allowed(sema, type, 0, &work);
}

/* Only newly admitted struct-bearing values need this extra live-signature
 * check. Pointer/slice pointees and legacy empty-array domains stay lazy. */
static bool
validate_internal_value_type(Block *block, TypeId type, uint32_t src)
{
	if (!type_has_struct_value(block->sema, type) && !type_has_enum_value(block->sema, type))
		return true;
	if (!ensure_type_layout(block, type, src))
		return false;
	if (!sema_type_runtime_storage_allowed(block->sema, type)) {
		sema_error(block, src, "not supported by szc yet: runtime function storage of type '%s'", sema_type_name(block->sema, type));
		return false;
	}
	return true;
}

/* Address/relocation-driven liveness has no caller Block. Resolve signature
 * layouts in the definition's own IR/namespace, before allocating runtime slots.
 * func_queue marks QUEUED first so recursive liveness cannot re-enter this check. */
static bool
func_validate_internal_signature(Sema *sema, FuncId id)
{
	SemaFunc *func = sema->funcs[id];
	TypeFn fn = sema->types[func->type].as.fn;
	InstMap map;
	inst_map_init(&map, sema->arena);
	Block block;
	block_init_comptime(&block, sema, func->namespace_scope, &map, func->name);
	block.ir = func->ir;
	const IrInstIndex *params = &func->ir->extra[func->ir->insts[func->func_inst].data.payload + IR_WORDS(IrFuncHeader)];
	bool valid = true;
	for (uint32_t i = 0; i < fn.param_count; i++)
		valid &= validate_internal_value_type(&block, fn.params[i], func->ir->insts[params[i]].src);
	valid &= validate_internal_value_type(&block, fn.ret, func->ir->insts[func->func_inst].src);
	return valid;
}

static TirRef
aggregate_slot(Block *block, TypeId pointer, uint32_t src)
{
	Sema *sema = block->sema;
	TypePointer shape = sema->types[pointer].as.pointer;
	uint64_t size;
	uint32_t alignment = pointer_alignment(block, shape, src);
	if (!sema_type_runtime_storage_allowed(sema, shape.child) || !sema_type_size(sema, shape.child, &size)) {
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
	if (!block->is_typeof && (size > limit || padding > limit - size || block->func->array_stack_bytes > limit - size - padding)) {
		sema_error(block, src, "not supported by szc yet: aggregate stack storage exceeds 16777216 bytes");
		return TIR_REF_NONE;
	}
	if (!block->is_typeof)
		block->func->array_stack_bytes += size + padding;
	return emit_tir(block, TIR_ALLOC, pointer, src);
}

static bool
store_aggregate(Block *block, Operand pointer, Operand value, uint32_t src)
{
	Sema *sema = block->sema;
	TypePointer shape = sema->types[pointer.type].as.pointer;
	if (shape.is_volatile) {
		sema_error(block, src, "not supported by szc yet: volatile whole-array copies");
		return false;
	}
	if (!sema_type_runtime_storage_allowed(sema, shape.child)) {
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
aggregate_snapshot(Block *block, Operand source, uint32_t alignment, uint32_t src)
{
	Sema *sema = block->sema;
	TypeId type = sema->types[source.type].as.pointer.child;
	TypeId pointer = type_pointer(sema, type, POINTER_ONE, true, false, alignment);
	TirRef slot = aggregate_slot(block, pointer, src);
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
		if (!require_runtime(block, src))
			return operand_none;
		TypePointer backing = sema->types[block->tir->insts[value.inst].type].as.pointer;
		if (alignment != 0 && alignment > pointer_alignment(block, backing, src)) {
			Operand copy = aggregate_snapshot(block, runtime_operand(block->tir->insts[value.inst].type, value.inst), alignment, src);
			if (copy.kind == OPERAND_NONE)
				return operand_none;
			return runtime_operand(pointer, copy.inst);
		}
		return runtime_operand(block->tir->insts[value.inst].type, value.inst);
	}
	if (value.kind == OPERAND_COMPTIME) {
		TypeId leaf = value.type;
		while (sema->types[leaf].kind == TYPE_ARRAY)
			leaf = sema->types[leaf].as.array.child;
		/* An actual typed root demands its by-value layout; pointer TYPE-only
		 * construction still does not. Do this before cached provenance walks,
		 * never while their queue/state arrays are borrowed. */
		if ((sema->types[leaf].kind == TYPE_STRUCT || sema->types[leaf].kind == TYPE_ENUM)
			&& !ensure_type_layout(block, value.type, src))
			return operand_none;
		return comptime_operand(pointer, value_ref(&sema->values, pointer, value.value, identity, alignment));
	}
	if (block->is_comptime || !require_runtime(block, src)) {
		sema_error(block, src, "unable to evaluate comptime expression");
		return operand_none;
	}
	if (kind != TYPE_INT && kind != TYPE_BOOL && kind != TYPE_POINTER && kind != TYPE_VOID
		&& !(kind == TYPE_ENUM && enum_runtime_allowed(sema, value.type))) {
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
	if (sema->types[type].kind == TYPE_ENUM && !ensure_type_layout(block, type, src))
		return operand_none;
	if (sema->types[type].kind == TYPE_ENUM && !enum_runtime_allowed(sema, type)) {
		sema_error(block, src, "not supported by szc yet: zero-width runtime enum storage");
		return operand_none;
	}
	if (is_comptime_only_type(sema, type)) {
		sema_error(block, src, "variable of type '%s' must be const or comptime", sema_type_name(sema, type));
		return operand_none;
	}
	if (sema->types[type].kind == TYPE_NORETURN || sema->types[type].kind == TYPE_FN || sema->types[type].kind == TYPE_FLOAT) {
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
	if (sema_type_is_aggregate(sema, type)) {
		TirRef slot = aggregate_slot(block, pointer, src);
		if (slot == TIR_REF_NONE)
			return operand_none;
		Operand address = runtime_operand(pointer, slot);
		return store_aggregate(block, address, init, src) ? address : operand_none;
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
		ValueKind root = value_get(&block->sema->values, address.root)->kind;
		if (root == VALUE_GLOBAL || root == VALUE_INT_PTR
			|| (runtime_pointer_view(block, operand)
				&& (root == VALUE_REF || root == VALUE_BYTES || root == VALUE_FUNCTION)))
			return true;
		if (root == VALUE_REF) {
			ValueId field;
			PointerAddress view;
			uint32_t work = 0;
			if (!struct_pointer_view(block, operand.value, &field, &view, &work, src))
				return false;
			if (field != VALUE_NONE)
				return true;
		}
	}
	if (operand.kind != OPERAND_RUNTIME) {
		sema_error(block, src, "not supported by szc yet: comptime pointers");
		return false;
	}
	return true;
}

static Operand
store_value(Block *block, Operand pointer, Operand value, uint32_t src)
{
	Sema *sema = block->sema;
	if (!pointer_type_operand(block, pointer, src) || value.kind == OPERAND_NONE)
		return operand_none;
	if (sema->types[pointer.type].as.pointer.is_const) {
		sema_error(block, src, "cannot assign to constant");
		return operand_none;
	}
	TypeId child = sema->types[pointer.type].as.pointer.child;

	value = coerce(block, value, child, src);
	if (value.kind == OPERAND_NONE)
		return operand_none;
	if (sema_type_is_aggregate(sema, child)) {
		if (pointer_operand(block, pointer, src) && store_aggregate(block, pointer, value, src))
			return void_operand(sema);
		return operand_none;
	}
	bool no_storage = child == T_VOID || (is_int(sema, child) && sema->types[child].as.integer.bits == 0);
	if (value.kind != OPERAND_NONE && !no_storage) {
		if (block->is_comptime && mutable_global_pointer(block, pointer)) {
			sema_error(block, src, "unable to evaluate comptime expression");
			return operand_none;
		}
		if (!require_runtime(block, src))
			return operand_none;
		bool is_volatile = sema->types[pointer.type].as.pointer.is_volatile;
		if ((is_volatile || !operand_is_undefined(block, value)) && pointer_operand(block, pointer, src)) {
			TirRef address = materialize(block, pointer, src);
			emit_tir_bin(block, is_volatile ? TIR_STORE_VOLATILE : TIR_STORE, T_VOID, src, address, materialize(block, value, src));
		}
	}
	return void_operand(sema);
}

static bool type_has_one_value(const Sema *sema, TypeId type);
static Operand one_value_operand(Block *block, TypeId type, uint32_t src);
static Operand constant_array_element(Block *block, Operand base, TypeArray array, uint64_t index, uint32_t src);
static Operand runtime_array_element(Block *block, Operand base, Operand index, TypeArray array, uint32_t src, bool address_only);
static Operand load_offset_pointer(Block *block, Operand pointer, uint32_t src);
static TypeId indexable_array_type(Sema *sema, TypeId type);
static TypeId array_type_id(Sema *sema, TypeId child, uint64_t length, ValueId sentinel);

/* Immutable declaration loads resolve their initializer; mutable contents need runtime. */
static Operand
load_pointer(Block *block, Operand pointer, uint32_t src)
{
	Sema *sema = block->sema;
	if (!pointer_type_operand(block, pointer, src))
		return operand_none;
	TypePointer pointer_type = sema->types[pointer.type].as.pointer;
	TypeId child = pointer_type.child;
	TypeId leaf = child;
	while (sema->types[leaf].kind == TYPE_ARRAY)
		leaf = sema->types[leaf].as.array.child;
	if (sema->types[leaf].kind == TYPE_ENUM && !ensure_type_layout(block, child, src))
		return operand_none;
	if (pointer_type.is_volatile && sema->types[leaf].kind == TYPE_STRUCT) {
		sema_error(block, src, "not supported by szc yet: volatile struct loads");
		return operand_none;
	}
	if (child == T_VOID)
		return void_operand(sema);
	if (is_int(sema, child) && sema->types[child].as.integer.bits == 0)
		return comptime_operand(child, value_int_u64(&sema->values, child, 0));
	uint64_t size;
	if (sema->types[child].kind == TYPE_ARRAY && type_has_one_value(sema, child)
		&& sema_type_size(sema, child, &size) && size == 0)
		return one_value_operand(block, child, src);
	bool physical_view = runtime_pointer_view(block, pointer);
	bool mutable_global = mutable_global_pointer(block, pointer);
	if (physical_view && (block->is_comptime || !mutable_global)) {
		/* Falling back to a runtime read of immutable known memory could invent
		 * values for undefined ABI padding or bypass an undefined-layout error. */
		sema_error(block, src, "not supported by szc yet: known immutable/comptime load through reinterpreted pointer");
		return operand_none;
	}
	if (mutable_global && block->is_comptime) {
		PointerAddress address = value_pointer_address(&sema->values, pointer.value);
		Global *global = sema->globals[value_get(&sema->values, address.root)->as.global];
		Operand resolved = resolve_decl(block, src, global->namespace_scope, global->decl, false);
		if (resolved.kind != OPERAND_NONE) {
			/* Canonical singleton struct values remain a separate comptime feature.
			 * Zero size selects an explicit boundary diagnostic, never a value fold. */
			uint64_t extent;
			if (type_has_struct_value(sema, child) && sema_type_size(sema, child, &extent) && extent == 0)
				sema_error(block, src, "not supported by szc yet: comptime reads of zero-sized mutable struct storage");
			else
				sema_error(block, src, "unable to resolve comptime value");
		}
		return operand_none;
	}
	bool volatile_field = false;
	if (pointer.kind == OPERAND_COMPTIME && pointer_type.is_volatile) {
		ValueId field;
		PointerAddress view;
		uint32_t work = 0;
		if (!struct_pointer_view(block, pointer.value, &field, &view, &work, src))
			return operand_none;
		volatile_field = field != VALUE_NONE;
	}
	bool absolute = absolute_pointer(block, pointer);
	if (pointer.kind == OPERAND_COMPTIME && !mutable_global && !absolute && !physical_view && !volatile_field) {
		const Value *value = operand_value(block, pointer);
		if (value->kind == VALUE_FUNCTION && sema->types[child].kind == TYPE_FN)
			return comptime_operand(child, value_function(&sema->values, child, value->as.function));
		PointerAddress address = value_pointer_address(&sema->values, pointer.value);
		const Value *root = value_get(&sema->values, address.root);
		if (root->kind == VALUE_BYTES && sema->types[child].kind == TYPE_ARRAY) {
			ByteString bytes = root->as.bytes;
			TypeArray array = sema->types[child].as.array;
			uint64_t extra = array.sentinel != VALUE_NONE ? 1u : 0u;
			if (array.length > UINT32_MAX - extra) {
				sema_error(block, src, "not supported by szc yet: array constant with this many elements");
				return operand_none;
			}
			uint32_t count = (uint32_t)(array.length + extra);
			if (address.offset > (uint64_t)bytes.length + 1 || count > (uint64_t)bytes.length + 1 - address.offset) {
				sema_error(block, src, "dereference exceeds bounds of containing byte array");
				return operand_none;
			}
			if (!reserve_array_elements(block, count, src))
				return operand_none;
			ValueId *items = ARENA_ARRAY(sema->arena, ValueId, count);
			for (uint32_t i = 0; i < count; i++) {
				uint64_t position = address.offset + i;
				items[i] = value_int_u64(&sema->values, array.child, position < bytes.length ? bytes.data[position] : 0);
			}
			return comptime_operand(child, value_array(&sema->values, child, items, count));
		}
		if (value->kind == VALUE_PTR_CAST) {
			ValueId parent = value->as.pointer;
			if (sema->types[child].kind == TYPE_ARRAY) {
				TypeArray array = sema->types[child].as.array;
				/* A view can begin at offset zero without having its root's extent.
				 * Reconstruct the requested physical elements instead of coercing the
				 * entire containing array after canonical pointer casts collapse. */
				Type many_type = make_type(TYPE_POINTER);
				many_type.as.pointer = pointer_type;
				many_type.as.pointer.size = POINTER_MANY;
				many_type.as.pointer.child = array.child;
				many_type.as.pointer.sentinel = VALUE_NONE;
				TypeId many = type_intern(sema, &many_type);
				Operand elements = comptime_operand(many, value_pointer_cast(&sema->values, many, pointer.value));
				uint64_t extra = array.sentinel != VALUE_NONE ? 1u : 0u;
				if (array.length > UINT32_MAX - extra) {
					sema_error(block, src, "not supported by szc yet: array constant with this many elements");
					return operand_none;
				}
				uint32_t count = (uint32_t)(array.length + extra);
				if (!reserve_array_elements(block, count, src))
					return operand_none;
				ValueId *items = ARENA_ARRAY(sema->arena, ValueId, count);
				for (uint32_t i = 0; i < count; i++) {
					Operand index = comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, i));
					Operand item = analyze_many_index(block, elements, index, src, false, false);
					if (item.kind == OPERAND_NONE)
						return operand_none;
					if (item.kind != OPERAND_COMPTIME) {
						sema_error(block, src, "unable to evaluate comptime expression");
						return operand_none;
					}
					items[i] = item.value;
				}
				return comptime_operand(child, value_array(&sema->values, child, items, count));
			}
			Operand stored = load_pointer(block, comptime_operand(value_get(&sema->values, parent)->type, parent), src);
			return coerce(block, stored, child, src);
		}
		if (value->kind == VALUE_STRUCT_FIELD_PTR) {
			StructFieldPointer field = value->as.struct_field;
			TypeId base_type = value_get(&sema->values, field.base)->type;
			Operand stored = load_pointer(block, comptime_operand(base_type, field.base), src);
			if (stored.kind == OPERAND_NONE)
				return operand_none;
			if (stored.kind != OPERAND_COMPTIME || sema->types[stored.type].kind != TYPE_STRUCT) {
				sema_error(block, src, "not supported by szc yet: runtime struct field load");
				return operand_none;
			}
			const Value *backing = operand_value(block, stored);
			SemaNamespace *ns = sema->types[stored.type].as.structure;
			if (field.index >= ns->field_count) {
				sema_error(block, src, "invalid struct field pointer path");
				return operand_none;
			}
			TypeId field_type = ns->fields[field.index].type;
			ValueId selected = backing->kind == VALUE_UNDEFINED
				? value_undefined(&sema->values, field_type) : backing->as.structure.fields[field.index];
			return coerce(block, comptime_operand(field_type, selected), child, src);
		}
		if (value->kind == VALUE_SLICE_FIELD_PTR) {
			SliceFieldPointer field = value->as.field;
			TypeId base_type = value_get(&sema->values, field.base)->type;
			Operand slice = load_pointer(block, comptime_operand(base_type, field.base), src);
			return slice.kind == OPERAND_NONE ? operand_none : slice_field(block, slice, field.length, src);
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
	if (sema_type_is_aggregate(sema, child)) {
		if (!pointer_operand(block, pointer, src))
			return operand_none;
		return aggregate_snapshot(block, pointer, 0, src);
	}
	if (kind != TYPE_INT && kind != TYPE_BOOL && kind != TYPE_POINTER
		&& !(kind == TYPE_ENUM && enum_runtime_allowed(sema, child))) {
		sema_error(block, src, "not supported by szc yet: dereference of '%s'", sema_type_name(sema, child));
		return operand_none;
	}
	if (!pointer_operand(block, pointer, src) || !require_runtime(block, src))
		return operand_none;
	return runtime_operand(child, emit_tir_un(block, pointer_type.is_volatile ? TIR_LOAD_VOLATILE : TIR_LOAD, child, src, materialize(block, pointer, src)));
}

/* Named namespace members and unqualified declarations share address identity,
 * lazy initialization and mutable-global loads. */
static Operand
read_declaration(Block *block, SemaNamespace *ns, SemaDecl *decl, bool as_reference, uint32_t src)
{
	Operand result = resolve_decl(block, src, ns, decl, as_reference);
	if (result.kind == OPERAND_NONE)
		return operand_none;
	if (decl->ir.kind == IR_DECL_VAR)
		return as_reference ? result : load_pointer(block, result, src);
	if (as_reference && decl->global == 0) {
		if (decl->address_identity == 0)
			decl->address_identity = ++block->sema->next_address_identity;
		return analyze_ref(block, result, src, decl->address_identity, decl->alignment);
	}
	return result;
}

/* Merely naming a container makes its exports reachable; an unused container
 * remains lazy. Defer nested exports until its type binding has been published. */
static void
resolve_namespace_exports(Block *block, SemaNamespace *ns)
{
	if (ns->exports_resolved)
		return;
	ns->exports_resolved = true;
	for (uint32_t i = 0; i < ns->decl_count; i++) {
		SemaDecl *decl = &ns->decls[i];
		if ((decl->ir.flags & IR_DECL_EXPORT) != 0)
			resolve_decl(block, decl->ir.src, ns, decl, false);
	}
}

/* The namespace chain is stable even when resolving a member grows type tables. */
static const char *
namespace_name(Sema *sema, SemaNamespace *ns)
{
	StringId id = sema->types[ns->type].name;
	const char *name = id == STRING_NONE ? (sema->types[ns->type].kind == TYPE_ENUM ? "enum" : "struct") : intern_text(sema->interner, id);
	if (ns->parent == NULL)
		return name;
	return arena_sprintf(sema->arena, "%s.%s", namespace_name(sema, ns->parent), name);
}

/* Lookup keeps every byte. Zig's diagnostic renderer stops the message at an
 * embedded NUL; mirror presentation only, never C-string-prefix lookup. */
static void
sema_name_error(Block *block, uint32_t src, const char *prefix, StringId name, const char *suffix)
{
	Sema *sema = block->sema;
	const char *bytes = intern_text(sema->interner, name);
	size_t length = intern_length(sema->interner, name);
	const char *zero = memchr(bytes, '\0', length);
	if (zero != NULL)
		length = (size_t)(zero - bytes);
	size_t head = strlen(prefix);
	size_t tail = zero == NULL ? strlen(suffix) : 0;
	char *text = arena_alloc(sema->arena, head + length + tail + 1, 1);
	memcpy(text, prefix, head);
	memcpy(text + head, bytes, length);
	memcpy(text + head + length, suffix, tail);
	text[head + length + tail] = '\0';
	diag_report_bytes(sema->diag, DIAG_ERROR, block->ir->file, src, text, head + length + tail);
}

/* Tags share the declaration namespace but never masquerade as declarations. */
static Operand
enum_field_value(Block *block, TypeId type, StringId name, uint32_t src)
{
	Sema *sema = block->sema;
	if (!ensure_enum_layout(block, type, src, "field usage"))
		return operand_none;
	SemaNamespace *ns = sema->types[type].as.structure;
	for (uint32_t i = 0; i < ns->field_count; i++)
		if (ns->fields[i].ir.name == name)
			return comptime_operand(type, value_enum(&sema->values, type, ns->enum_tags[i]));
	const char *prefix = arena_sprintf(sema->arena, "enum '%s' has no member named '", sema_type_name(sema, type));
	sema_name_error(block, src, prefix, name, "'");
	return operand_none;
}

/* Conversion checks membership in the original integer domain, never after a
 * truncating cast. The backend receives that same original operand at runtime. */
static Operand
analyze_enum_from_int(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	TypeId type;
	if (!operand_to_type(block, resolve_ref(block, inst->data.bin.lhs), inst->src, &type))
		return operand_none;
	if (sema->types[type].kind != TYPE_ENUM) {
		sema_error(block, inst->src, "expected enum, found '%s'", sema_type_name(sema, type));
		return operand_none;
	}
	Operand operand = resolve_ref(block, inst->data.bin.rhs);
	if (operand.kind == OPERAND_NONE)
		return operand_none;
	if (!is_int_or_comptime_int(sema, operand.type)) {
		sema_error(block, inst->src, "expected integer type, found '%s'", sema_type_name(sema, operand.type));
		return operand_none;
	}
	if (!ensure_enum_layout(block, type, inst->src, "field usage") || !check_defined(block, operand, inst->src))
		return operand_none;
	SemaNamespace *ns = sema->types[type].as.structure;
	if (operand.kind == OPERAND_COMPTIME) {
		BigInt number = operand_value(block, operand)->as.integer;
		if (ns->field_count > 1048576 - sema->enum_work) {
			sema_error(block, inst->src, "not supported by szc yet: enum membership work limit exceeded");
			return operand_none;
		}
		sema->enum_work += ns->field_count;
		for (uint32_t i = 0; i < ns->field_count; i++)
			if (bigint_compare(&number, &value_get(&sema->values, ns->enum_tags[i])->as.integer) == 0)
				return comptime_operand(type, value_enum(&sema->values, type, ns->enum_tags[i]));
		sema_error(block, inst->src, "enum '%s' has no tag with value '%s'", sema_type_name(sema, type), bigint_to_string(sema->arena, &number));
		return operand_none;
	}
	if (!enum_runtime_allowed(sema, type) || sema->types[operand.type].as.integer.bits > 64) {
		sema_error(block, inst->src, "not supported by szc yet: zero-width enum or wide runtime enum conversion");
		return operand_none;
	}
	if (!require_runtime(block, inst->src))
		return operand_none;
	TirRef source = materialize(block, operand, inst->src);
	if (source == TIR_REF_NONE)
		return operand_none;
	return runtime_operand(type, emit_tir_un(block, TIR_ENUM_FROM_INT, type, inst->src, source));
}

static Operand
analyze_int_from_enum(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	Operand operand = resolve_ref(block, inst->data.un.operand);
	if (operand.kind == OPERAND_NONE)
		return operand_none;
	if (sema->types[operand.type].kind != TYPE_ENUM) {
		sema_error(block, inst->src, "expected enum or tagged union, found '%s'", sema_type_name(sema, operand.type));
		return operand_none;
	}
	if (!ensure_enum_layout(block, operand.type, inst->src, "field usage"))
		return operand_none;
	TypeId tag = sema_enum_tag_type(sema, operand.type);
	if (operand.kind == OPERAND_COMPTIME) {
		if (operand_is_undefined(block, operand))
			return comptime_operand(tag, value_undefined(&sema->values, tag));
		return comptime_operand(tag, operand_value(block, operand)->as.enum_tag);
	}
	if (!enum_runtime_allowed(sema, operand.type) || !require_runtime(block, inst->src)) {
		sema_error(block, inst->src, "not supported by szc yet: zero-width runtime enum conversion");
		return operand_none;
	}
	return runtime_operand(tag, emit_tir_un(block, TIR_INT_CAST, tag, inst->src, operand.inst));
}

/* Address-oriented IR passes a pointer to the type binding. Do not confuse
 * namespace declarations with instance fields or an array value's .len. */
static bool
namespace_member(Block *block, Operand base, StringId name, bool as_reference, uint32_t src, Operand *out)
{
	Sema *sema = block->sema;
	if (sema->types[base.type].kind == TYPE_POINTER) {
		TypePointer pointer = sema->types[base.type].as.pointer;
		if (pointer.size != POINTER_ONE || pointer.child != T_TYPE)
			return false;
		if (!check_defined(block, base, src)) {
			*out = operand_none;
			return true;
		}
		base = load_pointer(block, base, src);
		if (base.kind == OPERAND_NONE) {
			*out = operand_none;
			return true;
		}
	} else if (base.type != T_TYPE) {
		return false;
	}
	*out = operand_none;
	if (!check_defined(block, base, src))
		return true;
	TypeId type;
	if (!operand_to_type(block, base, src, &type))
		return true;
	if (sema->types[type].kind == TYPE_ANYOPAQUE) {
		sema_name_error(block, src, "opaque 'anyopaque' has no member named '", name, "'");
		return true;
	}
	if (sema->types[type].kind != TYPE_STRUCT && sema->types[type].kind != TYPE_ENUM) {
		sema_error(block, src, "type '%s' has no members", sema_type_name(sema, type));
		return true;
	}
	SemaNamespace *ns = sema->types[type].as.structure;
	SemaDecl *decl = namespace_find(ns, name);
	if (decl == NULL && sema->types[type].kind == TYPE_ENUM) {
		*out = enum_field_value(block, type, name, src);
		if (as_reference && out->kind != OPERAND_NONE)
			*out = analyze_ref(block, *out, src, 0, 0);
		return true;
	}
	if (decl == NULL) {
		const char *prefix = arena_sprintf(sema->arena, "%sstruct '%s' has no member named '",
			ns->parent == NULL ? "root source file " : "", namespace_name(sema, ns));
		sema_name_error(block, src, prefix, name, "'");
		return true;
	}
	*out = read_declaration(block, ns, decl, as_reference, src);
	return true;
}

/* Resolve logical name bytes, not the sentinel or a C-string prefix. Even an
 * empty name requires a defined pointer into comptime-readable backing. */
static bool
resolve_const_string(Block *block, Operand name, uint32_t src, StringId *out)
{
	Sema *sema = block->sema;
	if (name.kind == OPERAND_NONE || !check_defined(block, name, src))
		return false;
	if (name.kind != OPERAND_COMPTIME) {
		sema_error(block, src, "unable to resolve comptime value");
		return false;
	}
	Operand data = slice_field(block, name, false, src);
	Operand size = slice_field(block, name, true, src);
	if (data.kind == OPERAND_NONE || size.kind == OPERAND_NONE
		|| !check_defined(block, data, src) || !check_defined(block, size, src))
		return false;
	if (data.kind != OPERAND_COMPTIME || size.kind != OPERAND_COMPTIME
		|| integer_pointer(block, data) || mutable_global_pointer(block, data)) {
		sema_error(block, src, "unable to resolve comptime value");
		return false;
	}
	uint64_t length = bigint_to_u64_bits(&operand_value(block, size)->as.integer);
	if (length > 16777216) {
		sema_error(block, src, "not supported by szc yet: comptime name exceeds 16777216 bytes");
		return false;
	}
	if (!reserve_array_elements(block, (uint32_t)length, src))
		return false;
	char *text = arena_alloc(sema->arena, (size_t)length + 1, 1);
	for (uint64_t i = 0; i < length; i++) {
		Operand index = comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, i));
		Operand byte = analyze_many_index(block, data, index, src, false, false);
		if (byte.kind == OPERAND_NONE || !check_defined(block, byte, src))
			return false;
		if (byte.kind != OPERAND_COMPTIME) {
			sema_error(block, src, "unable to resolve comptime value");
			return false;
		}
		text[i] = (char)bigint_to_u64_bits(&operand_value(block, byte)->as.integer);
	}
	/* Value.arrayToIpString/getOrPutTrailingString consume one trailing NUL,
	 * not the first embedded NUL and not every trailing NUL. Read all bytes
	 * above before normalization so undefined backing is still diagnosed. */
	if (length != 0 && text[length - 1] == '\0')
		length--;
	text[length] = '\0';
	*out = intern(sema->interner, text, (uint32_t)length);
	return true;
}

/* Name expressions must not inherit a runtime builder or control-flow target. */
static bool
resolve_name_body(Block *block, IrBody body, uint32_t src, StringId *out)
{
	InstMap map;
	inst_map_init(&map, block->sema->arena);
	map.parent = block->map;
	Block inner;
	block_init_comptime(&inner, block->sema, block->namespace_scope, &map, block->decl_name);
	inner.comptime_value_required = true;
	BodyResult result = analyze_body(&inner, body);
	return result.end == BODY_BREAK_RESULT && resolve_const_string(&inner, result.result, src, out);
}

/* Zig's NullTerminatedString.eqlSlice checks a literal prefix followed by NUL,
 * even if the interned name has more logical bytes. Namespace keys above must
 * still compare the complete StringId, never this built-in-field predicate. */
static bool
builtin_field_name(Sema *sema, StringId name, const char *literal)
{
	size_t length = strlen(literal);
	const char *bytes = intern_text(sema->interner, name);
	return intern_length(sema->interner, name) >= length
		&& memcmp(bytes, literal, length) == 0 && bytes[length] == '\0';
}

/* Resolve type and name before either membership kind check. hasDecl tests
 * public declarations only, even in the same lexical namespace; neither query
 * resolves a member value or searches parent namespaces. */
static Operand
analyze_has_member(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	IrHasMember payload;
	IR_READ(block->ir, inst->data.payload, &payload);
	Operand container = resolve_ref(block, payload.container);
	TypeId type;
	if (container.kind == OPERAND_NONE || !check_defined(block, container, payload.type_src)
		|| !operand_to_type(block, container, payload.type_src, &type))
		return operand_none;
	StringId name;
	if (!resolve_name_body(block, payload.name_body, payload.name_src, &name))
		return operand_none;
	if (inst->op == IR_OFFSET_OF || inst->op == IR_BIT_OFFSET_OF) {
		/* Unlike FieldType, the reference resolves layout before the kind check. */
		if (!ensure_type_layout(block, type, payload.type_src))
			return operand_none;
		if (sema->types[type].kind != TYPE_STRUCT) {
			sema_error(block, payload.type_src, "expected struct type, found '%s'", sema_type_name(sema, type));
			return operand_none;
		}
		SemaNamespace *ns = sema->types[type].as.structure;
		SemaField *field = struct_find_field(ns, name);
		if (field == NULL) {
			const char *suffix = arena_sprintf(sema->arena, "' in struct '%s'", namespace_name(sema, ns));
			sema_name_error(block, payload.name_src, "no field named '", name, suffix);
			return operand_none;
		}
		uint64_t offset = field->offset;
		/* Resolved ordinary layout is bounded by UINT32_MAX bytes. */
		if (inst->op == IR_BIT_OFFSET_OF)
			offset *= 8;
		return comptime_operand(T_COMPTIME_INT, value_int_u64(&sema->values, T_COMPTIME_INT, offset));
	}
	if (inst->op == IR_FIELD_TYPE) {
		if (sema->types[type].kind != TYPE_STRUCT) {
			sema_error(block, payload.type_src, "expected struct or union; found '%s'", sema_type_name(sema, type));
			return operand_none;
		}
		if (!ensure_type_layout(block, type, payload.type_src))
			return operand_none;
		SemaNamespace *ns = sema->types[type].as.structure;
		SemaField *field = struct_find_field(ns, name);
		if (field == NULL) {
			const char *suffix = arena_sprintf(sema->arena, "' in struct '%s'", namespace_name(sema, ns));
			sema_name_error(block, payload.name_src, "no field named '", name, suffix);
			return operand_none;
		}
		return type_operand(sema, field->type);
	}
	if (inst->op == IR_HAS_FIELD) {
		/* Declarations are not instance fields; no implicit pointer dereference. */
		bool found;
		switch (sema->types[type].kind) {
		case TYPE_ENUM:
		case TYPE_STRUCT:
			if (!ensure_type_layout(block, type, payload.type_src))
				return operand_none;
			found = struct_find_field(sema->types[type].as.structure, name) != NULL;
			break;
		case TYPE_ARRAY:
			found = builtin_field_name(sema, name, "len");
			break;
		case TYPE_POINTER:
			if (is_slice(sema, type)) {
				found = builtin_field_name(sema, name, "len") || builtin_field_name(sema, name, "ptr");
				break;
			}
			/* fall through */
		default:
			sema_error(block, payload.type_src, "type '%s' does not support '@hasField'", sema_type_name(sema, type));
			return operand_none;
		}
		return bool_operand(sema, found);
	}
	if (sema->types[type].kind == TYPE_ANYOPAQUE)
		return bool_operand(sema, false);
	if (sema->types[type].kind != TYPE_STRUCT && sema->types[type].kind != TYPE_ENUM) {
		sema_error(block, payload.type_src, "expected struct, enum, union, or opaque; found '%s'", sema_type_name(sema, type));
		return operand_none;
	}
	SemaDecl *decl = namespace_find(sema->types[type].as.structure, name);
	return bool_operand(sema, decl != NULL && (decl->ir.flags & IR_DECL_PUB) != 0);
}

/* Project a word of the original descriptor, never an owned descriptor copy. */
static Operand
slice_field_address(Block *block, Operand storage, bool length, uint32_t src)
{
	Sema *sema = block->sema;
	TypePointer shape = sema->types[storage.type].as.pointer;
	TypeId child = length ? T_USIZE : slice_pointer_type(sema, shape.child);
	uint32_t alignment = pointer_alignment(block, shape, src);
	uint32_t field_alignment = sema_type_align(sema, child);
	if (alignment > field_alignment)
		alignment = field_alignment;
	uint64_t offset = length ? 8u : 0u;
	shape.child = child;
	shape.alignment = alignment == sema_type_align(sema, child) ? 0 : alignment;
	Type pointer = make_type(TYPE_POINTER);
	pointer.as.pointer = shape;
	TypeId type = type_intern(sema, &pointer);
	if (operand_is_undefined(block, storage)) {
		sema_error(block, src, "use of undefined value here causes illegal behavior");
		return operand_none;
	}
	if (storage.kind == OPERAND_COMPTIME) {
		PointerAddress address = value_pointer_address(&sema->values, storage.value);
		if (address.offset > UINT64_MAX - offset) {
			sema_error(block, src, "pointer offset overflow");
			return operand_none;
		}
		return comptime_operand(type, value_slice_field_pointer(&sema->values, type,
			storage.value, pointer_path_offset(sema, storage.value, address.offset + offset), length));
	}
	if (!require_runtime(block, src))
		return operand_none;
	return runtime_operand(type, emit_tir_un(block, length ? TIR_SLICE_LEN_PTR : TIR_PTR_CAST,
		type, src, materialize(block, storage, src)));
}

static SemaField *require_struct_field(Block *block, SemaNamespace *ns, StringId name, uint32_t src);

static Operand
struct_field_address(Block *block, Operand storage, TypeId object, StringId name, uint32_t src)
{
	Sema *sema = block->sema;
	if (sema->types[sema->types[storage.type].as.pointer.child].kind == TYPE_POINTER)
		storage = load_pointer(block, storage, src);
	if (storage.kind == OPERAND_NONE || !check_defined(block, storage, src))
		return operand_none;
	if (!ensure_type_layout(block, object, src))
		return operand_none;
	SemaNamespace *ns = sema->types[object].as.structure;
	SemaField *field = require_struct_field(block, ns, name, src);
	if (field == NULL)
		return operand_none;
	TypePointer pointer = sema->types[storage.type].as.pointer;
	uint32_t alignment = pointer_alignment(block, pointer, src);
	uint32_t natural = sema_type_align(sema, field->type);
	/* Ordinary structs cap field alignment at the field's natural alignment,
	 * even for an over-aligned root or an offset divisible by a larger value. */
	if (alignment > natural)
		alignment = natural;
	Type type = make_type(TYPE_POINTER);
	type.as.pointer = pointer;
	type.as.pointer.child = field->type;
	type.as.pointer.size = POINTER_ONE;
	type.as.pointer.sentinel = VALUE_NONE;
	type.as.pointer.alignment = alignment == natural ? 0 : alignment;
	TypeId result = type_intern(sema, &type);
	if (storage.kind != OPERAND_COMPTIME) {
		if (!require_runtime(block, src))
			return operand_none;
		TirStructFieldPtr field_pointer = {materialize(block, storage, src), (uint32_t)field->offset};
		TirRef ref = emit_tir(block, TIR_STRUCT_FIELD_PTR, result, src);
		block->tir->insts[ref].data.payload = tir_extra(block->tir, sema->arena,
			&field_pointer, TIR_WORDS(TirStructFieldPtr));
		return runtime_operand(result, ref);
	}
	if (runtime_pointer_view(block, storage)) {
		sema_error(block, src, "not supported by szc yet: struct field addresses from reinterpreted roots");
		return operand_none;
	}
	PointerAddress address = value_pointer_address(&sema->values, storage.value);
	const Value *root = value_get(&sema->values, address.root);
	bool mutable_global = root->kind == VALUE_GLOBAL
		&& !sema->globals[root->as.global]->result.is_const
		&& sema_type_runtime_storage_allowed(sema, sema->globals[root->as.global]->result.type);
	if (!mutable_global && root->kind != VALUE_INT_PTR && (root->kind != VALUE_REF
		|| !sema_type_static_storage_allowed(sema, value_get(&sema->values, sema->values.objects[root->as.object].value)->type))) {
		sema_error(block, src, "not supported by szc yet: struct field addresses require completed static backing");
		return operand_none;
	}
	if (root->kind == VALUE_INT_PTR)
		address.offset -= root->as.address;
	if (address.offset > UINT64_MAX - field->offset) {
		sema_error(block, src, "pointer offset overflow");
		return operand_none;
	}
	ValueId value = value_struct_field_pointer(&sema->values, result, storage.value,
		(uint32_t)(field - ns->fields), address.offset + field->offset);
	return comptime_operand(result, value);
}

static Operand
analyze_field_pointer(Block *block, Operand base, StringId name, uint32_t src)
{
	Sema *sema = block->sema;
	if (base.kind == OPERAND_NONE)
		return operand_none;
	/* The base is an expression address. One pointer-valued object may itself
	 * point to a type binding; load its slot before namespace projection. */
	TypeId object = sema->types[base.type].kind == TYPE_POINTER
		? sema->types[base.type].as.pointer.child : 0;
	if (object != 0 && sema->types[object].kind == TYPE_POINTER
		&& sema->types[object].as.pointer.size == POINTER_ONE
		&& sema->types[object].as.pointer.child == T_TYPE) {
		base = load_pointer(block, base, src);
		if (base.kind == OPERAND_NONE)
			return operand_none;
	}
	Operand member;
	if (namespace_member(block, base, name, true, src, &member))
		return member;
	if (!pointer_type_operand(block, base, src))
		return operand_none;
	TypeId child = sema->types[base.type].as.pointer.child;
	TypeId object_type = child;
	if (sema->types[object_type].kind == TYPE_POINTER
		&& sema->types[object_type].as.pointer.size == POINTER_ONE)
		object_type = sema->types[object_type].as.pointer.child;
	if (sema->types[object_type].kind == TYPE_STRUCT)
		return struct_field_address(block, base, object_type, name, src);
	bool length = builtin_field_name(sema, name, "len");
	TypeId array = indexable_array_type(sema, child);
	if (array != 0 && length) {
		Operand count = comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, sema->types[array].as.array.length));
		return analyze_ref(block, count, src, 0, 0);
	}
	if (sema->types[child].kind == TYPE_POINTER && sema->types[child].as.pointer.size == POINTER_ONE
		&& is_slice(sema, sema->types[child].as.pointer.child)) {
		base = load_pointer(block, base, src);
		if (base.kind == OPERAND_NONE)
			return operand_none;
		child = sema->types[base.type].as.pointer.child;
	}
	if (!is_slice(sema, child) || (!length && !builtin_field_name(sema, name, "ptr"))) {
		if (is_slice(sema, child) || array != 0) {
			const char *suffix = arena_sprintf(sema->arena, "' in '%s'", sema_type_name(sema, child));
			sema_name_error(block, src, "no member named '", name, suffix);
		} else
			sema_error(block, src, "type '%s' does not support field access", sema_type_name(sema, child));
		return operand_none;
	}
	return slice_field_address(block, base, length, src);
}

/* Supported one-possible-value children follow Zig analyzeLoad's early fold. */
static bool
type_has_one_value(const Sema *sema, TypeId type)
{
	if (type == T_VOID || (is_int(sema, type) && sema->types[type].as.integer.bits == 0))
		return true;
	if (sema->types[type].kind != TYPE_ARRAY)
		return false;
	TypeArray array = sema->types[type].as.array;
	return (array.length == 0 && array.sentinel == VALUE_NONE) || type_has_one_value(sema, array.child);
}

static Operand
one_value_operand(Block *block, TypeId type, uint32_t src)
{
	Sema *sema = block->sema;
	if (type == T_VOID)
		return void_operand(sema);
	if (is_int(sema, type))
		return comptime_operand(type, value_int_u64(&sema->values, type, 0));
	TypeArray array = sema->types[type].as.array;
	uint64_t extra = array.sentinel != VALUE_NONE ? 1u : 0u;
	if (array.length > UINT32_MAX - extra) {
		sema_error(block, src, "not supported by szc yet: array constant with this many elements");
		return operand_none;
	}
	uint32_t count = (uint32_t)(array.length + extra);
	if (!reserve_array_elements(block, count, src))
		return operand_none;
	Operand child = array.length == 0 ? operand_none : one_value_operand(block, array.child, src);
	if (array.length != 0 && child.kind == OPERAND_NONE)
		return operand_none;
	ValueId *items = ARENA_ARRAY(sema->arena, ValueId, count);
	for (uint32_t i = 0; i < (uint32_t)array.length; i++)
		items[i] = child.value;
	if (array.sentinel != VALUE_NONE)
		items[count - 1] = array.sentinel;
	return comptime_operand(type, value_array(&sema->values, type, items, count));
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
	if (type.size == POINTER_SLICE) {
		if (pointer.kind != OPERAND_COMPTIME) {
			sema_error(block, inst->src, "index syntax required to access runtime-known slice");
			return operand_none;
		}
		/* Zig analyzeLoad returns an element's one possible value before
		 * converting a known slice to an array pointer. */
		if (inst->op == IR_DEREF && type_has_one_value(sema, type.child))
			return one_value_operand(block, type.child, inst->src);
		if (operand_is_undefined(block, pointer)) {
			sema_error(block, inst->src, "cannot dereference undefined value");
			return operand_none;
		}
		Operand length = slice_field(block, pointer, true, inst->src);
		if (operand_is_undefined(block, length)) {
			sema_error(block, inst->src, "cannot dereference slice with undefined length");
			return operand_none;
		}
		uint64_t count = bigint_to_u64_bits(&operand_value(block, length)->as.integer);
		Operand data = slice_field(block, pointer, false, inst->src);
		if (inst->op == IR_DEREF && operand_is_undefined(block, data)) {
			sema_error(block, inst->src, "use of undefined value here causes illegal behavior");
			return operand_none;
		}
		/* analyzeLoad's runtime fallback loads the slice element type, not
		 * an array. A known descriptor need not have known backing contents. */
		if (inst->op == IR_DEREF && (mutable_global_pointer(block, data) || integer_pointer(block, data))) {
			Type element_pointer = sema->types[data.type];
			element_pointer.as.pointer.size = POINTER_ONE;
			TypeId element_pointer_type = type_intern(sema, &element_pointer);
			Operand address = comptime_operand(element_pointer_type,
				value_pointer_cast(&sema->values, element_pointer_type, data.value));
			return load_pointer(block, address, inst->src);
		}
		Type array_pointer = sema->types[pointer.type];
		array_pointer.as.pointer.size = POINTER_ONE;
		array_pointer.as.pointer.child = array_type_id(sema, type.child, count, type.sentinel);
		array_pointer.as.pointer.sentinel = VALUE_NONE;
		TypeId array_pointer_type = type_intern(sema, &array_pointer);
		pointer = comptime_operand(array_pointer_type, operand_is_undefined(block, data)
			? value_undefined(&sema->values, array_pointer_type)
			: value_pointer_cast(&sema->values, array_pointer_type, data.value));
		return inst->op == IR_REF_DEREF ? pointer : load_pointer(block, pointer, inst->src);
	}
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
array_type_id(Sema *sema, TypeId child, uint64_t length, ValueId sentinel)
{
	Type type = make_type(TYPE_ARRAY);
	type.as.array.child = child;
	type.as.array.length = length;
	type.as.array.sentinel = sentinel;
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
	IrArrayType payload;
	if (inst->op == IR_ARRAY_TYPE)
		payload = (IrArrayType){inst->data.bin.lhs, inst->data.bin.rhs, IR_REF_NONE, inst->src};
	else
		IR_READ(block->ir, inst->data.payload, &payload);
	Operand length = coerce(block, resolve_ref(block, payload.length), T_USIZE, inst->src);
	TypeId child;
	if (length.kind == OPERAND_NONE || !operand_to_type(block, resolve_ref(block, payload.child), inst->src, &child))
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
	ValueId sentinel = VALUE_NONE;
	if (payload.sentinel != IR_REF_NONE && !analyze_scalar_sentinel(block,
		resolve_ref(block, payload.sentinel), child, payload.sentinel_src, &sentinel))
		return operand_none;
	return type_operand(sema, array_type_id(sema, child, count, sentinel));
}

/* Memory builtins accept slices, many pointers and single array pointers. */
static TypeId
memory_element_type(Block *block, Operand destination, uint32_t src)
{
	Sema *sema = block->sema;
	if (destination.kind == OPERAND_NONE)
		return 0;
	if (sema->types[destination.type].kind == TYPE_POINTER) {
		TypePointer pointer = sema->types[destination.type].as.pointer;
		if (pointer.size != POINTER_ONE)
			return pointer.child;
		if (sema->types[pointer.child].kind == TYPE_ARRAY)
			return sema->types[pointer.child].as.array.child;
	}
	sema_error(block, src, "type '%s' is not an indexable pointer", sema_type_name(sema, destination.type));
	return 0;
}

/* An absent length denotes a many pointer, not an undefined length. */
static Operand
memory_length(Block *block, Operand pointer, uint32_t src)
{
	Sema *sema = block->sema;
	TypePointer shape = sema->types[pointer.type].as.pointer;
	if (shape.size == POINTER_SLICE)
		return slice_field(block, pointer, true, src);
	if (shape.size == POINTER_ONE)
		return comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE,
			sema->types[shape.child].as.array.length));
	return operand_none;
}

/* Borrow data without changing const/volatile/alignment promises. */
static Operand
memory_data(Block *block, Operand pointer, TypeId child, uint32_t src)
{
	Sema *sema = block->sema;
	TypePointer shape = sema->types[pointer.type].as.pointer;
	if (shape.size == POINTER_SLICE)
		return slice_field(block, pointer, false, src);
	if (shape.size != POINTER_ONE)
		return pointer;
	Type type = make_type(TYPE_POINTER);
	type.as.pointer = shape;
	type.as.pointer.size = POINTER_MANY;
	type.as.pointer.child = child;
	type.as.pointer.sentinel = VALUE_NONE;
	return retag_pointer(block, pointer, type_intern(sema, &type), src);
}

/* Length/type validation precedes zero-bit elision; undefined data still errors
 * for zero-length copies with nonzero-bit elements, as in Zig zirMemcpy. */
static Operand
analyze_memcpy(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	Operand destination = resolve_ref(block, inst->data.bin.lhs);
	Operand source = resolve_ref(block, inst->data.bin.rhs);
	if (destination.kind == OPERAND_NONE || source.kind == OPERAND_NONE)
		return operand_none;
	TypeId child = memory_element_type(block, destination, inst->src);
	TypeId source_child = memory_element_type(block, source, inst->src);
	if (child == 0 || source_child == 0)
		return operand_none;
	Operand dest_len = memory_length(block, destination, inst->src);
	Operand source_len = memory_length(block, source, inst->src);
	if (sema->types[destination.type].as.pointer.is_const) {
		sema_error(block, inst->src, "cannot copy to constant pointer");
		return operand_none;
	}
	if (dest_len.kind == OPERAND_NONE && source_len.kind == OPERAND_NONE) {
		sema_error(block, inst->src, "unknown copy length");
		return operand_none;
	}
	if (!memory_coercion_allowed(block, child, source_child, false, inst->src)) {
		sema_error(block, inst->src, "pointer element type '%s' cannot coerce into element type '%s'",
			sema_type_name(sema, source_child), sema_type_name(sema, child));
		return operand_none;
	}
	if ((dest_len.kind != OPERAND_NONE && !check_defined(block, dest_len, inst->src))
		|| (source_len.kind != OPERAND_NONE && !check_defined(block, source_len, inst->src)))
		return operand_none;
	Operand length = dest_len.kind != OPERAND_NONE ? dest_len : source_len;
	if (source_len.kind == OPERAND_COMPTIME)
		length = source_len;
	if (dest_len.kind != OPERAND_NONE && source_len.kind != OPERAND_NONE) {
		if (dest_len.kind == OPERAND_COMPTIME && source_len.kind == OPERAND_COMPTIME) {
			if (bigint_compare(&operand_value(block, dest_len)->as.integer,
				&operand_value(block, source_len)->as.integer) != 0) {
				sema_error(block, inst->src, "non-matching copy lengths");
				return operand_none;
			}
		} else {
			if (!require_runtime(block, inst->src))
				return operand_none;
			emit_tir_bin(block, TIR_COPY_LEN_CHECK, T_VOID, inst->src,
				materialize(block, dest_len, inst->src), materialize(block, source_len, inst->src));
		}
	}
	uint64_t stride;
	if (!layout_is_comptime_only(sema, child) && type_has_one_value(sema, child))
		return void_operand(sema);
	if (!check_defined(block, destination, inst->src) || !check_defined(block, source, inst->src))
		return operand_none;
	Operand dest_data = memory_data(block, destination, child, inst->src);
	Operand source_data = memory_data(block, source, source_child, inst->src);
	if (dest_data.kind == OPERAND_NONE || source_data.kind == OPERAND_NONE
		|| !check_defined(block, dest_data, inst->src) || !check_defined(block, source_data, inst->src))
		return operand_none;
	if (length.kind == OPERAND_COMPTIME && bigint_is_zero(&operand_value(block, length)->as.integer))
		return void_operand(sema);
	TypeKind kind = sema->types[child].kind;
	bool aggregate = sema_type_is_aggregate(sema, child);
	if ((aggregate && !sema_type_array_storage_allowed(sema, child))
		|| (!aggregate && kind != TYPE_INT && kind != TYPE_BOOL && kind != TYPE_POINTER)) {
		sema_error(block, inst->src, "not supported by szc yet: @memcpy element type '%s'", sema_type_name(sema, child));
		return operand_none;
	}
	if (aggregate && (sema->types[dest_data.type].as.pointer.is_volatile
		|| sema->types[source_data.type].as.pointer.is_volatile)) {
		sema_error(block, inst->src, "not supported by szc yet: volatile aggregate @memcpy");
		return operand_none;
	}
	if (dest_data.kind == OPERAND_COMPTIME && source_data.kind == OPERAND_COMPTIME
		&& length.kind == OPERAND_COMPTIME && sema_type_size(sema, child, &stride)) {
		PointerAddress dest = value_pointer_address(&sema->values, dest_data.value);
		PointerAddress from = value_pointer_address(&sema->values, source_data.value);
		uint64_t count = bigint_to_u64_bits(&operand_value(block, length)->as.integer);
		uint64_t distance = dest.offset >= from.offset ? dest.offset - from.offset : from.offset - dest.offset;
		if (pointer_same_root(sema, dest, from) && distance < count * stride) {
			sema_error(block, inst->src, "'@memcpy' arguments alias");
			return operand_none;
		}
	}
	if (!require_runtime(block, inst->src) || !pointer_operand(block, dest_data, inst->src))
		return operand_none;
	TirMemcpy copy;
	copy.destination = materialize(block, dest_data, inst->src);
	copy.source = materialize(block, source_data, inst->src);
	copy.length = materialize(block, length, inst->src);
	TirRef ref = emit_tir(block, TIR_MEMCPY, T_VOID, inst->src);
	block->tir->insts[ref].data.payload = tir_extra(block->tir, sema->arena, &copy, TIR_WORDS(TirMemcpy));
	return void_operand(sema);
}

/* Fill logical elements, not byte storage or the trailing sentinel. */
static Operand
analyze_memset(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	Operand destination = resolve_ref(block, inst->data.bin.lhs);
	Operand value = resolve_ref(block, inst->data.bin.rhs);
	if (destination.kind == OPERAND_NONE || value.kind == OPERAND_NONE)
		return operand_none;
	TypeId child = memory_element_type(block, destination, inst->src);
	if (child == 0)
		return operand_none;
	TypePointer shape = sema->types[destination.type].as.pointer;
	if (shape.is_const) {
		sema_error(block, inst->src, "cannot memset constant pointer");
		return operand_none;
	}
	if (shape.size != POINTER_SLICE && shape.size != POINTER_ONE) {
		sema_error(block, inst->src, "unknown @memset length");
		return operand_none;
	}
	value = coerce(block, value, child, inst->src);
	if (value.kind == OPERAND_NONE)
		return operand_none;
	if (type_has_one_value(sema, child))
		return void_operand(sema);
	Operand data;
	Operand length;
	if (shape.size == POINTER_SLICE) {
		data = slice_field(block, destination, false, inst->src);
		length = slice_field(block, destination, true, inst->src);
	} else {
		uint64_t count = sema->types[shape.child].as.array.length;
		length = comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, count));
		Type pointer = make_type(TYPE_POINTER);
		pointer.as.pointer = shape;
		pointer.as.pointer.size = POINTER_MANY;
		pointer.as.pointer.child = child;
		pointer.as.pointer.sentinel = VALUE_NONE;
		data = retag_pointer(block, destination, type_intern(sema, &pointer), inst->src);
	}
	if (data.kind == OPERAND_NONE || length.kind == OPERAND_NONE || !check_defined(block, length, inst->src))
		return operand_none;
	if (length.kind == OPERAND_COMPTIME && bigint_is_zero(&operand_value(block, length)->as.integer))
		return void_operand(sema);
	if (!check_defined(block, data, inst->src) || !require_runtime(block, inst->src))
		return operand_none;
	if (!pointer_operand(block, data, inst->src))
		return operand_none;
	TypeKind kind = sema->types[child].kind;
	bool aggregate = sema_type_is_aggregate(sema, child);
	if ((aggregate && !sema_type_array_storage_allowed(sema, child))
		|| (!aggregate && kind != TYPE_INT && kind != TYPE_BOOL && kind != TYPE_POINTER)) {
		sema_error(block, inst->src, "not supported by szc yet: @memset element type '%s'", sema_type_name(sema, child));
		return operand_none;
	}
	if (aggregate && shape.is_volatile) {
		sema_error(block, inst->src, "not supported by szc yet: volatile aggregate @memset");
		return operand_none;
	}
	/* A nonvolatile undefined fill may leave any previous representation. */
	if (operand_is_undefined(block, value) && !shape.is_volatile)
		return void_operand(sema);
	TirMemset fill;
	fill.pointer = materialize(block, data, inst->src);
	fill.length = materialize(block, length, inst->src);
	/* Runtime aggregate values already own snapshots, independent of destination
	 * storage. Constants have immutable backing. Reuse that single fill value. */
	if (aggregate) {
		Operand source = analyze_ref(block, value, inst->src, 0, 0);
		if (source.kind == OPERAND_NONE)
			return operand_none;
		fill.value = materialize(block, source, inst->src);
	} else {
		fill.value = materialize(block, value, inst->src);
	}
	TirRef ref = emit_tir(block, TIR_MEMSET, T_VOID, inst->src);
	block->tir->insts[ref].data.payload = tir_extra(block->tir, sema->arena, &fill, TIR_WORDS(TirMemset));
	return void_operand(sema);
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
	if (array.sentinel != VALUE_NONE && count == UINT32_MAX) {
		sema_error(block, inst->src, "not supported by szc yet: array constant with this many elements");
		return operand_none;
	}
	uint32_t total = count + (array.sentinel != VALUE_NONE ? 1u : 0u);
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
	if (array.sentinel != VALUE_NONE) {
		items[count] = array.sentinel;
		elements[count] = comptime_operand(array.child, items[count]);
	}
	if (all_known)
		return comptime_operand(type, value_array(&sema->values, type, items, total));
	TypeId pointer = type_pointer(sema, type, POINTER_ONE, true, false, 0);
	TirRef slot = aggregate_slot(block, pointer, inst->src);
	if (slot == TIR_REF_NONE)
		return operand_none;
	TypeId child_pointer = type_pointer(sema, array.child, POINTER_ONE, true, false, 0);
	for (uint32_t i = 0; i < total; i++) {
		Operand index = comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, i));
		TirRef address = emit_tir_bin(block, TIR_ARRAY_ELEM_PTR, child_pointer, inst->src, slot, materialize(block, index, inst->src));
		if (sema_type_is_aggregate(sema, array.child)) {
			if (!store_aggregate(block, runtime_operand(child_pointer, address), elements[i], inst->src))
				return operand_none;
		} else if (!operand_is_undefined(block, elements[i])) {
			emit_tir_bin(block, TIR_STORE, T_VOID, inst->src, address, materialize(block, elements[i], inst->src));
		}
	}
	Operand result = runtime_operand(type, slot);
	result.kind = OPERAND_AGGREGATE;
	return result;
}

/* Shared destination checks and sentinel finalization keep ambiguous empty
 * initialization identical to the established nonempty array result-location path. */
static Operand
array_init_destination(Block *block, Operand pointer, uint64_t length, uint32_t src)
{
	Sema *sema = block->sema;
	if (!pointer_type_operand(block, pointer, src))
		return operand_none;
	TypePointer shape = sema->types[pointer.type].as.pointer;
	if (sema->types[shape.child].kind != TYPE_ARRAY) {
		if (sema->types[shape.child].kind == TYPE_STRUCT)
			sema_error(block, src, "not supported by szc yet: in-place struct initialization");
		else
			sema_error(block, src, "expected array type, found '%s'", sema_type_name(sema, shape.child));
		return operand_none;
	}
	TypeArray array = sema->types[shape.child].as.array;
	if (length != array.length)
		sema_error(block, src, "expected %" PRIu64 " array elements; found %" PRIu64, array.length, length);
	else if (shape.is_const)
		sema_error(block, src, "cannot assign to constant");
	else if (shape.is_volatile)
		sema_error(block, src, "not supported by szc yet: volatile in-place array initialization");
	else
		return pointer;
	return operand_none;
}

static Operand
finish_array_init(Block *block, Operand pointer, uint32_t src)
{
	Sema *sema = block->sema;
	if (!pointer_type_operand(block, pointer, src))
		return operand_none;
	TypeArray array = sema->types[sema->types[pointer.type].as.pointer.child].as.array;
	if (array.sentinel != VALUE_NONE) {
		Operand item_index = comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, array.length));
		Operand address = runtime_array_element(block, pointer, item_index, array, src, true);
		if (address.kind == OPERAND_NONE)
			return operand_none;
		Operand value = comptime_operand(array.child, array.sentinel);
		emit_tir_bin(block, TIR_STORE, T_VOID, src, materialize(block, address, src), materialize(block, value, src));
	}
	return void_operand(sema);
}

/* Named initializer bodies preserve source-order type/expression evaluation.
 * Typed-result paths defer only final field coercions until all bodies succeed;
 * virtual result locations coerce each body before proceeding to the next field. */
static SemaField *
require_struct_field(Block *block, SemaNamespace *ns, StringId name, uint32_t src)
{
	SemaField *field = struct_find_field(ns, name);
	if (field == NULL) {
		const char *suffix = arena_sprintf(block->sema->arena, "' in struct '%s'", namespace_name(block->sema, ns));
		sema_name_error(block, src, "no field named '", name, suffix);
	}
	return field;
}

/* Resolve the whole ordinary-default set in its definition context. Layout is
 * already published: a fully explicit self-type value is not a defaults cycle.
 * Caches are committed together; failed or active sets never expose partial data. */
static bool
ensure_struct_defaults(Block *block, SemaNamespace *ns, uint32_t src)
{
	if (ns->defaults_state == DEFAULTS_RESOLVED)
		return true;
	if (ns->defaults_state == DEFAULTS_FAILED)
		return false;
	if (ns->defaults_state == DEFAULTS_IN_PROGRESS) {
		sema_error(block, src, "default value of type '%s' depends on itself here", namespace_name(block->sema, ns));
		return false;
	}
	Sema *sema = block->sema;
	if (sema->defaults_depth >= 256) {
		sema_error(block, src, "not supported by szc yet: struct default dependency depth exceeds 256");
		ns->defaults_state = DEFAULTS_FAILED;
		return false;
	}
	if (!charge_defaults_work(block, src, ns->field_count)) {
		ns->defaults_state = DEFAULTS_FAILED;
		return false;
	}
	ns->defaults_state = DEFAULTS_IN_PROGRESS;
	sema->defaults_depth++;
	ValueId *values = arena_zalloc(sema->arena, ns->field_count * sizeof(ValueId), alignof(ValueId));
	InstMap map;
	inst_map_init(&map, sema->arena);
	map.parent = ns->captures;
	Block defaults;
	block_init_comptime(&defaults, sema, ns, &map, STRING_NONE);
	defaults.comptime_value_required = true;
	for (uint32_t i = 0; i < ns->field_count; i++) {
		SemaField *field = &ns->fields[i];
		if (field->ir.default_body == 0)
			continue;
		defaults.field_default_type = field->type;
		IrBody body = field->ir.default_body;
		uint32_t count = ir_body_count(ns->ir, body);
		uint32_t value_src = count == 0 ? field->ir.src
			: ns->ir->insts[ir_body_insts(ns->ir, body)[count - 1]].src;
		BodyResult result = analyze_body(&defaults, body);
		if (result.end != BODY_BREAK_RESULT || result.result.kind == OPERAND_NONE)
			goto fail;
		Operand value = coerce(&defaults, result.result, field->type, value_src);
		if (value.kind == OPERAND_NONE)
			goto fail;
		if (value.kind != OPERAND_COMPTIME) {
			sema_error(&defaults, value_src, "unable to resolve comptime value");
			goto fail;
		}
		values[i] = value.value;
	}
	for (uint32_t i = 0; i < ns->field_count; i++)
		ns->fields[i].default_value = values[i];
	ns->defaults_state = DEFAULTS_RESOLVED;
	sema->defaults_depth--;
	return true;
fail:
	ns->defaults_state = DEFAULTS_FAILED;
	sema->defaults_depth--;
	return false;
}

static Operand
analyze_struct_init(Block *block, const IrInst *inst, bool *noreturn)
{
	Sema *sema = block->sema;
	IrStructInit header;
	IR_READ(block->ir, inst->data.payload, &header);
	TypeId type;
	if (!operand_to_type(block, resolve_ref(block, header.type), inst->src, &type)
		|| !ensure_type_layout(block, type, inst->src))
		return operand_none;
	if (header.field_count == 0 && sema->types[type].kind == TYPE_ARRAY)
		return analyze_array_init(block, inst);
	if (sema->types[type].kind != TYPE_STRUCT) {
		sema_error(block, inst->src, "type '%s' does not support struct initialization syntax", sema_type_name(sema, type));
		return operand_none;
	}
	SemaNamespace *ns = sema->types[type].as.structure;
	const IrStructInitField *fields = (const IrStructInitField *)(block->ir->extra
		+ inst->data.payload + IR_WORDS(IrStructInit));
	if (!reserve_array_elements(block, ns->field_count, inst->src))
		return operand_none;
	ValueId *values = ARENA_ARRAY(sema->arena, ValueId, ns->field_count);
	Operand *selected = ARENA_ARRAY(sema->arena, Operand, ns->field_count);
	Operand *pending = ARENA_ARRAY(sema->arena, Operand, header.field_count);
	bool *seen = arena_zalloc(sema->arena, ns->field_count * sizeof(bool), alignof(bool));
	bool known = true;
	for (uint32_t i = 0; i < header.field_count; i++) {
		SemaField *field = require_struct_field(block, ns, fields[i].name, fields[i].src);
		if (field == NULL)
			return operand_none;
		BodyResult body = analyze_body(block, fields[i].body);
		if (body.end == BODY_NORETURN)
			*noreturn = true;
		if (body.end != BODY_BREAK_RESULT || body.result.kind == OPERAND_NONE)
			return operand_none;
		pending[i] = body.result;
		if ((header.flags & IR_STRUCT_INIT_TYPED) != 0)
			continue;
		Operand value = coerce(block, body.result, field->type, fields[i].src);
		if (value.kind == OPERAND_NONE)
			return operand_none;
		uint32_t index = (uint32_t)(field - ns->fields);
		selected[index] = value;
		known &= value.kind == OPERAND_COMPTIME;
		seen[index] = true;
	}
	if ((header.flags & IR_STRUCT_INIT_TYPED) != 0) {
		for (uint32_t i = 0; i < header.field_count; i++) {
			SemaField *field = struct_find_field(ns, fields[i].name);
			Operand value = coerce(block, pending[i], field->type, fields[i].src);
			if (value.kind == OPERAND_NONE)
				return operand_none;
			uint32_t index = (uint32_t)(field - ns->fields);
			selected[index] = value;
			known &= value.kind == OPERAND_COMPTIME;
			seen[index] = true;
		}
	}
	for (uint32_t i = 0; i < ns->field_count; i++) {
		if (!seen[i]) {
			if (!ensure_struct_defaults(block, ns, inst->src)) {
				*noreturn = true;
				return operand_none;
			}
			if (ns->fields[i].default_value == VALUE_NONE) {
				sema_name_error(block, inst->src, "missing struct field: ", ns->fields[i].ir.name, "");
				return operand_none;
			}
			selected[i] = comptime_operand(ns->fields[i].type, ns->fields[i].default_value);
		}
		if (known)
			values[i] = selected[i].value;
	}
	if (known)
		return comptime_operand(type, value_struct(&sema->values, type, values, ns->field_count));
	TypeId pointer = type_pointer(sema, type, POINTER_ONE, false, false, 0);
	TirRef slot = aggregate_slot(block, pointer, inst->src);
	if (slot == TIR_REF_NONE)
		return operand_none;
	Operand storage = runtime_operand(pointer, slot);
	for (uint32_t i = 0; i < ns->field_count; i++) {
		Operand field = struct_field_address(block, storage, type, ns->fields[i].ir.name, inst->src);
		if (field.kind == OPERAND_NONE || store_value(block, field, selected[i], inst->src).kind == OPERAND_NONE)
			return operand_none;
	}
	/* An aggregate operand owns these bytes. Only its initialization used a
	 * mutable pointer; later references must not expose that write capability. */
	TypeId immutable = type_pointer(sema, type, POINTER_ONE, true, false, 0);
	TirRef owned = emit_tir_un(block, TIR_PTR_CAST, immutable, inst->src, slot);
	Operand result = runtime_operand(type, owned);
	result.kind = OPERAND_AGGREGATE;
	return result;
}

/* Unlike a typed value initializer, this is a true destination protocol:
 * every completed field becomes observable before the next body executes. */
static Operand
struct_init_destination(Block *block, Operand pointer, const IrStructInitField *fields,
	uint32_t field_count, uint32_t src, bool *noreturn)
{
	Sema *sema = block->sema;
	if (!pointer_type_operand(block, pointer, src))
		return operand_none;
	TypePointer shape = sema->types[pointer.type].as.pointer;
	if (shape.is_const) {
		sema_error(block, src, "cannot assign to constant");
		return operand_none;
	}
	if (shape.size != POINTER_ONE || sema->types[shape.child].kind != TYPE_STRUCT) {
		sema_error(block, src, "type '%s' does not support struct initialization syntax", sema_type_name(sema, shape.child));
		return operand_none;
	}
	if (shape.is_volatile) {
		sema_error(block, src, "not supported by szc yet: volatile whole-struct initialization");
		return operand_none;
	}
	if (!ensure_type_layout(block, shape.child, src)
		|| !check_defined(block, pointer, src))
		return operand_none;
	if (!sema_type_runtime_storage_allowed(sema, shape.child)) {
		sema_error(block, src, "not supported by szc yet: runtime struct storage layout");
		return operand_none;
	}
	SemaNamespace *ns = sema->types[shape.child].as.structure;
	bool *seen = arena_zalloc(sema->arena, ns->field_count * sizeof(bool), alignof(bool));
	for (uint32_t i = 0; i < field_count; i++) {
		SemaField *field = require_struct_field(block, ns, fields[i].name, fields[i].src);
		if (field == NULL)
			return operand_none;
		BodyResult body = analyze_body(block, fields[i].body);
		if (body.end == BODY_NORETURN)
			*noreturn = true;
		if (body.end != BODY_BREAK_RESULT || body.result.kind == OPERAND_NONE)
			return operand_none;
		Operand destination = struct_field_address(block, pointer, shape.child, fields[i].name, fields[i].src);
		if (destination.kind == OPERAND_NONE
			|| store_value(block, destination, body.result, fields[i].src).kind == OPERAND_NONE)
			return operand_none;
		seen[(uint32_t)(field - ns->fields)] = true;
	}
	for (uint32_t i = 0; i < ns->field_count; i++) {
		if (seen[i])
			continue;
		if (!ensure_struct_defaults(block, ns, src)) {
			*noreturn = true;
			return operand_none;
		}
		SemaField *field = &ns->fields[i];
		if (field->default_value == VALUE_NONE) {
			sema_name_error(block, src, "missing struct field: ", field->ir.name, "");
			return operand_none;
		}
		Operand destination = struct_field_address(block, pointer, shape.child, field->ir.name, src);
		Operand value = comptime_operand(field->type, field->default_value);
		if (destination.kind == OPERAND_NONE || store_value(block, destination, value, src).kind == OPERAND_NONE)
			return operand_none;
	}
	return load_pointer(block, pointer, src);
}

/* Numeric/runtime roots and volatile storage are actual lvalues, even when
 * their address is wrapped in a known field path. Never read a whole aggregate
 * merely to project a scalar field from one of these roots. */
static bool
logical_projection_pointer(Block *block, Operand pointer)
{
	if (pointer.kind != OPERAND_COMPTIME)
		return false;
	if (block->sema->types[pointer.type].as.pointer.is_volatile || mutable_global_pointer(block, pointer))
		return false;
	return !absolute_pointer(block, pointer);
}

/* Instance reads are logical projections, not field addresses or runtime loads.
 * Expression addresses used by @field and borrowed .len are unwrapped first. */
static bool
struct_value_field(Block *block, Operand base, StringId name, uint32_t src,
	bool expression_address, Operand *out)
{
	Sema *sema = block->sema;
	TypeId type = base.type;
	if (expression_address) {
		if (sema->types[type].kind != TYPE_POINTER)
			return false;
		type = sema->types[type].as.pointer.child;
	}
	bool indirect = sema->types[type].kind == TYPE_POINTER
		&& sema->types[type].as.pointer.size == POINTER_ONE
		&& sema->types[sema->types[type].as.pointer.child].kind == TYPE_STRUCT;
	if (!indirect && sema->types[type].kind != TYPE_STRUCT)
		return false;
	if (base.kind != OPERAND_COMPTIME)
		return false;
	if (expression_address || indirect) {
		if (!logical_projection_pointer(block, base))
			return false;
		if (expression_address && indirect) {
			Operand target = load_pointer(block, base, src);
			if (target.kind == OPERAND_NONE) {
				*out = operand_none;
				return true;
			}
			if (!logical_projection_pointer(block, target))
				return false;
		}
	}
	*out = operand_none;
	if (expression_address)
		base = load_pointer(block, base, src);
	if (base.kind == OPERAND_NONE)
		return true;
	if (indirect) {
		if (!check_defined(block, base, src))
			return true;
		base = load_pointer(block, base, src);
	}
	if (base.kind == OPERAND_NONE)
		return true;
	if (!ensure_type_layout(block, base.type, src))
		return true;
	SemaNamespace *ns = sema->types[base.type].as.structure;
	SemaField *field = require_struct_field(block, ns, name, src);
	if (field == NULL)
		return true;
	if (base.kind != OPERAND_COMPTIME) {
		sema_error(block, src, "not supported by szc yet: runtime struct field access");
		return true;
	}
	const Value *value = operand_value(block, base);
	ValueId selected = value->kind == VALUE_UNDEFINED
		? value_undefined(&sema->values, field->type)
		: value->as.structure.fields[(uint32_t)(field - ns->fields)];
	Operand result = comptime_operand(field->type, selected);
	if (operand_is_undefined(block, result) && type_has_one_value(sema, field->type))
		result = one_value_operand(block, field->type, src);
	*out = result;
	return true;
}

/* A field-call receiver is an expression reference, not a bound-function value.
 * Freeze its pointer, selected callback, or owned value before argument bodies. */
static InstOutcome
analyze_field_call(Block *block, IrInstIndex index, Operand *out)
{
	Sema *sema = block->sema;
	const IrInst *inst = &block->ir->insts[index];
	IrFieldCallHeader header;
	IR_READ(block->ir, inst->data.payload, &header);
	const IrBody *bodies = &block->ir->extra[inst->data.payload + IR_WORDS(IrFieldCallHeader)];
	Operand base = resolve_ref(block, header.object);
	Operand callee = operand_none;
	Operand receiver = operand_none;
	*out = operand_none;
	if (base.kind == OPERAND_NONE || !pointer_type_operand(block, base, inst->src))
		return INST_CONTINUE;
	if (sema->types[base.type].as.pointer.size != POINTER_ONE) {
		sema_error(block, inst->src, "expected single pointer, found '%s'", sema_type_name(sema, base.type));
		return INST_CONTINUE;
	}
	TypeId concrete = sema->types[base.type].as.pointer.child;
	Operand object = base;
	if (sema->types[concrete].kind == TYPE_POINTER
		&& sema->types[concrete].as.pointer.size == POINTER_ONE) {
		object = load_pointer(block, base, inst->src);
		if (object.kind == OPERAND_NONE)
			return INST_CONTINUE;
		concrete = sema->types[object.type].as.pointer.child;
	}
	if (!check_defined(block, object, inst->src))
		return INST_CONTINUE;
	if (namespace_member(block, object, header.name, false, header.name_src, &callee))
		return analyze_call_core(block, index, callee, header.modifier,
			header.arg_count, bodies, receiver, out);
	if (!ensure_type_layout(block, concrete, inst->src))
		return INST_CONTINUE;
	if (sema->types[concrete].kind == TYPE_ENUM) {
		sema_error(block, header.name_src, "not supported by szc yet: enum method calls");
		return INST_CONTINUE;
	}
	if (sema->types[concrete].kind == TYPE_STRUCT) {
		SemaNamespace *ns = sema->types[concrete].as.structure;
		bool field_found = false;
		for (uint32_t i = 0; i < ns->field_count; i++)
			if (ns->fields[i].ir.name == header.name) {
				field_found = true;
				break;
			}
		if (field_found) {
			if (!struct_value_field(block, object, header.name, header.name_src, true, &callee)) {
				Operand address = analyze_field_pointer(block, object, header.name, header.name_src);
				callee = address.kind == OPERAND_NONE ? operand_none
					: load_pointer(block, address, header.name_src);
			}
			return analyze_call_core(block, index, callee, header.modifier,
				header.arg_count, bodies, receiver, out);
		}
		SemaDecl *decl = namespace_find(ns, header.name);
		if (decl != NULL) {
			callee = read_declaration(block, ns, decl, false, header.name_src);
			if (callee.kind == OPERAND_NONE)
				return INST_CONTINUE;
			if (sema->types[callee.type].kind == TYPE_FN) {
				TypeFn fn = sema->types[callee.type].as.fn;
				if (fn.is_generic)
					return analyze_call_core(block, index, callee, header.modifier,
						header.arg_count, bodies, object, out);
				if (fn.param_count != 0) {
					TypeId first = fn.params[0];
					if (first == concrete)
						receiver = load_pointer(block, object, inst->src);
					else if (sema->types[first].kind == TYPE_POINTER
						&& sema->types[first].as.pointer.size == POINTER_ONE
						&& sema->types[first].as.pointer.child == concrete)
						receiver = object;
					if (receiver.kind != OPERAND_NONE)
						return analyze_call_core(block, index, callee, header.modifier,
							header.arg_count, bodies, receiver, out);
					if (first == concrete)
						return INST_CONTINUE;
				}
			}
		}
	}
	const char *suffix = arena_sprintf(sema->arena, "' in '%s'", sema_type_name(sema, concrete));
	sema_name_error(block, inst->src, "no field or member function named '", header.name, suffix);
	return INST_CONTINUE;
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
		TypeId leaf = array.child;
		while (sema->types[leaf].kind == TYPE_ARRAY)
			leaf = sema->types[leaf].as.array.child;
		if (sema->types[base.type].as.pointer.is_volatile && sema->types[leaf].kind == TYPE_STRUCT) {
			sema_error(block, src, "not supported by szc yet: volatile struct loads");
			return operand_none;
		}
		if (!check_defined(block, base, src))
			return operand_none;
		PointerAddress address = value_pointer_address(&sema->values, base.value);
		const Value *root = value_get(&sema->values, address.root);
		if (root->kind == VALUE_BYTES) {
			ByteString bytes = root->as.bytes;
			if (address.offset > bytes.length || index > bytes.length - address.offset) {
				sema_error(block, src, "dereference exceeds bounds of containing byte array");
				return operand_none;
			}
			uint64_t position = address.offset + index;
			return comptime_operand(array.child, value_int_u64(&sema->values, array.child, position < bytes.length ? bytes.data[position] : 0));
		}
		const Value *pointer = operand_value(block, base);
		if (pointer->kind == VALUE_PTR_CAST) {
			ValueId parent = pointer->as.pointer;
			TypeId parent_type = value_get(&sema->values, parent)->type;
			if (sema->types[parent_type].as.pointer.size == POINTER_MANY) {
				Operand item = comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, index));
				return analyze_many_index(block, comptime_operand(parent_type, parent), item, src, false, false);
			}
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
array_element_stride(Block *block, TypeId child, uint64_t *stride, bool *logical, uint32_t src)
{
	Sema *sema = block->sema;
	uint64_t count = 1;
	TypeId leaf = child;
	while (sema->types[leaf].kind == TYPE_ARRAY)
		leaf = sema->types[leaf].as.array.child;
	*logical = is_comptime_only_type(sema, leaf);
	if (!*logical)
		return ensure_type_layout(block, child, src) && sema_type_size(sema, child, stride);
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
array_element_pointer_type(Block *block, TypePointer pointer, TypeArray array, Operand index, uint64_t stride, bool logical, uint32_t src)
{
	Sema *sema = block->sema;
	uint32_t alignment = pointer_alignment(block, pointer, src);
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
	type.as.pointer.sentinel = VALUE_NONE;
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
		ValueId field;
		PointerAddress view;
		uint32_t work = 0;
		if (!struct_pointer_view(block, pointer.value, &field, &view, &work, src))
			return operand_none;
		ValueId origin = field == VALUE_NONE ? address.root : field;
		TypeId origin_type = value_get(&sema->values, origin)->type;
		Operand stored = load_pointer(block, comptime_operand(origin_type, origin), src);
		uint64_t offset = field == VALUE_NONE ? address.offset : view.offset;
		TypeId containing_type = stored.type;
		while (stored.kind == OPERAND_COMPTIME) {
			if (offset == 0 && memory_coercion_allowed(block, child, stored.type, false, src))
				return coerce(block, stored, child, src);
			if (sema->types[stored.type].kind != TYPE_ARRAY)
				break;
			TypeArray array = sema->types[stored.type].as.array;
			uint64_t stride;
			bool logical;
			if (!array_element_stride(block, array.child, &stride, &logical, src))
				break;
			uint64_t index = stride == 0 ? 0 : offset / stride;
			if (index > array.length || (index == array.length && array.sentinel == VALUE_NONE))
				break;
			stored = constant_array_element(block, stored, array, index, src);
			if (stride != 0)
				offset %= stride;
		}
		if (stored.kind == OPERAND_NONE)
			return operand_none;
		if (field != VALUE_NONE) {
			sema_error(block, src, "dereference of '%s' exceeds bounds of containing decl of type '%s'",
				sema_type_name(sema, pointer.type), sema_type_name(sema, containing_type));
			return operand_none;
		}
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
	if (!array_element_stride(block, array.child, &stride, &logical, src)) {
		sema_error(block, src, "not supported by szc yet: array element offset exceeds 64-bit address space");
		return operand_none;
	}
	TypeId type = array_element_pointer_type(block, sema->types[base.type].as.pointer, array, index, stride, logical, src);
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
	return comptime_operand(type, value_element_pointer(&sema->values, type, base.value, item, pointer_path_offset(sema, base.value, offset), logical));
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
	if (type_has_enum_value(sema, array.child) && !ensure_type_layout(block, array.child, src))
		return operand_none;
	TypeKind kind = sema->types[array.child].kind;
	if (kind != TYPE_INT && kind != TYPE_BOOL && kind != TYPE_POINTER && kind != TYPE_VOID && kind != TYPE_ARRAY && kind != TYPE_STRUCT
		&& !(kind == TYPE_ENUM && enum_runtime_allowed(sema, array.child))) {
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
	if (!array_element_stride(block, array.child, &stride, &logical, src)) {
		sema_error(block, src, "not supported by szc yet: array element offset exceeds 64-bit address space");
		return operand_none;
	}
	TypeId element_pointer = array_element_pointer_type(block, pointer, array, index, stride, logical, src);
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
	TypeArray element_shape = {shape.child, 0, VALUE_NONE};
	uint64_t stride;
	bool logical;
	if (!array_element_stride(block, shape.child, &stride, &logical, src)) {
		sema_error(block, src, "not supported by szc yet: pointer element offset exceeds 64-bit address space");
		return operand_none;
	}
	TypeId pointer_type = array_element_pointer_type(block, shape, element_shape, index, stride, logical, src);
	Operand pointer;
	if (base.kind == OPERAND_COMPTIME && index.kind == OPERAND_COMPTIME) {
		uint64_t item = bigint_to_u64_bits(&operand_value(block, index)->as.integer);
		PointerAddress address = value_pointer_address(&sema->values, base.value);
		if (stride != 0 && item > (UINT64_MAX - address.offset) / stride) {
			sema_error(block, src, "not supported by szc yet: pointer element offset exceeds 64-bit address space");
			return operand_none;
		}
		pointer = comptime_operand(pointer_type, value_element_pointer(&sema->values, pointer_type,
			base.value, item, pointer_path_offset(sema, base.value, address.offset + item * stride), logical));
	} else {
		if (!require_runtime(block, src))
			return operand_none;
		if (logical || !sema_type_runtime_storage_allowed(sema, shape.child)) {
			sema_error(block, src, "not supported by szc yet: runtime many-pointer element type '%s'", sema_type_name(sema, shape.child));
			return operand_none;
		}
		TirRef base_ref = materialize(block, base, src);
		TirRef index_ref = materialize(block, index, src);
		pointer = runtime_operand(pointer_type, emit_tir_bin(block, TIR_PTR_ELEM_PTR, pointer_type, src, base_ref, index_ref));
	}
	return address_only || (borrow && (sema->types[shape.child].kind == TYPE_ARRAY || sema->types[shape.child].kind == TYPE_STRUCT))
		? pointer : load_pointer(block, pointer, src);
}

/* Constant backing is checked during analysis; explicit runtime assertions read
 * the physical end only after the slice bounds checks have succeeded. */
static bool
check_slice_sentinel(Block *block, Operand pointer, Operand length, ValueId sentinel, bool explicit, uint32_t src)
{
	if (sentinel == VALUE_NONE)
		return true;
	bool runtime = pointer.kind != OPERAND_COMPTIME || length.kind != OPERAND_COMPTIME;
	if ((!explicit && runtime) || (!runtime && (mutable_global_pointer(block, pointer) || integer_pointer(block, pointer))))
		return true;
	Operand actual = analyze_many_index(block, pointer, length, src, false, false);
	if (actual.kind == OPERAND_NONE || !check_defined(block, actual, src))
		return false;
	if (actual.kind == OPERAND_COMPTIME) {
		if (actual.value == sentinel)
			return true;
		sema_error(block, src, "value in memory does not match slice sentinel");
		return false;
	}
	if (!explicit || !require_runtime(block, src))
		return false;
	Operand expected = comptime_operand(actual.type, sentinel);
	TirRef actual_ref = materialize(block, actual, src);
	TirRef expected_ref = materialize(block, expected, src);
	emit_tir_bin(block, TIR_SENTINEL_CHECK, T_VOID, src, actual_ref, expected_ref);
	return true;
}

/* Slicing borrows storage; a known resulting length produces an array pointer. */
static Operand
analyze_slice(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	IrSlice slice;
	IR_READ(block->ir, inst->data.payload, &slice);
	Operand base = resolve_ref(block, slice.base);
	Operand start = coerce(block, resolve_ref(block, slice.start), T_USIZE, slice.start_src);
	Operand end = slice.end == IR_REF_NONE ? operand_none : coerce(block, resolve_ref(block, slice.end), T_USIZE, slice.end_src);
	if (base.kind == OPERAND_NONE || start.kind == OPERAND_NONE || (slice.end != IR_REF_NONE && end.kind == OPERAND_NONE))
		return operand_none;
	if (!pointer_type_operand(block, base, inst->src))
		return operand_none;
	TypeId input_type = sema->types[base.type].as.pointer.child;
	if (sema->types[input_type].kind == TYPE_POINTER)
		base = load_pointer(block, base, inst->src);
	else if (sema->types[input_type].kind != TYPE_ARRAY) {
		sema_error(block, inst->src, "slice of non-array type '%s'", sema_type_name(sema, input_type));
		return operand_none;
	}
	if (base.kind == OPERAND_NONE)
		return operand_none;
	TypePointer shape = sema->types[base.type].as.pointer;
	Operand data = base;
	Operand available = operand_none;
	ValueId inherited_sentinel = VALUE_NONE;
	bool single_item = false;
	if (shape.size == POINTER_SLICE) {
		if (!check_defined(block, base, inst->src))
			return operand_none;
		data = slice_field(block, base, false, inst->src);
		available = slice_field(block, base, true, inst->src);
		inherited_sentinel = shape.sentinel;
	} else if (shape.size == POINTER_ONE) {
		TypeArray array;
		if (sema->types[shape.child].kind == TYPE_ARRAY) {
			array = sema->types[shape.child].as.array;
			shape.child = array.child;
			inherited_sentinel = array.sentinel;
		} else {
			if (slice.end == IR_REF_NONE) {
				sema_error(block, inst->src, "slice of single-item pointer must be bounded");
				return operand_none;
			}
			array = (TypeArray){shape.child, 1, VALUE_NONE};
			single_item = true;
		}
		available = comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, array.length));
		shape.size = POINTER_MANY;
		Type type = make_type(TYPE_POINTER);
		type.as.pointer = shape;
		TypeId many = type_intern(sema, &type);
		if (operand_is_undefined(block, base))
			data = comptime_operand(many, value_undefined(&sema->values, many));
		else if (base.kind == OPERAND_COMPTIME)
			data = comptime_operand(many, value_pointer_cast(&sema->values, many, base.value));
		else
			data = runtime_operand(many, emit_tir_un(block, TIR_PTR_CAST, many, inst->src, base.inst));
	}
	if (data.kind == OPERAND_NONE || !check_defined(block, start, slice.start_src))
		return operand_none;
	ValueId asserted_sentinel = VALUE_NONE;
	if (slice.sentinel != IR_REF_NONE) {
		if (single_item) {
			sema_error(block, slice.sentinel_src, "slice of single-item pointer cannot have sentinel");
			return operand_none;
		}
		if (!analyze_scalar_sentinel(block, resolve_ref(block, slice.sentinel), shape.child,
			slice.sentinel_src, &asserted_sentinel))
			return operand_none;
	}
	if (end.kind == OPERAND_NONE) {
		if (available.kind == OPERAND_NONE) {
			if (asserted_sentinel != VALUE_NONE && asserted_sentinel != shape.sentinel) {
				sema_error(block, slice.sentinel_src, "sentinel-terminated slicing of many-item pointer must match existing sentinel");
				return operand_none;
			}
			return analyze_pointer_offset(block, IR_ADD, data, start, inst->src);
		}
		end = available;
	}
	if (!check_defined(block, end, slice.end_src))
		return operand_none;
	bool known_start = start.kind == OPERAND_COMPTIME;
	bool known_end = end.kind == OPERAND_COMPTIME;
	uint64_t first = known_start ? bigint_to_u64_bits(&operand_value(block, start)->as.integer) : 0;
	uint64_t last = known_end ? bigint_to_u64_bits(&operand_value(block, end)->as.integer) : 0;
	if (single_item && (!known_start || !known_end)) {
		sema_error(block, inst->src, "unable to resolve comptime value");
		return operand_none;
	}
	ValueId sentinel = slice.end == IR_REF_NONE
		|| (known_end && available.kind == OPERAND_COMPTIME && last == bigint_to_u64_bits(&operand_value(block, available)->as.integer))
		? inherited_sentinel : VALUE_NONE;
	if (asserted_sentinel != VALUE_NONE)
		sentinel = asserted_sentinel;
	Operand bound = available;
	if (inherited_sentinel != VALUE_NONE) {
		/* Zig uses wrapping usize addition for the physical sentinel bound. */
		if (available.kind == OPERAND_COMPTIME) {
			uint64_t length = bigint_to_u64_bits(&operand_value(block, available)->as.integer);
			bound = comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, length + 1));
		} else {
			if (!require_runtime(block, inst->src))
				return operand_none;
			Operand one = comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, 1));
			bound = runtime_operand(T_USIZE, emit_tir_bin(block, TIR_ADD_WRAP, T_USIZE, inst->src,
				materialize(block, available, inst->src), materialize(block, one, inst->src)));
		}
	}
	if (slice.end != IR_REF_NONE && known_end && bound.kind == OPERAND_COMPTIME) {
		uint64_t limit = bigint_to_u64_bits(&operand_value(block, bound)->as.integer);
		if (last > limit) {
			sema_error(block, slice.end_src, "end index %" PRIu64 " out of bounds for %s of length %" PRIu64 "%s", last,
				is_slice(sema, base.type) ? "slice" : "array", limit - (inherited_sentinel != VALUE_NONE ? 1u : 0u), inherited_sentinel != VALUE_NONE ? " +1 (sentinel)" : "");
			return operand_none;
		}
	}
	if (known_start && known_end && first > last) {
		sema_error(block, slice.start_src, "start index %" PRIu64 " is larger than end index %" PRIu64, first, last);
		return operand_none;
	}
	bool same_index = start.kind == OPERAND_RUNTIME && end.kind == OPERAND_RUNTIME && start.inst == end.inst;
	if ((!known_start || !known_end) && !(known_start && first == 0) && !same_index) {
		if (!require_runtime(block, inst->src))
			return operand_none;
		TirRef start_ref = materialize(block, start, inst->src);
		TirRef end_ref = materialize(block, end, inst->src);
		emit_tir_bin(block, TIR_SLICE_START_CHECK, T_VOID, inst->src, start_ref, end_ref);
	}
	bool runtime_sentinel = inherited_sentinel != VALUE_NONE && (!known_start || !known_end);
	bool runtime_assertion = asserted_sentinel != VALUE_NONE
		&& (data.kind != OPERAND_COMPTIME || !known_start || !known_end);
	/* Zig's dynamic-result path uses a known descriptor's logical length,
	 * but runtime descriptors and array backing include the physical sentinel. */
	Operand runtime_bound = runtime_sentinel && is_slice(sema, base.type)
		&& available.kind == OPERAND_COMPTIME ? available : bound;
	if (runtime_bound.kind != OPERAND_NONE && (runtime_sentinel || runtime_assertion
		|| (slice.end != IR_REF_NONE && !(known_end && runtime_bound.kind == OPERAND_COMPTIME)))) {
		if (!require_runtime(block, inst->src))
			return operand_none;
		Operand physical_end = end;
		if (runtime_assertion) {
			if (known_end)
				physical_end = comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, last + 1));
			else {
				Operand one = comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, 1));
				TirRef end_ref = materialize(block, end, inst->src);
				TirRef one_ref = materialize(block, one, inst->src);
				physical_end = runtime_operand(T_USIZE, emit_tir_bin(block, TIR_ADD_WRAP, T_USIZE, inst->src, end_ref, one_ref));
			}
		}
		/* Zig's known-length result path elides a constant physical-end check;
		 * runtime lengths/descriptors retain their dynamic bounds guard. */
		if (!(known_start && known_end && physical_end.kind == OPERAND_COMPTIME
			&& runtime_bound.kind == OPERAND_COMPTIME)) {
			TirRef end_ref = materialize(block, physical_end, inst->src);
			TirRef bound_ref = materialize(block, runtime_bound, inst->src);
			emit_tir_bin(block, TIR_SLICE_END_CHECK, T_VOID, inst->src, end_ref, bound_ref);
		}
	}
	Operand pointer = analyze_pointer_offset(block, IR_ADD, data, start, inst->src);
	if (pointer.kind == OPERAND_NONE)
		return operand_none;
	Type result = sema->types[pointer.type];
	if (known_start && known_end) {
		uint64_t count = last - first;
		Operand length = comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, count));
		if (!check_slice_sentinel(block, pointer, length, sentinel, asserted_sentinel != VALUE_NONE, inst->src))
			return operand_none;
		result.as.pointer.size = POINTER_ONE;
		result.as.pointer.sentinel = VALUE_NONE;
		result.as.pointer.child = array_type_id(sema, result.as.pointer.child, count, sentinel);
		TypeId type = type_intern(sema, &result);
		if (operand_is_undefined(block, pointer)) {
			if (count != 0) {
				sema_error(block, inst->src, "non-zero length slice of undefined pointer");
				return operand_none;
			}
			return comptime_operand(type, value_undefined(&sema->values, type));
		}
		if (pointer.kind == OPERAND_COMPTIME)
			return comptime_operand(type, value_pointer_cast(&sema->values, type, pointer.value));
		return runtime_operand(type, emit_tir_un(block, TIR_PTR_CAST, type, inst->src, pointer.inst));
	}
	TirRef length = emit_tir_bin(block, TIR_SUB_WRAP, T_USIZE, inst->src,
		materialize(block, end, inst->src), materialize(block, start, inst->src));
	if (!check_slice_sentinel(block, pointer, runtime_operand(T_USIZE, length), sentinel,
		asserted_sentinel != VALUE_NONE, inst->src))
		return operand_none;
	result.as.pointer.size = POINTER_SLICE;
	result.as.pointer.sentinel = sentinel;
	return make_slice(block, type_intern(sema, &result), pointer, runtime_operand(T_USIZE, length), inst->src);
}

static Operand
analyze_array_index(Block *block, const IrInst *inst, bool address_only, bool borrow)
{
	Sema *sema = block->sema;
	Operand base = resolve_ref(block, inst->data.bin.lhs);
	Operand index = coerce(block, resolve_ref(block, inst->data.bin.rhs), T_USIZE, inst->src);
	if (base.kind == OPERAND_NONE || index.kind == OPERAND_NONE)
		return operand_none;
	if (sema->types[base.type].kind == TYPE_POINTER && sema->types[base.type].as.pointer.size == POINTER_SLICE) {
		Operand length = slice_field(block, base, true, inst->src);
		Operand data = slice_field(block, base, false, inst->src);
		if (!check_defined(block, index, inst->src) || !check_defined(block, length, inst->src))
			return operand_none;
		if (index.kind == OPERAND_COMPTIME && length.kind == OPERAND_COMPTIME) {
			uint64_t item = bigint_to_u64_bits(&operand_value(block, index)->as.integer);
			uint64_t count = bigint_to_u64_bits(&operand_value(block, length)->as.integer);
			bool has_sentinel = sema->types[base.type].as.pointer.sentinel != VALUE_NONE;
			if (item > count || (item == count && !has_sentinel) || (count == UINT64_MAX && has_sentinel)) {
				sema_error(block, inst->src, "index %" PRIu64 " outside slice of length %" PRIu64 "%s", item, count, has_sentinel ? " +1 (sentinel)" : "");
				return operand_none;
			}
		} else {
			if (!require_runtime(block, inst->src))
				return operand_none;
			emit_tir_bin(block, sema->types[base.type].as.pointer.sentinel != VALUE_NONE
				? TIR_SLICE_END_CHECK : TIR_INDEX_CHECK, T_VOID, inst->src,
				materialize(block, index, inst->src), materialize(block, length, inst->src));
		}
		return analyze_many_index(block, data, index, inst->src, address_only, borrow);
	}
	if (sema->types[base.type].kind == TYPE_POINTER && sema->types[base.type].as.pointer.size == POINTER_MANY)
		return analyze_many_index(block, base, index, inst->src, address_only, borrow);
	TypeId type = indexable_array_type(sema, base.type);
	if (type == 0) {
		sema_error(block, inst->src, "expected array or pointer to array, found '%s'", sema_type_name(sema, base.type));
		return operand_none;
	}
	TypeArray array = sema->types[type].as.array;
	if ((array.length == 0 && array.sentinel == VALUE_NONE)
		|| (array.length == UINT64_MAX && array.sentinel != VALUE_NONE)) {
		sema_error(block, inst->src, sema->types[base.type].kind == TYPE_POINTER
			? "cannot index into empty array" : "indexing into empty array is not allowed");
		return operand_none;
	}
	if (!check_defined(block, index, inst->src))
		return operand_none;
	if (index.kind == OPERAND_COMPTIME) {
		uint64_t item = bigint_to_u64_bits(&operand_value(block, index)->as.integer);
		if (item > array.length || (item == array.length && array.sentinel == VALUE_NONE)) {
			sema_error(block, inst->src, "index %" PRIu64 " outside array of length %" PRIu64 "%s", item, array.length, array.sentinel != VALUE_NONE ? " +1 (sentinel)" : "");
			return operand_none;
		}
		if (base.kind == OPERAND_COMPTIME) {
			if (address_only || (borrow && (sema->types[array.child].kind == TYPE_ARRAY || sema->types[array.child].kind == TYPE_STRUCT)))
				return constant_array_pointer(block, base, index, array, inst->src);
			if (mutable_global_pointer(block, base) || absolute_pointer(block, base) || runtime_pointer_view(block, base)) {
				Operand pointer = constant_array_pointer(block, base, index, array, inst->src);
				return load_pointer(block, pointer, inst->src);
			}
			return constant_array_element(block, base, array, item, inst->src);
		}
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
	return runtime_array_element(block, base, index, array, inst->src, address_only || (borrow && (sema->types[array.child].kind == TYPE_ARRAY || sema->types[array.child].kind == TYPE_STRUCT)));
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
	ValueId sentinel = VALUE_NONE;
	if (left.sentinel != VALUE_NONE || right.sentinel != VALUE_NONE) {
		ValueId first = left.sentinel != VALUE_NONE ? left.sentinel : right.sentinel;
		Operand value = coerce(block, comptime_operand(value_get(&sema->values, first)->type, first), child, inst->src);
		if (value.kind == OPERAND_NONE)
			return operand_none;
		sentinel = value.value;
		if (left.sentinel != VALUE_NONE && right.sentinel != VALUE_NONE) {
			Operand other = coerce(block, comptime_operand(value_get(&sema->values, right.sentinel)->type, right.sentinel), child, inst->src);
			if (other.kind == OPERAND_NONE)
				return operand_none;
			if (other.value != sentinel)
				sentinel = VALUE_NONE;
		}
	}
	if (right.length > UINT64_MAX - left.length) {
		sema_error(block, inst->src, "concatenating arrays of length %" PRIu64 " and %" PRIu64 " produces an array too large for this compiler implementation to handle", left.length, right.length);
		return operand_none;
	}
	uint64_t count = left.length + right.length;
	if (count > UINT32_MAX - (sentinel != VALUE_NONE ? 1u : 0u)) {
		sema_error(block, inst->src, "not supported by szc yet: array constant with this many elements");
		return operand_none;
	}
	uint32_t total = (uint32_t)count + (sentinel != VALUE_NONE ? 1u : 0u);
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
	if (sentinel != VALUE_NONE)
		items[count] = sentinel;
	TypeId type = array_type_id(sema, child, count, sentinel);
	bool reference = sema->types[lhs.type].kind == TYPE_POINTER || sema->types[rhs.type].kind == TYPE_POINTER;
	if (!reference)
		return comptime_operand(type, value_array(&sema->values, type, items, total));
	TypeId pointer = type_pointer(sema, type, POINTER_ONE, true, false, 0);
	/* Byte arrays use the existing compact backing, including panic messages. */
	if (child == type_id_of_well_known(IR_REF_TYPE_U8) && (sentinel == VALUE_NONE || sentinel_is_zero(sema, sentinel))) {
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

static Operand
typeof_conflict(Block *block, const Operand *operands, uint32_t a, uint32_t b, uint32_t src)
{
	uint32_t first = a < b ? a : b;
	uint32_t second = a < b ? b : a;
	sema_error(block, src, "incompatible types: '%s' and '%s'",
		sema_type_name(block->sema, operands[first].type),
		sema_type_name(block->sema, operands[second].type));
	return operand_none;
}

/* Integer peer selection is n-ary and intentionally value-dependent in Zig.
 * In particular, a known value (including typed undefined) enables its legacy
 * signed/unsigned tie rule; folding pairs would lose both that fact and order. */
static Operand
typeof_peer(Block *block, const IrRef *args, uint32_t count, uint32_t src)
{
	Sema *sema = block->sema;
	Operand *operands = ARENA_ARRAY(sema->arena, Operand, count);
	uint32_t signed_index = UINT32_MAX;
	uint32_t unsigned_index = UINT32_MAX;
	uint32_t float_index = UINT32_MAX;
	uint32_t undefined_int = UINT32_MAX;
	bool numeric = true;
	bool integers = true;
	bool known = false;
	for (uint32_t i = 0; i < count; i++) {
		Operand operand = resolve_ref(block, args[i]);
		if (operand.kind == OPERAND_NONE)
			return operand_none;
		operands[i] = operand;
		Type type = sema->types[operand.type];
		if (type.kind == TYPE_UNDEFINED || type.kind == TYPE_NORETURN)
			continue;
		if (type.kind == TYPE_COMPTIME_INT) {
			if (operand.kind != OPERAND_COMPTIME || operand_is_undefined(block, operand))
				undefined_int = i;
			known = true;
			continue;
		}
		if (type.kind == TYPE_FLOAT) {
			integers = false;
			if (float_index == UINT32_MAX || type.as.floating.bits > sema->types[operands[float_index].type].as.floating.bits)
				float_index = i;
			continue;
		}
		if (type.kind == TYPE_COMPTIME_FLOAT) {
			integers = false;
			continue;
		}
		if (type.kind != TYPE_INT) {
			integers = false;
			numeric = false;
			continue;
		}
		known |= operand.kind == OPERAND_COMPTIME;
		uint32_t *index = type.as.integer.is_signed ? &signed_index : &unsigned_index;
		if (*index == UINT32_MAX || type.as.integer.bits > sema->types[operands[*index].type].as.integer.bits)
			*index = i;
	}
	if (numeric && float_index != UINT32_MAX) {
		TypeId type = operands[float_index].type;
		uint32_t bits = sema->types[type].as.floating.bits;
		uint32_t precision = bits == 16 ? 11 : bits == 32 ? 24 : bits == 64 ? 53 : bits == 80 ? 64 : 113;
		for (uint32_t i = 0; i < count; i++) {
			Type peer = sema->types[operands[i].type];
			if (peer.kind == TYPE_INT && operands[i].kind != OPERAND_COMPTIME
				&& peer.as.integer.bits > precision + (uint32_t)peer.as.integer.is_signed)
				return typeof_conflict(block, operands, float_index, i, src);
		}
		return type_operand(sema, type);
	}
	if (integers && (signed_index != UINT32_MAX || unsigned_index != UINT32_MAX)) {
		if (undefined_int != UINT32_MAX) {
			uint32_t index = signed_index == UINT32_MAX ? unsigned_index
				: unsigned_index == UINT32_MAX ? signed_index
				: signed_index < unsigned_index ? signed_index : unsigned_index;
			return typeof_conflict(block, operands, index, undefined_int, src);
		}
		uint32_t index;
		if (signed_index == UINT32_MAX)
			index = unsigned_index;
		else if (unsigned_index == UINT32_MAX)
			index = signed_index;
		else {
			uint32_t signed_bits = sema->types[operands[signed_index].type].as.integer.bits;
			uint32_t unsigned_bits = sema->types[operands[unsigned_index].type].as.integer.bits;
			if (signed_bits > unsigned_bits)
				index = signed_index;
			else if (known)
				index = unsigned_bits > signed_bits ? unsigned_index
					: signed_index < unsigned_index ? signed_index : unsigned_index;
			else
				return typeof_conflict(block, operands, unsigned_index, signed_index, src);
		}
		return type_operand(sema, operands[index].type);
	}
	Operand peer = operands[0];
	for (uint32_t i = 1; i < count; i++) {
		if ((sema->types[peer.type].kind == TYPE_NULL && sema->types[operands[i].type].kind != TYPE_NULL && operands[i].type != T_UNDEFINED)
			|| (sema->types[operands[i].type].kind == TYPE_NULL && sema->types[peer.type].kind != TYPE_NULL && peer.type != T_UNDEFINED)) {
			sema_error(block, src, "not supported by szc yet: optional peer type resolution");
			return operand_none;
		}
		TypeId type;
		if (!peer_type(block, peer, operands[i], src, &type))
			return operand_none;
		peer.type = type;
	}
	return type_operand(sema, peer.type);
}

/* Analyze on the existing builder so captured runtime refs keep their types.
 * The private instruction body never reaches the parent body, and both builder
 * lengths roll back afterwards (including slots scanned by the backend).
 * Declaration/function reachability is deliberately not rolled back: Zig still
 * validates the bodies/initializers referenced only inside @TypeOf. */
static Operand
analyze_typeof(Block *block, const IrInst *inst)
{
	Sema *sema = block->sema;
	IrTypeOf header;
	IR_READ(block->ir, inst->data.payload, &header);
	InstMap map;
	inst_map_init(&map, sema->arena);
	map.parent = block->map;
	Block query;
	block_init_child(&query, block);
	query.map = &map;
	query.is_comptime = false;
	query.is_typeof = true;
	query.comptime_value_required = false;
	/* Break/continue validation must not append exits to enclosing targets. */
	ControlTarget **link = &query.control;
	for (ControlTarget *target = block->control; target != NULL; target = target->parent) {
		ControlTarget *copy = ARENA_NEW(sema->arena, ControlTarget);
		*copy = *target;
		copy->exits = NULL;
		copy->count = 0;
		copy->capacity = 0;
		*link = copy;
		link = &copy->parent;
	}
	*link = NULL;
	TirFunc temporary = {0};
	if (query.tir == NULL)
		query.tir = &temporary;
	uint32_t inst_count = query.tir->inst_count;
	uint32_t extra_count = query.tir->extra_count;
	BodyResult body = analyze_body(&query, header.body);
	Operand result = operand_none;
	if (body.end == BODY_NORETURN)
		sema_error(block, inst->src, "unreachable code");
	else if (body.end == BODY_BREAK_RESULT) {
		const IrRef *args = block->ir->extra + inst->data.payload + IR_WORDS(IrTypeOf);
		result = typeof_peer(&query, args, header.arg_count, inst->src);
	}
	query.tir->inst_count = inst_count;
	query.tir->extra_count = extra_count;
	return result;
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
	case IR_ARRAY_TYPE_SENTINEL:
		result = analyze_array_type(block, inst);
		break;
	case IR_SLICE_ELEM_TYPE: {
		Operand base = resolve_ref(block, inst->data.un.operand);
		if (!pointer_type_operand(block, base, src))
			break;
		TypeId type = sema->types[base.type].as.pointer.child;
		if (sema->types[type].kind == TYPE_POINTER) {
			TypePointer pointer = sema->types[type].as.pointer;
			type = pointer.child;
			if (pointer.size != POINTER_ONE) {
				result = type_operand(sema, type);
				break;
			}
		}
		if (sema->types[type].kind == TYPE_ARRAY)
			type = sema->types[type].as.array.child;
		result = type_operand(sema, type);
		break;
	}
	case IR_ARRAY_ELEM_TYPE: {
		TypeId type;
		if (!operand_to_type(block, resolve_ref(block, inst->data.un.operand), src, &type))
			break;
		if (sema->types[type].kind == TYPE_STRUCT)
			sema_error(block, src, "not supported by szc yet: positional struct initialization");
		else if (sema->types[type].kind != TYPE_ARRAY)
			sema_error(block, src, "type '%s' does not support array initialization syntax", sema_type_name(sema, type));
		else
			result = type_operand(sema, sema->types[type].as.array.child);
		break;
	}
	case IR_ARRAY_INIT:
		result = analyze_array_init(block, inst);
		break;
	case IR_STRUCT_INIT: {
		bool noreturn = false;
		result = analyze_struct_init(block, inst, &noreturn);
		if (noreturn)
			return INST_NORETURN;
		break;
	}
	case IR_STRUCT_INIT_PTR: {
		IrStructInitPtr header;
		IR_READ(block->ir, inst->data.payload, &header);
		const IrStructInitField *fields = (const IrStructInitField *)(block->ir->extra
			+ inst->data.payload + IR_WORDS(IrStructInitPtr));
		bool noreturn = false;
		result = struct_init_destination(block, resolve_ref(block, header.pointer), fields,
			header.field_count, src, &noreturn);
		if (noreturn)
			return INST_NORETURN;
		break;
	}
	case IR_STRUCT_FIELD_TYPE: {
		TypeId type;
		if (!operand_to_type(block, resolve_ref(block, inst->data.name.operand), src, &type)
			|| !ensure_type_layout(block, type, src))
			break;
		if (sema->types[type].kind != TYPE_STRUCT) {
			sema_error(block, src, "expected struct type, found '%s'", sema_type_name(sema, type));
			break;
		}
		SemaField *field = require_struct_field(block, sema->types[type].as.structure, inst->data.name.name, src);
		if (field != NULL)
			result = type_operand(sema, field->type);
		break;
	}
	case IR_INIT_EMPTY_PTR: {
		Operand pointer = resolve_ref(block, inst->data.un.operand);
		if (pointer.kind == OPERAND_NONE)
			break;
		if (sema->types[pointer.type].kind == TYPE_POINTER
			&& sema->types[sema->types[pointer.type].as.pointer.child].kind == TYPE_STRUCT) {
			bool noreturn = false;
			result = struct_init_destination(block, pointer, NULL, 0, src, &noreturn);
		} else {
			pointer = array_init_destination(block, pointer, 0, src);
			if (pointer.kind != OPERAND_NONE && finish_array_init(block, pointer, src).kind != OPERAND_NONE)
				result = load_pointer(block, pointer, src);
		}
		break;
	}

	case IR_ARRAY_BASE_PTR: {
		Operand pointer = resolve_ref(block, inst->data.un.operand);
		if (!pointer_type_operand(block, pointer, src))
			break;
		TypeId child = sema->types[pointer.type].as.pointer.child;
		if (sema->types[child].kind == TYPE_POINTER)
			pointer = load_pointer(block, pointer, src);
		if (pointer.kind != OPERAND_NONE && (indexable_array_type(sema, pointer.type) != 0
			|| (sema->types[pointer.type].kind == TYPE_POINTER && (sema->types[pointer.type].as.pointer.size == POINTER_MANY
				|| sema->types[pointer.type].as.pointer.size == POINTER_SLICE))))
			result = pointer;
		else if (pointer.kind != OPERAND_NONE)
			sema_error(block, src, "expected array or pointer to array, found '%s'", sema_type_name(sema, child));
		break;
	}
	case IR_ARRAY_INIT_PTR: {
		Operand pointer = resolve_ref(block, inst->data.bin.lhs);
		Operand count = resolve_ref(block, inst->data.bin.rhs);
		if (count.kind != OPERAND_NONE) {
			uint64_t length = bigint_to_u64_bits(&operand_value(block, count)->as.integer);
			result = array_init_destination(block, pointer, length, src);
		}
		break;
	}
	case IR_ARRAY_INIT_SENTINEL:
		result = finish_array_init(block, resolve_ref(block, inst->data.un.operand), src);
		break;
	case IR_SLICE:
		result = analyze_slice(block, inst);
		break;
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
	case IR_FIELD_NAMED_PTR:
	case IR_FIELD_NAMED_READ_REF:
	case IR_FIELD_NAMED_VAL: {
		IrNamedField payload;
		IR_READ(block->ir, inst->data.payload, &payload);
		Operand base = resolve_ref(block, payload.base);
		StringId name;
		if (base.kind == OPERAND_NONE || !resolve_name_body(block, payload.name_body, payload.name_src, &name))
			break;
		if (inst->op != IR_FIELD_NAMED_PTR && struct_value_field(block, base, name, src, true, &result)) {
			if (inst->op == IR_FIELD_NAMED_READ_REF)
				result = analyze_ref(block, result, src, 0, 0);
			break;
		}
		Operand address = analyze_field_pointer(block, base, name, src);
		result = address.kind == OPERAND_NONE || inst->op != IR_FIELD_NAMED_VAL
			? address : load_pointer(block, address, src);
		break;
	}
	case IR_FIELD_READ_REF: {
		Operand base = resolve_ref(block, inst->data.name.operand);
		if (base.kind == OPERAND_NONE)
			break;
		if (struct_value_field(block, base, inst->data.name.name, src, true, &result))
			result = analyze_ref(block, result, src, 0, 0);
		else
			result = analyze_field_pointer(block, base, inst->data.name.name, src);
		break;
	}
	case IR_FIELD_PTR:
		result = analyze_field_pointer(block, resolve_ref(block, inst->data.name.operand), inst->data.name.name, src);
		break;
	case IR_FIELD_REF:
	case IR_FIELD_VAL: {
		Operand base = resolve_ref(block, inst->data.name.operand);
		if (base.kind == OPERAND_NONE)
			break;
		if (namespace_member(block, base, inst->data.name.name, false, src, &result))
			break;
		if (struct_value_field(block, base, inst->data.name.name, src, inst->op == IR_FIELD_REF, &result))
			break;
		TypeId object = inst->op == IR_FIELD_REF && sema->types[base.type].kind == TYPE_POINTER
			? sema->types[base.type].as.pointer.child : base.type;
		if (sema->types[object].kind == TYPE_POINTER && sema->types[object].as.pointer.size == POINTER_ONE)
			object = sema->types[object].as.pointer.child;
		if (sema->types[object].kind == TYPE_STRUCT) {
			Operand storage = inst->op == IR_FIELD_REF ? base : analyze_ref(block, base, src, 0, 0);
			Operand address = storage.kind == OPERAND_NONE ? operand_none
				: analyze_field_pointer(block, storage, inst->data.name.name, src);
			result = address.kind == OPERAND_NONE ? operand_none : load_pointer(block, address, src);
			break;
		}
		TypeId base_type = base.type;
		if (inst->op == IR_FIELD_REF) {
			if (!pointer_type_operand(block, base, src))
				break;
			base_type = sema->types[base.type].as.pointer.child;
		}
		bool slice_pointer = sema->types[base_type].kind == TYPE_POINTER
			&& sema->types[base_type].as.pointer.size == POINTER_ONE
			&& is_slice(sema, sema->types[base_type].as.pointer.child);
		if (inst->op == IR_FIELD_REF && (is_slice(sema, base_type) || slice_pointer)) {
			Operand address = analyze_field_pointer(block, base, inst->data.name.name, src);
			result = address.kind == OPERAND_NONE ? operand_none : load_pointer(block, address, src);
			break;
		}
		if (slice_pointer) {
			bool length = inst->data.name.name == intern_cstr(sema->interner, "len");
			if (length || inst->data.name.name == intern_cstr(sema->interner, "ptr")) {
				Operand address = slice_field_address(block, base, length, src);
				result = address.kind == OPERAND_NONE ? operand_none : load_pointer(block, address, src);
			} else
				result = unsupported_inst(block, inst);
			break;
		}
		if (is_slice(sema, base.type)) {
			bool length = inst->data.name.name == intern_cstr(sema->interner, "len");
			if (length || inst->data.name.name == intern_cstr(sema->interner, "ptr"))
				result = slice_field(block, base, length, src);
			else
				result = unsupported_inst(block, inst);
			break;
		}
		TypeId type = indexable_array_type(sema, base_type);
		if (type != 0 && inst->data.name.name == intern_cstr(sema->interner, "len"))
			result = comptime_operand(T_USIZE, value_int_u64(&sema->values, T_USIZE, sema->types[type].as.array.length));
		else
			result = unsupported_inst(block, inst);
		break;
	}
	case IR_ENUM_DECL:
	case IR_STRUCT_DECL: {
		IrContainerHeader header;
		IR_READ(block->ir, inst->data.payload, &header);
		if (inst->op == IR_STRUCT_DECL && ((header.flags & IR_CONTAINER_KIND_MASK) != AST_CONTAINER_STRUCT || header.arg_body != 0)) {
			result = unsupported_inst(block, inst);
			break;
		}
		SemaNamespace *ns = namespace_create(sema, block->namespace_scope, block->ir, index, block->decl_name, block->map);
		result = type_operand(sema, ns->type);
		break;
	}
	case IR_FUNC:
	case IR_FN_TYPE:
		result = analyze_func_inst(block, index);
		break;
	case IR_IN_COMPTIME:
		result = bool_operand(sema, block->is_comptime);
		break;
	case IR_TYPEOF_BUILTIN:
		result = analyze_typeof(block, inst);
		break;
	case IR_HAS_DECL:
	case IR_HAS_FIELD:
	case IR_FIELD_TYPE:
	case IR_OFFSET_OF:
	case IR_BIT_OFFSET_OF:
		result = analyze_has_member(block, inst);
		break;
	case IR_THIS:
		result = type_operand(sema, block->namespace_scope->type);
		break;
	case IR_FIELD_CAPTURE: {
		Operand captured = resolve_ref(block, inst->data.un.operand);
		if (captured.kind == OPERAND_COMPTIME)
			result = captured;
		else
			sema_error(block, src, "not supported by szc yet: field capture requires a comptime-known immutable local");
		break;
	}
	case IR_ENUM_TAG_TYPE:
		if (block->enum_tag_type != 0)
			result = type_operand(sema, block->enum_tag_type);
		break;
	case IR_ENUM_LITERAL:
		result = comptime_operand(T_ENUM_LITERAL, value_enum_literal(&sema->values, T_ENUM_LITERAL, inst->data.name.name));
		break;
	case IR_DECL_LITERAL: {
		TypeId type;
		if (!operand_to_type(block, resolve_ref(block, inst->data.name.operand), src, &type))
			break;
		if (sema->types[type].kind == TYPE_ENUM_LITERAL) {
			result = comptime_operand(type, value_enum_literal(&sema->values, type, inst->data.name.name));
			break;
		}
		if (sema->types[type].kind != TYPE_ENUM) {
			sema_error(block, src, "not supported by szc yet: declaration literal for '%s'", sema_type_name(sema, type));
			break;
		}
		Operand member;
		if (namespace_member(block, type_operand(sema, type), inst->data.name.name, false, src, &member))
			result = coerce(block, member, type, src);
		break;
	}
	case IR_INT_FROM_ENUM:
		result = analyze_int_from_enum(block, inst);
		break;
	case IR_ENUM_FROM_INT:
		result = analyze_enum_from_int(block, inst);
		break;
	case IR_FIELD_DEFAULT_TYPE:
		if (block->field_default_type != 0)
			result = type_operand(sema, block->field_default_type);
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
		result = read_declaration(block, ns, decl, inst->op == IR_DECL_REF, src);
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
		pointer.as.pointer.size = (payload.flags & IR_POINTER_SLICE) != 0 ? POINTER_SLICE
			: (payload.flags & IR_POINTER_MANY) != 0 ? POINTER_MANY : POINTER_ONE;
		if (payload.sentinel != IR_REF_NONE && !analyze_scalar_sentinel(block,
			resolve_ref(block, payload.sentinel), type, payload.sentinel_src, &pointer.as.pointer.sentinel))
			break;
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
			result = aggregate_snapshot(block, pointer, alignment, src);
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
		if (inst->op == IR_REF_ARRAY_TYPE && (pointer.size == POINTER_MANY || pointer.size == POINTER_SLICE)) {
			Operand length = resolve_ref(block, inst->data.bin.rhs);
			if (length.kind != OPERAND_COMPTIME)
				break;
			child = array_type_id(sema, child, bigint_to_u64_bits(&operand_value(block, length)->as.integer), pointer.sentinel);
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
	case IR_FOR_PTR_CHECK: {
		Operand value = resolve_ref(block, inst->data.un.operand);
		if (value.kind == OPERAND_NONE)
			break;
		if (sema->types[value.type].kind == TYPE_ARRAY)
			sema_error(block, src, "pointer capture of non pointer type '%s'", sema_type_name(sema, value.type));
		else if (sema->types[value.type].kind != TYPE_POINTER)
			sema_error(block, src, "expected pointer for reference capture, found '%s'", sema_type_name(sema, value.type));
		else
			result = void_operand(sema);
		break;
	}
	case IR_FOR_LEN:
		result = analyze_for_len(block, inst);
		break;
	case IR_BIT_CAST:
		result = analyze_bit_cast(block, inst);
		break;
	case IR_INT_CAST:
	case IR_TRUNCATE:
		result = analyze_cast(block, inst);
		break;
	case IR_BIT_SIZE_OF:
		result = analyze_bit_size(block, inst);
		break;
	case IR_SIZE_OF:
	case IR_ALIGN_OF:
		result = analyze_type_layout(block, inst);
		break;
	case IR_CONST_CAST:
	case IR_VOLATILE_CAST: {
		Operand operand = resolve_ref(block, inst->data.un.operand);
		result = operand.kind == OPERAND_NONE ? operand_none : cast_pointer_qualifier(block, operand, inst->op, inst->src);
		break;
	}
	case IR_PTR_CAST:
		result = analyze_ptr_cast(block, inst);
		break;
	case IR_ALIGN_CAST:
		result = analyze_align_cast(block, inst);
		break;
	case IR_PTR_FROM_INT:
		result = analyze_ptr_from_int(block, inst);
		break;
	case IR_INT_FROM_PTR:
		result = analyze_int_from_ptr(block, inst);
		break;
	case IR_MEMSET_ELEM_TYPE: {
		TypeId child = memory_element_type(block, resolve_ref(block, inst->data.un.operand), src);
		if (child != 0)
			result = type_operand(sema, child);
		break;
	}
	case IR_MEMCPY:
		result = analyze_memcpy(block, inst);
		break;
	case IR_MEMSET:
		result = analyze_memset(block, inst);
		break;
	case IR_ABS:
		result = analyze_abs(block, inst);
		break;
	case IR_BIT_REVERSE:
	case IR_BYTE_SWAP:
		result = analyze_integer_transform(block, inst);
		break;
	case IR_CLZ:
	case IR_CTZ:
	case IR_POP_COUNT:
		result = analyze_bit_count(block, inst);
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
	case IR_STORE:
		result = store_value(block, resolve_ref(block, inst->data.bin.lhs),
			resolve_ref(block, inst->data.bin.rhs), src);
		break;

	case IR_FIELD_CALL:
		if (analyze_field_call(block, index, &result) == INST_NORETURN)
			return INST_NORETURN;
		break;
	case IR_CALL:
		if (analyze_call(block, index, &result) == INST_NORETURN)
			return INST_NORETURN;
		break;
	case IR_SWITCH_TYPE:
	case IR_SWITCH_UNDERSCORE:
	case IR_SWITCH_INDEX_VALUE:
	case IR_SWITCH_CAPTURE:
		result = analyze_switch_context(block, inst);
		break;
	case IR_SWITCH:
		result = analyze_switch(block, index);
		if (result.type == T_NORETURN)
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
		if (sema_type_is_aggregate(sema, ret)) {
			TirRef pointer = block->func->return_pointer;
			Operand destination = runtime_operand(block->tir->insts[pointer].type, pointer);
			if (!store_aggregate(block, destination, value, src))
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
		result = unsupported_inst(block, inst);
		break;
	case IR_OP_COUNT:
		break;
	}
	if (inst->op == IR_REF || inst->op == IR_DECL_REF || inst->op == IR_FIELD_PTR
		|| inst->op == IR_FIELD_NAMED_PTR || inst->op == IR_FIELD_READ_REF
		|| inst->op == IR_FIELD_NAMED_READ_REF || inst->op == IR_AS)
		queue_function_address(block, result);
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
		bool array = sema_type_is_aggregate(sema, fn.params[i]);
		/* Transport pointers promise only byte alignment; the private parameter
		 * snapshot restores ABI alignment independently of the caller's storage. */
		TypeId type = array ? type_pointer(sema, fn.params[i], POINTER_ONE, true, false, 1) : fn.params[i];
		TirRef arg = emit_tir(&block, TIR_ARG, type, src);
		func->tir.insts[arg].data.index = i;
		Operand value = runtime_operand(type, arg);
		if (array)
			value = aggregate_snapshot(&block, value, 0, src);
		inst_map_put(&map, sema->arena, params[i], value);
	}
	if (sema_type_is_aggregate(sema, fn.ret)) {
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

/* With deferred ordinary defaults, discovery order crosses analysis units.
 * For this single-file subset, order an all-error semantic suffix by source.
 * Keep notes/multi-file diagnostics untouched until they have explicit groups. */
typedef struct DefaultDiagnostic {
	Diagnostic item;
	uint32_t ordinal;
} DefaultDiagnostic;

static int
compare_default_diagnostic(const void *left, const void *right)
{
	const DefaultDiagnostic *a = left;
	const DefaultDiagnostic *b = right;
	if (a->item.offset != b->item.offset)
		return a->item.offset < b->item.offset ? -1 : 1;
	return a->ordinal < b->ordinal ? -1 : a->ordinal != b->ordinal;
}

static void
sort_default_diagnostics(Sema *sema, uint32_t start)
{
	Diag *diag = sema->diag;
	uint32_t count = diag->count - start;
	if (sema->defaults_queue_count == 0 || count < 2)
		return;
	FileId file = diag->items[start].file;
	for (uint32_t i = start; i < diag->count; i++)
		if (diag->items[i].level != DIAG_ERROR || diag->items[i].file != file)
			return;
	DefaultDiagnostic *items = ARENA_ARRAY(sema->arena, DefaultDiagnostic, count);
	for (uint32_t i = 0; i < count; i++) {
		items[i].item = diag->items[start + i];
		items[i].ordinal = i;
	}
	qsort(items, count, sizeof(*items), compare_default_diagnostic);
	for (uint32_t i = 0; i < count; i++)
		diag->items[start + i] = items[i].item;
}

bool
sema_analyze_executable(Sema *sema, FileId file, const IrFile *ir)
{
	uint32_t errors_before = sema->diag->error_count;
	uint32_t diagnostics_before = sema->diag->count;
	StringId root_name = file_struct_name(sema, file);
	SemaNamespace *root = namespace_create(sema, NULL, ir, ir->root, root_name, NULL);
	InstMap map;
	inst_map_init(&map, sema->arena);
	Block block;
	block_init_comptime(&block, sema, root, &map, root_name);
	uint32_t root_src = ir->insts[ir->root].src;

	/* Root exports are reachable even before main is resolved. */
	resolve_namespace_exports(&block, root);

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
	TypeId namespace_index = 1;
	uint32_t defaults_index = 0;
	while (function_index < sema->queue_count || global_index < sema->global_count
		|| namespace_index < sema->type_count || defaults_index < sema->defaults_queue_count) {
		while (namespace_index < sema->type_count) {
			Type type = sema->types[namespace_index++];
			if (type.kind == TYPE_STRUCT)
				resolve_namespace_exports(&block, type.as.structure);
		}
		while (function_index < sema->queue_count)
			analyze_func_body(sema, sema->queue[function_index++]);
		while (global_index < sema->global_count) {
			Global *global = sema->globals[global_index++];
			if (global->decl->state == DECL_TYPED)
				resolve_decl(&block, global->result.src, global->namespace_scope, global->decl, false);
		}
		while (defaults_index < sema->defaults_queue_count) {
			SemaNamespace *ns = sema->defaults_queue[defaults_index++];
			ensure_struct_defaults(&block, ns, ns->ir->insts[ns->inst].src);
		}
	}
	sort_default_diagnostics(sema, diagnostics_before);
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
	case VALUE_INT_PTR:
		return arena_sprintf(sema->arena, "ptr(0x%" PRIx64 ")", value->as.address);
	case VALUE_REINTERPRET_PTR:
		return arena_sprintf(sema->arena, "ptr_view(value.%u)", (unsigned)value->as.pointer);
	case VALUE_PTR_CAST:
		return arena_sprintf(sema->arena, "ptr_cast(value.%u)", (unsigned)value->as.pointer);
	case VALUE_ELEMENT_PTR:
		return arena_sprintf(sema->arena, "elem(base.%u, index.%" PRIu64 ", %s.%" PRIu64 ")",
			(unsigned)value->as.element.base, value->as.element.index,
			value->as.element.logical ? "leaf" : "offset", value->as.element.offset);
	case VALUE_STRUCT_FIELD_PTR:
		return arena_sprintf(sema->arena, "struct_field(base.%u, index.%u, offset.%" PRIu64 ")",
			(unsigned)value->as.struct_field.base, (unsigned)value->as.struct_field.index, value->as.struct_field.offset);
	case VALUE_SLICE_FIELD_PTR:
		return arena_sprintf(sema->arena, "slice_field(base.%u, %s, offset.%" PRIu64 ")",
			(unsigned)value->as.field.base, value->as.field.length ? "len" : "ptr", value->as.field.offset);
	case VALUE_SLICE:
		return arena_sprintf(sema->arena, "slice(ptr.%u, len.%s)", (unsigned)value->as.slice.pointer, value_text(sema, value->as.slice.length));
	case VALUE_ENUM:
		return arena_sprintf(sema->arena, "enum(%s, %s)", sema_type_name(sema, value->type), value_text(sema, value->as.enum_tag));
	case VALUE_ENUM_LITERAL:
		return arena_sprintf(sema->arena, ".%s", intern_text(sema->interner, value->as.enum_literal));
	case VALUE_STRUCT:
		return arena_sprintf(sema->arena, "struct.%u", (unsigned)id);
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
	case TIR_SLICE_LEN_PTR:
	case TIR_PTR_FROM_INT:
	case TIR_PTR_TO_INT:
	case TIR_NULL_CHECK:
	case TIR_PTR_CAST:
	case TIR_BIT_CAST:
	case TIR_INT_CAST:
	case TIR_TRUNCATE:
	case TIR_ABS:
	case TIR_BIT_REVERSE:
	case TIR_BYTE_SWAP:
	case TIR_CLZ:
	case TIR_CTZ:
	case TIR_POP_COUNT:
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
	case TIR_STRUCT_FIELD_PTR: {
		TirStructFieldPtr field;
		memcpy(&field, &tir->extra[inst->data.payload], sizeof(field));
		fprintf(out, " %%%u, %u\n", field.base, field.offset);
		return;
	}
	case TIR_MEMCPY: {
		TirMemcpy copy;
		memcpy(&copy, &tir->extra[inst->data.payload], sizeof(copy));
		fprintf(out, " %%%u, %%%u, %%%u\n", copy.destination, copy.source, copy.length);
		return;
	}
	case TIR_MEMSET: {
		TirMemset fill;
		memcpy(&fill, &tir->extra[inst->data.payload], sizeof(fill));
		fprintf(out, " %%%u, %%%u, %%%u\n", fill.pointer, fill.length, fill.value);
		return;
	}
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
