#ifndef SZC_AST_H
#define SZC_AST_H

#include <stdbool.h>
#include <stdint.h>

#include "source.h"

/*
 * Abstract syntax tree.
 *
 * Every node kind has its own struct. Each struct starts with an `AstNode`
 * header (`base`), so a pointer to any node can be used as an `AstNode *`, and
 * `AST_CAST` turns it back into the specific struct after checking the kind.
 * Expressions and types are both expressions in Zig, so an `AstNode *` child
 * can be any expression node. Names are `Span`s into the source; no text is copied.
 */

typedef struct Span {
	uint32_t start;
	uint32_t end;
} Span;

typedef enum AstKind {
	/* Container level */
	AST_CONTAINER_DECL,
	AST_CONTAINER_FIELD,
	AST_FN_DECL,
	AST_VAR_DECL,
	AST_TEST_DECL,
	AST_COMPTIME_DECL,

	/* Literals and names */
	AST_IDENTIFIER,
	AST_NUMBER_LITERAL,
	AST_CHAR_LITERAL,
	AST_STRING_LITERAL,
	AST_MULTILINE_STRING_LITERAL,
	AST_ENUM_LITERAL,
	AST_ERROR_VALUE,
	AST_UNREACHABLE,
	AST_ANYFRAME_LITERAL,

	/* Operators */
	AST_BINARY,
	AST_CATCH,
	AST_UNARY,
	AST_GROUPED,

	/* Suffix expressions */
	AST_CALL,
	AST_BUILTIN_CALL,
	AST_FIELD_ACCESS,
	AST_DEREF,
	AST_UNWRAP_OPTIONAL,
	AST_INDEX,
	AST_SLICE,
	AST_INIT_LIST,

	/* Types */
	AST_POINTER_TYPE,
	AST_ARRAY_TYPE,
	AST_OPTIONAL_TYPE,
	AST_ERROR_UNION_TYPE,
	AST_ANYFRAME_TYPE,
	AST_FN_PROTO,
	AST_ERROR_SET_DECL,

	/* Control flow */
	AST_BLOCK,
	AST_IF,
	AST_WHILE,
	AST_FOR,
	AST_SWITCH,
	AST_BREAK,
	AST_CONTINUE,
	AST_RETURN,
	AST_COMPTIME,
	AST_NOSUSPEND,
	AST_SUSPEND,
	AST_RESUME,
	AST_ASM,

	/* Statements */
	AST_ASSIGN,
	AST_ASSIGN_DESTRUCTURE,
	AST_DEFER,

	AST_KIND_COUNT
} AstKind;

typedef struct AstNode {
	AstKind  kind;
	FileId   file;
	uint32_t offset;	/* byte offset of the node's main token, for diagnostics */
	uint32_t start;	/* byte offset of the node's first token */
	uint32_t end;	/* byte offset just past the node's last token */
} AstNode;

typedef struct AstNodeList {
	AstNode  **items;
	uint32_t count;
} AstNodeList;

/* `|x|`, `|*x|`, `|x, i|` captures. */
typedef struct AstCapture {
	Span name;
	bool by_pointer;
} AstCapture;

typedef struct AstCaptureList {
	AstCapture *items;
	uint32_t   count;
} AstCaptureList;

/* Optional block or loop label. has_label is false when there is none. */
typedef struct AstLabel {
	bool has_label;
	Span name;
} AstLabel;

/* ---------- Container level ---------- */

typedef enum AstContainerKind {
	AST_CONTAINER_STRUCT,
	AST_CONTAINER_ENUM,
	AST_CONTAINER_UNION,
	AST_CONTAINER_OPAQUE,
} AstContainerKind;

typedef enum AstContainerLayout {
	AST_LAYOUT_AUTO,
	AST_LAYOUT_EXTERN,
	AST_LAYOUT_PACKED,
} AstContainerLayout;

/* struct/enum/union/opaque { ... }. A file's root is a struct container. */
typedef struct AstContainerDecl {
	AstNode            base;
	AstContainerKind   container_kind;
	AstContainerLayout layout;
	AstNode            *arg;	/* struct(T), enum(T), union(T): NULL if absent */
	bool               union_enum_tag;	/* union(enum) or union(enum(T)) */
	AstNodeList        members;
} AstContainerDecl;

typedef struct AstContainerField {
	AstNode base;
	bool    has_name;	/* false for tuple fields: `struct { u32, u8 }` */
	Span    name;
	bool    is_comptime;
	AstNode *type;	/* NULL for enum fields and untyped union fields */
	AstNode *align;
	AstNode *default_value;	/* also the enum tag value: `a = 1` */
} AstContainerField;

