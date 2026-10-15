/*
 * irgen: AST -> untyped IR (see docs/IR.md).
 *
 * Like Zig's AstGen. Walks one file's AST, resolves identifiers lexically and
 * emits instructions into bodies. No types are known here: every operation is
 * emitted as written and checked later by sema.
 */
#include "ir.h"

#include <inttypes.h>
#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Generator state ---------- */

typedef struct IrGen {
	Arena      *arena;
	Diag       *diag;
	Interner   *interner;
	FileId     file;
	const char *source;
	IrFile     *ir;
	uint32_t   inst_capacity;
	uint32_t   extra_capacity;
	IrRef      *references;	/* instruction -> anchored immutable reference, or NONE */
} IrGen;

/*
 * Scopes form a chain from the innermost scope outwards. They live on the C
 * stack for body/control boundaries, or in the arena for sequential locals
 * and defer registrations.
 */
typedef enum ScopeKind {
	SCOPE_NAMESPACE,	/* container: its declaration names */
	SCOPE_LOCAL,	/* one local constant, variable or parameter */
	SCOPE_BODY,	/* an instruction body being built */
	SCOPE_CONTROL,	/* block/loop targets and optional label */
	SCOPE_DEFER,	/* registered cleanup, visible after its declaration */
	SCOPE_DEFER_BODY,	/* control transfers cannot cross this boundary */
} ScopeKind;

typedef struct Scope {
	ScopeKind    kind;
	struct Scope *parent;
} Scope;

typedef struct NamespaceScope {
	Scope    base;
	StringId *names;
	uint32_t *name_srcs;
	uint32_t name_count;
} NamespaceScope;

typedef enum LocalKind {
	LOCAL_CONST,	/* ref is the value */
	LOCAL_PARAM,	/* ref is the param instruction */
	LOCAL_VAR,	/* ref is a pointer to the variable */
} LocalKind;

typedef struct LocalScope {
	Scope     base;
	StringId  name;
	LocalKind local_kind;
	IrRef     ref;
	uint32_t  src;
} LocalScope;

typedef struct DeferScope {
	Scope base;
	IrBody body;
	uint32_t src;
} DeferScope;

typedef struct GenBody {
	Scope       base;
	IrGen       *gen;
	IrInstIndex *insts;
	uint32_t    count;
	uint32_t    capacity;
	bool        is_comptime;
	bool        in_function;	/* inside a function body: `return` is allowed */
} GenBody;

/* What the value of an expression is used for. */
typedef enum ResultKind {
	RESULT_NONE,	/* any type */
	RESULT_TYPE,	/* coerce to the type `ref` */
	RESULT_PTR_ELEM,	/* coerce to the pointee type of pointer `ref` (assignment) */
} ResultKind;

typedef struct ResultInfo {
	ResultKind kind;
	IrRef      ref;
} ResultInfo;

typedef struct ControlScope {
	Scope base;
	StringId label;
	IrInstIndex break_target;
	IrInstIndex continue_target;
	ResultInfo result;
	bool allow_unlabeled;
	bool allow_continue;
	bool label_used;
} ControlScope;

static const ResultInfo result_none = {RESULT_NONE, IR_REF_NONE};

static ResultInfo
result_type(IrRef type)
{
	ResultInfo info;
	info.kind = RESULT_TYPE;
	info.ref = type;
	return info;
}

/* ---------- Diagnostics ---------- */

static void
error_at(IrGen *gen, uint32_t offset, const char *format, const char *argument)
{
	diag_report(gen->diag, DIAG_ERROR, gen->file, offset, format, argument);
}

static StringId
intern_span(IrGen *gen, Span span)
{
	return intern(gen->interner, gen->source + span.start, span.end - span.start);
}

/* ---------- Emitting ---------- */

static IrInstIndex
new_inst(IrGen *gen, IrOp op, uint32_t src, IrData data)
{
	IrFile *ir = gen->ir;
	if (ir->inst_count == gen->inst_capacity) {
		uint32_t new_capacity = gen->inst_capacity * 2;
		ir->insts = arena_grow_array(gen->arena, ir->insts, ir->inst_count, new_capacity, sizeof(IrInst), alignof(IrInst));
		gen->references = arena_grow_array(gen->arena, gen->references, ir->inst_count, new_capacity, sizeof(IrRef), alignof(IrRef));
		memset(gen->references + ir->inst_count, 0, (new_capacity - ir->inst_count) * sizeof(IrRef));
		gen->inst_capacity = new_capacity;
	}
	IrInstIndex index = ir->inst_count;
	ir->insts[index].op = op;
	ir->insts[index].src = src;
	ir->insts[index].data = data;
	ir->inst_count = ir->inst_count + 1;
	return index;
}

static uint32_t
reserve_extra(IrGen *gen, uint32_t words)
{
	IrFile *ir = gen->ir;
	while (ir->extra_count + words > gen->extra_capacity) {
		uint32_t new_capacity = gen->extra_capacity * 2;
		ir->extra = arena_grow_array(gen->arena, ir->extra, ir->extra_count, new_capacity, sizeof(uint32_t), alignof(uint32_t));
		gen->extra_capacity = new_capacity;
	}
	uint32_t index = ir->extra_count;
	memset(&ir->extra[index], 0, words * sizeof(uint32_t));
	ir->extra_count = ir->extra_count + words;
	return index;
}

static void
write_extra(IrGen *gen, uint32_t index, const void *data, size_t size)
{
	memcpy(&gen->ir->extra[index], data, size);
}

/* Appends a payload struct to extra and returns its index. */
#define APPEND_EXTRA(gen, value) append_extra_words((gen), &(value), sizeof(value))

static uint32_t
append_extra_words(IrGen *gen, const void *data, size_t size)
{
	uint32_t index = reserve_extra(gen, (uint32_t)(size / sizeof(uint32_t)));
	write_extra(gen, index, data, size);
	return index;
}

static void
body_push(GenBody *body, IrInstIndex index)
{
	if (body->count == body->capacity) {
		uint32_t new_capacity = body->capacity == 0 ? 8 : body->capacity * 2;
		body->insts = arena_grow_array(body->gen->arena, body->insts, body->count, new_capacity, sizeof(IrInstIndex), alignof(IrInstIndex));
		body->capacity = new_capacity;
	}
	body->insts[body->count] = index;
	body->count = body->count + 1;
}

/* Emits an instruction at the end of `body` and returns its ref. */
static IrRef
emit(GenBody *body, IrOp op, uint32_t src, IrData data)
{
	IrInstIndex index = new_inst(body->gen, op, src, data);
	body_push(body, index);
	return ir_ref_of_inst(index);
}

static IrRef
emit_un(GenBody *body, IrOp op, uint32_t src, IrRef operand)
{
	IrData data;
	memset(&data, 0, sizeof(data));
	data.un.operand = operand;
	return emit(body, op, src, data);
}

static IrRef
emit_bin(GenBody *body, IrOp op, uint32_t src, IrRef lhs, IrRef rhs)
{
	IrData data;
	memset(&data, 0, sizeof(data));
	data.bin.lhs = lhs;
	data.bin.rhs = rhs;
	return emit(body, op, src, data);
}

static IrRef
emit_name(GenBody *body, IrOp op, uint32_t src, StringId name, IrRef operand)
{
	IrData data;
	memset(&data, 0, sizeof(data));
	data.name.name = name;
	data.name.operand = operand;
	return emit(body, op, src, data);
}

static IrRef
emit_none(GenBody *body, IrOp op, uint32_t src)
{
	IrData data;
	memset(&data, 0, sizeof(data));
	return emit(body, op, src, data);
}

static IrRef
emit_payload(GenBody *body, IrOp op, uint32_t src, uint32_t payload)
{
	IrData data;
	memset(&data, 0, sizeof(data));
	data.payload = payload;
	return emit(body, op, src, data);
}

static IrRef
emit_break(GenBody *body, IrOp op, uint32_t src, IrInstIndex block, IrRef operand)
{
	IrData data;
	memset(&data, 0, sizeof(data));
	data.brk.block = block;
	data.brk.operand = operand;
	return emit(body, op, src, data);
}

static void
body_init(GenBody *body, IrGen *gen, Scope *parent, bool is_comptime)
{
	memset(body, 0, sizeof(*body));
	body->base.kind = SCOPE_BODY;
	body->base.parent = parent;
	body->gen = gen;
	body->is_comptime = is_comptime;
	if (parent != NULL) {
		for (Scope *scope = parent; scope != NULL; scope = scope->parent) {
			if (scope->kind == SCOPE_BODY) {
				body->in_function = ((GenBody *)scope)->in_function;
				break;
			}
			if (scope->kind == SCOPE_NAMESPACE)
				break;
		}
	}
}

/* References are anchored after their defining value, never in a use-site branch. */
static uint32_t
reference_count(const IrGen *gen, IrInstIndex index)
{
	uint32_t count = 0;
	while (gen->references[index] != IR_REF_NONE) {
		index = ir_ref_inst(gen->references[index]);
		count++;
	}
	return count;
}

static uint32_t
append_references(IrGen *gen, uint32_t cursor, IrInstIndex index)
{
	while (gen->references[index] != IR_REF_NONE) {
		index = ir_ref_inst(gen->references[index]);
		gen->ir->extra[cursor++] = index;
	}
	return cursor;
}

/* Extra definitions (function parameters) have references but no body instruction. */
static IrBody
body_finish_extra(GenBody *body, const IrInstIndex *definitions, uint32_t definition_count)
{
	IrGen *gen = body->gen;
	uint32_t count = body->count;
	for (uint32_t i = 0; i < definition_count; i++)
		count += reference_count(gen, definitions[i]);
	for (uint32_t i = 0; i < body->count; i++)
		count += reference_count(gen, body->insts[i]);
	uint32_t index = reserve_extra(gen, 1 + count);
	gen->ir->extra[index] = count;
	uint32_t cursor = index + 1;
	for (uint32_t i = 0; i < definition_count; i++)
		cursor = append_references(gen, cursor, definitions[i]);
	for (uint32_t i = 0; i < body->count; i++) {
		gen->ir->extra[cursor++] = body->insts[i];
		cursor = append_references(gen, cursor, body->insts[i]);
	}
	return index;
}

static IrBody
body_finish(GenBody *body)
{
	return body_finish_extra(body, NULL, 0);
}

static IrRef
reference_value(GenBody *body, IrRef operand, uint32_t src)
{
	IrGen *gen = body->gen;
	if (!ir_ref_is_inst(operand))
		return emit_un(body, IR_REF, src, operand);
	IrInstIndex index = ir_ref_inst(operand);
	if (gen->references[index] == IR_REF_NONE) {
		IrData data;
		memset(&data, 0, sizeof(data));
		data.un.operand = operand;
		IrInstIndex reference = new_inst(gen, IR_REF, src, data);
		gen->references[index] = ir_ref_of_inst(reference);
	}
	return gen->references[index];
}

/* True if the body's last instruction is a syntactic terminator. */
static bool
body_is_terminated(const GenBody *body)
{
	if (body->count == 0)
		return false;
	switch (body->gen->ir->insts[body->insts[body->count - 1]].op) {
	case IR_BREAK:
	case IR_BREAK_INLINE:
	case IR_CONDBR:
	case IR_REPEAT:
	case IR_RET:
	case IR_RET_IMPLICIT:
	case IR_UNREACHABLE:
	case IR_PANIC:
		return true;
	default:
		return false;
	}
}

/* ---------- Name resolution ---------- */

typedef struct Primitive {
	const char *spelling;
	IrRef      ref;
} Primitive;

static const Primitive primitives[] = {
#define X(name, spelling) {spelling, IR_REF_##name},
	IR_WELL_KNOWN_LIST(X)
#undef X
};

/* `i7`, `u64`, ...: an integer type name. Sets bits and signedness. */
static bool
parse_int_type_name(const char *text, uint32_t length, bool *is_signed, uint32_t *bits)
{
	if (length < 2 || (text[0] != 'i' && text[0] != 'u'))
		return false;
	if (text[1] == '0' && length > 2)
		return false;
	uint32_t value = 0;
	for (uint32_t i = 1; i < length; i++) {
		if (text[i] < '0' || text[i] > '9')
			return false;
		value = value * 10 + (uint32_t)(text[i] - '0');
		if (value > 65535)
			return false;
	}
	*is_signed = text[0] == 'i';
	*bits = value;
	return true;
}

/* Returns true and sets *ref if `text` names a primitive value or type. */
static bool
lookup_primitive(GenBody *body, uint32_t src, const char *text, uint32_t length, IrRef *ref)
{
	for (size_t i = 0; i < sizeof(primitives) / sizeof(primitives[0]); i++) {
		const char *spelling = primitives[i].spelling;
		if (spelling != NULL && strlen(spelling) == length && memcmp(spelling, text, length) == 0) {
			*ref = primitives[i].ref;
			return true;
		}
	}
	bool is_signed;
	uint32_t bits;
	if (parse_int_type_name(text, length, &is_signed, &bits)) {
		IrData data;
		memset(&data, 0, sizeof(data));
		data.integer = (is_signed ? 0x10000u : 0u) | bits;
		*ref = emit(body, IR_INT_TYPE, src, data);
		return true;
	}
	return false;
}

static bool
namespace_has(const NamespaceScope *namespace_scope, StringId name)
{
	for (uint32_t i = 0; i < namespace_scope->name_count; i++) {
		if (namespace_scope->names[i] == name)
			return true;
	}
	return false;
}

typedef enum LookupKind {
	LOOKUP_NOT_FOUND,
	LOOKUP_LOCAL,
	LOOKUP_DECL,
} LookupKind;

typedef struct Lookup {
	LookupKind kind;
	LocalScope *local;
} Lookup;

static Lookup
lookup_name(Scope *scope, StringId name)
{
	Lookup result;
	result.kind = LOOKUP_NOT_FOUND;
	result.local = NULL;
	for (; scope != NULL; scope = scope->parent) {
		if (scope->kind == SCOPE_LOCAL) {
			LocalScope *local = (LocalScope *)scope;
			if (local->name == name) {
				result.kind = LOOKUP_LOCAL;
				result.local = local;
				return result;
			}
		} else if (scope->kind == SCOPE_NAMESPACE) {
			if (namespace_has((NamespaceScope *)scope, name)) {
				result.kind = LOOKUP_DECL;
				return result;
			}
			/* TODO(M7.3): locals of an enclosing function are captured by nested containers. */
		}
	}
	return result;
}

