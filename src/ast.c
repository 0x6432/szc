/* ast.c: AST helpers and the tree dumper used by `szc ast`. */
#include "ast.h"
#include "ast_dump.h"

#include <stdlib.h>

static const char *const kind_names[AST_KIND_COUNT] = {
	[AST_CONTAINER_DECL] = "container_decl",
	[AST_CONTAINER_FIELD] = "container_field",
	[AST_FN_DECL] = "fn_decl",
	[AST_VAR_DECL] = "var_decl",
	[AST_TEST_DECL] = "test_decl",
	[AST_COMPTIME_DECL] = "comptime_decl",
	[AST_IDENTIFIER] = "identifier",
	[AST_NUMBER_LITERAL] = "number_literal",
	[AST_CHAR_LITERAL] = "char_literal",
	[AST_STRING_LITERAL] = "string_literal",
	[AST_MULTILINE_STRING_LITERAL] = "multiline_string_literal",
	[AST_ENUM_LITERAL] = "enum_literal",
	[AST_ERROR_VALUE] = "error_value",
	[AST_UNREACHABLE] = "unreachable",
	[AST_ANYFRAME_LITERAL] = "anyframe",
	[AST_BINARY] = "binary",
	[AST_CATCH] = "catch",
	[AST_UNARY] = "unary",
	[AST_GROUPED] = "grouped",
	[AST_CALL] = "call",
	[AST_BUILTIN_CALL] = "builtin_call",
	[AST_FIELD_ACCESS] = "field_access",
	[AST_DEREF] = "deref",
	[AST_UNWRAP_OPTIONAL] = "unwrap_optional",
	[AST_INDEX] = "index",
	[AST_SLICE] = "slice",
	[AST_INIT_LIST] = "init_list",
	[AST_POINTER_TYPE] = "pointer_type",
	[AST_ARRAY_TYPE] = "array_type",
	[AST_OPTIONAL_TYPE] = "optional_type",
	[AST_ERROR_UNION_TYPE] = "error_union_type",
	[AST_ANYFRAME_TYPE] = "anyframe_type",
	[AST_FN_PROTO] = "fn_proto",
	[AST_ERROR_SET_DECL] = "error_set_decl",
	[AST_BLOCK] = "block",
	[AST_IF] = "if",
	[AST_WHILE] = "while",
	[AST_FOR] = "for",
	[AST_SWITCH] = "switch",
	[AST_BREAK] = "break",
	[AST_CONTINUE] = "continue",
	[AST_RETURN] = "return",
	[AST_COMPTIME] = "comptime",
	[AST_NOSUSPEND] = "nosuspend",
	[AST_SUSPEND] = "suspend",
	[AST_RESUME] = "resume",
	[AST_ASM] = "asm",
	[AST_ASSIGN] = "assign",
	[AST_ASSIGN_DESTRUCTURE] = "assign_destructure",
	[AST_DEFER] = "defer",
};

static const char *const binary_op_names[AST_BINARY_OP_COUNT] = {
	[AST_OP_BOOL_OR] = "or",
	[AST_OP_BOOL_AND] = "and",
	[AST_OP_EQUAL] = "==",
	[AST_OP_NOT_EQUAL] = "!=",
	[AST_OP_LESS] = "<",
	[AST_OP_GREATER] = ">",
	[AST_OP_LESS_EQUAL] = "<=",
	[AST_OP_GREATER_EQUAL] = ">=",
	[AST_OP_BIT_AND] = "&",
	[AST_OP_BIT_XOR] = "^",
	[AST_OP_BIT_OR] = "|",
	[AST_OP_ORELSE] = "orelse",
	[AST_OP_SHL] = "<<",
	[AST_OP_SHR] = ">>",
	[AST_OP_SHL_SAT] = "<<|",
	[AST_OP_ADD] = "+",
	[AST_OP_SUB] = "-",
	[AST_OP_CONCAT] = "++",
	[AST_OP_ADD_WRAP] = "+%",
	[AST_OP_SUB_WRAP] = "-%",
	[AST_OP_ADD_SAT] = "+|",
	[AST_OP_SUB_SAT] = "-|",
	[AST_OP_MERGE_ERROR_SETS] = "||",
	[AST_OP_MUL] = "*",
	[AST_OP_DIV] = "/",
	[AST_OP_MOD] = "%",
	[AST_OP_MUL_WRAP] = "*%",
	[AST_OP_MUL_SAT] = "*|",
};

static const char *const unary_op_names[AST_UNARY_OP_COUNT] = {
	[AST_UNARY_BOOL_NOT] = "!",
	[AST_UNARY_NEGATE] = "-",
	[AST_UNARY_BIT_NOT] = "~",
	[AST_UNARY_NEGATE_WRAP] = "-%",
	[AST_UNARY_ADDRESS_OF] = "&",
	[AST_UNARY_TRY] = "try",
};

