#ifndef SZC_IR_H
#define SZC_IR_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "arena.h"
#include "ast.h"
#include "diag.h"
#include "intern.h"
#include "source.h"

/*
 * Untyped IR produced by irgen.c and analysed by sema.c. See docs/IR.md.
 */

/* ---------- Refs ---------- */

/*
 * Well-known refs: constants every file can use without an instruction.
 * X(NAME, primitive identifier or NULL)
 */
#define IR_WELL_KNOWN_LIST(X) \
	X(NONE, NULL) \
	X(VOID_VALUE, NULL) \
	X(TRUE, "true") \
	X(FALSE, "false") \
	X(NULL_VALUE, "null") \
	X(UNDEFINED, "undefined") \
	X(ZERO_USIZE, NULL) \
	X(ONE_USIZE, NULL) \
	X(TYPE_U1, "u1") \
	X(TYPE_U8, "u8") \
	X(TYPE_I8, "i8") \
	X(TYPE_U16, "u16") \
	X(TYPE_I16, "i16") \
	X(TYPE_U29, "u29") \
	X(TYPE_U32, "u32") \
	X(TYPE_I32, "i32") \
	X(TYPE_U64, "u64") \
	X(TYPE_I64, "i64") \
	X(TYPE_U128, "u128") \
	X(TYPE_I128, "i128") \
	X(TYPE_USIZE, "usize") \
	X(TYPE_ISIZE, "isize") \
	X(TYPE_C_CHAR, "c_char") \
	X(TYPE_C_SHORT, "c_short") \
	X(TYPE_C_USHORT, "c_ushort") \
	X(TYPE_C_INT, "c_int") \
	X(TYPE_C_UINT, "c_uint") \
	X(TYPE_C_LONG, "c_long") \
	X(TYPE_C_ULONG, "c_ulong") \
	X(TYPE_C_LONGLONG, "c_longlong") \
	X(TYPE_C_ULONGLONG, "c_ulonglong") \
	X(TYPE_C_LONGDOUBLE, "c_longdouble") \
	X(TYPE_F16, "f16") \
	X(TYPE_F32, "f32") \
	X(TYPE_F64, "f64") \
	X(TYPE_F80, "f80") \
	X(TYPE_F128, "f128") \
	X(TYPE_ANYOPAQUE, "anyopaque") \
	X(TYPE_BOOL, "bool") \
	X(TYPE_VOID, "void") \
	X(TYPE_TYPE, "type") \
	X(TYPE_ANYERROR, "anyerror") \
	X(TYPE_COMPTIME_INT, "comptime_int") \
	X(TYPE_COMPTIME_FLOAT, "comptime_float") \
	X(TYPE_NORETURN, "noreturn") \
	X(TYPE_NULL, NULL) \
	X(TYPE_UNDEFINED, NULL) \
	X(TYPE_ENUM_LITERAL, NULL)

typedef enum IrWellKnown {
#define X(name, spelling) IR_REF_##name,
	IR_WELL_KNOWN_LIST(X)
#undef X
	IR_REF_FIRST_INST
} IrWellKnown;

/* An operand: a well-known constant (< IR_REF_FIRST_INST) or an instruction. */
typedef uint32_t IrRef;

/* Index of an instruction in IrFile.insts. */
typedef uint32_t IrInstIndex;

/* Extra index of a body (`count, inst...`); 0 means "no body". */
typedef uint32_t IrBody;

static inline IrRef
ir_ref_of_inst(IrInstIndex index)
{
	return (IrRef)(index + IR_REF_FIRST_INST);
}

static inline bool
ir_ref_is_inst(IrRef ref)
{
	return ref >= IR_REF_FIRST_INST;
}

static inline IrInstIndex
ir_ref_inst(IrRef ref)
{
	return (IrInstIndex)(ref - IR_REF_FIRST_INST);
}

/* ---------- Instructions ---------- */

/* How an instruction's `data` is used (drives the dumper). */
typedef enum IrDataKind {
	IR_DATA_NONE,
	IR_DATA_UN,	/* un.operand */
	IR_DATA_BIN,	/* bin.lhs, bin.rhs */
	IR_DATA_INT,	/* integer */
	IR_DATA_FLOAT,	/* floating */
	IR_DATA_NAME,	/* name.name, name.operand (may be NONE) */
	IR_DATA_BREAK,	/* brk.block (instruction index of the block), brk.operand */
	IR_DATA_PAYLOAD,	/* payload: extra index, layout depends on op */
} IrDataKind;