/* Reports `name` if it shadows a local or declaration in scope. */
static void
check_shadowing(IrGen *gen, Scope *scope, StringId name, uint32_t src)
{
	Lookup found = lookup_name(scope, name);
	if (found.kind == LOOKUP_LOCAL)
		error_at(gen, src, "local '%s' shadows an earlier local", intern_text(gen->interner, name));
	else if (found.kind == LOOKUP_DECL)
		error_at(gen, src, "local '%s' shadows a declaration", intern_text(gen->interner, name));
}

static void
local_init(LocalScope *local, Scope *parent, StringId name, LocalKind kind, IrRef ref, uint32_t src)
{
	local->base.kind = SCOPE_LOCAL;
	local->base.parent = parent;
	local->name = name;
	local->local_kind = kind;
	local->ref = ref;
	local->src = src;
}

/* ---------- Expressions ---------- */

static IrRef expr(GenBody *body, Scope *scope, ResultInfo info, AstNode *node);
static IrRef type_expr(GenBody *body, Scope *scope, AstNode *node);
static IrRef address_of(GenBody *body, Scope *scope, ResultInfo info, AstNode *node);
static IrRef emit_usize(GenBody *body, uint32_t src, uint32_t value);
static IrRef index_base_expr(GenBody *body, Scope *scope, AstNode *node);
static IrRef function_proto(GenBody *body, Scope *scope, AstFnProto *proto, StringId name, uint32_t flags, StringId extern_lib, AstNode *definition, IrOp op);
static void  block_statements(GenBody *body, Scope *scope, AstBlock *block);
static IrRef container_decl(GenBody *body, Scope *scope, AstContainerDecl *decl, StringId name);

/* True if `value` already has type `type`, so coercing it again is pointless. */
static bool
is_coerced_to(const GenBody *body, IrRef value, IrRef type)
{
	if (type == IR_REF_TYPE_TYPE && value >= IR_REF_TYPE_U1 && value < IR_REF_FIRST_INST)
		return true;
	if (type == IR_REF_TYPE_BOOL && (value == IR_REF_TRUE || value == IR_REF_FALSE))
		return true;
	if (!ir_ref_is_inst(value))
		return false;
	const IrInst *inst = &body->gen->ir->insts[ir_ref_inst(value)];
	if (inst->op == IR_AS && inst->data.bin.lhs == type)
		return true;
	if (type == IR_REF_TYPE_TYPE && inst->op == IR_INT_TYPE)
		return true;
	if (type == IR_REF_TYPE_BOOL && (inst->op == IR_BOOL_NOT || inst->op == IR_BOOL_AND || inst->op == IR_BOOL_OR))
		return true;
	if (type == IR_REF_TYPE_BOOL && inst->op >= IR_CMP_EQ && inst->op <= IR_CMP_GTE)
		return true;
	if ((inst->op == IR_INT_CAST || inst->op == IR_TRUNCATE) && inst->data.bin.lhs == type)
		return true;
	return false;
}

/* Applies the result info to a computed value (like AstGen's rvalue). */
static IrRef
rvalue(GenBody *body, ResultInfo info, IrRef value, uint32_t src)
{
	if (body_is_terminated(body))
		return value;
	switch (info.kind) {
	case RESULT_NONE:
		return value;
	case RESULT_TYPE:
		if (is_coerced_to(body, value, info.ref))
			return value;
		return emit_bin(body, IR_AS, src, info.ref, value);
	case RESULT_PTR_ELEM: {
		IrRef type = emit_un(body, IR_ELEM_TYPE, src, info.ref);
		return emit_bin(body, IR_AS, src, type, value);
	}
	}
	return value;
}

static void
unsupported(GenBody *body, AstNode *node, const char *what)
{
	error_at(body->gen, node->offset, "not supported by szc yet: %s", what);
}

/* Decodes an integer literal. Reports malformed literals. */
static IrRef
number_literal(GenBody *body, AstNode *node)
{
	IrGen *gen = body->gen;
	AstLiteral *literal = AST_CAST(AstLiteral, node, AST_NUMBER_LITERAL);
	const char *text = gen->source + literal->text.start;
	uint32_t length = literal->text.end - literal->text.start;
	uint32_t base = 10;
	uint32_t i = 0;

	if (length >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'o' || text[1] == 'b')) {
		base = text[1] == 'x' ? 16 : text[1] == 'o' ? 8 : 2;
		i = 2;
	}
	for (uint32_t j = i; j < length; j++) {
		char c = text[j];
		if (c == '.' || ((c == 'e' || c == 'E') && base == 10) || ((c == 'p' || c == 'P') && base == 16)) {
			unsupported(body, node, "float literals");
			return IR_REF_VOID_VALUE;
		}
	}

	/* Accumulate into 32-bit limbs, least significant first. */
	uint32_t limbs[64];
	uint32_t limb_count = 1;
	limbs[0] = 0;
	bool previous_underscore = true;	/* no leading '_' */
	bool any_digit = false;
	for (; i < length; i++) {
		char c = text[i];
		if (c == '_') {
			if (previous_underscore) {
				error_at(gen, node->offset, "%s", "invalid underscore in number literal");
				return IR_REF_VOID_VALUE;
			}
			previous_underscore = true;
			continue;
		}
		uint32_t digit;
		if (c >= '0' && c <= '9')
			digit = (uint32_t)(c - '0');
		else if (c >= 'a' && c <= 'f')
			digit = (uint32_t)(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F')
			digit = (uint32_t)(c - 'A' + 10);
		else
			digit = 99;
		if (digit >= base) {
			error_at(gen, node->offset, "%s", "invalid digit in number literal");
			return IR_REF_VOID_VALUE;
		}
		previous_underscore = false;
		any_digit = true;
		uint64_t carry = digit;
		for (uint32_t k = 0; k < limb_count; k++) {
			uint64_t product = (uint64_t)limbs[k] * base + carry;
			limbs[k] = (uint32_t)product;
			carry = product >> 32;
		}
		if (carry != 0) {
			if (limb_count == 64) {
				error_at(gen, node->offset, "%s", "number literal is too large");
				return IR_REF_VOID_VALUE;
			}
			limbs[limb_count] = (uint32_t)carry;
			limb_count = limb_count + 1;
		}
	}
	if (!any_digit || previous_underscore) {
		error_at(gen, node->offset, "%s", "invalid number literal");
		return IR_REF_VOID_VALUE;
	}
	if (base == 10 && length > 1 && text[0] == '0') {
		error_at(gen, node->offset, "%s", "number literal has leading zeroes");
		return IR_REF_VOID_VALUE;
	}

	if (limb_count <= 2) {
		IrData data;
		memset(&data, 0, sizeof(data));
		data.integer = limbs[0] | (limb_count == 2 ? (uint64_t)limbs[1] << 32 : 0);
		return emit(body, IR_INT, node->offset, data);
	}
	uint32_t payload = reserve_extra(gen, 1 + limb_count);
	gen->ir->extra[payload] = limb_count;
	memcpy(&gen->ir->extra[payload + 1], limbs, limb_count * sizeof(uint32_t));
	return emit_payload(body, IR_INT_BIG, node->offset, payload);
}

static uint32_t
hex_digit(uint8_t c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return UINT32_MAX;
}

static IrRef
literal_error(GenBody *body, uint32_t start, uint32_t index, const char *message, uint8_t c)
{
	const char *text = arena_sprintf(body->gen->arena, message, (unsigned)c);
	error_at(body->gen, start + index, "%s", text);
	return IR_REF_VOID_VALUE;
}

/* Same escapes and UTF-8 encoding as 0.17 std.zig.string_literal.parseWrite. */
static IrRef
string_literal(GenBody *body, AstNode *node)
{
	IrGen *gen = body->gen;
	AstLiteral *literal = AST_CAST(AstLiteral, node, AST_STRING_LITERAL);
	Span span = literal->text;
	const uint8_t *text = (const uint8_t *)gen->source + span.start;
	uint32_t length = span.end - span.start;
	uint8_t *bytes = ARENA_ARRAY(gen->arena, uint8_t, length);
	uint32_t count = 0;
	for (uint32_t i = 1; i < length - 1;) {
		uint8_t c = text[i++];
		if (c != '\\') {
			bytes[count++] = c;
			continue;
		}
		uint32_t escape = i;
		c = text[i++];
		switch (c) {
		case 'n': bytes[count++] = '\n'; break;
		case 'r': bytes[count++] = '\r'; break;
		case 't': bytes[count++] = '\t'; break;
		case '\\':
		case '\'':
		case '"': bytes[count++] = c; break;
		case 'x': {
			uint32_t value = 0;
			for (uint32_t j = 0; j < 2; j++, i++) {
				uint32_t digit = i < length ? hex_digit(text[i]) : UINT32_MAX;
				if (digit == UINT32_MAX)
					return literal_error(body, span.start, i, "expected hex digit, found '%c'", i < length ? text[i] : 0);
				value = value * 16 + digit;
			}
			bytes[count++] = (uint8_t)value;
			break;
		}
		case 'u': {
			if (i >= length || text[i] != '{')
				return literal_error(body, span.start, i, "expected '{', found '%c'", i < length ? text[i] : 0);
			i++;
			if (i < length && text[i] == '}')
				return literal_error(body, span.start, i, "empty unicode escape sequence", 0);
			uint32_t value = 0;
			while (i < length && text[i] != '}') {
				uint32_t digit = hex_digit(text[i]);
				if (digit == UINT32_MAX)
					return literal_error(body, span.start, i, "expected hex digit or '}', found '%c'", text[i]);
				value = value * 16 + digit;
				if (value > 0x10ffff)
					return literal_error(body, span.start, i, "unicode escape does not correspond to a valid unicode scalar value", 0);
				i++;
			}
			if (i >= length)
				return literal_error(body, span.start, i, "expected '}', found '%c'", 0);
			i++;
			if (value >= 0xd800 && value <= 0xdfff)
				return literal_error(body, span.start, escape + 1, "unicode escape does not correspond to a valid unicode scalar value", 0);
			if (value <= 0x7f) {
				bytes[count++] = (uint8_t)value;
			} else if (value <= 0x7ff) {
				bytes[count++] = (uint8_t)(0xc0 | value >> 6);
				bytes[count++] = (uint8_t)(0x80 | (value & 0x3f));
			} else if (value <= 0xffff) {
				bytes[count++] = (uint8_t)(0xe0 | value >> 12);
				bytes[count++] = (uint8_t)(0x80 | (value >> 6 & 0x3f));
				bytes[count++] = (uint8_t)(0x80 | (value & 0x3f));
			} else {
				bytes[count++] = (uint8_t)(0xf0 | value >> 18);
				bytes[count++] = (uint8_t)(0x80 | (value >> 12 & 0x3f));
				bytes[count++] = (uint8_t)(0x80 | (value >> 6 & 0x3f));
				bytes[count++] = (uint8_t)(0x80 | (value & 0x3f));
			}
			break;
		}
		default:
			return literal_error(body, span.start, escape, "invalid escape character: '%c'", c);
		}
	}
	return emit_name(body, IR_STR, node->offset, intern(gen->interner, (const char *)bytes, count), IR_REF_NONE);
}

/* Multiline lines are raw bytes, joined by newlines without a trailing newline. */
static IrRef
multiline_literal(GenBody *body, AstNode *node)
{
	AstMultilineString *literal = AST_CAST(AstMultilineString, node, AST_MULTILINE_STRING_LITERAL);
	uint32_t capacity = node->end - node->offset;
	char *bytes = ARENA_ARRAY(body->gen->arena, char, capacity);
	uint32_t count = 0;
	for (uint32_t i = 0; i < literal->line_count; i++) {
		Span line = literal->lines[i];
		if (i != 0)
			bytes[count++] = '\n';
		uint32_t length = line.end - line.start - 2;
		memcpy(bytes + count, body->gen->source + line.start + 2, length);
		count += length;
	}
	return emit_name(body, IR_STR, node->offset, intern(body->gen->interner, bytes, count), IR_REF_NONE);
}

static IrRef
identifier(GenBody *body, Scope *scope, ResultInfo info, AstNode *node)
{
	IrGen *gen = body->gen;
	AstIdentifier *identifier_node = AST_CAST(AstIdentifier, node, AST_IDENTIFIER);
	const char *text = gen->source + identifier_node->name.start;
	uint32_t length = identifier_node->name.end - identifier_node->name.start;

	if (length == 1 && text[0] == '_') {
		error_at(gen, node->offset, "%s", "'_' used as an identifier without @\"_\" syntax");
		return IR_REF_VOID_VALUE;
	}
	StringId name = intern_span(gen, identifier_node->name);
	Lookup found = lookup_name(scope, name);
	if (found.kind == LOOKUP_LOCAL) {
		LocalScope *local = found.local;
		if (local->local_kind == LOCAL_VAR)
			return rvalue(body, info, emit_un(body, IR_LOAD, node->offset, local->ref), node->offset);
		return rvalue(body, info, local->ref, node->offset);
	}
	if (found.kind == LOOKUP_DECL)
		return rvalue(body, info, emit_name(body, IR_DECL_VAL, node->offset, name, IR_REF_NONE), node->offset);

	IrRef ref;
	if (lookup_primitive(body, node->offset, text, length, &ref))
		return rvalue(body, info, ref, node->offset);
	error_at(gen, node->offset, "use of undeclared identifier '%s'", intern_text(gen->interner, name));
	return IR_REF_VOID_VALUE;
}

/* Generates the address of an assignable expression. */
static IrRef
lvalue(GenBody *body, Scope *scope, AstNode *node)
{
	IrGen *gen = body->gen;
	if (node->kind == AST_GROUPED)
		return lvalue(body, scope, AST_CAST(AstWrap, node, AST_GROUPED)->operand);
	if (node->kind == AST_IDENTIFIER) {
		AstIdentifier *identifier_node = AST_CAST(AstIdentifier, node, AST_IDENTIFIER);
		StringId name = intern_span(gen, identifier_node->name);
		Lookup found = lookup_name(scope, name);
		if (found.kind == LOOKUP_LOCAL) {
			if (found.local->local_kind == LOCAL_VAR)
				return found.local->ref;
			error_at(gen, node->offset, "cannot assign to constant '%s'", intern_text(gen->interner, name));
			return IR_REF_VOID_VALUE;
		}
		if (found.kind == LOOKUP_DECL)
			return emit_name(body, IR_DECL_REF, node->offset, name, IR_REF_NONE);
		error_at(gen, node->offset, "use of undeclared identifier '%s'", intern_text(gen->interner, name));
		return IR_REF_VOID_VALUE;
	}
	if (node->kind == AST_INDEX)
		return address_of(body, scope, result_none, node);
	if (node->kind == AST_DEREF)
		return emit_un(body, IR_REF_DEREF, node->offset, expr(body, scope, result_none, AST_CAST(AstWrap, node, AST_DEREF)->operand));
	unsupported(body, node, "this kind of assignment target");
	return IR_REF_VOID_VALUE;
}