static const char *const assign_op_names[AST_ASSIGN_OP_COUNT] = {
	[AST_ASSIGN_PLAIN] = "=",
	[AST_ASSIGN_MUL] = "*=",
	[AST_ASSIGN_MUL_SAT] = "*|=",
	[AST_ASSIGN_DIV] = "/=",
	[AST_ASSIGN_MOD] = "%=",
	[AST_ASSIGN_ADD] = "+=",
	[AST_ASSIGN_ADD_SAT] = "+|=",
	[AST_ASSIGN_SUB] = "-=",
	[AST_ASSIGN_SUB_SAT] = "-|=",
	[AST_ASSIGN_SHL] = "<<=",
	[AST_ASSIGN_SHL_SAT] = "<<|=",
	[AST_ASSIGN_SHR] = ">>=",
	[AST_ASSIGN_BIT_AND] = "&=",
	[AST_ASSIGN_BIT_XOR] = "^=",
	[AST_ASSIGN_BIT_OR] = "|=",
	[AST_ASSIGN_MUL_WRAP] = "*%=",
	[AST_ASSIGN_ADD_WRAP] = "+%=",
	[AST_ASSIGN_SUB_WRAP] = "-%=",
};

const char *
ast_kind_name(AstKind kind)
{
	if (kind >= AST_KIND_COUNT)
		return "?";
	return kind_names[kind];
}

const char *
ast_binary_op_name(AstBinaryOp op)
{
	if (op >= AST_BINARY_OP_COUNT)
		return "?";
	return binary_op_names[op];
}

const char *
ast_unary_op_name(AstUnaryOp op)
{
	if (op >= AST_UNARY_OP_COUNT)
		return "?";
	return unary_op_names[op];
}

const char *
ast_assign_op_name(AstAssignOp op)
{
	if (op >= AST_ASSIGN_OP_COUNT)
		return "?";
	return assign_op_names[op];
}

AstNode *
ast_expect_kind(AstNode *node, AstKind kind)
{
	if (node == NULL || node->kind != kind) {
		fprintf(stderr, "szc: internal error: expected AST node %s, got %s\n", ast_kind_name(kind), node == NULL ? "NULL" : ast_kind_name(node->kind));
		abort();
	}
	return node;
}

/* ---------- Dumper ---------- */

typedef struct Dumper {
	FILE             *out;
	const SourceFile *file;
} Dumper;

static void dump_node(const Dumper *dumper, const AstNode *node, uint32_t depth, const char *label);

static void
indent(const Dumper *dumper, uint32_t depth)
{
	for (uint32_t i = 0; i < depth; i++)
		fputs("  ", dumper->out);
}

static void
print_span(const Dumper *dumper, Span span)
{
	fprintf(dumper->out, "%.*s", (int)(span.end - span.start), dumper->file->text + span.start);
}

/* Prints "<indent>label: text" on its own line. */
static void
dump_span_line(const Dumper *dumper, uint32_t depth, const char *label, Span span)
{
	indent(dumper, depth);
	fprintf(dumper->out, "%s: ", label);
	print_span(dumper, span);
	fputc('\n', dumper->out);
}

static void
dump_flag(const Dumper *dumper, const char *name, bool value)
{
	if (value)
		fprintf(dumper->out, " %s", name);
}

static void
dump_list(const Dumper *dumper, const AstNodeList *list, uint32_t depth, const char *label)
{
	for (uint32_t i = 0; i < list->count; i++)
		dump_node(dumper, list->items[i], depth, label);
}

static void
dump_label(const Dumper *dumper, AstLabel label)
{
	if (!label.has_label)
		return;
	fputs(" label=", dumper->out);
	print_span(dumper, label.name);
}

static void
dump_capture(const Dumper *dumper, uint32_t depth, const char *label, AstCapture capture)
{
	indent(dumper, depth);
	fprintf(dumper->out, "%s: %s", label, capture.by_pointer ? "*" : "");
	print_span(dumper, capture.name);
	fputc('\n', dumper->out);
}

static const char *
container_kind_name(AstContainerKind kind)
{
	switch (kind) {
	case AST_CONTAINER_STRUCT:
		return "struct";
	case AST_CONTAINER_ENUM:
		return "enum";
	case AST_CONTAINER_UNION:
		return "union";
	case AST_CONTAINER_OPAQUE:
		return "opaque";
	}
	return "?";
}

static const char *
layout_name(AstContainerLayout layout)
{
	switch (layout) {
	case AST_LAYOUT_AUTO:
		return "";
	case AST_LAYOUT_EXTERN:
		return " extern";
	case AST_LAYOUT_PACKED:
		return " packed";
	}
	return "?";
}

static const char *
fn_modifier_name(AstFnModifier modifier)
{
	switch (modifier) {
	case AST_FN_MODIFIER_NONE:
		return "";
	case AST_FN_MODIFIER_EXPORT:
		return " export";
	case AST_FN_MODIFIER_EXTERN:
		return " extern";
	case AST_FN_MODIFIER_INLINE:
		return " inline";
	case AST_FN_MODIFIER_NOINLINE:
		return " noinline";
	}
	return "?";
}

static const char *
var_modifier_name(AstVarModifier modifier)
{
	switch (modifier) {
	case AST_VAR_MODIFIER_NONE:
		return "";
	case AST_VAR_MODIFIER_EXPORT:
		return " export";
	case AST_VAR_MODIFIER_EXTERN:
		return " extern";
	}
	return "?";
}