/*
 * X(NAME, "text", data kind)   -- operands described in the comment.
 */
#define IR_OP_LIST(X) \
	/* Declarations and functions */ \
	X(STRUCT_DECL, "struct_decl", IR_DATA_PAYLOAD)	/* IrContainerHeader + fields + decls */ \
	X(FN_TYPE, "fn_type", IR_DATA_PAYLOAD)	/* IrFuncHeader + params; signature only */ \
	X(FUNC, "func", IR_DATA_PAYLOAD)	/* IrFuncHeader, params, bodies */ \
	X(PARAM, "param", IR_DATA_PAYLOAD)	/* name, flags, type body */ \
	X(PARAM_ANYTYPE, "param_anytype", IR_DATA_NAME)	/* name */ \
	X(DECL_TYPE, "decl_type", IR_DATA_NONE)	/* explicit mutable global initializer result type */ \
	X(DECL_VAL, "decl_val", IR_DATA_NAME)	/* value of the declaration `name` */ \
	X(DECL_REF, "decl_ref", IR_DATA_NAME)	/* pointer to the declaration `name` */ \
	/* Constants */ \
	X(INT, "int", IR_DATA_INT)	/* comptime_int that fits in u64 */ \
	X(INT_BIG, "int_big", IR_DATA_PAYLOAD)	/* limb count, then 32-bit limbs, least significant first */ \
	X(FLOAT, "float", IR_DATA_FLOAT)	/* comptime_float */ \
	X(STR, "str", IR_DATA_NAME)	/* string literal bytes (interned) */ \
	X(ENUM_LITERAL, "enum_literal", IR_DATA_NAME)	/* .name */ \
	X(INT_TYPE, "int_type", IR_DATA_INT)	/* bit 16 = signed, bits 0..15 = bit count */ \
	X(ALIGN, "align", IR_DATA_PAYLOAD)	/* comptime u29 byte alignment body */ \
	X(CONST_BIND, "const_bind", IR_DATA_BIN)	/* immutable value, declaration alignment ref */ \
	X(ARRAY_TYPE, "array_type", IR_DATA_BIN)	/* length (usize), child type */ \
	X(ARRAY_ELEM_TYPE, "array_elem_type", IR_DATA_UN)	/* array type -> child type */ \
	X(ARRAY_INIT, "array_init", IR_DATA_PAYLOAD)	/* type ref, count, element refs */ \
	X(ARRAY_BASE_PTR, "array_base_ptr", IR_DATA_UN)	/* normalize array slot or pointer slot */ \
	X(ARRAY_INDEX_PTR, "array_index_ptr", IR_DATA_BIN)	/* checked element address */ \
	X(ARRAY_INDEX_BASE, "array_index_base", IR_DATA_BIN)	/* borrow nested array storage for indexing */ \
	X(ARRAY_INIT_PTR, "array_init_ptr", IR_DATA_BIN)	/* validate anonymous initializer destination and count */ \
	X(ARRAY_INDEX, "array_index", IR_DATA_BIN)	/* array/pointer value, usize index */ \
	X(ARRAY_CAT, "array_cat", IR_DATA_BIN)	/* concatenate indexable constants */ \
	X(FIELD_VAL, "field_val", IR_DATA_NAME)	/* field name, base operand (array .len initially) */ \
	X(PTR_TYPE, "ptr_type", IR_DATA_PAYLOAD)	/* IrPointerType */ \
	/* Result types and coercion */ \
	X(AS_RETURN, "as_return", IR_DATA_BIN)	/* direct return-value coercion */ \
	X(AS, "as", IR_DATA_BIN)	/* coerce rhs to type lhs */ \
	X(TYPEOF, "typeof", IR_DATA_UN)	/* type of operand */ \
	X(ELEM_TYPE, "elem_type", IR_DATA_UN)	/* pointee type of pointer value operand */ \
	X(LOG2_INT_TYPE, "log2_int_type", IR_DATA_UN)	/* shift amount type for integer value operand */ \
	X(PANIC, "panic", IR_DATA_PAYLOAD)	/* normal message expression operand */ \
	X(DEFER, "defer", IR_DATA_PAYLOAD)	/* reusable cleanup body, ending in break_inline */ \
	X(ENSURE_RESULT, "ensure_result", IR_DATA_UN)	/* discarded expression must be void or noreturn */ \
	X(FOR_LEN, "for_len", IR_DATA_PAYLOAD)	/* count, then bounded input length refs */ \
	X(RET_TYPE, "ret_type", IR_DATA_NONE)	/* return type of the function being analysed */ \
	X(ARG_TYPE, "arg_type", IR_DATA_BIN)	/* type of parameter rhs (index) of the call instruction lhs */ \
	/* Casts (builtins) */ \
	X(INT_CAST, "int_cast", IR_DATA_BIN)	/* @intCast: integer rhs to type lhs, value must fit */ \
	X(TRUNCATE, "truncate", IR_DATA_BIN)	/* @truncate: integer rhs to type lhs, dropping high bits */ \
	X(INT_FROM_BOOL, "int_from_bool", IR_DATA_UN)	/* @intFromBool: u1 */ \
	/* Arithmetic and logic */ \
	X(ADD, "add", IR_DATA_BIN) \
	X(SUB, "sub", IR_DATA_BIN) \
	X(MUL, "mul", IR_DATA_BIN) \
	X(DIV, "div", IR_DATA_BIN) \
	X(MOD, "mod", IR_DATA_BIN) \
	X(DIV_TRUNC, "div_trunc", IR_DATA_BIN) \
	X(DIV_FLOOR, "div_floor", IR_DATA_BIN) \
	X(DIV_CEIL, "div_ceil", IR_DATA_BIN) \
	X(DIV_EXACT, "div_exact", IR_DATA_BIN) \
	X(REM, "rem", IR_DATA_BIN) \
	X(MOD_BUILTIN, "mod_builtin", IR_DATA_BIN) \
	X(ADD_WRAP, "add_wrap", IR_DATA_BIN) \
	X(SUB_WRAP, "sub_wrap", IR_DATA_BIN) \
	X(MUL_WRAP, "mul_wrap", IR_DATA_BIN) \
	X(ADD_SAT, "add_sat", IR_DATA_BIN) \
	X(SUB_SAT, "sub_sat", IR_DATA_BIN) \
	X(MUL_SAT, "mul_sat", IR_DATA_BIN) \
	X(SHL, "shl", IR_DATA_BIN) \
	X(SHL_SAT, "shl_sat", IR_DATA_BIN) \
	X(SHR, "shr", IR_DATA_BIN) \
	X(BIT_AND, "bit_and", IR_DATA_BIN) \
	X(BIT_OR, "bit_or", IR_DATA_BIN) \
	X(BIT_XOR, "bit_xor", IR_DATA_BIN) \
	X(CMP_EQ, "cmp_eq", IR_DATA_BIN) \
	X(CMP_NEQ, "cmp_neq", IR_DATA_BIN) \
	X(CMP_LT, "cmp_lt", IR_DATA_BIN) \
	X(CMP_GT, "cmp_gt", IR_DATA_BIN) \
	X(CMP_LTE, "cmp_lte", IR_DATA_BIN) \
	X(CMP_GTE, "cmp_gte", IR_DATA_BIN) \
	X(NEGATE, "negate", IR_DATA_UN) \
	X(NEGATE_WRAP, "negate_wrap", IR_DATA_UN) \
	X(BIT_NOT, "bit_not", IR_DATA_UN) \
	X(BOOL_NOT, "bool_not", IR_DATA_UN) \
	X(BOOL_AND, "bool_and", IR_DATA_PAYLOAD)	/* lhs ref, rhs body */ \
	X(BOOL_OR, "bool_or", IR_DATA_PAYLOAD)	/* lhs ref, rhs body */ \
	/* Memory */ \
	X(ALLOC, "alloc", IR_DATA_PAYLOAD)	/* type ref (or NONE: inferred), init ref, flags: local variable */ \
	X(DEREF, "deref", IR_DATA_UN)	/* validated source dereference */ \
	X(REF_DEREF, "ref_deref", IR_DATA_UN)	/* validated address of dereference */ \
	X(REF, "ref", IR_DATA_UN)	/* immutable address, anchored after operand definition */ \
	X(REF_TYPE, "ref_type", IR_DATA_UN)	/* pointee of an address-of result type */ \
	X(LOAD, "load", IR_DATA_UN)	/* pointer */ \
	X(STORE, "store", IR_DATA_BIN)	/* pointer, value */ \
	/* Control flow */ \
	X(BLOCK, "block", IR_DATA_PAYLOAD)	/* body */ \
	X(BREAK, "break", IR_DATA_BREAK)	/* leave block with operand */ \
	X(BREAK_INLINE, "break_inline", IR_DATA_BREAK)	/* comptime-only break */ \
	X(CONDBR, "condbr", IR_DATA_PAYLOAD)	/* condition ref, then body, else body */ \
	X(IF, "if", IR_DATA_PAYLOAD)	/* IrCondbr; branches yield via break_inline BODY_RESULT */ \
	X(LOOP, "loop", IR_DATA_PAYLOAD)	/* body (ends with repeat) */ \
	X(REPEAT, "repeat", IR_DATA_UN)	/* loop instruction ref */ \
	X(CALL, "call", IR_DATA_PAYLOAD)	/* callee ref, modifier, arg count, arg bodies */ \
	X(RET, "ret", IR_DATA_UN)	/* return operand */ \
	X(RET_IMPLICIT, "ret_implicit", IR_DATA_NONE)	/* end of a function body */ \
	X(UNREACHABLE, "unreachable", IR_DATA_NONE)