/* Named bindings keep their type; only temporary values use the result context. */
static IrRef
address_of(GenBody *body, Scope *scope, ResultInfo info, AstNode *node)
{
	if (node->kind == AST_GROUPED)
		return address_of(body, scope, info, AST_CAST(AstWrap, node, AST_GROUPED)->operand);
	if (node->kind == AST_DEREF)
		return lvalue(body, scope, node);
	if (node->kind == AST_INDEX) {
		AstIndex *index = AST_CAST(AstIndex, node, AST_INDEX);
		IrRef base = emit_un(body, IR_ARRAY_BASE_PTR, node->offset, address_of(body, scope, result_none, index->lhs));
		IrRef item = expr(body, scope, result_type(IR_REF_TYPE_USIZE), index->index);
		return emit_bin(body, IR_ARRAY_INDEX_PTR, node->offset, base, item);
	}
	if (node->kind == AST_IDENTIFIER) {
		StringId name = intern_span(body->gen, AST_CAST(AstIdentifier, node, AST_IDENTIFIER)->name);
		Lookup found = lookup_name(scope, name);
		if (found.kind == LOOKUP_LOCAL)
			return found.local->local_kind == LOCAL_VAR ? found.local->ref : reference_value(body, found.local->ref, node->offset);
		if (found.kind == LOOKUP_DECL)
			return emit_name(body, IR_DECL_REF, node->offset, name, IR_REF_NONE);
	}
	ResultInfo operand_info = result_none;
	if (info.kind != RESULT_NONE && !(node->kind == AST_INIT_LIST
		&& AST_CAST(AstInitList, node, AST_INIT_LIST)->type != NULL)) {
		IrRef type = info.kind == RESULT_TYPE ? info.ref : emit_un(body, IR_ELEM_TYPE, node->offset, info.ref);
		if (node->kind == AST_INIT_LIST) {
			uint32_t count = AST_CAST(AstInitList, node, AST_INIT_LIST)->elements.count;
			IrRef length = emit_usize(body, node->offset, count);
			operand_info = result_type(emit_bin(body, IR_REF_ARRAY_TYPE, node->offset, type, length));
		} else {
			operand_info = result_type(emit_un(body, IR_REF_TYPE, node->offset, type));
		}
	}
	IrRef operand = expr(body, scope, operand_info, node);
	if (body_is_terminated(body))
		return IR_REF_VOID_VALUE;
	return reference_value(body, operand, node->offset);
}

static IrBody
alignment_body(IrGen *gen, Scope *scope, AstNode *node)
{
	GenBody body;
	body_init(&body, gen, scope, true);
	IrRef value = expr(&body, &body.base, result_type(IR_REF_TYPE_U29), node);
	if (!body_is_terminated(&body))
		emit_break(&body, IR_BREAK_INLINE, node->offset, IR_BODY_RESULT, value);
	return body_finish(&body);
}

static IrRef
pointer_type(GenBody *body, Scope *scope, AstPointerType *node)
{
	if ((node->size != AST_POINTER_ONE && node->size != AST_POINTER_MANY && node->size != AST_POINTER_SLICE) || node->sentinel != NULL || node->bit_offset != NULL || node->host_size != NULL || node->addrspace != NULL) {
		unsupported(body, &node->base, "pointer sizes other than single pointers or pointer attributes");
		return IR_REF_VOID_VALUE;
	}
	IrRef child = type_expr(body, scope, node->child);
	IrPointerType payload = {child, node->size == AST_POINTER_MANY ? IR_POINTER_MANY : node->size == AST_POINTER_SLICE ? IR_POINTER_SLICE : 0, 0, 0};
	if (node->align != NULL) {
		payload.alignment_body = alignment_body(body->gen, scope, node->align);
		payload.alignment_src = node->align->offset;
	}
	if (node->is_const)
		payload.flags |= IR_POINTER_CONST;
	if (node->is_allowzero)
		payload.flags |= IR_POINTER_ALLOWZERO;
	if (node->is_volatile)
		payload.flags |= IR_POINTER_VOLATILE;
	return emit_payload(body, IR_PTR_TYPE, node->child->offset, APPEND_EXTRA(body->gen, payload));
}

static IrOp
binary_op(AstBinaryOp op)
{
	switch (op) {
	case AST_OP_CONCAT:
		return IR_ARRAY_CAT;
	case AST_OP_ADD:
		return IR_ADD;
	case AST_OP_SUB:
		return IR_SUB;
	case AST_OP_MUL:
		return IR_MUL;
	case AST_OP_DIV:
		return IR_DIV;
	case AST_OP_MOD:
		return IR_MOD;
	case AST_OP_ADD_WRAP:
		return IR_ADD_WRAP;
	case AST_OP_SUB_WRAP:
		return IR_SUB_WRAP;
	case AST_OP_MUL_WRAP:
		return IR_MUL_WRAP;
	case AST_OP_ADD_SAT:
		return IR_ADD_SAT;
	case AST_OP_SUB_SAT:
		return IR_SUB_SAT;
	case AST_OP_MUL_SAT:
		return IR_MUL_SAT;
	case AST_OP_SHL:
		return IR_SHL;
	case AST_OP_SHL_SAT:
		return IR_SHL_SAT;
	case AST_OP_SHR:
		return IR_SHR;
	case AST_OP_BIT_AND:
		return IR_BIT_AND;
	case AST_OP_BIT_OR:
		return IR_BIT_OR;
	case AST_OP_BIT_XOR:
		return IR_BIT_XOR;
	case AST_OP_EQUAL:
		return IR_CMP_EQ;
	case AST_OP_NOT_EQUAL:
		return IR_CMP_NEQ;
	case AST_OP_LESS:
		return IR_CMP_LT;
	case AST_OP_GREATER:
		return IR_CMP_GT;
	case AST_OP_LESS_EQUAL:
		return IR_CMP_LTE;
	case AST_OP_GREATER_EQUAL:
		return IR_CMP_GTE;
	default:
		return IR_OP_COUNT;
	}
}

static IrRef
binary(GenBody *body, Scope *scope, ResultInfo info, AstNode *node)
{
	AstBinary *binary_node = AST_CAST(AstBinary, node, AST_BINARY);
	if (binary_node->op == AST_OP_BOOL_AND || binary_node->op == AST_OP_BOOL_OR) {
		IrRef lhs = expr(body, scope, result_type(IR_REF_TYPE_BOOL), binary_node->lhs);
		GenBody rhs_body;
		body_init(&rhs_body, body->gen, scope, body->is_comptime);
		IrRef rhs = expr(&rhs_body, &rhs_body.base, result_type(IR_REF_TYPE_BOOL), binary_node->rhs);
		emit_break(&rhs_body, IR_BREAK_INLINE, node->offset, IR_BODY_RESULT, rhs);
		IrBoolBr payload;
		payload.lhs = lhs;
		payload.rhs_body = body_finish(&rhs_body);
		IrOp op = binary_node->op == AST_OP_BOOL_AND ? IR_BOOL_AND : IR_BOOL_OR;
		return rvalue(body, info, emit_payload(body, op, node->offset, APPEND_EXTRA(body->gen, payload)), node->offset);
	}
	IrOp op = binary_op(binary_node->op);
	if (op == IR_OP_COUNT) {
		unsupported(body, node, ast_binary_op_name(binary_node->op));
		return IR_REF_VOID_VALUE;
	}
	IrRef lhs = expr(body, scope, result_none, binary_node->lhs);
	IrRef rhs = expr(body, scope, result_none, binary_node->rhs);
	return rvalue(body, info, emit_bin(body, op, node->offset, lhs, rhs), node->offset);
}

static IrRef
unary(GenBody *body, Scope *scope, ResultInfo info, AstNode *node)
{
	AstUnary *unary_node = AST_CAST(AstUnary, node, AST_UNARY);
	IrOp op;
	switch (unary_node->op) {
	case AST_UNARY_ADDRESS_OF:
		return rvalue(body, info, address_of(body, scope, info, unary_node->operand), node->offset);
	case AST_UNARY_BOOL_NOT:
		op = IR_BOOL_NOT;
		break;
	case AST_UNARY_NEGATE:
		op = IR_NEGATE;
		break;
	case AST_UNARY_BIT_NOT:
		op = IR_BIT_NOT;
		break;
	case AST_UNARY_NEGATE_WRAP:
		op = IR_NEGATE_WRAP;
		break;
	default:
		unsupported(body, node, ast_unary_op_name(unary_node->op));
		return IR_REF_VOID_VALUE;
	}
	ResultInfo operand_info = op == IR_BOOL_NOT ? result_type(IR_REF_TYPE_BOOL) : result_none;
	IrRef operand = expr(body, scope, operand_info, unary_node->operand);
	return rvalue(body, info, emit_un(body, op, node->offset, operand), node->offset);
}

static IrRef
call(GenBody *body, Scope *scope, ResultInfo info, AstNode *node)
{
	IrGen *gen = body->gen;
	AstCall *call_node = AST_CAST(AstCall, node, AST_CALL);
	IrRef callee = expr(body, scope, result_none, call_node->callee);

	/* Reserve the call so argument bodies can refer to it for their result types. */
	IrData data;
	memset(&data, 0, sizeof(data));
	IrInstIndex call_index = new_inst(gen, IR_CALL, node->offset, data);
	IrRef call_ref = ir_ref_of_inst(call_index);

	uint32_t arg_count = call_node->args.count;
	IrBody *arg_bodies = arena_alloc(gen->arena, (arg_count + 1) * sizeof(IrBody), alignof(IrBody));
	for (uint32_t i = 0; i < arg_count; i++) {
		GenBody arg_body;
		body_init(&arg_body, gen, scope, body->is_comptime);
		IrRef arg_type = emit_bin(&arg_body, IR_ARG_TYPE, call_node->args.items[i]->offset, call_ref, (IrRef)i);
		IrRef value = expr(&arg_body, &arg_body.base, result_type(arg_type), call_node->args.items[i]);
		emit_break(&arg_body, IR_BREAK_INLINE, call_node->args.items[i]->offset, IR_BODY_RESULT, value);
		arg_bodies[i] = body_finish(&arg_body);
	}

	IrCallHeader header;
	header.callee = callee;
	header.modifier = body->is_comptime ? IR_CALL_COMPTIME : IR_CALL_AUTO;
	header.arg_count = arg_count;
	uint32_t payload = APPEND_EXTRA(gen, header);
	uint32_t bodies = reserve_extra(gen, arg_count);
	memcpy(&gen->ir->extra[bodies], arg_bodies, arg_count * sizeof(IrBody));
	gen->ir->insts[call_index].data.payload = payload;
	body_push(body, call_index);
	return rvalue(body, info, call_ref, node->offset);
}

/* ---------- Builtin calls ---------- */

typedef enum Builtin {
	BUILTIN_AS,
	BUILTIN_INT_CAST,
	BUILTIN_TRUNCATE,
	BUILTIN_INT_FROM_BOOL,
	BUILTIN_INT_FROM_PTR,
	BUILTIN_LAYOUT,
	BUILTIN_ARITH,
	BUILTIN_PANIC,
} Builtin;

typedef struct BuiltinInfo {
	const char *name;	/* without '@' */
	Builtin		builtin;
	uint32_t	arg_count;
	IrOp			arith_op;
} BuiltinInfo;

static const BuiltinInfo builtin_table[] = {
	{"panic", BUILTIN_PANIC, 1, IR_OP_COUNT},
	{"as", BUILTIN_AS, 2, IR_OP_COUNT},
	{"intCast", BUILTIN_INT_CAST, 1, IR_OP_COUNT},
	{"truncate", BUILTIN_TRUNCATE, 1, IR_OP_COUNT},
	{"intFromBool", BUILTIN_INT_FROM_BOOL, 1, IR_OP_COUNT},
	{"intFromPtr", BUILTIN_INT_FROM_PTR, 1, IR_OP_COUNT},
	{"sizeOf", BUILTIN_LAYOUT, 1, IR_SIZE_OF},
	{"bitSizeOf", BUILTIN_LAYOUT, 1, IR_BIT_SIZE_OF},
	{"alignOf", BUILTIN_LAYOUT, 1, IR_ALIGN_OF},
	{"divTrunc", BUILTIN_ARITH, 2, IR_DIV_TRUNC},
	{"divFloor", BUILTIN_ARITH, 2, IR_DIV_FLOOR},
	{"divCeil", BUILTIN_ARITH, 2, IR_DIV_CEIL},
	{"divExact", BUILTIN_ARITH, 2, IR_DIV_EXACT},
	{"rem", BUILTIN_ARITH, 2, IR_REM},
	{"mod", BUILTIN_ARITH, 2, IR_MOD_BUILTIN},
};

static const BuiltinInfo *
lookup_builtin(const char *name, uint32_t length)
{
	for (size_t i = 0; i < sizeof(builtin_table) / sizeof(builtin_table[0]); i++) {
		if (strlen(builtin_table[i].name) == length && memcmp(builtin_table[i].name, name, length) == 0)
			return &builtin_table[i];
	}
	return NULL;
}

/*
 * The result type a cast builtin converts to, or NONE (after reporting) if the
 * expression has no result type.
 */
static IrRef
cast_result_type(GenBody *body, ResultInfo info, AstNode *node, const char *name)
{
	switch (info.kind) {
	case RESULT_TYPE:
		return info.ref;
	case RESULT_PTR_ELEM:
		return emit_un(body, IR_ELEM_TYPE, node->offset, info.ref);
	case RESULT_NONE:
		break;
	}
	error_at(body->gen, node->offset, "@%s must have a known result type", name);
	return IR_REF_NONE;
}