static const char *
pointer_size_name(AstPointerSize size)
{
	switch (size) {
	case AST_POINTER_ONE:
		return "one";
	case AST_POINTER_MANY:
		return "many";
	case AST_POINTER_SLICE:
		return "slice";
	case AST_POINTER_C:
		return "c";
	}
	return "?";
}

static void
dump_fn_proto(const Dumper *dumper, const AstFnProto *proto, uint32_t depth)
{
	if (proto->has_name) {
		fputc(' ', dumper->out);
		print_span(dumper, proto->name);
	}
	dump_flag(dumper, "var_args", proto->is_var_args);
	dump_flag(dumper, "inferred_error", proto->inferred_error);
	fputc('\n', dumper->out);
	for (uint32_t i = 0; i < proto->param_count; i++) {
		const AstParam *param = &proto->params[i];
		indent(dumper, depth + 1);
		fputs("param", dumper->out);
		if (param->kind == AST_PARAM_COMPTIME)
			fputs(" comptime", dumper->out);
		if (param->kind == AST_PARAM_NOALIAS)
			fputs(" noalias", dumper->out);
		if (param->has_name) {
			fputc(' ', dumper->out);
			print_span(dumper, param->name);
		}
		if (param->is_anytype)
			fputs(" anytype", dumper->out);
		fputc('\n', dumper->out);
		dump_node(dumper, param->type, depth + 2, "type");
	}
	dump_node(dumper, proto->align, depth + 1, "align");
	dump_node(dumper, proto->addrspace, depth + 1, "addrspace");
	dump_node(dumper, proto->linksection, depth + 1, "linksection");
	dump_node(dumper, proto->callconv, depth + 1, "callconv");
	dump_node(dumper, proto->return_type, depth + 1, "return");
}