typedef enum IrOp {
#define X(name, text, kind) IR_##name,
	IR_OP_LIST(X)
#undef X
	IR_OP_COUNT
} IrOp;

typedef union IrData {
	struct {
		IrRef operand;
	} un;
	struct {
		IrRef lhs;
		IrRef rhs;
	} bin;
	uint64_t integer;
	double   floating;
	struct {
		StringId name;
		IrRef    operand;
	} name;
	struct {
		IrInstIndex block;
		IrRef       operand;
	} brk;
	uint32_t payload;
} IrData;

typedef struct IrInst {
	IrOp     op;
	uint32_t src;	/* byte offset of the AST node's main token */
	IrData   data;
} IrInst;

/* ---------- Payload layouts (in IrFile.extra) ---------- */

typedef enum IrPointerFlags {
	IR_POINTER_CONST = 0x1,
	IR_POINTER_ALLOWZERO = 0x2,
	IR_POINTER_VOLATILE = 0x4,
} IrPointerFlags;

typedef struct IrPointerType {
	IrRef			child;
	uint32_t	flags;
	IrBody		alignment_body;	/* 0 = implicit ABI alignment */
	uint32_t	alignment_src;
} IrPointerType;

/* STRUCT_DECL: header, then field_count IrField, then decl_count IrDecl. */
typedef enum IrContainerFlags {
	IR_CONTAINER_KIND_MASK = 0x3,	/* AstContainerKind */
	IR_CONTAINER_LAYOUT_SHIFT = 2,	/* AstContainerLayout << 2 */
	IR_CONTAINER_LAYOUT_MASK = 0xc,
	IR_CONTAINER_UNION_ENUM_TAG = 0x10,
	IR_CONTAINER_IS_TUPLE = 0x20,
} IrContainerFlags;