static IrRef
builtin_call(GenBody *body, Scope *scope, ResultInfo info, AstNode *node)
{
	IrGen *gen = body->gen;
	AstBuiltinCall *call_node = AST_CAST(AstBuiltinCall, node, AST_BUILTIN_CALL);
	const char *name = gen->source + call_node->name.start + 1;
	uint32_t length = call_node->name.end - call_node->name.start - 1;
	const BuiltinInfo *builtin = lookup_builtin(name, length);
	if (builtin == NULL) {
		error_at(gen, node->offset, "not supported by szc yet: builtin %s", arena_strndup(gen->arena, name - 1, length + 1));
		return IR_REF_VOID_VALUE;
	}
	AstNode **args = call_node->args.items;
	if (call_node->args.count != builtin->arg_count) {
		char message[64];
		snprintf(message, sizeof(message), "expected %u argument%s, found %u", (unsigned)builtin->arg_count, builtin->arg_count == 1 ? "" : "s", (unsigned)call_node->args.count);
		error_at(gen, node->offset, "%s", message);
		return IR_REF_VOID_VALUE;
	}
	switch (builtin->builtin) {
	case BUILTIN_PANIC: {
		IrPanic payload;
		payload.message = expr(body, scope, result_none, args[0]);
		payload.argument_src = args[0]->offset;
		return emit_payload(body, IR_PANIC, node->offset, APPEND_EXTRA(gen, payload));
	}
	case BUILTIN_AS: {
		IrRef type = type_expr(body, scope, args[0]);
		IrRef value = expr(body, scope, result_type(type), args[1]);
		return rvalue(body, info, value, node->offset);
	}
	case BUILTIN_INT_CAST:
	case BUILTIN_TRUNCATE: {
		IrRef type = cast_result_type(body, info, node, builtin->name);
		IrRef operand = expr(body, scope, result_none, args[0]);
		if (type == IR_REF_NONE)
			return IR_REF_VOID_VALUE;
		IrOp op = builtin->builtin == BUILTIN_INT_CAST ? IR_INT_CAST : IR_TRUNCATE;
		return emit_bin(body, op, node->offset, type, operand);
	}
	case BUILTIN_ARITH: {
		IrRef lhs = expr(body, scope, result_none, args[0]);
		IrRef rhs = expr(body, scope, result_none, args[1]);
		return rvalue(body, info, emit_bin(body, builtin->arith_op, node->offset, lhs, rhs), node->offset);
	}
	case BUILTIN_LAYOUT: {
		IrRef type = type_expr(body, scope, args[0]);
		return rvalue(body, info, emit_un(body, builtin->arith_op, node->offset, type), node->offset);
	}
	case BUILTIN_INT_FROM_PTR:
	case BUILTIN_INT_FROM_BOOL: {
		IrRef operand = expr(body, scope, result_none, args[0]);
		return rvalue(body, info, emit_un(body, builtin->builtin == BUILTIN_INT_FROM_PTR ? IR_INT_FROM_PTR : IR_INT_FROM_BOOL, node->offset, operand), node->offset);
	}
	}
	return IR_REF_VOID_VALUE;
}

/* Emit only the cleanup registrations crossed by this scope exit, newest first. */
static void
emit_defers(GenBody *body, Scope *scope, const Scope *stop)
{
	for (Scope *current = scope; current != stop && current != NULL && current->kind != SCOPE_NAMESPACE; current = current->parent) {
		if (current->kind == SCOPE_DEFER) {
			DeferScope *defer_scope = (DeferScope *)current;
			emit_payload(body, IR_DEFER, defer_scope->src, defer_scope->body);
		}
	}
}

static IrRef
return_expr(GenBody *body, Scope *scope, AstNode *node)
{
	AstReturn *return_node = AST_CAST(AstReturn, node, AST_RETURN);
	if (!body->in_function) {
		error_at(body->gen, node->offset, "%s", "'return' outside function scope");
		return IR_REF_VOID_VALUE;
	}
	for (Scope *current = scope; current != NULL && current->kind != SCOPE_NAMESPACE; current = current->parent) {
		if (current->kind == SCOPE_DEFER_BODY) {
			error_at(body->gen, node->offset, "%s", "cannot return from defer expression");
			return IR_REF_VOID_VALUE;
		}
	}
	IrRef value = IR_REF_VOID_VALUE;
	if (return_node->value != NULL) {
		IrRef type = emit_none(body, IR_RET_TYPE, node->offset);
		value = expr(body, scope, result_type(type), return_node->value);
		if (body_is_terminated(body)) {
			error_at(body->gen, node->offset, "%s", "unreachable code");
			return IR_REF_VOID_VALUE;
		}
		/* Nested result-location coercions remain ordinary `as` operations. */
		if (ir_ref_is_inst(value)) {
			IrInst *inst = &body->gen->ir->insts[ir_ref_inst(value)];
			if (inst->op == IR_AS && inst->data.bin.lhs == type)
				inst->op = IR_AS_RETURN;
		}
	}
	if (!body_is_terminated(body)) {
		emit_defers(body, scope, NULL);
		emit_un(body, IR_RET, node->offset, value);
	}
	return IR_REF_VOID_VALUE;
}

static StringId
check_label(IrGen *gen, Scope *scope, AstLabel label)
{
	if (!label.has_label)
		return STRING_NONE;
	StringId name = intern_span(gen, label.name);
	for (Scope *current = scope; current != NULL && current->kind != SCOPE_NAMESPACE; current = current->parent) {
		if (current->kind == SCOPE_CONTROL && ((ControlScope *)current)->label == name) {
			error_at(gen, label.name.start, "redefinition of label '%s'", intern_text(gen->interner, name));
			break;
		}
	}
	return name;
}

/* A labeled block yields through named breaks; unlabeled blocks yield void. */
static IrRef
block_expr(GenBody *body, Scope *scope, ResultInfo info, AstNode *node)
{
	AstBlock *block = AST_CAST(AstBlock, node, AST_BLOCK);
	if (!block->label.has_label) {
		block_statements(body, scope, block);
		return rvalue(body, info, IR_REF_VOID_VALUE, node->offset);
	}
	IrGen *gen = body->gen;
	StringId label = check_label(gen, scope, block->label);
	IrRef result = emit_payload(body, IR_BLOCK, node->offset, 0);
	ControlScope control;
	memset(&control, 0, sizeof(control));
	control.base.kind = SCOPE_CONTROL;
	control.base.parent = scope;
	control.label = label;
	control.break_target = ir_ref_inst(result);
	control.result = info;
	GenBody inner;
	body_init(&inner, gen, &control.base, body->is_comptime);
	block_statements(&inner, &inner.base, block);
	if (!body_is_terminated(&inner)) {
		IrRef value = rvalue(&inner, info, IR_REF_VOID_VALUE, node->offset);
		emit_break(&inner, IR_BREAK, node->offset, control.break_target, value);
	}
	gen->ir->insts[control.break_target].data.payload = body_finish(&inner);
	if (!control.label_used)
		error_at(gen, block->label.name.start, "%s", "unused block label");
	return result;
}

static void assign(GenBody *body, Scope *scope, AstAssign *node);

static IrRef
jump_expr(GenBody *body, Scope *scope, AstNode *node)
{
	AstJump *jump = AST_CAST(AstJump, node, node->kind);
	bool is_continue = node->kind == AST_CONTINUE;
	StringId label = jump->label.has_label ? intern_span(body->gen, jump->label.name) : STRING_NONE;
	if (is_continue && label == STRING_NONE && jump->value != NULL) {
		error_at(body->gen, node->offset, "%s", "cannot continue with operand without label");
		return IR_REF_VOID_VALUE;
	}
	for (Scope *current = scope; current != NULL && current->kind != SCOPE_NAMESPACE; current = current->parent) {
		if (current->kind == SCOPE_DEFER_BODY) {
			error_at(body->gen, node->offset, "%s", is_continue ? "cannot continue out of defer expression" : "cannot break out of defer expression");
			return IR_REF_VOID_VALUE;
		}
		if (current->kind != SCOPE_CONTROL)
			continue;
		ControlScope *control = (ControlScope *)current;
		if (label == STRING_NONE ? !control->allow_unlabeled : control->label != label)
			continue;
		if (label != STRING_NONE)
			control->label_used = true;
		if (is_continue && !control->allow_continue) {
			error_at(body->gen, node->offset, "%s", "continue outside of loop or labeled switch expression");
			return IR_REF_VOID_VALUE;
		}
		if (is_continue && jump->value != NULL) {
			error_at(body->gen, node->offset, "%s", label == STRING_NONE ? "cannot continue with operand without label" : "cannot continue loop with operand");
			return IR_REF_VOID_VALUE;
		}
		IrRef value = IR_REF_VOID_VALUE;
		if (!is_continue)
			value = jump->value == NULL ? rvalue(body, control->result, value, node->offset) : expr(body, scope, control->result, jump->value);
		if (!is_continue && jump->value != NULL && body_is_terminated(body)) {
			error_at(body->gen, node->offset, "%s", "unreachable code");
			return IR_REF_VOID_VALUE;
		}
		if (!body_is_terminated(body)) {
			emit_defers(body, scope, current);
			emit_break(body, IR_BREAK, node->offset, is_continue ? control->continue_target : control->break_target, value);
		}
		return IR_REF_VOID_VALUE;
	}
	if (label != STRING_NONE)
		error_at(body->gen, jump->label.name.start, "label not found: '%s'", intern_text(body->gen->interner, label));
	else
		error_at(body->gen, node->offset, "%s", is_continue ? "continue expression outside loop" : "break expression outside loop");
	return IR_REF_VOID_VALUE;
}

static IrRef
while_expr(GenBody *body, Scope *scope, ResultInfo info, AstNode *node)
{
	AstWhile *loop = AST_CAST(AstWhile, node, AST_WHILE);
	IrGen *gen = body->gen;
	if (loop->is_inline || loop->has_capture || loop->has_else_capture) {
		unsupported(body, node, loop->is_inline ? "inline while" : "while payload captures");
		return IR_REF_VOID_VALUE;
	}
	IrRef result = emit_payload(body, IR_BLOCK, node->offset, 0);
	GenBody outer;
	body_init(&outer, gen, scope, body->is_comptime);
	IrRef repeat = emit_payload(&outer, IR_LOOP, node->offset, 0);
	GenBody condition;
	body_init(&condition, gen, &outer.base, body->is_comptime);
	IrCondbr branch;
	branch.condition = expr(&condition, &condition.base, result_type(IR_REF_TYPE_BOOL), loop->condition);
	GenBody then_body;
	body_init(&then_body, gen, &condition.base, body->is_comptime);
	IrRef continuation = emit_payload(&then_body, IR_BLOCK, node->offset, 0);
	ControlScope control;
	memset(&control, 0, sizeof(control));
	control.base.kind = SCOPE_CONTROL;
	control.base.parent = &then_body.base;
	control.label = check_label(gen, scope, loop->label);
	control.break_target = ir_ref_inst(result);
	control.continue_target = ir_ref_inst(continuation);
	control.result = info;
	control.allow_unlabeled = true;
	control.allow_continue = true;
	GenBody iteration;
	body_init(&iteration, gen, &control.base, body->is_comptime);
	expr(&iteration, &iteration.base, result_type(IR_REF_TYPE_VOID), loop->body);
	if (!body_is_terminated(&iteration))
		emit_break(&iteration, IR_BREAK, node->offset, control.continue_target, IR_REF_VOID_VALUE);
	gen->ir->insts[control.continue_target].data.payload = body_finish(&iteration);
	if (loop->continue_expr != NULL)
		expr(&then_body, &control.base, result_type(IR_REF_TYPE_VOID), loop->continue_expr);
	if (!body_is_terminated(&then_body))
		emit_un(&then_body, IR_REPEAT, node->offset, repeat);
	branch.then_body = body_finish(&then_body);
	GenBody else_body;
	control.allow_unlabeled = false;
	control.allow_continue = false;
	body_init(&else_body, gen, &control.base, body->is_comptime);
	IrRef value = loop->else_branch == NULL ? rvalue(&else_body, info, IR_REF_VOID_VALUE, node->offset) : expr(&else_body, &else_body.base, info, loop->else_branch);
	if (!body_is_terminated(&else_body))
		emit_break(&else_body, IR_BREAK, node->offset, control.break_target, value);
	branch.else_body = body_finish(&else_body);
	emit_payload(&condition, IR_CONDBR, node->offset, APPEND_EXTRA(gen, branch));
	gen->ir->insts[ir_ref_inst(repeat)].data.payload = body_finish(&condition);
	gen->ir->insts[control.break_target].data.payload = body_finish(&outer);
	if (control.label != STRING_NONE && !control.label_used)
		error_at(gen, loop->label.name.start, "%s", "unused while loop label");
	return result;
}

static bool
capture_is_discard(const IrGen *gen, AstCapture capture)
{
	return capture.name.end - capture.name.start == 1 && gen->source[capture.name.start] == '_';
}