typedef enum AstFnModifier {
	AST_FN_MODIFIER_NONE,
	AST_FN_MODIFIER_EXPORT,
	AST_FN_MODIFIER_EXTERN,
	AST_FN_MODIFIER_INLINE,
	AST_FN_MODIFIER_NOINLINE,
} AstFnModifier;

typedef enum AstParamKind {
	AST_PARAM_NORMAL,
	AST_PARAM_COMPTIME,
	AST_PARAM_NOALIAS,
} AstParamKind;

typedef struct AstParam {
	AstParamKind kind;
	bool         has_name;
	Span         name;
	bool         is_anytype;
	AstNode      *type;	/* NULL when is_anytype */
} AstParam;

typedef struct AstFnProto {
	AstNode  base;
	bool     has_name;
	Span     name;
	AstParam *params;
	uint32_t param_count;
	bool     is_var_args;	/* trailing `...` */
	AstNode  *align;
	AstNode  *addrspace;
	AstNode  *linksection;
	AstNode  *callconv;
	bool     inferred_error;	/* `!T` return type */
	AstNode  *return_type;
} AstFnProto;

typedef struct AstFnDecl {
	AstNode       base;
	bool          is_pub;
	AstFnModifier modifier;
	bool          has_extern_lib;
	Span          extern_lib;	/* extern "c" fn ...: the string literal */
	AstFnProto    *proto;
	AstNode       *body;	/* AstBlock, or NULL for a prototype ending in ';' */
} AstFnDecl;

typedef enum AstVarModifier {
	AST_VAR_MODIFIER_NONE,
	AST_VAR_MODIFIER_EXPORT,
	AST_VAR_MODIFIER_EXTERN,
} AstVarModifier;

/* Global or local `const`/`var` declaration. */
typedef struct AstVarDecl {
	AstNode        base;
	bool           is_pub;
	bool           is_const;
	bool           is_comptime;	/* local `comptime var x` */
	bool           is_threadlocal;
	AstVarModifier modifier;
	bool           has_extern_lib;
	Span           extern_lib;
	Span           name;
	AstNode        *type;
	AstNode        *align;
	AstNode        *addrspace;
	AstNode        *linksection;
	AstNode        *init;
} AstVarDecl;

typedef struct AstTestDecl {
	AstNode base;
	bool    has_name;
	bool    name_is_identifier;	/* test foo {} vs test "foo" {} */
	Span    name;
	AstNode *body;
} AstTestDecl;

typedef struct AstComptimeDecl {
	AstNode base;
	AstNode *body;
} AstComptimeDecl;

/* ---------- Literals and names ---------- */

/* Identifiers, including @"..." identifiers (name then includes the quotes). */
typedef struct AstIdentifier {
	AstNode base;
	Span    name;
} AstIdentifier;

/* Number, char and single-line string literals: just the token text. */
typedef struct AstLiteral {
	AstNode base;
	Span    text;
} AstLiteral;

/* Consecutive `\\` lines; each span is one line token. */
typedef struct AstMultilineString {
	AstNode  base;
	Span     *lines;
	uint32_t line_count;
} AstMultilineString;

/* `.name` (enum literal) or `error.Name`. */
typedef struct AstNamedValue {
	AstNode base;
	Span    name;
} AstNamedValue;

/* `unreachable`, `anyframe`: no data beyond the header. */
typedef struct AstSimple {
	AstNode base;
} AstSimple;

/* ---------- Operators ---------- */

typedef enum AstBinaryOp {
	AST_OP_BOOL_OR,
	AST_OP_BOOL_AND,
	AST_OP_EQUAL,
	AST_OP_NOT_EQUAL,
	AST_OP_LESS,
	AST_OP_GREATER,
	AST_OP_LESS_EQUAL,
	AST_OP_GREATER_EQUAL,
	AST_OP_BIT_AND,
	AST_OP_BIT_XOR,
	AST_OP_BIT_OR,
	AST_OP_ORELSE,
	AST_OP_SHL,
	AST_OP_SHR,
	AST_OP_SHL_SAT,
	AST_OP_ADD,
	AST_OP_SUB,
	AST_OP_CONCAT,
	AST_OP_ADD_WRAP,
	AST_OP_SUB_WRAP,
	AST_OP_ADD_SAT,
	AST_OP_SUB_SAT,
	AST_OP_MERGE_ERROR_SETS,
	AST_OP_MUL,
	AST_OP_DIV,
	AST_OP_MOD,
	AST_OP_MUL_WRAP,
	AST_OP_MUL_SAT,
	AST_OP_ARRAY_MULT,
	AST_BINARY_OP_COUNT
} AstBinaryOp;