/* Prints the header line's extra fields for `node` and then its children at depth + 1. */
static void
dump_body(const Dumper *dumper, const AstNode *node, uint32_t depth)
{
	AstNode *mutable_node = (AstNode *)node;
	uint32_t child = depth + 1;

	switch (node->kind) {
	case AST_CONTAINER_DECL: {
		AstContainerDecl *decl = AST_CAST(AstContainerDecl, mutable_node, AST_CONTAINER_DECL);
		fprintf(dumper->out, "%s %s", layout_name(decl->layout), container_kind_name(decl->container_kind));
		dump_flag(dumper, "enum_tag", decl->union_enum_tag);
		fputc('\n', dumper->out);
		dump_node(dumper, decl->arg, child, "arg");
		dump_list(dumper, &decl->members, child, NULL);
		return;
	}
	case AST_CONTAINER_FIELD: {
		AstContainerField *field = AST_CAST(AstContainerField, mutable_node, AST_CONTAINER_FIELD);
		if (field->has_name) {
			fputc(' ', dumper->out);
			print_span(dumper, field->name);
		}
		dump_flag(dumper, "comptime", field->is_comptime);
		fputc('\n', dumper->out);
		dump_node(dumper, field->type, child, "type");
		dump_node(dumper, field->align, child, "align");
		dump_node(dumper, field->default_value, child, "value");
		return;
	}
	case AST_FN_DECL: {
		AstFnDecl *decl = AST_CAST(AstFnDecl, mutable_node, AST_FN_DECL);
		dump_flag(dumper, "pub", decl->is_pub);
		fputs(fn_modifier_name(decl->modifier), dumper->out);
		if (decl->has_extern_lib) {
			fputs(" lib=", dumper->out);
			print_span(dumper, decl->extern_lib);
		}
		fputc('\n', dumper->out);
		dump_node(dumper, &decl->proto->base, child, "proto");
		dump_node(dumper, decl->body, child, "body");
		return;
	}
	case AST_VAR_DECL: {
		AstVarDecl *decl = AST_CAST(AstVarDecl, mutable_node, AST_VAR_DECL);
		dump_flag(dumper, "pub", decl->is_pub);
		fputs(var_modifier_name(decl->modifier), dumper->out);
		if (decl->has_extern_lib) {
			fputs(" lib=", dumper->out);
			print_span(dumper, decl->extern_lib);
		}
		dump_flag(dumper, "threadlocal", decl->is_threadlocal);
		dump_flag(dumper, "comptime", decl->is_comptime);
		fputs(decl->is_const ? " const " : " var ", dumper->out);
		print_span(dumper, decl->name);
		fputc('\n', dumper->out);
		dump_node(dumper, decl->type, child, "type");
		dump_node(dumper, decl->align, child, "align");
		dump_node(dumper, decl->addrspace, child, "addrspace");
		dump_node(dumper, decl->linksection, child, "linksection");
		dump_node(dumper, decl->init, child, "init");
		return;
	}
	case AST_TEST_DECL: {
		AstTestDecl *decl = AST_CAST(AstTestDecl, mutable_node, AST_TEST_DECL);
		if (decl->has_name) {
			fputc(' ', dumper->out);
			print_span(dumper, decl->name);
		}
		fputc('\n', dumper->out);
		dump_node(dumper, decl->body, child, NULL);
		return;
	}
	case AST_COMPTIME_DECL: {
		AstComptimeDecl *decl = AST_CAST(AstComptimeDecl, mutable_node, AST_COMPTIME_DECL);
		fputc('\n', dumper->out);
		dump_node(dumper, decl->body, child, NULL);
		return;
	}
	case AST_IDENTIFIER: {
		AstIdentifier *identifier = AST_CAST(AstIdentifier, mutable_node, AST_IDENTIFIER);
		fputc(' ', dumper->out);
		print_span(dumper, identifier->name);
		fputc('\n', dumper->out);
		return;
	}
	case AST_NUMBER_LITERAL:
	case AST_CHAR_LITERAL:
	case AST_STRING_LITERAL: {
		AstLiteral *literal = (AstLiteral *)mutable_node;
		fputc(' ', dumper->out);
		print_span(dumper, literal->text);
		fputc('\n', dumper->out);
		return;
	}
	case AST_MULTILINE_STRING_LITERAL: {
		AstMultilineString *string = AST_CAST(AstMultilineString, mutable_node, AST_MULTILINE_STRING_LITERAL);
		fputc('\n', dumper->out);
		for (uint32_t i = 0; i < string->line_count; i++)
			dump_span_line(dumper, child, "line", string->lines[i]);
		return;
	}
	case AST_ENUM_LITERAL:
	case AST_ERROR_VALUE: {
		AstNamedValue *value = (AstNamedValue *)mutable_node;
		fputc(' ', dumper->out);
		print_span(dumper, value->name);
		fputc('\n', dumper->out);
		return;
	}
	case AST_UNREACHABLE:
	case AST_ANYFRAME_LITERAL:
		fputc('\n', dumper->out);
		return;
	case AST_BINARY: {
		AstBinary *binary = AST_CAST(AstBinary, mutable_node, AST_BINARY);
		fprintf(dumper->out, " %s\n", ast_binary_op_name(binary->op));
		dump_node(dumper, binary->lhs, child, NULL);
		dump_node(dumper, binary->rhs, child, NULL);
		return;
	}
	case AST_CATCH: {
		AstCatch *catch_node = AST_CAST(AstCatch, mutable_node, AST_CATCH);
		if (catch_node->has_capture) {
			fputs(" |", dumper->out);
			print_span(dumper, catch_node->capture);
			fputc('|', dumper->out);
		}
		fputc('\n', dumper->out);
		dump_node(dumper, catch_node->lhs, child, NULL);
		dump_node(dumper, catch_node->rhs, child, NULL);
		return;
	}
	case AST_UNARY: {
		AstUnary *unary = AST_CAST(AstUnary, mutable_node, AST_UNARY);
		fprintf(dumper->out, " %s\n", ast_unary_op_name(unary->op));
		dump_node(dumper, unary->operand, child, NULL);
		return;
	}
	case AST_GROUPED:
	case AST_DEREF:
	case AST_UNWRAP_OPTIONAL:
	case AST_OPTIONAL_TYPE:
	case AST_COMPTIME:
	case AST_NOSUSPEND:
	case AST_RESUME: {
		AstWrap *wrap = (AstWrap *)mutable_node;
		fputc('\n', dumper->out);
		dump_node(dumper, wrap->operand, child, NULL);
		return;
	}
	case AST_CALL: {
		AstCall *call = AST_CAST(AstCall, mutable_node, AST_CALL);
		fputc('\n', dumper->out);
		dump_node(dumper, call->callee, child, "callee");
		dump_list(dumper, &call->args, child, "arg");
		return;
	}
	case AST_BUILTIN_CALL: {
		AstBuiltinCall *call = AST_CAST(AstBuiltinCall, mutable_node, AST_BUILTIN_CALL);
		fputc(' ', dumper->out);
		print_span(dumper, call->name);
		fputc('\n', dumper->out);
		dump_list(dumper, &call->args, child, "arg");
		return;
	}
	case AST_FIELD_ACCESS: {
		AstFieldAccess *access = AST_CAST(AstFieldAccess, mutable_node, AST_FIELD_ACCESS);
		fputc(' ', dumper->out);
		print_span(dumper, access->name);
		fputc('\n', dumper->out);
		dump_node(dumper, access->lhs, child, NULL);
		return;
	}
	case AST_INDEX: {
		AstIndex *index = AST_CAST(AstIndex, mutable_node, AST_INDEX);
		fputc('\n', dumper->out);
		dump_node(dumper, index->lhs, child, NULL);
		dump_node(dumper, index->index, child, "index");
		return;
	}
	case AST_SLICE: {
		AstSlice *slice = AST_CAST(AstSlice, mutable_node, AST_SLICE);
		fputc('\n', dumper->out);
		dump_node(dumper, slice->lhs, child, NULL);
		dump_node(dumper, slice->start, child, "start");
		dump_node(dumper, slice->end, child, "end");
		dump_node(dumper, slice->sentinel, child, "sentinel");
		return;
	}
	case AST_INIT_LIST: {
		AstInitList *init = AST_CAST(AstInitList, mutable_node, AST_INIT_LIST);
		fputs(init->is_struct_init ? " struct" : " array", dumper->out);
		fputc('\n', dumper->out);
		dump_node(dumper, init->type, child, "type");
		for (uint32_t i = 0; i < init->field_count; i++) {
			dump_span_line(dumper, child, "field", init->fields[i].name);
			dump_node(dumper, init->fields[i].value, child + 1, NULL);
		}
		dump_list(dumper, &init->elements, child, NULL);
		return;
	}
	case AST_POINTER_TYPE: {
		AstPointerType *pointer = AST_CAST(AstPointerType, mutable_node, AST_POINTER_TYPE);
		fprintf(dumper->out, " %s", pointer_size_name(pointer->size));
		dump_flag(dumper, "const", pointer->is_const);
		dump_flag(dumper, "volatile", pointer->is_volatile);
		dump_flag(dumper, "allowzero", pointer->is_allowzero);
		fputc('\n', dumper->out);
		dump_node(dumper, pointer->sentinel, child, "sentinel");
		dump_node(dumper, pointer->align, child, "align");
		dump_node(dumper, pointer->bit_offset, child, "bit_offset");
		dump_node(dumper, pointer->host_size, child, "host_size");
		dump_node(dumper, pointer->addrspace, child, "addrspace");
		dump_node(dumper, pointer->child, child, NULL);
		return;
	}
	case AST_ARRAY_TYPE: {
		AstArrayType *array = AST_CAST(AstArrayType, mutable_node, AST_ARRAY_TYPE);
		fputc('\n', dumper->out);
		dump_node(dumper, array->length, child, "length");
		dump_node(dumper, array->sentinel, child, "sentinel");
		dump_node(dumper, array->child, child, NULL);
		return;
	}
	case AST_ERROR_UNION_TYPE: {
		AstErrorUnionType *error_union = AST_CAST(AstErrorUnionType, mutable_node, AST_ERROR_UNION_TYPE);
		fputc('\n', dumper->out);
		dump_node(dumper, error_union->error_set, child, "error_set");
		dump_node(dumper, error_union->payload, child, "payload");
		return;
	}
	case AST_ANYFRAME_TYPE: {
		AstAnyframeType *anyframe = AST_CAST(AstAnyframeType, mutable_node, AST_ANYFRAME_TYPE);
		fputc('\n', dumper->out);
		dump_node(dumper, anyframe->result, child, NULL);
		return;
	}
	case AST_FN_PROTO:
		dump_fn_proto(dumper, AST_CAST(AstFnProto, mutable_node, AST_FN_PROTO), depth);
		return;
	case AST_ERROR_SET_DECL: {
		AstErrorSetDecl *set = AST_CAST(AstErrorSetDecl, mutable_node, AST_ERROR_SET_DECL);
		fputc('\n', dumper->out);
		for (uint32_t i = 0; i < set->name_count; i++)
			dump_span_line(dumper, child, "name", set->names[i]);
		return;
	}
	case AST_BLOCK: {
		AstBlock *block = AST_CAST(AstBlock, mutable_node, AST_BLOCK);
		dump_label(dumper, block->label);
		fputc('\n', dumper->out);
		dump_list(dumper, &block->statements, child, NULL);
		return;
	}
	case AST_IF: {
		AstIf *if_node = AST_CAST(AstIf, mutable_node, AST_IF);
		fputc('\n', dumper->out);
		dump_node(dumper, if_node->condition, child, "condition");
		if (if_node->has_capture)
			dump_capture(dumper, child, "capture", if_node->capture);
		dump_node(dumper, if_node->then_branch, child, "then");
		if (if_node->has_else_capture)
			dump_span_line(dumper, child, "else_capture", if_node->else_capture);
		dump_node(dumper, if_node->else_branch, child, "else");
		return;
	}
	case AST_WHILE: {
		AstWhile *while_node = AST_CAST(AstWhile, mutable_node, AST_WHILE);
		dump_label(dumper, while_node->label);
		dump_flag(dumper, "inline", while_node->is_inline);
		fputc('\n', dumper->out);
		dump_node(dumper, while_node->condition, child, "condition");
		if (while_node->has_capture)
			dump_capture(dumper, child, "capture", while_node->capture);
		dump_node(dumper, while_node->continue_expr, child, "continue");
		dump_node(dumper, while_node->body, child, "body");
		if (while_node->has_else_capture)
			dump_span_line(dumper, child, "else_capture", while_node->else_capture);
		dump_node(dumper, while_node->else_branch, child, "else");
		return;
	}
	case AST_FOR: {
		AstFor *for_node = AST_CAST(AstFor, mutable_node, AST_FOR);
		dump_label(dumper, for_node->label);
		dump_flag(dumper, "inline", for_node->is_inline);
		fputc('\n', dumper->out);
		for (uint32_t i = 0; i < for_node->input_count; i++) {
			const AstForInput *input = &for_node->inputs[i];
			dump_node(dumper, input->expr, child, input->is_range ? "range_start" : "input");
			if (input->is_range)
				dump_node(dumper, input->range_end, child, "range_end");
		}
		for (uint32_t i = 0; i < for_node->captures.count; i++)
			dump_capture(dumper, child, "capture", for_node->captures.items[i]);
		dump_node(dumper, for_node->body, child, "body");
		dump_node(dumper, for_node->else_branch, child, "else");
		return;
	}
	case AST_SWITCH: {
		AstSwitch *switch_node = AST_CAST(AstSwitch, mutable_node, AST_SWITCH);
		dump_label(dumper, switch_node->label);
		fputc('\n', dumper->out);
		dump_node(dumper, switch_node->operand, child, "operand");
		for (uint32_t i = 0; i < switch_node->prong_count; i++) {
			const AstSwitchProng *prong = &switch_node->prongs[i];
			indent(dumper, child);
			fputs("prong", dumper->out);
			dump_flag(dumper, "inline", prong->is_inline);
			dump_flag(dumper, "else", prong->is_else);
			fputc('\n', dumper->out);
			for (uint32_t j = 0; j < prong->item_count; j++) {
				dump_node(dumper, prong->items[j].value, child + 1, "item");
				dump_node(dumper, prong->items[j].range_end, child + 1, "range_end");
			}
			if (prong->has_capture)
				dump_capture(dumper, child + 1, "capture", prong->capture);
			if (prong->has_index_capture)
				dump_span_line(dumper, child + 1, "index_capture", prong->index_capture);
			dump_node(dumper, prong->body, child + 1, "body");
		}
		return;
	}
	case AST_BREAK:
	case AST_CONTINUE: {
		AstJump *jump = (AstJump *)mutable_node;
		dump_label(dumper, jump->label);
		fputc('\n', dumper->out);
		dump_node(dumper, jump->value, child, NULL);
		return;
	}
	case AST_RETURN: {
		AstReturn *return_node = AST_CAST(AstReturn, mutable_node, AST_RETURN);
		fputc('\n', dumper->out);
		dump_node(dumper, return_node->value, child, NULL);
		return;
	}
	case AST_SUSPEND: {
		AstSuspend *suspend = AST_CAST(AstSuspend, mutable_node, AST_SUSPEND);
		fputc('\n', dumper->out);
		dump_node(dumper, suspend->body, child, NULL);
		return;
	}
	case AST_ASM: {
		AstAsm *asm_node = AST_CAST(AstAsm, mutable_node, AST_ASM);
		dump_flag(dumper, "volatile", asm_node->is_volatile);
		fputc('\n', dumper->out);
		dump_node(dumper, asm_node->template_expr, child, "template");
		for (uint32_t i = 0; i < asm_node->output_count; i++) {
			const AstAsmOutput *output = &asm_node->outputs[i];
			indent(dumper, child);
			fputs("output [", dumper->out);
			print_span(dumper, output->symbolic_name);
			fputs("] ", dumper->out);
			print_span(dumper, output->constraint);
			if (output->returns_type) {
				fputs(" ->\n", dumper->out);
				dump_node(dumper, output->type, child + 1, NULL);
			} else {
				fputc(' ', dumper->out);
				print_span(dumper, output->variable);
				fputc('\n', dumper->out);
			}
		}
		for (uint32_t i = 0; i < asm_node->input_count; i++) {
			const AstAsmInput *input = &asm_node->inputs[i];
			indent(dumper, child);
			fputs("input [", dumper->out);
			print_span(dumper, input->symbolic_name);
			fputs("] ", dumper->out);
			print_span(dumper, input->constraint);
			fputc('\n', dumper->out);
			dump_node(dumper, input->value, child + 1, NULL);
		}
		dump_node(dumper, asm_node->clobbers, child, "clobbers");
		return;
	}
	case AST_ASSIGN: {
		AstAssign *assign = AST_CAST(AstAssign, mutable_node, AST_ASSIGN);
		fprintf(dumper->out, " %s\n", ast_assign_op_name(assign->op));
		dump_node(dumper, assign->target, child, NULL);
		dump_node(dumper, assign->value, child, NULL);
		return;
	}
	case AST_ASSIGN_DESTRUCTURE: {
		AstAssignDestructure *assign = AST_CAST(AstAssignDestructure, mutable_node, AST_ASSIGN_DESTRUCTURE);
		dump_flag(dumper, "comptime", assign->is_comptime);
		fputc('\n', dumper->out);
		dump_list(dumper, &assign->targets, child, "target");
		dump_node(dumper, assign->value, child, "value");
		return;
	}
	case AST_DEFER: {
		AstDefer *defer = AST_CAST(AstDefer, mutable_node, AST_DEFER);
		dump_flag(dumper, "errdefer", defer->is_errdefer);
		fputc('\n', dumper->out);
		dump_node(dumper, defer->body, child, NULL);
		return;
	}
	case AST_KIND_COUNT:
		break;
	}
	fputs(" ?\n", dumper->out);
}