/* Bounds and sequence values are evaluated once; one index drives all inputs. */
static IrRef
for_expr(GenBody *body, Scope *scope, ResultInfo info, AstNode *node)
{
	AstFor *loop = AST_CAST(AstFor, node, AST_FOR);
	IrGen *gen = body->gen;
	StringId label = check_label(gen, scope, loop->label);
	if (loop->is_inline) {
		unsupported(body, node, "inline for");
		return IR_REF_VOID_VALUE;
	}
	if (loop->captures.count < loop->input_count) {
		error_at(gen, loop->inputs[loop->captures.count].expr->offset, "%s", "for input is not captured");
		return IR_REF_VOID_VALUE;
	}
	if (loop->captures.count > loop->input_count) {
		error_at(gen, loop->captures.items[loop->input_count].name.start, "%s", "extra capture in for loop");
		return IR_REF_VOID_VALUE;
	}
	uint32_t bounded_count = 0;
	for (uint32_t i = 0; i < loop->input_count; i++) {
		AstForInput input = loop->inputs[i];
		AstCapture capture = loop->captures.items[i];
		bool discard = capture_is_discard(gen, capture);
		if (capture.by_pointer && !input.is_range && discard) {
			error_at(gen, capture.name.start, "%s", "pointer modifier invalid on discard");
			return IR_REF_VOID_VALUE;
		}
		if (capture.by_pointer && input.is_range) {
			error_at(gen, capture.name.start, "%s", discard ? "pointer modifier invalid on discard" : "cannot capture reference to range");
			return IR_REF_VOID_VALUE;
		}
		if (input.is_range && input.range_end == NULL && discard)
			error_at(gen, capture.name.start, "%s", "discard of unbounded counter");
		if (!input.is_range || input.range_end != NULL)
			bounded_count++;
	}
	if (bounded_count == 0) {
		error_at(gen, node->offset, "%s", "unbounded for loop");
		return IR_REF_VOID_VALUE;
	}
	IrAlloc alloc = {IR_REF_TYPE_USIZE, IR_REF_ZERO_USIZE, 0, IR_REF_NONE};
	IrRef index_ptr = emit_payload(body, IR_ALLOC, node->offset, APPEND_EXTRA(gen, alloc));
	IrRef *starts = ARENA_ARRAY(gen->arena, IrRef, loop->input_count);
	IrRef *ends = ARENA_ARRAY(gen->arena, IrRef, loop->input_count);
	for (uint32_t i = 0; i < loop->input_count; i++) {
		AstForInput input = loop->inputs[i];
		if (input.is_range) {
			starts[i] = expr(body, scope, result_type(IR_REF_TYPE_USIZE), input.expr);
			ends[i] = input.range_end == NULL ? IR_REF_NONE : expr(body, scope, result_type(IR_REF_TYPE_USIZE), input.range_end);
		} else {
			starts[i] = expr(body, scope, result_none, input.expr);
			if (loop->captures.items[i].by_pointer)
				emit_un(body, IR_FOR_PTR_CHECK, input.expr->offset, starts[i]);
			ends[i] = emit_name(body, IR_FIELD_VAL, input.expr->offset, intern_cstr(gen->interner, "len"), starts[i]);
		}
	}
	IrRef *lengths = ARENA_ARRAY(gen->arena, IrRef, bounded_count);
	uint32_t length_index = 0;
	for (uint32_t i = 0; i < loop->input_count; i++) {
		if (ends[i] != IR_REF_NONE)
			lengths[length_index++] = loop->inputs[i].is_range
				? emit_bin(body, IR_SUB, loop->inputs[i].expr->offset, ends[i], starts[i]) : ends[i];
	}
	uint32_t payload = reserve_extra(gen, 1 + bounded_count);
	gen->ir->extra[payload] = bounded_count;
	memcpy(&gen->ir->extra[payload + 1], lengths, bounded_count * sizeof(IrRef));
	IrRef length = emit_payload(body, IR_FOR_LEN, node->offset, payload);
	IrRef result = emit_payload(body, IR_BLOCK, node->offset, 0);
	GenBody outer;
	body_init(&outer, gen, scope, body->is_comptime);
	IrRef repeat = emit_payload(&outer, IR_LOOP, node->offset, 0);
	GenBody condition;
	body_init(&condition, gen, &outer.base, body->is_comptime);
	IrRef index = emit_un(&condition, IR_LOAD, node->offset, index_ptr);
	IrCondbr branch;
	branch.condition = emit_bin(&condition, IR_CMP_LT, node->offset, index, length);
	GenBody then_body;
	body_init(&then_body, gen, &condition.base, body->is_comptime);
	IrRef continuation = emit_payload(&then_body, IR_BLOCK, node->offset, 0);
	ControlScope control;
	memset(&control, 0, sizeof(control));
	control.base.kind = SCOPE_CONTROL;
	control.base.parent = &then_body.base;
	control.label = label;
	control.break_target = ir_ref_inst(result);
	control.continue_target = ir_ref_inst(continuation);
	control.result = info;
	control.allow_unlabeled = true;
	control.allow_continue = true;
	GenBody iteration;
	body_init(&iteration, gen, &control.base, body->is_comptime);
	Scope *capture_scope = &iteration.base;
	for (uint32_t i = 0; i < loop->input_count; i++) {
		AstCapture capture = loop->captures.items[i];
		if (capture_is_discard(gen, capture))
			continue;
		StringId name = intern_span(gen, capture.name);
		check_shadowing(gen, capture_scope, name, capture.name.start);
		IrRef value = emit_bin(&iteration, loop->inputs[i].is_range ? IR_ADD : capture.by_pointer ? IR_ARRAY_INDEX_PTR : IR_ARRAY_INDEX,
			capture.name.start, starts[i], index);
		LocalScope *local = ARENA_NEW(gen->arena, LocalScope);
		local_init(local, capture_scope, name, LOCAL_CONST, value, capture.name.start);
		capture_scope = &local->base;
	}
	expr(&iteration, capture_scope, result_type(IR_REF_TYPE_VOID), loop->body);
	if (!body_is_terminated(&iteration))
		emit_break(&iteration, IR_BREAK, node->offset, control.continue_target, IR_REF_VOID_VALUE);
	gen->ir->insts[control.continue_target].data.payload = body_finish(&iteration);
	IrRef next = emit_bin(&then_body, IR_ADD, node->offset, index, IR_REF_ONE_USIZE);
	emit_bin(&then_body, IR_STORE, node->offset, index_ptr, next);
	emit_un(&then_body, IR_REPEAT, node->offset, repeat);
	branch.then_body = body_finish(&then_body);
	GenBody else_body;
	control.allow_unlabeled = false;
	control.allow_continue = false;
	body_init(&else_body, gen, &control.base, body->is_comptime);
	IrRef value = loop->else_branch == NULL ? rvalue(&else_body, info, IR_REF_VOID_VALUE, node->offset) : expr(&else_body, &else_body.base, info, loop->else_branch);
	if (!body_is_terminated(&else_body))
		emit_break(&else_body, IR_BREAK, node->offset, control.break_target, value);
	branch.else_body = body_finish(&else_body);
	emit_payload(&condition, IR_CONDBR, node->offset, APPEND_EXTRA(gen, branch));
	gen->ir->insts[ir_ref_inst(repeat)].data.payload = body_finish(&condition);
	gen->ir->insts[control.break_target].data.payload = body_finish(&outer);
	if (control.label != STRING_NONE && !control.label_used)
		error_at(gen, loop->label.name.start, "%s", "unused for loop label");
	return result;
}

/* Each arm yields a value, unless it ends in return/unreachable. */
static IrBody
if_branch(GenBody *body, Scope *scope, ResultInfo info, AstNode *node, uint32_t src)
{
	GenBody branch;
	body_init(&branch, body->gen, scope, body->is_comptime);
	IrRef value = IR_REF_VOID_VALUE;
	if (node != NULL)
		value = expr(&branch, &branch.base, info, node);
	else
		value = rvalue(&branch, info, value, src);
	if (!body_is_terminated(&branch))
		emit_break(&branch, IR_BREAK_INLINE, src, IR_BODY_RESULT, value);
	return body_finish(&branch);
}

static IrRef
if_expr(GenBody *body, Scope *scope, ResultInfo info, AstNode *node)
{
	AstIf *conditional = AST_CAST(AstIf, node, AST_IF);
	if (conditional->has_capture || conditional->has_else_capture) {
		unsupported(body, node, "if payload captures");
		return IR_REF_VOID_VALUE;
	}
	IrCondbr payload;
	payload.condition = expr(body, scope, result_type(IR_REF_TYPE_BOOL), conditional->condition);
	payload.then_body = if_branch(body, scope, info, conditional->then_branch, node->offset);
	payload.else_body = if_branch(body, scope, info, conditional->else_branch, node->offset);
	return emit_payload(body, IR_IF, node->offset, APPEND_EXTRA(body->gen, payload));
}

/* A literal supplies the length for `[_]T`; it is not a standalone type. */
static IrRef
array_type(GenBody *body, Scope *scope, AstArrayType *node, bool infer, uint32_t count)
{
	if (node->sentinel != NULL) {
		unsupported(body, &node->base, "explicit array sentinels");
		return IR_REF_VOID_VALUE;
	}
	bool underscore = node->length->kind == AST_IDENTIFIER
		&& intern_span(body->gen, AST_CAST(AstIdentifier, node->length, AST_IDENTIFIER)->name) == intern_cstr(body->gen->interner, "_");
	IrRef length;
	if (underscore) {
		if (!infer) {
			error_at(body->gen, node->length->offset, "%s", "unable to infer array size");
			return IR_REF_VOID_VALUE;
		}
		IrData data;
		memset(&data, 0, sizeof(data));
		data.integer = count;
		length = rvalue(body, result_type(IR_REF_TYPE_USIZE), emit(body, IR_INT, node->length->offset, data), node->length->offset);
	} else {
		length = expr(body, scope, result_type(IR_REF_TYPE_USIZE), node->length);
	}
	IrRef child = type_expr(body, scope, node->child);
	return emit_bin(body, IR_ARRAY_TYPE, node->child->offset, length, child);
}

/* Small typed indices for compiler-generated literal initialization. */
static IrRef
emit_usize(GenBody *body, uint32_t src, uint32_t value)
{
	IrData data;
	memset(&data, 0, sizeof(data));
	data.integer = value;
	return rvalue(body, result_type(IR_REF_TYPE_USIZE), emit(body, IR_INT, src, data), src);
}

static IrRef
index_base_expr(GenBody *body, Scope *scope, AstNode *node)
{
	if (node->kind == AST_GROUPED)
		return index_base_expr(body, scope, AST_CAST(AstWrap, node, AST_GROUPED)->operand);
	if (node->kind == AST_INDEX) {
		AstIndex *index = AST_CAST(AstIndex, node, AST_INDEX);
		IrRef base = index_base_expr(body, scope, index->lhs);
		IrRef item = expr(body, scope, result_type(IR_REF_TYPE_USIZE), index->index);
		return emit_bin(body, IR_ARRAY_INDEX_BASE, node->offset, base, item);
	}
	bool borrow = node->kind == AST_DEREF;
	if (node->kind == AST_IDENTIFIER) {
		StringId name = intern_span(body->gen, AST_CAST(AstIdentifier, node, AST_IDENTIFIER)->name);
		Lookup found = lookup_name(scope, name);
		borrow = found.kind == LOOKUP_DECL || (found.kind == LOOKUP_LOCAL && found.local->local_kind == LOCAL_VAR);
	}
	if (borrow)
		return emit_un(body, IR_ARRAY_BASE_PTR, node->offset, address_of(body, scope, result_none, node));
	return expr(body, scope, result_none, node);
}

/* Element result types propagate into nested literals and integer casts. */
static IrRef
array_init(GenBody *body, Scope *scope, ResultInfo info, AstInitList *node)
{
	if (node->is_struct_init && node->field_count != 0) {
		unsupported(body, &node->base, "struct field initializers");
		return IR_REF_VOID_VALUE;
	}
	IrRef type;
	if (node->type != NULL) {
		type = node->type->kind == AST_ARRAY_TYPE
			? array_type(body, scope, AST_CAST(AstArrayType, node->type, AST_ARRAY_TYPE), true, node->elements.count)
			: type_expr(body, scope, node->type);
	} else if (info.kind == RESULT_TYPE) {
		type = info.ref;
	} else if (info.kind == RESULT_PTR_ELEM) {
		type = emit_un(body, IR_ELEM_TYPE, node->base.offset, info.ref);
	} else {
		unsupported(body, &node->base, "anonymous tuples without an array result type");
		return IR_REF_VOID_VALUE;
	}
	IrRef child = emit_un(body, IR_ARRAY_ELEM_TYPE, node->base.offset, type);
	uint32_t count = node->elements.count;
	/* Only anonymous assignment literals inherit the destination in Zig 0.17.
	 * Explicit type syntax and @as create independent temporaries. */
	if (node->type == NULL && info.kind == RESULT_PTR_ELEM) {
		IrRef length = emit_usize(body, node->base.offset, count);
		IrRef destination = emit_bin(body, IR_ARRAY_INIT_PTR, node->base.offset, info.ref, length);
		for (uint32_t i = 0; i < count; i++) {
			IrRef index = emit_usize(body, node->base.offset, i);
			IrRef pointer = emit_bin(body, IR_ARRAY_INDEX_PTR, node->base.offset, destination, index);
			ResultInfo element_info = {RESULT_PTR_ELEM, pointer};
			IrRef value = expr(body, scope, element_info, node->elements.items[i]);
			emit_bin(body, IR_STORE, node->base.offset, pointer, value);
		}
		return emit_un(body, IR_LOAD, node->base.offset, destination);
	}
	IrRef *items = ARENA_ARRAY(body->gen->arena, IrRef, count);
	for (uint32_t i = 0; i < count; i++)
		items[i] = expr(body, scope, result_type(child), node->elements.items[i]);
	uint32_t payload = reserve_extra(body->gen, count + 2);
	body->gen->ir->extra[payload] = type;
	body->gen->ir->extra[payload + 1] = count;
	for (uint32_t i = 0; i < count; i++)
		body->gen->ir->extra[payload + 2 + i] = items[i];
	return rvalue(body, info, emit_payload(body, IR_ARRAY_INIT, node->base.offset, payload), node->base.offset);
}