typedef struct IrContainerHeader {
	uint32_t flags;
	IrBody   arg_body;	/* backing integer / tag type, or 0 */
	uint32_t field_count;
	uint32_t decl_count;
} IrContainerHeader;

typedef enum IrFieldFlags {
	IR_FIELD_COMPTIME = 0x1,
	IR_FIELD_HAS_TYPE = 0x2,	/* false for enum fields and untyped union fields */
} IrFieldFlags;

typedef struct IrField {
	StringId name;
	uint32_t flags;
	uint32_t src;
	IrBody   type_body;
	IrBody   align_body;
	IrBody   default_body;	/* default value, or enum tag value */
} IrField;

typedef enum IrDeclKind {
	IR_DECL_CONST,
	IR_DECL_VAR,
	IR_DECL_FN,
	IR_DECL_TEST,
	IR_DECL_COMPTIME,
} IrDeclKind;

typedef enum IrDeclFlags {
	IR_DECL_PUB = 0x1,
	IR_DECL_EXPORT = 0x2,
	IR_DECL_EXTERN = 0x4,
	IR_DECL_THREADLOCAL = 0x8,
} IrDeclFlags;

typedef struct IrDecl {
	StringId		name;	/* test name for tests, STRING_NONE for comptime blocks */
	IrDeclKind	kind;
	uint32_t		flags;
	uint32_t		src;
	IrBody			type_body;	/* explicit mutable/aligned-const global type, otherwise zero */
	IrBody			alignment_body;	/* global alignment, otherwise zero */
	uint32_t		alignment_src;
	IrBody			value_body;	/* ends with break_inline of the value */
} IrDecl;