static void
dump_node(const Dumper *dumper, const AstNode *node, uint32_t depth, const char *label)
{
	if (node == NULL)
		return;
	indent(dumper, depth);
	if (label != NULL)
		fprintf(dumper->out, "%s: ", label);
	fputs(ast_kind_name(node->kind), dumper->out);
	dump_body(dumper, node, depth);
}

void
ast_dump(FILE *out, const SourceFile *file, const AstNode *root)
{
	Dumper dumper;
	dumper.out = out;
	dumper.file = file;
	dump_node(&dumper, root, 0, NULL);
}

/* ---------- Child traversal ---------- */

static void
visit(AstChildVisitor visitor, void *context, AstNode *child)
{
	if (child != NULL)
		visitor(context, child);
}

static void
visit_list(AstChildVisitor visitor, void *context, const AstNodeList *list)
{
	for (uint32_t i = 0; i < list->count; i++)
		visit(visitor, context, list->items[i]);
}

static void
visit_fn_proto(AstChildVisitor visitor, void *context, AstFnProto *proto)
{
	for (uint32_t i = 0; i < proto->param_count; i++)
		visit(visitor, context, proto->params[i].type);
	visit(visitor, context, proto->align);
	visit(visitor, context, proto->addrspace);
	visit(visitor, context, proto->linksection);
	visit(visitor, context, proto->callconv);
	visit(visitor, context, proto->return_type);
}