static IrRef
expr(GenBody *body, Scope *scope, ResultInfo info, AstNode *node)
{
	switch (node->kind) {
	case AST_ARRAY_TYPE:
		return rvalue(body, info, array_type(body, scope, AST_CAST(AstArrayType, node, AST_ARRAY_TYPE), false, 0), node->offset);
	case AST_INIT_LIST:
		return array_init(body, scope, info, AST_CAST(AstInitList, node, AST_INIT_LIST));
	case AST_INDEX: {
		AstIndex *index = AST_CAST(AstIndex, node, AST_INDEX);
		IrRef base = index_base_expr(body, scope, index->lhs);
		IrRef item = expr(body, scope, result_type(IR_REF_TYPE_USIZE), index->index);
		return rvalue(body, info, emit_bin(body, IR_ARRAY_INDEX, node->offset, base, item), node->offset);
	}
	case AST_FIELD_ACCESS: {
		AstFieldAccess *field = AST_CAST(AstFieldAccess, node, AST_FIELD_ACCESS);
		StringId name = intern_span(body->gen, field->name);
		AstNode *lhs = field->lhs;
		while (lhs->kind == AST_GROUPED)
			lhs = AST_CAST(AstWrap, lhs, AST_GROUPED)->operand;
		bool borrow = lhs->kind == AST_DEREF || lhs->kind == AST_INDEX;
		if (lhs->kind == AST_IDENTIFIER) {
			Lookup found = lookup_name(scope, intern_span(body->gen, AST_CAST(AstIdentifier, lhs, AST_IDENTIFIER)->name));
			borrow = found.kind == LOOKUP_DECL || (found.kind == LOOKUP_LOCAL && found.local->local_kind == LOCAL_VAR);
		}
		bool field_ref = borrow && name == intern_cstr(body->gen->interner, "len");
		IrRef base = field_ref ? address_of(body, scope, result_none, field->lhs) : expr(body, scope, result_none, field->lhs);
		return rvalue(body, info, emit_name(body, field_ref ? IR_FIELD_REF : IR_FIELD_VAL, node->offset, name, base), node->offset);
	}
	case AST_STRING_LITERAL:
		return rvalue(body, info, string_literal(body, node), node->offset);
	case AST_MULTILINE_STRING_LITERAL:
		return rvalue(body, info, multiline_literal(body, node), node->offset);
	case AST_NUMBER_LITERAL:
		return rvalue(body, info, number_literal(body, node), node->offset);
	case AST_IDENTIFIER:
		return identifier(body, scope, info, node);
	case AST_FN_PROTO:
		return rvalue(body, info, function_proto(body, scope, AST_CAST(AstFnProto, node, AST_FN_PROTO), STRING_NONE, 0, STRING_NONE, NULL, IR_FN_TYPE), node->offset);
	case AST_POINTER_TYPE:
		return rvalue(body, info, pointer_type(body, scope, AST_CAST(AstPointerType, node, AST_POINTER_TYPE)), node->offset);
	case AST_DEREF:
		return rvalue(body, info, emit_un(body, IR_DEREF, node->offset, expr(body, scope, result_none, AST_CAST(AstWrap, node, AST_DEREF)->operand)), node->offset);
	case AST_GROUPED:
		return expr(body, scope, info, AST_CAST(AstWrap, node, AST_GROUPED)->operand);
	case AST_BINARY:
		return binary(body, scope, info, node);
	case AST_UNARY:
		return unary(body, scope, info, node);
	case AST_CALL:
		return call(body, scope, info, node);
	case AST_BUILTIN_CALL:
		return builtin_call(body, scope, info, node);
	case AST_RETURN:
		return return_expr(body, scope, node);
	case AST_BLOCK:
		return block_expr(body, scope, info, node);
	case AST_IF:
		return if_expr(body, scope, info, node);
	case AST_WHILE:
		return while_expr(body, scope, info, node);
	case AST_FOR:
		return for_expr(body, scope, info, node);
	case AST_BREAK:
	case AST_CONTINUE:
		return jump_expr(body, scope, node);
	case AST_ASSIGN:
		assign(body, scope, AST_CAST(AstAssign, node, AST_ASSIGN));
		return rvalue(body, info, IR_REF_VOID_VALUE, node->offset);
	case AST_UNREACHABLE:
		emit_none(body, IR_UNREACHABLE, node->offset);
		return IR_REF_VOID_VALUE;
	case AST_CONTAINER_DECL:
		return rvalue(body, info, container_decl(body, scope, AST_CAST(AstContainerDecl, node, AST_CONTAINER_DECL), STRING_NONE), node->offset);
	default:
		unsupported(body, node, ast_kind_name(node->kind));
		return IR_REF_VOID_VALUE;
	}
}

static IrRef
type_expr(GenBody *body, Scope *scope, AstNode *node)
{
	return expr(body, scope, result_type(IR_REF_TYPE_TYPE), node);
}

/* ---------- Statements ---------- */

/*
 * Local `const`/`var`. Returns the new scope (a LocalScope allocated in the
 * arena so it outlives this call while the enclosing block is generated).
 */
static Scope *
local_var_decl(GenBody *body, Scope *scope, AstVarDecl *decl)
{
	IrGen *gen = body->gen;
	StringId name = intern_span(gen, decl->name);
	check_shadowing(gen, scope, name, decl->base.offset);
	if (decl->addrspace != NULL || decl->linksection != NULL)
		unsupported(body, &decl->base, "align/addrspace/linksection on locals");

	IrRef type = IR_REF_NONE;
	if (decl->type != NULL)
		type = type_expr(body, scope, decl->type);
	IrRef alignment = IR_REF_NONE;
	if (decl->align != NULL)
		alignment = emit_payload(body, IR_ALIGN, decl->align->offset, alignment_body(gen, scope, decl->align));
	ResultInfo info = type == IR_REF_NONE ? result_none : result_type(type);
	IrRef init = expr(body, scope, info, decl->init);

	LocalScope *local = ARENA_NEW(gen->arena, LocalScope);
	if (decl->is_const && !decl->is_comptime) {
		/* A copied binding has its own storage alignment, not its source's. */
		if (alignment != IR_REF_NONE || (ir_ref_is_inst(init) && gen->ir->insts[ir_ref_inst(init)].op == IR_CONST_BIND))
			init = emit_bin(body, IR_CONST_BIND, decl->base.offset, init, alignment);
		local_init(local, scope, name, LOCAL_CONST, init, decl->base.offset);
		return &local->base;
	}
	IrAlloc alloc;
	alloc.type = type;
	alloc.init = init;
	alloc.alignment = alignment;
	alloc.flags = decl->is_comptime ? IR_ALLOC_COMPTIME : 0;
	if (decl->is_const)
		alloc.flags = alloc.flags | IR_ALLOC_CONST;
	IrRef pointer = emit_payload(body, IR_ALLOC, decl->base.offset, APPEND_EXTRA(gen, alloc));
	local_init(local, scope, name, LOCAL_VAR, pointer, decl->base.offset);
	return &local->base;
}

static IrOp
assignment_op(AstAssignOp op)
{
	switch (op) {
	case AST_ASSIGN_ADD:
		return IR_ADD;
	case AST_ASSIGN_SUB:
		return IR_SUB;
	case AST_ASSIGN_MUL:
		return IR_MUL;
	case AST_ASSIGN_DIV:
		return IR_DIV;
	case AST_ASSIGN_MOD:
		return IR_MOD;
	case AST_ASSIGN_ADD_WRAP:
		return IR_ADD_WRAP;
	case AST_ASSIGN_SUB_WRAP:
		return IR_SUB_WRAP;
	case AST_ASSIGN_MUL_WRAP:
		return IR_MUL_WRAP;
	case AST_ASSIGN_ADD_SAT:
		return IR_ADD_SAT;
	case AST_ASSIGN_SUB_SAT:
		return IR_SUB_SAT;
	case AST_ASSIGN_MUL_SAT:
		return IR_MUL_SAT;
	case AST_ASSIGN_SHL:
		return IR_SHL;
	case AST_ASSIGN_SHL_SAT:
		return IR_SHL_SAT;
	case AST_ASSIGN_SHR:
		return IR_SHR;
	case AST_ASSIGN_BIT_AND:
		return IR_BIT_AND;
	case AST_ASSIGN_BIT_OR:
		return IR_BIT_OR;
	case AST_ASSIGN_BIT_XOR:
		return IR_BIT_XOR;
	case AST_ASSIGN_PLAIN:
	case AST_ASSIGN_OP_COUNT:
		return IR_OP_COUNT;
	}
	return IR_OP_COUNT;
}

static void
assign(GenBody *body, Scope *scope, AstAssign *node)
{
	if (node->op == AST_ASSIGN_PLAIN && node->target->kind == AST_IDENTIFIER) {
		AstIdentifier *target = AST_CAST(AstIdentifier, node->target, AST_IDENTIFIER);
		if (target->name.end - target->name.start == 1 && body->gen->source[target->name.start] == '_') {
			/* `_ = x;` discards the value. */
			expr(body, scope, result_none, node->value);
			return;
		}
	}
	IrRef pointer = lvalue(body, scope, node->target);
	ResultInfo info;
	info.kind = RESULT_PTR_ELEM;
	info.ref = pointer;
	IrRef value;
	if (node->op == AST_ASSIGN_PLAIN) {
		value = expr(body, scope, info, node->value);
	} else {
		IrOp op = assignment_op(node->op);
		IrRef lhs = emit_un(body, IR_LOAD, node->base.offset, pointer);
		ResultInfo rhs_info = info;
		if (op == IR_ADD || op == IR_SUB)
			rhs_info = result_type(emit_un(body, IR_ADD_SUB_RHS_TYPE, node->base.offset, lhs));
		else if (op == IR_SHL || op == IR_SHR)
			rhs_info = result_type(emit_un(body, IR_LOG2_INT_TYPE, node->base.offset, lhs));
		else if (op == IR_SHL_SAT)
			rhs_info = result_none;
		IrRef rhs = expr(body, scope, rhs_info, node->value);
		value = rvalue(body, info, emit_bin(body, op, node->base.offset, lhs, rhs), node->base.offset);
	}
	emit_bin(body, IR_STORE, node->base.offset, pointer, value);
}

/* Lower once at registration, with lexical names and a control-transfer barrier. */
static Scope *
defer_stmt(GenBody *body, Scope *scope, AstDefer *node)
{
	if (node->is_errdefer) {
		unsupported(body, &node->base, "errdefer");
		return scope;
	}
	Scope barrier = {SCOPE_DEFER_BODY, scope};
	GenBody cleanup;
	body_init(&cleanup, body->gen, &barrier, body->is_comptime);
	IrRef value = expr(&cleanup, &cleanup.base, result_none, node->body);
	if (!body_is_terminated(&cleanup)) {
		emit_un(&cleanup, IR_ENSURE_RESULT, node->body->offset, value);
		emit_break(&cleanup, IR_BREAK_INLINE, node->base.offset, IR_BODY_RESULT, IR_REF_VOID_VALUE);
	}
	DeferScope *registration = ARENA_NEW(body->gen->arena, DeferScope);
	registration->base.kind = SCOPE_DEFER;
	registration->base.parent = scope;
	registration->body = body_finish(&cleanup);
	registration->src = node->base.offset;
	return &registration->base;
}

static void
block_statements(GenBody *body, Scope *scope, AstBlock *block)
{
	Scope *inner = scope;
	for (uint32_t i = 0; i < block->statements.count; i++) {
		AstNode *statement = block->statements.items[i];
		if (body_is_terminated(body)) {
			error_at(body->gen, statement->offset, "%s", "unreachable code");
			return;
		}
		switch (statement->kind) {
		case AST_VAR_DECL:
			inner = local_var_decl(body, inner, AST_CAST(AstVarDecl, statement, AST_VAR_DECL));
			break;
		case AST_DEFER:
			inner = defer_stmt(body, inner, AST_CAST(AstDefer, statement, AST_DEFER));
			break;
		case AST_ASSIGN:
			assign(body, inner, AST_CAST(AstAssign, statement, AST_ASSIGN));
			break;
		default: {
			IrRef value = expr(body, inner, result_none, statement);
			/* Cleanup blocks obey the same discarded-result rule as the defer operand. */
			for (Scope *current = inner; current != NULL && current->kind != SCOPE_NAMESPACE; current = current->parent) {
				if (current->kind == SCOPE_DEFER_BODY) {
					if (!body_is_terminated(body) && value != IR_REF_VOID_VALUE)
						emit_un(body, IR_ENSURE_RESULT, statement->offset, value);
					break;
				}
			}
			break;
		}
		}
	}
	if (!body_is_terminated(body))
		emit_defers(body, inner, scope);
}

/* ---------- Declarations ---------- */

static IrRef
function_proto(GenBody *body, Scope *scope, AstFnProto *proto, StringId name, uint32_t flags, StringId extern_lib, AstNode *definition, IrOp op)
{
	IrGen *gen = body->gen;
	uint32_t param_count = proto->param_count;
	IrInstIndex *params = arena_alloc(gen->arena, (param_count + 1) * sizeof(IrInstIndex), alignof(IrInstIndex));
	Scope *param_scope = scope;

	for (uint32_t i = 0; i < param_count; i++) {
		AstParam *param = &proto->params[i];
		StringId param_name = param->has_name ? intern_span(gen, param->name) : STRING_NONE;
		uint32_t src = param->has_name ? param->name.start : proto->base.offset;
		IrInstIndex index;
		if (param->is_anytype) {
			IrData data;
			memset(&data, 0, sizeof(data));
			data.name.name = param_name;
			index = new_inst(gen, IR_PARAM_ANYTYPE, src, data);
		} else {
			GenBody type_body;
			body_init(&type_body, gen, param_scope, true);
			IrRef type = type_expr(&type_body, &type_body.base, param->type);
			emit_break(&type_body, IR_BREAK_INLINE, param->type->offset, IR_BODY_RESULT, type);
			IrParam payload;
			payload.name = param_name;
			payload.flags = 0;
			if (param->kind == AST_PARAM_COMPTIME)
				payload.flags = IR_PARAM_COMPTIME;
			else if (param->kind == AST_PARAM_NOALIAS)
				payload.flags = IR_PARAM_NOALIAS;
			payload.type_body = body_finish(&type_body);
			IrData data;
			memset(&data, 0, sizeof(data));
			data.payload = APPEND_EXTRA(gen, payload);
			index = new_inst(gen, IR_PARAM, src, data);
		}
		params[i] = index;
		if (param_name != STRING_NONE) {
			check_shadowing(gen, param_scope, param_name, src);
			LocalScope *local = ARENA_NEW(gen->arena, LocalScope);
			local_init(local, param_scope, param_name, LOCAL_PARAM, ir_ref_of_inst(index), src);
			param_scope = &local->base;
		}
	}

	IrFuncHeader header;
	memset(&header, 0, sizeof(header));
	header.name = name;
	header.param_count = param_count;
	header.flags = flags;
	header.extern_lib = extern_lib;
	if (proto->is_var_args)
		header.flags = header.flags | IR_FUNC_VAR_ARGS;
	if (proto->inferred_error)
		header.flags = header.flags | IR_FUNC_INFERRED_ERROR;
	if (proto->addrspace != NULL || proto->linksection != NULL)
		unsupported(body, &proto->base, "addrspace/linksection on functions");
	if (proto->align != NULL) {
		if (op == IR_FN_TYPE)
			diag_report(gen->diag, DIAG_ERROR, gen->file, proto->align->offset, "function type cannot have an alignment");
		else {
			header.align_body = alignment_body(gen, param_scope, proto->align);
			header.align_src = proto->align->offset;
		}
	}

	GenBody ret_body;
	body_init(&ret_body, gen, param_scope, true);
	IrRef ret_type = type_expr(&ret_body, &ret_body.base, proto->return_type);
	emit_break(&ret_body, IR_BREAK_INLINE, proto->return_type->offset, IR_BODY_RESULT, ret_type);
	header.ret_type_body = body_finish(&ret_body);

	if (proto->callconv != NULL) {
		GenBody cc_body;
		body_init(&cc_body, gen, param_scope, true);
		IrRef cc = expr(&cc_body, &cc_body.base, result_none, proto->callconv);
		emit_break(&cc_body, IR_BREAK_INLINE, proto->callconv->offset, IR_BODY_RESULT, cc);
		header.callconv_body = body_finish(&cc_body);
	}

	if (definition != NULL) {
		GenBody fn_body;
		body_init(&fn_body, gen, param_scope, false);
		fn_body.in_function = true;
		block_statements(&fn_body, &fn_body.base, AST_CAST(AstBlock, definition, AST_BLOCK));
		if (!body_is_terminated(&fn_body))
			emit_none(&fn_body, IR_RET_IMPLICIT, definition->end > 0 ? definition->end - 1 : definition->offset);
		header.body = body_finish_extra(&fn_body, params, param_count);
		header.flags = header.flags | IR_FUNC_HAS_BODY;
	}

	uint32_t payload = APPEND_EXTRA(gen, header);
	uint32_t param_words = reserve_extra(gen, param_count);
	memcpy(&gen->ir->extra[param_words], params, param_count * sizeof(IrInstIndex));
	return emit_payload(body, op, proto->base.offset, payload);
}