/* FUNC: header, then param_count instruction indices of PARAM/PARAM_ANYTYPE. */
typedef enum IrFuncFlags {
	IR_FUNC_INLINE = 0x1,
	IR_FUNC_NOINLINE = 0x2,
	IR_FUNC_EXTERN = 0x4,
	IR_FUNC_EXPORT = 0x8,
	IR_FUNC_VAR_ARGS = 0x10,
	IR_FUNC_INFERRED_ERROR = 0x20,
	IR_FUNC_HAS_BODY = 0x40,
} IrFuncFlags;

typedef struct IrFuncHeader {
	uint32_t flags;
	StringId name;	/* declaration name, for symbols and messages */
	StringId extern_lib;
	IrBody   ret_type_body;
	IrBody   callconv_body;
	IrBody   align_body;
	uint32_t align_src;
	IrBody   body;
	uint32_t param_count;
} IrFuncHeader;

typedef enum IrParamFlags {
	IR_PARAM_COMPTIME = 0x1,
	IR_PARAM_NOALIAS = 0x2,
} IrParamFlags;

typedef struct IrParam {
	StringId name;
	uint32_t flags;
	IrBody   type_body;
} IrParam;

typedef enum IrAllocFlags {
	IR_ALLOC_COMPTIME = 0x1,
	IR_ALLOC_CONST = 0x2,	/* a `const` whose address is taken */
} IrAllocFlags;

typedef struct IrAlloc {
	IrRef			type;	/* NONE: inferred from init */
	IrRef			init;
	uint32_t	flags;
	IrRef			alignment;	/* NONE: implicit ABI alignment */
} IrAlloc;

typedef struct IrCondbr {
	IrRef  condition;
	IrBody then_body;
	IrBody else_body;
} IrCondbr;

typedef struct IrBoolBr {
	IrRef  lhs;
	IrBody rhs_body;
} IrBoolBr;

typedef enum IrCallModifier {
	IR_CALL_AUTO,
	IR_CALL_COMPTIME,	/* comptime f() */
} IrCallModifier;

/* CALL: header, then arg_count bodies. Each arg body ends with break of the value. */
typedef struct IrPanic {
	IrRef message;
	uint32_t argument_src;
} IrPanic;

typedef struct IrCallHeader {
	IrRef          callee;
	IrCallModifier modifier;
	uint32_t       arg_count;
} IrCallHeader;

/* ---------- Files ---------- */

typedef struct IrFile {
	FileId      file;
	IrInst      *insts;
	uint32_t    inst_count;
	uint32_t    *extra;
	uint32_t    extra_count;
	IrInstIndex root;	/* STRUCT_DECL of the file */
} IrFile;

/* Builds the IR of one parsed file. Errors go to `diag`; the IR is still returned. */
IrFile *irgen_file(Arena *arena, Diag *diag, Interner *interner, const SourceFile *source, FileId file, AstContainerDecl *root);

/* Prints the IR in a readable text form (`szc ir`). */
void ir_dump(FILE *out, const IrFile *ir, const Interner *interner);

const char *ir_op_name(IrOp op);

/* Number of extra words a payload struct occupies (all payload fields are 32 bits). */
#define IR_WORDS(Type) ((uint32_t)(sizeof(Type) / sizeof(uint32_t)))

/* Copies the payload struct at extra index `index` into `*out` (no aliasing tricks). */
#define IR_READ(ir, index, out) ir_read_words((ir), (index), (out), sizeof(*(out)))

void ir_read_words(const IrFile *ir, uint32_t index, void *out, size_t size);

/* The body at extra index `body`: its instruction count and first instruction slot. */
uint32_t           ir_body_count(const IrFile *ir, IrBody body);
const IrInstIndex *ir_body_insts(const IrFile *ir, IrBody body);

/* brk.block value of a break that ends a declaration, type or argument body. */
#define IR_BODY_RESULT ((IrInstIndex)0xffffffffu)

#endif