void
ast_visit_children(AstNode *node, AstChildVisitor visitor, void *context)
{
	switch (node->kind) {
	case AST_CONTAINER_DECL: {
		AstContainerDecl *decl = AST_CAST(AstContainerDecl, node, AST_CONTAINER_DECL);
		visit(visitor, context, decl->arg);
		visit_list(visitor, context, &decl->members);
		break;
	}
	case AST_CONTAINER_FIELD: {
		AstContainerField *field = AST_CAST(AstContainerField, node, AST_CONTAINER_FIELD);
		visit(visitor, context, field->type);
		visit(visitor, context, field->align);
		visit(visitor, context, field->default_value);
		break;
	}
	case AST_FN_DECL: {
		AstFnDecl *decl = AST_CAST(AstFnDecl, node, AST_FN_DECL);
		visit(visitor, context, &decl->proto->base);
		visit(visitor, context, decl->body);
		break;
	}
	case AST_VAR_DECL: {
		AstVarDecl *decl = AST_CAST(AstVarDecl, node, AST_VAR_DECL);
		visit(visitor, context, decl->type);
		visit(visitor, context, decl->align);
		visit(visitor, context, decl->addrspace);
		visit(visitor, context, decl->linksection);
		visit(visitor, context, decl->init);
		break;
	}
	case AST_TEST_DECL:
		visit(visitor, context, AST_CAST(AstTestDecl, node, AST_TEST_DECL)->body);
		break;
	case AST_COMPTIME_DECL:
		visit(visitor, context, AST_CAST(AstComptimeDecl, node, AST_COMPTIME_DECL)->body);
		break;
	case AST_IDENTIFIER:
	case AST_NUMBER_LITERAL:
	case AST_CHAR_LITERAL:
	case AST_STRING_LITERAL:
	case AST_MULTILINE_STRING_LITERAL:
	case AST_ENUM_LITERAL:
	case AST_ERROR_VALUE:
	case AST_UNREACHABLE:
	case AST_ANYFRAME_LITERAL:
	case AST_ERROR_SET_DECL:
		break;
	case AST_BINARY: {
		AstBinary *binary = AST_CAST(AstBinary, node, AST_BINARY);
		visit(visitor, context, binary->lhs);
		visit(visitor, context, binary->rhs);
		break;
	}
	case AST_CATCH: {
		AstCatch *catch_node = AST_CAST(AstCatch, node, AST_CATCH);
		visit(visitor, context, catch_node->lhs);
		visit(visitor, context, catch_node->rhs);
		break;
	}
	case AST_UNARY:
		visit(visitor, context, AST_CAST(AstUnary, node, AST_UNARY)->operand);
		break;
	case AST_GROUPED:
	case AST_DEREF:
	case AST_UNWRAP_OPTIONAL:
	case AST_OPTIONAL_TYPE:
	case AST_COMPTIME:
	case AST_NOSUSPEND:
	case AST_RESUME:
		visit(visitor, context, AST_CAST(AstWrap, node, node->kind)->operand);
		break;
	case AST_CALL: {
		AstCall *call = AST_CAST(AstCall, node, AST_CALL);
		visit(visitor, context, call->callee);
		visit_list(visitor, context, &call->args);
		break;
	}
	case AST_BUILTIN_CALL:
		visit_list(visitor, context, &AST_CAST(AstBuiltinCall, node, AST_BUILTIN_CALL)->args);
		break;
	case AST_FIELD_ACCESS:
		visit(visitor, context, AST_CAST(AstFieldAccess, node, AST_FIELD_ACCESS)->lhs);
		break;
	case AST_INDEX: {
		AstIndex *index = AST_CAST(AstIndex, node, AST_INDEX);
		visit(visitor, context, index->lhs);
		visit(visitor, context, index->index);
		break;
	}
	case AST_SLICE: {
		AstSlice *slice = AST_CAST(AstSlice, node, AST_SLICE);
		visit(visitor, context, slice->lhs);
		visit(visitor, context, slice->start);
		visit(visitor, context, slice->end);
		visit(visitor, context, slice->sentinel);
		break;
	}
	case AST_INIT_LIST: {
		AstInitList *init = AST_CAST(AstInitList, node, AST_INIT_LIST);
		visit(visitor, context, init->type);
		for (uint32_t i = 0; i < init->field_count; i++)
			visit(visitor, context, init->fields[i].value);
		visit_list(visitor, context, &init->elements);
		break;
	}
	case AST_POINTER_TYPE: {
		AstPointerType *pointer = AST_CAST(AstPointerType, node, AST_POINTER_TYPE);
		visit(visitor, context, pointer->sentinel);
		visit(visitor, context, pointer->align);
		visit(visitor, context, pointer->bit_offset);
		visit(visitor, context, pointer->host_size);
		visit(visitor, context, pointer->addrspace);
		visit(visitor, context, pointer->child);
		break;
	}
	case AST_ARRAY_TYPE: {
		AstArrayType *array = AST_CAST(AstArrayType, node, AST_ARRAY_TYPE);
		visit(visitor, context, array->length);
		visit(visitor, context, array->sentinel);
		visit(visitor, context, array->child);
		break;
	}
	case AST_ERROR_UNION_TYPE: {
		AstErrorUnionType *error_union = AST_CAST(AstErrorUnionType, node, AST_ERROR_UNION_TYPE);
		visit(visitor, context, error_union->error_set);
		visit(visitor, context, error_union->payload);
		break;
	}
	case AST_ANYFRAME_TYPE:
		visit(visitor, context, AST_CAST(AstAnyframeType, node, AST_ANYFRAME_TYPE)->result);
		break;
	case AST_FN_PROTO:
		visit_fn_proto(visitor, context, AST_CAST(AstFnProto, node, AST_FN_PROTO));
		break;
	case AST_BLOCK:
		visit_list(visitor, context, &AST_CAST(AstBlock, node, AST_BLOCK)->statements);
		break;
	case AST_IF: {
		AstIf *if_node = AST_CAST(AstIf, node, AST_IF);
		visit(visitor, context, if_node->condition);
		visit(visitor, context, if_node->then_branch);
		visit(visitor, context, if_node->else_branch);
		break;
	}
	case AST_WHILE: {
		AstWhile *while_node = AST_CAST(AstWhile, node, AST_WHILE);
		visit(visitor, context, while_node->condition);
		visit(visitor, context, while_node->continue_expr);
		visit(visitor, context, while_node->body);
		visit(visitor, context, while_node->else_branch);
		break;
	}
	case AST_FOR: {
		AstFor *for_node = AST_CAST(AstFor, node, AST_FOR);
		for (uint32_t i = 0; i < for_node->input_count; i++) {
			visit(visitor, context, for_node->inputs[i].expr);
			visit(visitor, context, for_node->inputs[i].range_end);
		}
		visit(visitor, context, for_node->body);
		visit(visitor, context, for_node->else_branch);
		break;
	}
	case AST_SWITCH: {
		AstSwitch *switch_node = AST_CAST(AstSwitch, node, AST_SWITCH);
		visit(visitor, context, switch_node->operand);
		for (uint32_t i = 0; i < switch_node->prong_count; i++) {
			AstSwitchProng *prong = &switch_node->prongs[i];
			for (uint32_t j = 0; j < prong->item_count; j++) {
				visit(visitor, context, prong->items[j].value);
				visit(visitor, context, prong->items[j].range_end);
			}
			visit(visitor, context, prong->body);
		}
		break;
	}
	case AST_BREAK:
	case AST_CONTINUE:
		visit(visitor, context, AST_CAST(AstJump, node, node->kind)->value);
		break;
	case AST_RETURN:
		visit(visitor, context, AST_CAST(AstReturn, node, AST_RETURN)->value);
		break;
	case AST_SUSPEND:
		visit(visitor, context, AST_CAST(AstSuspend, node, AST_SUSPEND)->body);
		break;
	case AST_ASM: {
		AstAsm *asm_node = AST_CAST(AstAsm, node, AST_ASM);
		visit(visitor, context, asm_node->template_expr);
		for (uint32_t i = 0; i < asm_node->output_count; i++)
			visit(visitor, context, asm_node->outputs[i].type);
		for (uint32_t i = 0; i < asm_node->input_count; i++)
			visit(visitor, context, asm_node->inputs[i].value);
		visit(visitor, context, asm_node->clobbers);
		break;
	}
	case AST_ASSIGN: {
		AstAssign *assign = AST_CAST(AstAssign, node, AST_ASSIGN);
		visit(visitor, context, assign->target);
		visit(visitor, context, assign->value);
		break;
	}
	case AST_ASSIGN_DESTRUCTURE: {
		AstAssignDestructure *assign = AST_CAST(AstAssignDestructure, node, AST_ASSIGN_DESTRUCTURE);
		visit_list(visitor, context, &assign->targets);
		visit(visitor, context, assign->value);
		break;
	}
	case AST_DEFER:
		visit(visitor, context, AST_CAST(AstDefer, node, AST_DEFER)->body);
		break;
	case AST_KIND_COUNT:
		break;
	}
}