static IrRef
fn_decl(GenBody *body, Scope *scope, AstFnDecl *decl, StringId name)
{
	uint32_t flags = 0;
	switch (decl->modifier) {
	case AST_FN_MODIFIER_INLINE: flags = IR_FUNC_INLINE; break;
	case AST_FN_MODIFIER_NOINLINE: flags = IR_FUNC_NOINLINE; break;
	case AST_FN_MODIFIER_EXTERN: flags = IR_FUNC_EXTERN; break;
	case AST_FN_MODIFIER_EXPORT: flags = IR_FUNC_EXPORT; break;
	case AST_FN_MODIFIER_NONE: break;
	}
	StringId lib = STRING_NONE;
	if (decl->has_extern_lib)
		lib = intern(body->gen->interner, body->gen->source + decl->extern_lib.start + 1, decl->extern_lib.end - decl->extern_lib.start - 2);
	return function_proto(body, scope, decl->proto, name, flags, lib, decl->body, IR_FUNC);
}

/* Value body of a container-level `const`/`var`. */
static IrBody
global_var_body(IrGen *gen, Scope *scope, AstVarDecl *decl, IrDecl *out)
{
	GenBody body;
	body_init(&body, gen, scope, true);
	IrRef type = IR_REF_NONE;
	if (decl->type != NULL) {
		GenBody type_body;
		body_init(&type_body, gen, scope, true);
		IrRef declared = type_expr(&type_body, &type_body.base, decl->type);
		emit_break(&type_body, IR_BREAK_INLINE, decl->type->offset, IR_BODY_RESULT, declared);
		out->type_body = body_finish(&type_body);
		type = emit_none(&body, IR_DECL_TYPE, decl->type->offset);
	}
	if (decl->addrspace != NULL || decl->linksection != NULL)
		unsupported(&body, &decl->base, "align/addrspace/linksection on globals");
	if (decl->align != NULL) {
		out->alignment_body = alignment_body(gen, scope, decl->align);
		out->alignment_src = decl->align->offset;
	}
	IrRef value;
	if (decl->init == NULL) {
		if (decl->modifier != AST_VAR_MODIFIER_EXTERN)
			error_at(gen, decl->base.offset, "%s", "variables must be initialized");
		unsupported(&body, &decl->base, "extern variables");
		value = IR_REF_VOID_VALUE;
	} else {
		value = expr(&body, &body.base, type == IR_REF_NONE ? result_none : result_type(type), decl->init);
	}
	emit_break(&body, IR_BREAK_INLINE, decl->base.offset, IR_BODY_RESULT, value);
	return body_finish(&body);
}

static StringId
member_name(IrGen *gen, AstNode *member, uint32_t *src)
{
	*src = member->offset;
	switch (member->kind) {
	case AST_VAR_DECL: {
		AstVarDecl *decl = AST_CAST(AstVarDecl, member, AST_VAR_DECL);
		*src = decl->name.start;
		return intern_span(gen, decl->name);
	}
	case AST_FN_DECL: {
		AstFnProto *proto = AST_CAST(AstFnDecl, member, AST_FN_DECL)->proto;
		if (!proto->has_name)
			return STRING_NONE;
		*src = proto->name.start;
		return intern_span(gen, proto->name);
	}
	default:
		return STRING_NONE;
	}
}

/* Text of a string literal without decoding escapes (test names). */
static StringId
raw_string(IrGen *gen, Span span)
{
	return intern(gen->interner, gen->source + span.start + 1, span.end - span.start - 2);
}

static IrRef
container_decl(GenBody *body, Scope *scope, AstContainerDecl *decl, StringId name)
{
	IrGen *gen = body->gen;
	(void)name;
	if (decl->container_kind != AST_CONTAINER_STRUCT || decl->layout != AST_LAYOUT_AUTO || decl->arg != NULL) {
		unsupported(body, &decl->base, "this kind of container");
		return IR_REF_VOID_VALUE;
	}

	/* Collect the declaration names first: members may refer to later ones. */
	NamespaceScope namespace_scope;
	namespace_scope.base.kind = SCOPE_NAMESPACE;
	namespace_scope.base.parent = scope;
	namespace_scope.names = arena_alloc(gen->arena, (decl->members.count + 1) * sizeof(StringId), alignof(StringId));
	namespace_scope.name_srcs = arena_alloc(gen->arena, (decl->members.count + 1) * sizeof(uint32_t), alignof(uint32_t));
	namespace_scope.name_count = 0;
	uint32_t field_count = 0;
	uint32_t decl_count = 0;
	for (uint32_t i = 0; i < decl->members.count; i++) {
		AstNode *member = decl->members.items[i];
		if (member->kind == AST_CONTAINER_FIELD) {
			field_count = field_count + 1;
			continue;
		}
		decl_count = decl_count + 1;
		uint32_t src;
		StringId member_name_id = member_name(gen, member, &src);
		if (member_name_id == STRING_NONE)
			continue;
		for (uint32_t j = 0; j < namespace_scope.name_count; j++) {
			if (namespace_scope.names[j] == member_name_id) {
				error_at(gen, src, "duplicate declaration '%s'", intern_text(gen->interner, member_name_id));
				break;
			}
		}
		namespace_scope.names[namespace_scope.name_count] = member_name_id;
		namespace_scope.name_srcs[namespace_scope.name_count] = src;
		namespace_scope.name_count = namespace_scope.name_count + 1;
	}

	IrField *fields = arena_zalloc(gen->arena, (field_count + 1) * sizeof(IrField), alignof(IrField));
	IrDecl *decls = arena_zalloc(gen->arena, (decl_count + 1) * sizeof(IrDecl), alignof(IrDecl));
	uint32_t field_index = 0;
	uint32_t decl_index = 0;
	Scope *member_scope = &namespace_scope.base;

	for (uint32_t i = 0; i < decl->members.count; i++) {
		AstNode *member = decl->members.items[i];
		if (member->kind == AST_CONTAINER_FIELD) {
			AstContainerField *field = AST_CAST(AstContainerField, member, AST_CONTAINER_FIELD);
			IrField *out = &fields[field_index];
			field_index = field_index + 1;
			out->name = field->has_name ? intern_span(gen, field->name) : STRING_NONE;
			out->src = member->offset;
			out->flags = IR_FIELD_HAS_TYPE | (field->is_comptime ? IR_FIELD_COMPTIME : 0);
			GenBody type_body;
			body_init(&type_body, gen, member_scope, true);
			IrRef type = type_expr(&type_body, &type_body.base, field->type);
			emit_break(&type_body, IR_BREAK_INLINE, member->offset, IR_BODY_RESULT, type);
			out->type_body = body_finish(&type_body);
			if (field->align != NULL)
				unsupported(body, member, "field alignment");
			if (field->default_value != NULL) {
				GenBody default_body;
				body_init(&default_body, gen, member_scope, true);
				IrRef value = expr(&default_body, &default_body.base, result_none, field->default_value);
				emit_break(&default_body, IR_BREAK_INLINE, member->offset, IR_BODY_RESULT, value);
				out->default_body = body_finish(&default_body);
			}
			continue;
		}

		IrDecl *out = &decls[decl_index];
		decl_index = decl_index + 1;
		uint32_t src;
		out->name = member_name(gen, member, &src);
		out->src = src;
		switch (member->kind) {
		case AST_VAR_DECL: {
			AstVarDecl *var = AST_CAST(AstVarDecl, member, AST_VAR_DECL);
			out->kind = var->is_const ? IR_DECL_CONST : IR_DECL_VAR;
			out->flags = (var->is_pub ? IR_DECL_PUB : 0) | (var->is_threadlocal ? IR_DECL_THREADLOCAL : 0) | (var->modifier == AST_VAR_MODIFIER_EXPORT ? IR_DECL_EXPORT : 0) | (var->modifier == AST_VAR_MODIFIER_EXTERN ? IR_DECL_EXTERN : 0);
			out->value_body = global_var_body(gen, member_scope, var, out);
			break;
		}
		case AST_FN_DECL: {
			AstFnDecl *fn = AST_CAST(AstFnDecl, member, AST_FN_DECL);
			out->kind = IR_DECL_FN;
			out->flags = (fn->is_pub ? IR_DECL_PUB : 0) | (fn->modifier == AST_FN_MODIFIER_EXPORT ? IR_DECL_EXPORT : 0) | (fn->modifier == AST_FN_MODIFIER_EXTERN ? IR_DECL_EXTERN : 0);
			GenBody value_body;
			body_init(&value_body, gen, member_scope, true);
			IrRef value = fn_decl(&value_body, member_scope, fn, out->name);
			emit_break(&value_body, IR_BREAK_INLINE, member->offset, IR_BODY_RESULT, value);
			out->value_body = body_finish(&value_body);
			break;
		}
		case AST_TEST_DECL: {
			AstTestDecl *test = AST_CAST(AstTestDecl, member, AST_TEST_DECL);
			out->kind = IR_DECL_TEST;
			out->name = test->has_name ? (test->name_is_identifier ? intern_span(gen, test->name) : raw_string(gen, test->name)) : STRING_NONE;
			GenBody test_body;
			body_init(&test_body, gen, member_scope, false);
			test_body.in_function = true;
			block_statements(&test_body, &test_body.base, AST_CAST(AstBlock, test->body, AST_BLOCK));
			if (!body_is_terminated(&test_body))
				emit_none(&test_body, IR_RET_IMPLICIT, test->body->offset);
			out->value_body = body_finish(&test_body);
			break;
		}
		case AST_COMPTIME_DECL: {
			AstComptimeDecl *comptime_decl = AST_CAST(AstComptimeDecl, member, AST_COMPTIME_DECL);
			out->kind = IR_DECL_COMPTIME;
			GenBody comptime_body;
			body_init(&comptime_body, gen, member_scope, true);
			block_statements(&comptime_body, &comptime_body.base, AST_CAST(AstBlock, comptime_decl->body, AST_BLOCK));
			if (!body_is_terminated(&comptime_body))
				emit_break(&comptime_body, IR_BREAK_INLINE, member->offset, IR_BODY_RESULT, IR_REF_VOID_VALUE);
			out->value_body = body_finish(&comptime_body);
			break;
		}
		default:
			unsupported(body, member, ast_kind_name(member->kind));
			break;
		}
	}

	IrContainerHeader header;
	header.flags = (uint32_t)decl->container_kind | ((uint32_t)decl->layout << IR_CONTAINER_LAYOUT_SHIFT);
	header.arg_body = 0;
	header.field_count = field_count;
	header.decl_count = decl_count;
	uint32_t payload = APPEND_EXTRA(gen, header);
	for (uint32_t i = 0; i < field_count; i++)
		APPEND_EXTRA(gen, fields[i]);
	for (uint32_t i = 0; i < decl_count; i++)
		APPEND_EXTRA(gen, decls[i]);
	return emit_payload(body, IR_STRUCT_DECL, decl->base.offset, payload);
}

/* ---------- Entry point ---------- */

IrFile *
irgen_file(Arena *arena, Diag *diag, Interner *interner, const SourceFile *source, FileId file, AstContainerDecl *root)
{
	IrGen gen;
	gen.arena = arena;
	gen.diag = diag;
	gen.interner = interner;
	gen.file = file;
	gen.source = source->text;
	gen.ir = ARENA_NEW(arena, IrFile);
	gen.ir->file = file;
	gen.inst_capacity = 256;
	gen.references = ARENA_ARRAY(arena, IrRef, gen.inst_capacity);
	gen.ir->insts = arena_alloc(arena, gen.inst_capacity * sizeof(IrInst), alignof(IrInst));
	gen.ir->inst_count = 0;
	gen.extra_capacity = 1024;
	gen.ir->extra = arena_alloc(arena, gen.extra_capacity * sizeof(uint32_t), alignof(uint32_t));
	gen.ir->extra_count = 0;
	reserve_extra(&gen, 1);	/* extra index 0 means "no body" */

	GenBody top;
	body_init(&top, &gen, NULL, true);
	IrRef root_ref = container_decl(&top, NULL, root, STRING_NONE);
	gen.ir->root = ir_ref_is_inst(root_ref) ? ir_ref_inst(root_ref) : 0;
	return gen.ir;
}

/* ---------- Accessors ---------- */

void
ir_read_words(const IrFile *ir, uint32_t index, void *out, size_t size)
{
	memcpy(out, &ir->extra[index], size);
}

uint32_t
ir_body_count(const IrFile *ir, IrBody body)
{
	return body == 0 ? 0 : ir->extra[body];
}

const IrInstIndex *
ir_body_insts(const IrFile *ir, IrBody body)
{
	return &ir->extra[body + 1];
}

static const char *const op_names[] = {
#define X(name, text, kind) text,
	IR_OP_LIST(X)
#undef X
};

static const IrDataKind op_data_kinds[] = {
#define X(name, text, kind) kind,
	IR_OP_LIST(X)
#undef X
};

static const char *const well_known_names[] = {
#define X(name, spelling) #name,
	IR_WELL_KNOWN_LIST(X)
#undef X
};

const char *
ir_op_name(IrOp op)
{
	if (op >= IR_OP_COUNT)
		return "?";
	return op_names[op];
}

/* ---------- Dumper ---------- */

typedef struct IrDumper {
	FILE           *out;
	const IrFile   *ir;
	const Interner *interner;
} IrDumper;

static void dump_body(const IrDumper *dumper, IrBody body, uint32_t depth);

static void
dump_indent(const IrDumper *dumper, uint32_t depth)
{
	for (uint32_t i = 0; i < depth; i++)
		fputs("  ", dumper->out);
}