typedef struct AstBinary {
	AstNode     base;
	AstBinaryOp op;
	AstNode     *lhs;
	AstNode     *rhs;
} AstBinary;

/* `lhs catch |err| rhs` */
typedef struct AstCatch {
	AstNode base;
	AstNode *lhs;
	bool    has_capture;
	Span    capture;
	AstNode *rhs;
} AstCatch;

typedef enum AstUnaryOp {
	AST_UNARY_BOOL_NOT,
	AST_UNARY_NEGATE,
	AST_UNARY_BIT_NOT,
	AST_UNARY_NEGATE_WRAP,
	AST_UNARY_ADDRESS_OF,
	AST_UNARY_TRY,
	AST_UNARY_OP_COUNT
} AstUnaryOp;

typedef struct AstUnary {
	AstNode    base;
	AstUnaryOp op;
	AstNode    *operand;
} AstUnary;

/* (expr), also used for single-operand wrappers: deref, unwrap optional, comptime, nosuspend, resume, optional type. */
typedef struct AstWrap {
	AstNode base;
	AstNode *operand;
} AstWrap;

/* ---------- Suffix expressions ---------- */

typedef struct AstCall {
	AstNode     base;
	AstNode     *callee;
	AstNodeList args;
} AstCall;

typedef struct AstBuiltinCall {
	AstNode     base;
	Span        name;	/* includes the '@' */
	AstNodeList args;
} AstBuiltinCall;

typedef struct AstFieldAccess {
	AstNode base;
	AstNode *lhs;
	Span    name;
} AstFieldAccess;

typedef struct AstIndex {
	AstNode base;
	AstNode *lhs;
	AstNode *index;
} AstIndex;

/* lhs[start..end :sentinel]; end and sentinel may be NULL. */
typedef struct AstSlice {
	AstNode base;
	AstNode *lhs;
	AstNode *start;
	AstNode *end;
	AstNode *sentinel;
} AstSlice;

typedef struct AstFieldInit {
	Span    name;
	AstNode *value;
} AstFieldInit;

/* T{ .a = 1 } / T{ 1, 2 } / .{ ... } (type is NULL) / T{} */
typedef struct AstInitList {
	AstNode      base;
	AstNode      *type;
	bool         is_struct_init;	/* field initializers vs. positional elements */
	AstFieldInit *fields;
	uint32_t     field_count;
	AstNodeList  elements;
} AstInitList;

/* ---------- Types ---------- */

typedef enum AstPointerSize {
	AST_POINTER_ONE,	/* *T */
	AST_POINTER_MANY,	/* [*]T */
	AST_POINTER_SLICE,	/* []T */
	AST_POINTER_C,	/* [*c]T */
} AstPointerSize;

typedef struct AstPointerType {
	AstNode        base;
	AstPointerSize size;
	AstNode        *sentinel;
	AstNode        *align;
	AstNode        *bit_offset;	/* align(a:b:c) */
	AstNode        *host_size;
	AstNode        *addrspace;
	bool           is_const;
	bool           is_volatile;
	bool           is_allowzero;
	AstNode        *child;
} AstPointerType;

typedef struct AstArrayType {
	AstNode base;
	AstNode *length;
	AstNode *sentinel;
	AstNode *child;
} AstArrayType;

/* error_set!payload */
typedef struct AstErrorUnionType {
	AstNode base;
	AstNode *error_set;
	AstNode *payload;
} AstErrorUnionType;

/* anyframe->T */
typedef struct AstAnyframeType {
	AstNode base;
	AstNode *result;
} AstAnyframeType;

typedef struct AstErrorSetDecl {
	AstNode  base;
	Span     *names;
	uint32_t name_count;
} AstErrorSetDecl;

/* ---------- Control flow ---------- */

typedef struct AstBlock {
	AstNode     base;
	AstLabel    label;
	AstNodeList statements;
} AstBlock;

/* if (cond) |capture| then else |else_capture| otherwise */
typedef struct AstIf {
	AstNode    base;
	AstNode    *condition;
	bool       has_capture;
	AstCapture capture;
	AstNode    *then_branch;
	bool       has_else_capture;
	Span       else_capture;
	AstNode    *else_branch;
} AstIf;

typedef struct AstWhile {
	AstNode    base;
	AstLabel   label;
	bool       is_inline;
	AstNode    *condition;
	bool       has_capture;
	AstCapture capture;
	AstNode    *continue_expr;
	AstNode    *body;
	bool       has_else_capture;
	Span       else_capture;
	AstNode    *else_branch;
} AstWhile;