static void
dump_ref(const IrDumper *dumper, IrRef ref)
{
	if (ir_ref_is_inst(ref)) {
		fprintf(dumper->out, "%%%" PRIu32, ir_ref_inst(ref));
		return;
	}
	fputc('@', dumper->out);
	for (const char *c = well_known_names[ref]; *c != '\0'; c++)
		fputc(*c >= 'A' && *c <= 'Z' ? *c - 'A' + 'a' : *c, dumper->out);
}

static void
dump_string(const IrDumper *dumper, StringId id)
{
	const char *text = intern_text(dumper->interner, id);
	uint32_t length = intern_length(dumper->interner, id);
	fputc('"', dumper->out);
	for (uint32_t i = 0; i < length; i++) {
		unsigned char c = (unsigned char)text[i];
		if (c == '"' || c == '\\')
			fprintf(dumper->out, "\\%c", c);
		else if (c >= 0x20 && c < 0x7f)
			fputc(c, dumper->out);
		else
			fprintf(dumper->out, "\\x%02x", c);
	}
	fputc('"', dumper->out);
}

/* Prints ` label {` + body + `}` on following lines. */
static void
dump_labeled_body(const IrDumper *dumper, const char *label, IrBody body, uint32_t depth)
{
	if (body == 0)
		return;
	fprintf(dumper->out, " %s {\n", label);
	dump_body(dumper, body, depth + 1);
	dump_indent(dumper, depth);
	fputc('}', dumper->out);
}

static const char *
decl_kind_name(IrDeclKind kind)
{
	switch (kind) {
	case IR_DECL_CONST:
		return "const";
	case IR_DECL_VAR:
		return "var";
	case IR_DECL_FN:
		return "fn";
	case IR_DECL_TEST:
		return "test";
	case IR_DECL_COMPTIME:
		return "comptime";
	}
	return "?";
}

static void
dump_struct_decl(const IrDumper *dumper, uint32_t payload, uint32_t depth)
{
	const IrFile *ir = dumper->ir;
	IrContainerHeader header;
	IR_READ(ir, payload, &header);
	uint32_t index = payload + IR_WORDS(IrContainerHeader);
	fprintf(dumper->out, "(flags=%" PRIu32 ") {\n", header.flags);
	for (uint32_t i = 0; i < header.field_count; i++) {
		IrField field;
		IR_READ(ir, index, &field);
		index = index + IR_WORDS(IrField);
		dump_indent(dumper, depth + 1);
		fputs("field ", dumper->out);
		dump_string(dumper, field.name);
		if (field.flags & IR_FIELD_COMPTIME)
			fputs(" comptime", dumper->out);
		dump_labeled_body(dumper, "type", field.type_body, depth + 1);
		dump_labeled_body(dumper, "align", field.align_body, depth + 1);
		dump_labeled_body(dumper, "default", field.default_body, depth + 1);
		fputc('\n', dumper->out);
	}
	for (uint32_t i = 0; i < header.decl_count; i++) {
		IrDecl decl;
		IR_READ(ir, index, &decl);
		index = index + IR_WORDS(IrDecl);
		dump_indent(dumper, depth + 1);
		fprintf(dumper->out, "decl %s ", decl_kind_name(decl.kind));
		dump_string(dumper, decl.name);
		if (decl.flags & IR_DECL_PUB)
			fputs(" pub", dumper->out);
		if (decl.flags & IR_DECL_EXPORT)
			fputs(" export", dumper->out);
		if (decl.flags & IR_DECL_EXTERN)
			fputs(" extern", dumper->out);
		if (decl.flags & IR_DECL_THREADLOCAL)
			fputs(" threadlocal", dumper->out);
		if (decl.type_body != 0)
			dump_labeled_body(dumper, "type", decl.type_body, depth + 1);
		if (decl.alignment_body != 0)
			dump_labeled_body(dumper, "align", decl.alignment_body, depth + 1);
		dump_labeled_body(dumper, "value", decl.value_body, depth + 1);
		fputc('\n', dumper->out);
	}
	dump_indent(dumper, depth);
	fputc('}', dumper->out);
}

static void
dump_param(const IrDumper *dumper, IrInstIndex index, uint32_t depth)
{
	const IrInst *inst = &dumper->ir->insts[index];
	dump_indent(dumper, depth);
	fprintf(dumper->out, "%%%" PRIu32 " = %s ", index, ir_op_name(inst->op));
	if (inst->op == IR_PARAM_ANYTYPE) {
		dump_string(dumper, inst->data.name.name);
		fputc('\n', dumper->out);
		return;
	}
	IrParam param;
	IR_READ(dumper->ir, inst->data.payload, &param);
	dump_string(dumper, param.name);
	if (param.flags & IR_PARAM_COMPTIME)
		fputs(" comptime", dumper->out);
	if (param.flags & IR_PARAM_NOALIAS)
		fputs(" noalias", dumper->out);
	dump_labeled_body(dumper, "type", param.type_body, depth);
	fputc('\n', dumper->out);
}

static void
dump_func(const IrDumper *dumper, uint32_t payload, uint32_t depth)
{
	const IrFile *ir = dumper->ir;
	IrFuncHeader header;
	IR_READ(ir, payload, &header);
	dump_string(dumper, header.name);
	static const struct {
		uint32_t   flag;
		const char *name;
	} flag_names[] = {
		{IR_FUNC_INLINE, "inline"},
		{IR_FUNC_NOINLINE, "noinline"},
		{IR_FUNC_EXTERN, "extern"},
		{IR_FUNC_EXPORT, "export"},
		{IR_FUNC_VAR_ARGS, "var_args"},
		{IR_FUNC_INFERRED_ERROR, "inferred_error"},
	};
	for (size_t i = 0; i < sizeof(flag_names) / sizeof(flag_names[0]); i++) {
		if (header.flags & flag_names[i].flag)
			fprintf(dumper->out, " %s", flag_names[i].name);
	}
	if (header.extern_lib != STRING_NONE) {
		fputs(" lib=", dumper->out);
		dump_string(dumper, header.extern_lib);
	}
	fputs(" {\n", dumper->out);
	uint32_t params = payload + IR_WORDS(IrFuncHeader);
	for (uint32_t i = 0; i < header.param_count; i++)
		dump_param(dumper, ir->extra[params + i], depth + 1);
	dump_indent(dumper, depth + 1);
	fputs("ret", dumper->out);
	dump_labeled_body(dumper, "type", header.ret_type_body, depth + 1);
	fputc('\n', dumper->out);
	if (header.callconv_body != 0) {
		dump_indent(dumper, depth + 1);
		fputs("callconv", dumper->out);
		dump_labeled_body(dumper, "value", header.callconv_body, depth + 1);
		fputc('\n', dumper->out);
	}
	if (header.align_body != 0) {
		dump_indent(dumper, depth + 1);
		fputs("align", dumper->out);
		dump_labeled_body(dumper, "value", header.align_body, depth + 1);
		fputc('\n', dumper->out);
	}
	if (header.body != 0) {
		dump_indent(dumper, depth + 1);
		fputs("body", dumper->out);
		dump_labeled_body(dumper, "insts", header.body, depth + 1);
		fputc('\n', dumper->out);
	}
	dump_indent(dumper, depth);
	fputc('}', dumper->out);
}

static void
dump_inst(const IrDumper *dumper, IrInstIndex index, uint32_t depth)
{
	const IrFile *ir = dumper->ir;
	const IrInst *inst = &ir->insts[index];
	FILE *out = dumper->out;

	dump_indent(dumper, depth);
	fprintf(out, "%%%" PRIu32 " = %s", index, ir_op_name(inst->op));
	switch (inst->op) {
	case IR_STRUCT_DECL:
		fputc(' ', out);
		dump_struct_decl(dumper, inst->data.payload, depth);
		break;
	case IR_FUNC:
	case IR_FN_TYPE:
		fputc(' ', out);
		dump_func(dumper, inst->data.payload, depth);
		break;
	case IR_INT_TYPE:
		fprintf(out, " %c%" PRIu64, (inst->data.integer & 0x10000u) ? 'i' : 'u', inst->data.integer & 0xffffu);
		break;
	case IR_PANIC: {
		IrPanic payload;
		IR_READ(ir, inst->data.payload, &payload);
		fputc(' ', out);
		dump_ref(dumper, payload.message);
		break;
	}
	case IR_ARRAY_INIT: {
		uint32_t payload = inst->data.payload;
		fputc(' ', out);
		dump_ref(dumper, ir->extra[payload]);
		uint32_t count = ir->extra[payload + 1];
		for (uint32_t i = 0; i < count; i++) {
			fputs(", ", out);
			dump_ref(dumper, ir->extra[payload + 2 + i]);
		}
		break;
	}
	case IR_FOR_LEN: {
		uint32_t count = ir->extra[inst->data.payload];
		for (uint32_t i = 0; i < count; i++) {
			fputs(i == 0 ? " " : ", ", out);
			dump_ref(dumper, ir->extra[inst->data.payload + 1 + i]);
		}
		break;
	}
	case IR_INT_BIG: {
		uint32_t limb_count = ir->extra[inst->data.payload];
		fputs(" 0x", out);
		for (uint32_t i = limb_count; i > 0; i--)
			fprintf(out, i == limb_count ? "%" PRIx32 : "%08" PRIx32, ir->extra[inst->data.payload + i]);
		break;
	}
	case IR_ARG_TYPE:
		fputc(' ', out);
		dump_ref(dumper, inst->data.bin.lhs);
		fprintf(out, ", %" PRIu32, inst->data.bin.rhs);
		break;
	case IR_BOOL_AND:
	case IR_BOOL_OR: {
		IrBoolBr payload;
		IR_READ(ir, inst->data.payload, &payload);
		fputc(' ', out);
		dump_ref(dumper, payload.lhs);
		dump_labeled_body(dumper, "rhs", payload.rhs_body, depth);
		break;
	}
	case IR_ALLOC: {
		IrAlloc payload;
		IR_READ(ir, inst->data.payload, &payload);
		fputs(" type=", out);
		if (payload.type == IR_REF_NONE)
			fputs("inferred", out);
		else
			dump_ref(dumper, payload.type);
		fputs(" init=", out);
		dump_ref(dumper, payload.init);
		if (payload.flags & IR_ALLOC_COMPTIME)
			fputs(" comptime", out);
		if (payload.flags & IR_ALLOC_CONST)
			fputs(" const", out);
		if (payload.alignment != IR_REF_NONE) {
			fputs(" align=", out);
			dump_ref(dumper, payload.alignment);
		}
		break;
	}
	case IR_BLOCK:
	case IR_LOOP:
	case IR_DEFER:
		dump_labeled_body(dumper, "", inst->data.payload, depth);
		break;
	case IR_CONDBR:
	case IR_IF: {
		IrCondbr payload;
		IR_READ(ir, inst->data.payload, &payload);
		fputc(' ', out);
		dump_ref(dumper, payload.condition);
		dump_labeled_body(dumper, "then", payload.then_body, depth);
		dump_labeled_body(dumper, "else", payload.else_body, depth);
		break;
	}
	case IR_ALIGN:
		dump_labeled_body(dumper, "value", inst->data.payload, depth);
		break;
	case IR_PTR_TYPE: {
		IrPointerType payload;
		IR_READ(ir, inst->data.payload, &payload);
		fputc(' ', out);
		dump_ref(dumper, payload.child);
		fputs(", ", out);
		dump_ref(dumper, (payload.flags & IR_POINTER_CONST) != 0 ? IR_REF_TRUE : IR_REF_FALSE);
		if ((payload.flags & IR_POINTER_MANY) != 0)
			fputs(" many", out);
		if ((payload.flags & IR_POINTER_SLICE) != 0)
			fputs(" slice", out);
		if ((payload.flags & IR_POINTER_ALLOWZERO) != 0)
			fputs(" allowzero", out);
		if ((payload.flags & IR_POINTER_VOLATILE) != 0)
			fputs(" volatile", out);
		dump_labeled_body(dumper, "align", payload.alignment_body, depth);
		break;
	}
	case IR_CALL: {
		IrCallHeader header;
		IR_READ(ir, inst->data.payload, &header);
		fputc(' ', out);
		dump_ref(dumper, header.callee);
		if (header.modifier == IR_CALL_COMPTIME)
			fputs(" comptime", out);
		uint32_t bodies = inst->data.payload + IR_WORDS(IrCallHeader);
		for (uint32_t i = 0; i < header.arg_count; i++) {
			char label[32];
			snprintf(label, sizeof(label), "arg%" PRIu32, i);
			dump_labeled_body(dumper, label, ir->extra[bodies + i], depth);
		}
		break;
	}
	default:
		switch (op_data_kinds[inst->op]) {
		case IR_DATA_NONE:
		case IR_DATA_PAYLOAD:
			break;
		case IR_DATA_UN:
			fputc(' ', out);
			dump_ref(dumper, inst->data.un.operand);
			break;
		case IR_DATA_BIN:
			fputc(' ', out);
			dump_ref(dumper, inst->data.bin.lhs);
			fputs(", ", out);
			dump_ref(dumper, inst->data.bin.rhs);
			break;
		case IR_DATA_INT:
			fprintf(out, " %" PRIu64, inst->data.integer);
			break;
		case IR_DATA_FLOAT:
			fprintf(out, " %.17g", inst->data.floating);
			break;
		case IR_DATA_NAME:
			fputc(' ', out);
			dump_string(dumper, inst->data.name.name);
			if (inst->data.name.operand != IR_REF_NONE) {
				fputs(", ", out);
				dump_ref(dumper, inst->data.name.operand);
			}
			break;
		case IR_DATA_BREAK:
			if (inst->data.brk.block == IR_BODY_RESULT)
				fputs(" result", out);
			else
				fprintf(out, " %%%" PRIu32, inst->data.brk.block);
			fputs(", ", out);
			dump_ref(dumper, inst->data.brk.operand);
			break;
		}
		break;
	}
	fputc('\n', out);
}

static void
dump_body(const IrDumper *dumper, IrBody body, uint32_t depth)
{
	uint32_t count = ir_body_count(dumper->ir, body);
	const IrInstIndex *insts = ir_body_insts(dumper->ir, body);
	for (uint32_t i = 0; i < count; i++)
		dump_inst(dumper, insts[i], depth);
}

void
ir_dump(FILE *out, const IrFile *ir, const Interner *interner)
{
	IrDumper dumper;
	dumper.out = out;
	dumper.ir = ir;
	dumper.interner = interner;
	if (ir->inst_count == 0)
		return;
	dump_inst(&dumper, ir->root, 0);
}