/* One input of a for loop: `expr` or `start..end` (end may be NULL: `0..`). */
typedef struct AstForInput {
	AstNode *expr;
	bool    is_range;
	AstNode *range_end;
} AstForInput;

typedef struct AstFor {
	AstNode        base;
	AstLabel       label;
	bool           is_inline;
	AstForInput    *inputs;
	uint32_t       input_count;
	AstCaptureList captures;
	AstNode        *body;
	AstNode        *else_branch;
} AstFor;

/* One switch case item: `value` or `value...range_end`. */
typedef struct AstSwitchItem {
	AstNode *value;
	AstNode *range_end;
} AstSwitchItem;

typedef struct AstSwitchProng {
	uint32_t       offset;
	bool           is_inline;
	bool           is_else;
	AstSwitchItem  *items;
	uint32_t       item_count;
	bool           has_capture;
	AstCapture     capture;
	bool           has_index_capture;
	Span           index_capture;
	AstNode        *body;
} AstSwitchProng;

typedef struct AstSwitch {
	AstNode        base;
	AstLabel       label;
	AstNode        *operand;
	AstSwitchProng *prongs;
	uint32_t       prong_count;
} AstSwitch;

/* break :label value / continue :label value */
typedef struct AstJump {
	AstNode  base;
	AstLabel label;
	AstNode  *value;
} AstJump;

typedef struct AstReturn {
	AstNode base;
	AstNode *value;
} AstReturn;

/* suspend: body statement */
typedef struct AstSuspend {
	AstNode base;
	AstNode *body;
} AstSuspend;

typedef struct AstAsmOutput {
	Span    symbolic_name;
	Span    constraint;
	bool    returns_type;	/* (-> T) vs (variable) */
	AstNode *type;
	Span    variable;
} AstAsmOutput;

typedef struct AstAsmInput {
	Span    symbolic_name;
	Span    constraint;
	AstNode *value;
} AstAsmInput;

typedef struct AstAsm {
	AstNode      base;
	bool         is_volatile;
	AstNode      *template_expr;
	AstAsmOutput *outputs;
	uint32_t     output_count;
	AstAsmInput  *inputs;
	uint32_t     input_count;
	AstNode      *clobbers;	/* 0.17.0: clobbers are an expression */
} AstAsm;

/* ---------- Statements ---------- */

typedef enum AstAssignOp {
	AST_ASSIGN_PLAIN,
	AST_ASSIGN_MUL,
	AST_ASSIGN_MUL_SAT,
	AST_ASSIGN_DIV,
	AST_ASSIGN_MOD,
	AST_ASSIGN_ADD,
	AST_ASSIGN_ADD_SAT,
	AST_ASSIGN_SUB,
	AST_ASSIGN_SUB_SAT,
	AST_ASSIGN_SHL,
	AST_ASSIGN_SHL_SAT,
	AST_ASSIGN_SHR,
	AST_ASSIGN_BIT_AND,
	AST_ASSIGN_BIT_XOR,
	AST_ASSIGN_BIT_OR,
	AST_ASSIGN_MUL_WRAP,
	AST_ASSIGN_ADD_WRAP,
	AST_ASSIGN_SUB_WRAP,
	AST_ASSIGN_OP_COUNT
} AstAssignOp;

typedef struct AstAssign {
	AstNode     base;
	AstAssignOp op;
	AstNode     *target;
	AstNode     *value;
} AstAssign;

/* a, var b, const c = value; targets are expressions or AstVarDecl without init. */
typedef struct AstAssignDestructure {
	AstNode     base;
	bool        is_comptime;
	AstNodeList targets;
	AstNode     *value;
} AstAssignDestructure;

typedef struct AstDefer {
	AstNode base;
	bool    is_errdefer;
	bool    has_capture;
	Span    capture;
	AstNode *body;
} AstDefer;

/* Checked downcast from AstNode * to a specific node struct. */
#define AST_CAST(Type, node, expected_kind) ((Type *)ast_expect_kind((node), (expected_kind)))

typedef void (*AstChildVisitor)(void *context, AstNode *child);

AstNode    *ast_expect_kind(AstNode *node, AstKind kind);
void        ast_visit_children(AstNode *node, AstChildVisitor visitor, void *context);
const char *ast_kind_name(AstKind kind);
const char *ast_binary_op_name(AstBinaryOp op);
const char *ast_unary_op_name(AstUnaryOp op);
const char *ast_assign_op_name(AstAssignOp op);

#endif
