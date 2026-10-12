/*
 * parser.c: recursive descent parser for Zig 0.17.0.
 *
 * Each parse function starts with the PEG rule it implements, copied from
 * docs/grammar.peg. Binary operators (BoolOrExpr ... MultiplyExpr) use one Pratt
 * (precedence climbing) function driven by `binary_operators`.
 *
 * Tokens come from the lexer one at a time and go through a small lookahead ring
 * buffer. Doc comments are dropped when fetched (they don't affect the meaning).
 *
 * Error handling: a syntax error is reported to Diag and then the parser
 * longjmps to the nearest recovery point (container member or block statement). That point
 * skips ahead to a likely boundary and parsing continues. All memory comes from
 * the arena, so unwinding with longjmp cannot leak.
 */
#include "parser.h"

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"

#define LOOKAHEAD_SIZE 4
#define MAX_NESTING 1024

typedef struct Parser {
	Arena      *arena;
	Diag       *diag;
	FileId     file;
	const char *source;
	uint32_t   source_length;
	Lexer      lexer;
	Token      lookahead[LOOKAHEAD_SIZE];
	uint32_t   lookahead_first;
	uint32_t   lookahead_count;
	Token      previous;	/* last consumed token */
	bool       has_previous;	/* false until the first token is consumed */
	jmp_buf    *recovery;	/* innermost recovery point */
	uint32_t   nesting;
} Parser;

/* ---------- Growable arrays (arena-backed) ---------- */

typedef struct NodeBuilder {
	AstNode  **items;
	uint32_t count;
	uint32_t capacity;
} NodeBuilder;

static void
node_builder_push(Parser *parser, NodeBuilder *builder, AstNode *node)
{
	if (builder->count == builder->capacity) {
		uint32_t new_capacity = builder->capacity == 0 ? 4 : builder->capacity * 2;
		builder->items = arena_grow_array(parser->arena, builder->items, builder->count, new_capacity, sizeof(AstNode *), alignof(AstNode *));
		builder->capacity = new_capacity;
	}
	builder->items[builder->count] = node;
	builder->count = builder->count + 1;
}

static AstNodeList
node_builder_finish(NodeBuilder *builder)
{
	AstNodeList list;
	list.items = builder->items;
	list.count = builder->count;
	return list;
}

/*
 * Grows a typed array by one element and returns a pointer to the zeroed new slot.
 * `items`, `count` and `capacity` are updated in place.
 */
static void *
grow_one(Parser *parser, void **items, uint32_t *count, uint32_t *capacity, size_t elem_size, size_t align)
{
	if (*count == *capacity) {
		uint32_t new_capacity = *capacity == 0 ? 4 : *capacity * 2;
		*items = arena_grow_array(parser->arena, *items, *count, new_capacity, elem_size, align);
		*capacity = new_capacity;
	}
	unsigned char *slot = (unsigned char *)*items + (size_t)*count * elem_size;
	memset(slot, 0, elem_size);
	*count = *count + 1;
	return slot;
}

#define GROW_ONE(parser, items, count, capacity, Type) \
	((Type *)grow_one((parser), (void **)&(items), &(count), &(capacity), sizeof(Type), alignof(Type)))

/* ---------- Token access ---------- */

static Token
fetch_token(Parser *parser)
{
	for (;;) {
		Token token = lexer_next(&parser->lexer);
		if (token.kind == TK_INVALID) {
			diag_report(parser->diag, DIAG_ERROR, parser->file, token.start, "invalid token");
			continue;
		}
		return token;
	}
}

/* Returns the token `distance` positions ahead (0 = current) without consuming it. */
static Token
peek_token(Parser *parser, uint32_t distance)
{
	while (parser->lookahead_count <= distance) {
		uint32_t slot = (parser->lookahead_first + parser->lookahead_count) % LOOKAHEAD_SIZE;
		parser->lookahead[slot] = fetch_token(parser);
		parser->lookahead_count = parser->lookahead_count + 1;
	}
	return parser->lookahead[(parser->lookahead_first + distance) % LOOKAHEAD_SIZE];
}

static TokenKind
peek(Parser *parser)
{
	return peek_token(parser, 0).kind;
}

static TokenKind
peek_at(Parser *parser, uint32_t distance)
{
	return peek_token(parser, distance).kind;
}

static Token
advance(Parser *parser)
{
	Token token = peek_token(parser, 0);
	if (token.kind == TK_EOF)
		return token;
	parser->lookahead_first = (parser->lookahead_first + 1) % LOOKAHEAD_SIZE;
	parser->lookahead_count = parser->lookahead_count - 1;
	parser->previous = token;
	parser->has_previous = true;
	return token;
}

static bool
accept(Parser *parser, TokenKind kind)
{
	if (peek(parser) != kind)
		return false;
	advance(parser);
	return true;
}

static Span
token_span(Token token)
{
	Span span;
	span.start = token.start;
	span.end = token.end;
	return span;
}

/* ---------- Errors ---------- */

static void
describe_found(Parser *parser, Token token, char *buffer, size_t size)
{
	const char *lexeme = token_kind_lexeme(token.kind);
	if (lexeme != NULL)
		snprintf(buffer, size, "'%s'", lexeme);
	else if (token.kind == TK_IDENTIFIER || token.kind == TK_BUILTIN || token.kind == TK_NUMBER_LITERAL)
		snprintf(buffer, size, "%s '%.*s'", token_kind_describe(token.kind), (int)(token.end - token.start > 40 ? 40 : token.end - token.start), parser->source + token.start);
	else
		snprintf(buffer, size, "%s", token_kind_describe(token.kind));
}

/* Reports an error at the current token and unwinds to the nearest recovery point. */
static _Noreturn void
fail_expected(Parser *parser, const char *expected)
{
	Token token = peek_token(parser, 0);
	char found[96];
	describe_found(parser, token, found, sizeof(found));
	diag_report(parser->diag, DIAG_ERROR, parser->file, token.start, "expected %s, found %s", expected, found);
	longjmp(*parser->recovery, 1);
}

static _Noreturn void
fail_at(Parser *parser, uint32_t offset, const char *message)
{
	diag_report(parser->diag, DIAG_ERROR, parser->file, offset, "%s", message);
	longjmp(*parser->recovery, 1);
}

/* Reports an error without unwinding (the parse can continue normally). */
static void
warn_at(Parser *parser, uint32_t offset, const char *message)
{
	diag_report(parser->diag, DIAG_ERROR, parser->file, offset, "%s", message);
}

/*
 * Skips `///` doc comments where the grammar allows them (before container
 * members, error set names and parameters). A doc comment on the same line as
 * the previous token is an error and does not document anything. Returns true
 * and sets `first` to the first documenting comment if there is one.
 */
static bool
eat_doc_comments(Parser *parser, Token *first)
{
	if (peek(parser) != TK_DOC_COMMENT)
		return false;
	bool has_previous = parser->has_previous;
	Token previous = parser->previous;
	Token token = advance(parser);
	bool on_same_line = has_previous && memchr(parser->source + previous.start, '\n', token.start - previous.start) == NULL;
	if (on_same_line) {
		warn_at(parser, token.start, "same line documentation comment");
		if (peek(parser) != TK_DOC_COMMENT)
			return false;
		token = advance(parser);
	}
	*first = token;
	while (peek(parser) == TK_DOC_COMMENT)
		advance(parser);
	return true;
}

static Token
expect(Parser *parser, TokenKind kind)
{
	if (peek(parser) != kind) {
		char expected[64];
		const char *lexeme = token_kind_lexeme(kind);
		if (lexeme != NULL)
			snprintf(expected, sizeof(expected), "'%s'", lexeme);
		else
			snprintf(expected, sizeof(expected), "%s", token_kind_describe(kind));
		fail_expected(parser, expected);
	}
	return advance(parser);
}

static void
enter_nesting(Parser *parser)
{
	parser->nesting = parser->nesting + 1;
	if (parser->nesting > MAX_NESTING)
		fail_at(parser, peek_token(parser, 0).start, "expression is nested too deeply");
}

static void
leave_nesting(Parser *parser)
{
	parser->nesting = parser->nesting - 1;
}

/* ---------- Node allocation ---------- */

static void *
new_node(Parser *parser, size_t size, size_t align, AstKind kind, uint32_t offset)
{
	AstNode *node = arena_zalloc(parser->arena, size, align);
	node->kind = kind;
	node->file = parser->file;
	node->offset = offset;
	node->start = offset;
	node->end = 0;
	return node;
}

/* Records the end of a node: just past the last consumed token. Call once the node is complete. */
static AstNode *
finish(Parser *parser, AstNode *node)
{
	node->end = parser->previous.end;
	return node;
}

/* A node whose first child comes before its main token (binary operators, suffix operators, ...). */
static void
start_at(AstNode *node, const AstNode *first_child)
{
	node->start = first_child->start;
}

#define NEW_NODE(parser, Type, kind, offset) ((Type *)new_node((parser), sizeof(Type), alignof(Type), (kind), (offset)))

static AstNode *
new_wrap(Parser *parser, AstKind kind, uint32_t offset, AstNode *operand)
{
	AstWrap *wrap = NEW_NODE(parser, AstWrap, kind, offset);
	wrap->operand = operand;
	if (kind == AST_DEREF || kind == AST_UNWRAP_OPTIONAL)
		start_at(&wrap->base, operand);
	return finish(parser, &wrap->base);
}

/* ---------- Forward declarations ---------- */

typedef enum BodyMode {
	BODY_EXPR,	/* bodies are Expr (IfExpr, ForExpr, WhileExpr) */
	BODY_TYPE_EXPR,	/* bodies are TypeExpr (IfTypeExpr, ...) */
	BODY_STATEMENT,	/* bodies are statements (IfStatement, ...) */
} BodyMode;

static AstNode          *parse_expr(Parser *parser);
static AstNode          *parse_type_expr(Parser *parser);
static AstNode          *parse_prefix_expr(Parser *parser);
static AstNode          *parse_primary_expr(Parser *parser);
static AstNode          *parse_primary_type_expr(Parser *parser);
static AstNode          *parse_block(Parser *parser, AstLabel label);
static AstNode          *parse_assign_expr(Parser *parser);
static AstNode          *parse_statement(Parser *parser, bool allow_defer_and_var);
static AstNodeList       parse_container_members(Parser *parser, bool is_root);
static AstFnProto       *parse_fn_proto(Parser *parser);
static AstNode          *parse_if(Parser *parser, BodyMode mode);
static AstNode          *parse_while(Parser *parser, AstLabel label, bool is_inline, BodyMode mode);
static AstNode          *parse_for(Parser *parser, AstLabel label, bool is_inline, BodyMode mode);
static AstNode          *parse_switch(Parser *parser, AstLabel label);

/* ---------- Small grammar helpers ---------- */

/* True when the current token can begin an expression (used for `return`, `break` values, etc.). */
static bool
can_start_expr(TokenKind kind)
{
	switch (kind) {
	case TK_IDENTIFIER:
	case TK_STRING_LITERAL:
	case TK_MULTILINE_STRING_LITERAL_LINE:
	case TK_CHAR_LITERAL:
	case TK_NUMBER_LITERAL:
	case TK_BUILTIN:
	case TK_BANG:
	case TK_MINUS:
	case TK_TILDE:
	case TK_MINUS_PERCENT:
	case TK_AMPERSAND:
	case TK_TRY:
	case TK_L_PAREN:
	case TK_L_BRACE:
	case TK_L_BRACKET:
	case TK_PERIOD:
	case TK_ASTERISK:
	case TK_QUESTION_MARK:
	case TK_ASM:
	case TK_IF:
	case TK_BREAK:
	case TK_COMPTIME:
	case TK_NOSUSPEND:
	case TK_CONTINUE:
	case TK_RESUME:
	case TK_RETURN:
	case TK_INLINE:
	case TK_FOR:
	case TK_WHILE:
	case TK_SWITCH:
	case TK_STRUCT:
	case TK_ENUM:
	case TK_UNION:
	case TK_OPAQUE:
	case TK_EXTERN:
	case TK_PACKED:
	case TK_ERROR:
	case TK_FN:
	case TK_UNREACHABLE:
	case TK_ANYFRAME:
		return true;
	default:
		return false;
	}
}

/*
 * Start offset of a loop: its label, else a preceding `inline` (the token just
 * consumed), else the loop keyword itself.
 */
static uint32_t
loop_start(Parser *parser, AstLabel label, bool is_inline)
{
	if (label.has_label)
		return label.name.start;
	if (is_inline)
		return parser->previous.start;
	return peek_token(parser, 0).start;
}

/* BlockLabel <- IDENTIFIER COLON, only when followed by something a label can name. */
static bool
at_block_label(Parser *parser)
{
	if (peek(parser) != TK_IDENTIFIER || peek_at(parser, 1) != TK_COLON)
		return false;
	TokenKind after = peek_at(parser, 2);
	return after == TK_L_BRACE || after == TK_INLINE || after == TK_FOR || after == TK_WHILE || after == TK_SWITCH;
}

/* BlockLabel <- IDENTIFIER COLON */
static AstLabel
parse_block_label(Parser *parser)
{
	AstLabel label;
	label.has_label = false;
	label.name.start = 0;
	label.name.end = 0;
	if (!at_block_label(parser))
		return label;
	label.has_label = true;
	label.name = token_span(advance(parser));
	advance(parser);
	return label;
}

/* BreakLabel <- COLON IDENTIFIER */
static AstLabel
parse_break_label(Parser *parser)
{
	AstLabel label;
	label.has_label = false;
	label.name.start = 0;
	label.name.end = 0;
	if (!accept(parser, TK_COLON))
		return label;
	label.has_label = true;
	label.name = token_span(expect(parser, TK_IDENTIFIER));
	return label;
}

/* Payload <- PIPE IDENTIFIER PIPE. Returns false if there is no payload. */
static bool
parse_payload(Parser *parser, Span *out)
{
	if (!accept(parser, TK_PIPE))
		return false;
	*out = token_span(expect(parser, TK_IDENTIFIER));
	expect(parser, TK_PIPE);
	return true;
}

/* PtrPayload <- PIPE ASTERISK? IDENTIFIER PIPE */
static bool
parse_ptr_payload(Parser *parser, AstCapture *out)
{
	if (!accept(parser, TK_PIPE))
		return false;
	out->by_pointer = accept(parser, TK_ASTERISK);
	out->name = token_span(expect(parser, TK_IDENTIFIER));
	expect(parser, TK_PIPE);
	return true;
}

/* STRINGLITERALSINGLE */
static Span
expect_string_single(Parser *parser)
{
	return token_span(expect(parser, TK_STRING_LITERAL));
}

/* ( Expr ) used by ByteAlign, AddrSpace, LinkSection, CallConv. */
static AstNode *
parse_paren_expr(Parser *parser)
{
	expect(parser, TK_L_PAREN);
	AstNode *expr = parse_expr(parser);
	expect(parser, TK_R_PAREN);
	return expr;
}

/* ---------- Binary operators (Pratt) ---------- */

typedef enum Associativity {
	ASSOC_LEFT,
	ASSOC_NONE,	/* comparisons: a == b == c is an error */
} Associativity;

typedef struct BinaryOperator {
	uint8_t       precedence;	/* 0 = not a binary operator */
	AstBinaryOp   op;
	Associativity associativity;
	bool          is_catch;
} BinaryOperator;

#define PREC_OR 10
#define PREC_AND 20
#define PREC_COMPARE 30
#define PREC_BITWISE 40
#define PREC_SHIFT 50
#define PREC_ADD 60
#define PREC_MULTIPLY 70

static const BinaryOperator binary_operators[TK_COUNT] = {
	[TK_OR] = {PREC_OR, AST_OP_BOOL_OR, ASSOC_LEFT, false},
	[TK_AND] = {PREC_AND, AST_OP_BOOL_AND, ASSOC_LEFT, false},
	[TK_EQUAL_EQUAL] = {PREC_COMPARE, AST_OP_EQUAL, ASSOC_NONE, false},
	[TK_BANG_EQUAL] = {PREC_COMPARE, AST_OP_NOT_EQUAL, ASSOC_NONE, false},
	[TK_ANGLE_BRACKET_LEFT] = {PREC_COMPARE, AST_OP_LESS, ASSOC_NONE, false},
	[TK_ANGLE_BRACKET_RIGHT] = {PREC_COMPARE, AST_OP_GREATER, ASSOC_NONE, false},
	[TK_ANGLE_BRACKET_LEFT_EQUAL] = {PREC_COMPARE, AST_OP_LESS_EQUAL, ASSOC_NONE, false},
	[TK_ANGLE_BRACKET_RIGHT_EQUAL] = {PREC_COMPARE, AST_OP_GREATER_EQUAL, ASSOC_NONE, false},
	[TK_AMPERSAND] = {PREC_BITWISE, AST_OP_BIT_AND, ASSOC_LEFT, false},
	[TK_CARET] = {PREC_BITWISE, AST_OP_BIT_XOR, ASSOC_LEFT, false},
	[TK_PIPE] = {PREC_BITWISE, AST_OP_BIT_OR, ASSOC_LEFT, false},
	[TK_ORELSE] = {PREC_BITWISE, AST_OP_ORELSE, ASSOC_LEFT, false},
	[TK_CATCH] = {PREC_BITWISE, AST_OP_BIT_OR, ASSOC_LEFT, true},
	[TK_ANGLE_BRACKET_ANGLE_BRACKET_LEFT] = {PREC_SHIFT, AST_OP_SHL, ASSOC_LEFT, false},
	[TK_ANGLE_BRACKET_ANGLE_BRACKET_RIGHT] = {PREC_SHIFT, AST_OP_SHR, ASSOC_LEFT, false},
	[TK_ANGLE_BRACKET_ANGLE_BRACKET_LEFT_PIPE] = {PREC_SHIFT, AST_OP_SHL_SAT, ASSOC_LEFT, false},
	[TK_PLUS] = {PREC_ADD, AST_OP_ADD, ASSOC_LEFT, false},
	[TK_MINUS] = {PREC_ADD, AST_OP_SUB, ASSOC_LEFT, false},
	[TK_PLUS_PLUS] = {PREC_ADD, AST_OP_CONCAT, ASSOC_LEFT, false},
	[TK_PLUS_PERCENT] = {PREC_ADD, AST_OP_ADD_WRAP, ASSOC_LEFT, false},
	[TK_MINUS_PERCENT] = {PREC_ADD, AST_OP_SUB_WRAP, ASSOC_LEFT, false},
	[TK_PLUS_PIPE] = {PREC_ADD, AST_OP_ADD_SAT, ASSOC_LEFT, false},
	[TK_MINUS_PIPE] = {PREC_ADD, AST_OP_SUB_SAT, ASSOC_LEFT, false},
	[TK_PIPE_PIPE] = {PREC_MULTIPLY, AST_OP_MERGE_ERROR_SETS, ASSOC_LEFT, false},
	[TK_ASTERISK] = {PREC_MULTIPLY, AST_OP_MUL, ASSOC_LEFT, false},
	[TK_SLASH] = {PREC_MULTIPLY, AST_OP_DIV, ASSOC_LEFT, false},
	[TK_PERCENT] = {PREC_MULTIPLY, AST_OP_MOD, ASSOC_LEFT, false},
	[TK_ASTERISK_PERCENT] = {PREC_MULTIPLY, AST_OP_MUL_WRAP, ASSOC_LEFT, false},
	[TK_ASTERISK_PIPE] = {PREC_MULTIPLY, AST_OP_MUL_SAT, ASSOC_LEFT, false},
};

static bool
is_whitespace_byte(char c)
{
	/* std.ascii.isWhitespace: space and '\t' through '\r' (includes VT and FF). */
	return c == ' ' || (c >= '\t' && c <= '\r');
}

/*
 * Zig requires whitespace on both sides of a binary operator or on neither side
 * (`a -b` is an error), and rejects `&&`.
 */
static void
check_operator_whitespace(Parser *parser, Token token)
{
	bool before = token.start > 0 && is_whitespace_byte(parser->source[token.start - 1]);
	bool after = is_whitespace_byte(parser->source[token.end]);

	if (token.kind == TK_AMPERSAND && parser->source[token.end] == '&') {
		warn_at(parser, token.start, "ambiguous use of '&&'; use 'and' for logical AND, or change whitespace to ' & &' for bitwise AND");
		return;
	}
	if (before != after)
		warn_at(parser, token.start, "binary operator has whitespace on one side, but not the other");
}

/*
 * Pratt parser for all binary operator levels:
 *
 *   BoolOrExpr   <- BoolAndExpr (OrOp BoolAndExpr)*
 *   BoolAndExpr  <- CompareExpr (AndOp CompareExpr)*
 *   CompareExpr  <- BitwiseExpr (CompareOp BitwiseExpr)?
 *   BitwiseExpr  <- BitShiftExpr (BitwiseOp BitShiftExpr)*
 *   BitShiftExpr <- AdditionExpr (BitShiftOp AdditionExpr)*
 *   AdditionExpr <- MultiplyExpr (AdditionOp MultiplyExpr)*
 *   MultiplyExpr <- PrefixExpr (MultiplyOp PrefixExpr)*
 *   BitwiseOp    <- ... / KEYWORD_catch Payload?
 *
 * Parses operators whose precedence is at least `min_precedence`.
 */
static AstNode *
parse_expr_precedence(Parser *parser, uint8_t min_precedence)
{
	enter_nesting(parser);
	AstNode *lhs = parse_prefix_expr(parser);
	uint8_t banned_precedence = 0;

	for (;;) {
		Token token = peek_token(parser, 0);
		BinaryOperator info = binary_operators[token.kind];
		if (info.precedence == 0 || info.precedence < min_precedence)
			break;
		if (info.precedence == banned_precedence)
			fail_at(parser, token.start, "comparison operators cannot be chained");
		advance(parser);
		check_operator_whitespace(parser, token);

		if (info.is_catch) {
			AstCatch *node = NEW_NODE(parser, AstCatch, AST_CATCH, token.start);
			node->lhs = lhs;
			node->has_capture = parse_payload(parser, &node->capture);
			node->rhs = parse_expr_precedence(parser, (uint8_t)(info.precedence + 1));
			start_at(&node->base, lhs);
			lhs = finish(parser, &node->base);
		} else {
			AstBinary *node = NEW_NODE(parser, AstBinary, AST_BINARY, token.start);
			node->op = info.op;
			node->lhs = lhs;
			node->rhs = parse_expr_precedence(parser, (uint8_t)(info.precedence + 1));
			start_at(&node->base, lhs);
			lhs = finish(parser, &node->base);
		}
		if (info.associativity == ASSOC_NONE)
			banned_precedence = info.precedence;
	}
	leave_nesting(parser);
	return lhs;
}

/* Expr <- BoolOrExpr */
static AstNode *
parse_expr(Parser *parser)
{
	return parse_expr_precedence(parser, PREC_OR);
}

/* Parses an Expr if the current token can start one, otherwise returns NULL. */
static AstNode *
parse_optional_expr(Parser *parser)
{
	if (!can_start_expr(peek(parser)))
		return NULL;
	return parse_expr(parser);
}

/*
 * PrefixExpr <- PrefixOp* PrimaryExpr
 * PrefixOp   <- EXCLAMATIONMARK / MINUS / TILDE / MINUSPERCENT / AMPERSAND / KEYWORD_try
 */
static AstNode *
parse_prefix_expr(Parser *parser)
{
	Token token = peek_token(parser, 0);
	AstUnaryOp op;

	switch (token.kind) {
	case TK_BANG:
		op = AST_UNARY_BOOL_NOT;
		break;
	case TK_MINUS:
		op = AST_UNARY_NEGATE;
		break;
	case TK_TILDE:
		op = AST_UNARY_BIT_NOT;
		break;
	case TK_MINUS_PERCENT:
		op = AST_UNARY_NEGATE_WRAP;
		break;
	case TK_AMPERSAND:
		op = AST_UNARY_ADDRESS_OF;
		break;
	case TK_TRY:
		op = AST_UNARY_TRY;
		break;
	default:
		return parse_primary_expr(parser);
	}

	advance(parser);
	enter_nesting(parser);
	AstUnary *node = NEW_NODE(parser, AstUnary, AST_UNARY, token.start);
	node->op = op;
	node->operand = parse_prefix_expr(parser);
	leave_nesting(parser);
	return finish(parser, &node->base);
}

/* ---------- Primary expressions ---------- */

/*
 * FieldInit <- DOT IDENTIFIER EQUAL Expr
 * InitList
 *     <- LBRACE FieldInit (COMMA FieldInit)* COMMA? RBRACE
 *      / LBRACE Expr (COMMA Expr)* COMMA? RBRACE
 *      / LBRACE RBRACE
 */
static AstNode *
parse_init_list(Parser *parser, AstNode *type, uint32_t offset)
{
	AstInitList *node = NEW_NODE(parser, AstInitList, AST_INIT_LIST, offset);
	node->type = type;
	if (type != NULL)
		start_at(&node->base, type);
	expect(parser, TK_L_BRACE);

	if (peek(parser) == TK_PERIOD && peek_at(parser, 1) == TK_IDENTIFIER && peek_at(parser, 2) == TK_EQUAL) {
		node->is_struct_init = true;
		uint32_t capacity = 0;
		while (peek(parser) != TK_R_BRACE) {
			expect(parser, TK_PERIOD);
			AstFieldInit *field = GROW_ONE(parser, node->fields, node->field_count, capacity, AstFieldInit);
			field->name = token_span(expect(parser, TK_IDENTIFIER));
			expect(parser, TK_EQUAL);
			field->value = parse_expr(parser);
			if (!accept(parser, TK_COMMA))
				break;
		}
		expect(parser, TK_R_BRACE);
		return finish(parser, &node->base);
	}

	NodeBuilder elements = {0};
	while (peek(parser) != TK_R_BRACE) {
		node_builder_push(parser, &elements, parse_expr(parser));
		if (!accept(parser, TK_COMMA))
			break;
	}
	expect(parser, TK_R_BRACE);
	node->elements = node_builder_finish(&elements);
	/* `T{}` is an empty struct-or-array init; record it as a struct init like the reference does. */
	node->is_struct_init = elements.count == 0;
	return finish(parser, &node->base);
}

/* CurlySuffixExpr <- TypeExpr (InitList / !LBRACE) */
static AstNode *
parse_curly_suffix_expr(Parser *parser)
{
	AstNode *type = parse_type_expr(parser);
	if (peek(parser) != TK_L_BRACE)
		return type;
	return parse_init_list(parser, type, peek_token(parser, 0).start);
}

/*
 * AsmOutputItem <- LBRACKET IDENTIFIER RBRACKET STRINGLITERALSINGLE LPAREN (MINUSRARROW TypeExpr / IDENTIFIER) RPAREN
 * AsmInputItem  <- LBRACKET IDENTIFIER RBRACKET STRINGLITERALSINGLE LPAREN Expr RPAREN
 * AsmExpr       <- KEYWORD_asm KEYWORD_volatile? LPAREN Expr AsmOutput? RPAREN
 * AsmOutput     <- COLON AsmOutputList AsmInput?
 * AsmInput      <- COLON AsmInputList AsmClobbers?
 * AsmClobbers   <- COLON Expr
 */
static AstNode *
parse_asm_expr(Parser *parser)
{
	Token asm_token = expect(parser, TK_ASM);
	AstAsm *node = NEW_NODE(parser, AstAsm, AST_ASM, asm_token.start);
	node->is_volatile = accept(parser, TK_VOLATILE);
	expect(parser, TK_L_PAREN);
	node->template_expr = parse_expr(parser);

	if (accept(parser, TK_COLON)) {
		uint32_t capacity = 0;
		while (peek(parser) == TK_L_BRACKET) {
			AstAsmOutput *output = GROW_ONE(parser, node->outputs, node->output_count, capacity, AstAsmOutput);
			advance(parser);
			output->symbolic_name = token_span(expect(parser, TK_IDENTIFIER));
			expect(parser, TK_R_BRACKET);
			output->constraint = expect_string_single(parser);
			expect(parser, TK_L_PAREN);
			if (accept(parser, TK_ARROW)) {
				output->returns_type = true;
				output->type = parse_type_expr(parser);
			} else {
				output->variable = token_span(expect(parser, TK_IDENTIFIER));
			}
			expect(parser, TK_R_PAREN);
			if (!accept(parser, TK_COMMA))
				break;
		}
		if (accept(parser, TK_COLON)) {
			capacity = 0;
			while (peek(parser) == TK_L_BRACKET) {
				AstAsmInput *input = GROW_ONE(parser, node->inputs, node->input_count, capacity, AstAsmInput);
				advance(parser);
				input->symbolic_name = token_span(expect(parser, TK_IDENTIFIER));
				expect(parser, TK_R_BRACKET);
				input->constraint = expect_string_single(parser);
				input->value = parse_paren_expr(parser);
				if (!accept(parser, TK_COMMA))
					break;
			}
			if (accept(parser, TK_COLON))
				node->clobbers = parse_expr(parser);
		}
	}
	expect(parser, TK_R_PAREN);
	return finish(parser, &node->base);
}

/*
 * PrimaryExpr
 *     <- AsmExpr
 *      / IfExpr
 *      / KEYWORD_break BreakLabel? (Expr / !ExprPrefix)
 *      / KEYWORD_comptime Expr
 *      / KEYWORD_nosuspend Expr
 *      / KEYWORD_continue BreakLabel? (Expr / !ExprPrefix)
 *      / KEYWORD_resume Expr
 *      / KEYWORD_return (Expr / !ExprPrefix)
 *      / BlockLabel? LoopExpr
 *      / Block
 *      / CurlySuffixExpr
 */
static AstNode *
parse_primary_expr(Parser *parser)
{
	Token token = peek_token(parser, 0);

	switch (token.kind) {
	case TK_ASM:
		return parse_asm_expr(parser);
	case TK_IF:
		return parse_if(parser, BODY_EXPR);
	case TK_BREAK:
	case TK_CONTINUE: {
		advance(parser);
		AstKind kind = token.kind == TK_BREAK ? AST_BREAK : AST_CONTINUE;
		AstJump *node = NEW_NODE(parser, AstJump, kind, token.start);
		node->label = parse_break_label(parser);
		node->value = parse_optional_expr(parser);
		return finish(parser, &node->base);
	}
	case TK_COMPTIME:
		advance(parser);
		return new_wrap(parser, AST_COMPTIME, token.start, parse_expr(parser));
	case TK_NOSUSPEND:
		advance(parser);
		return new_wrap(parser, AST_NOSUSPEND, token.start, parse_expr(parser));
	case TK_RESUME:
		advance(parser);
		return new_wrap(parser, AST_RESUME, token.start, parse_expr(parser));
	case TK_RETURN: {
		advance(parser);
		AstReturn *node = NEW_NODE(parser, AstReturn, AST_RETURN, token.start);
		node->value = parse_optional_expr(parser);
		return finish(parser, &node->base);
	}
	case TK_INLINE:
		advance(parser);
		if (peek(parser) == TK_FOR)
			return parse_for(parser, parse_block_label(parser), true, BODY_EXPR);
		if (peek(parser) == TK_WHILE)
			return parse_while(parser, parse_block_label(parser), true, BODY_EXPR);
		fail_expected(parser, "'for' or 'while' after 'inline'");
	case TK_FOR: {
		AstLabel none = parse_block_label(parser);
		return parse_for(parser, none, false, BODY_EXPR);
	}
	case TK_WHILE: {
		AstLabel none = parse_block_label(parser);
		return parse_while(parser, none, false, BODY_EXPR);
	}
	case TK_L_BRACE: {
		AstLabel none = parse_block_label(parser);
		return parse_block(parser, none);
	}
	case TK_IDENTIFIER:
		/* Only labeled loops are PrimaryExpr; labeled blocks and switches are PrimaryTypeExpr (suffix ops apply). */
		if (at_block_label(parser)) {
			TokenKind after_label = peek_at(parser, 2);
			if (after_label == TK_INLINE || after_label == TK_FOR || after_label == TK_WHILE) {
				AstLabel label = parse_block_label(parser);
				bool is_inline = accept(parser, TK_INLINE);
				if (peek(parser) == TK_FOR)
					return parse_for(parser, label, is_inline, BODY_EXPR);
				if (peek(parser) == TK_WHILE)
					return parse_while(parser, label, is_inline, BODY_EXPR);
				fail_expected(parser, "'for' or 'while' after 'inline'");
			}
		}
		return parse_curly_suffix_expr(parser);
	default:
		return parse_curly_suffix_expr(parser);
	}
}

/* ---------- Type expressions ---------- */

/*
 * PtrMod <- KEYWORD_allowzero / KEYWORD_const / KEYWORD_volatile
 * PtrMods / SinglePtrMods: any order of PtrMod, ByteAlign/BitAlign and AddrSpace.
 * ByteAlign <- KEYWORD_align LPAREN Expr RPAREN
 * BitAlign  <- KEYWORD_align LPAREN Expr (COLON Expr COLON Expr)? RPAREN
 * AddrSpace <- KEYWORD_addrspace LPAREN Expr RPAREN
 */
static void
parse_pointer_modifiers(Parser *parser, AstPointerType *pointer, bool allow_bit_align)
{
	for (;;) {
		Token token = peek_token(parser, 0);
		switch (token.kind) {
		case TK_CONST:
			advance(parser);
			if (pointer->is_const)
				warn_at(parser, token.start, "duplicate const qualifier");
			pointer->is_const = true;
			break;
		case TK_VOLATILE:
			advance(parser);
			if (pointer->is_volatile)
				warn_at(parser, token.start, "duplicate volatile qualifier");
			pointer->is_volatile = true;
			break;
		case TK_ALLOWZERO:
			advance(parser);
			if (pointer->is_allowzero)
				warn_at(parser, token.start, "duplicate allowzero qualifier");
			pointer->is_allowzero = true;
			break;
		case TK_ALIGN:
			advance(parser);
			if (pointer->align != NULL)
				warn_at(parser, token.start, "duplicate align qualifier");
			expect(parser, TK_L_PAREN);
			pointer->align = parse_expr(parser);
			if (allow_bit_align && accept(parser, TK_COLON)) {
				pointer->bit_offset = parse_expr(parser);
				expect(parser, TK_COLON);
				pointer->host_size = parse_expr(parser);
			}
			expect(parser, TK_R_PAREN);
			break;
		case TK_ADDRSPACE:
			advance(parser);
			if (pointer->addrspace != NULL)
				warn_at(parser, token.start, "duplicate addrspace qualifier");
			pointer->addrspace = parse_paren_expr(parser);
			break;
		default:
			return;
		}
	}
}

static AstPointerType *
new_pointer(Parser *parser, AstPointerSize size, uint32_t offset)
{
	AstPointerType *pointer = NEW_NODE(parser, AstPointerType, AST_POINTER_TYPE, offset);
	pointer->size = size;
	return pointer;
}

/* True when `[*c]` follows: the identifier must be exactly "c". */
static bool
at_c_pointer(Parser *parser)
{
	if (peek(parser) != TK_L_BRACKET || peek_at(parser, 1) != TK_ASTERISK || peek_at(parser, 2) != TK_IDENTIFIER)
		return false;
	Token name = peek_token(parser, 2);
	return name.end - name.start == 1 && parser->source[name.start] == 'c' && peek_at(parser, 3) == TK_R_BRACKET;
}

/*
 * TypeExpr <- PrefixTypeOp* !PrefixTypeOpPrefix ErrorUnionExpr
 * PrefixTypeOp
 *     <- QUESTIONMARK
 *      / KEYWORD_anyframe MINUSRARROW
 *      / (ManyPtrTypeStart / SliceTypeStart) PtrMods
 *      / SinglePtrTypeStart SinglePtrMods
 *      / ArrayTypeStart
 * SliceTypeStart     <- LBRACKET (COLON Expr)? RBRACKET
 * SinglePtrTypeStart <- ASTERISK
 * ManyPtrTypeStart   <- LBRACKET ASTERISK (LETTERC / COLON Expr)? RBRACKET
 * ArrayTypeStart     <- LBRACKET !ASTERISK Expr (COLON Expr)? RBRACKET
 * ErrorUnionExpr     <- SuffixExpr (EXCLAMATIONMARK TypeExpr / !EXCLAMATIONMARK)
 */
static AstNode *parse_suffix_expr(Parser *parser);

static AstNode *
parse_type_expr(Parser *parser)
{
	Token token = peek_token(parser, 0);
	AstNode *result = NULL;

	enter_nesting(parser);
	switch (token.kind) {
	case TK_QUESTION_MARK:
		advance(parser);
		result = new_wrap(parser, AST_OPTIONAL_TYPE, token.start, parse_type_expr(parser));
		break;
	case TK_ANYFRAME:
		if (peek_at(parser, 1) != TK_ARROW) {
			result = NULL;
			break;
		}
		advance(parser);
		advance(parser);
		{
			AstAnyframeType *node = NEW_NODE(parser, AstAnyframeType, AST_ANYFRAME_TYPE, token.start);
			node->result = parse_type_expr(parser);
			result = &node->base;
		}
		break;
	case TK_ASTERISK: {
		advance(parser);
		AstPointerType *pointer = new_pointer(parser, AST_POINTER_ONE, token.start);
		parse_pointer_modifiers(parser, pointer, true);
		pointer->child = parse_type_expr(parser);
		result = &pointer->base;
		break;
	}
	case TK_L_BRACKET: {
		if (at_c_pointer(parser)) {
			advance(parser);
			advance(parser);
			advance(parser);
			advance(parser);
			AstPointerType *pointer = new_pointer(parser, AST_POINTER_C, token.start);
			parse_pointer_modifiers(parser, pointer, false);
			pointer->child = parse_type_expr(parser);
			result = &pointer->base;
			break;
		}
		advance(parser);
		if (accept(parser, TK_ASTERISK)) {
			AstPointerType *pointer = new_pointer(parser, AST_POINTER_MANY, token.start);
			if (accept(parser, TK_COLON))
				pointer->sentinel = parse_expr(parser);
			expect(parser, TK_R_BRACKET);
			parse_pointer_modifiers(parser, pointer, false);
			pointer->child = parse_type_expr(parser);
			result = &pointer->base;
			break;
		}
		if (peek(parser) == TK_R_BRACKET || peek(parser) == TK_COLON) {
			AstPointerType *pointer = new_pointer(parser, AST_POINTER_SLICE, token.start);
			if (accept(parser, TK_COLON))
				pointer->sentinel = parse_expr(parser);
			expect(parser, TK_R_BRACKET);
			parse_pointer_modifiers(parser, pointer, false);
			if (pointer->bit_offset != NULL)
				warn_at(parser, token.start, "bit-pointer alignment is not allowed on slices");
			pointer->child = parse_type_expr(parser);
			result = &pointer->base;
			break;
		}
		AstArrayType *array = NEW_NODE(parser, AstArrayType, AST_ARRAY_TYPE, token.start);
		array->length = parse_expr(parser);
		if (accept(parser, TK_COLON))
			array->sentinel = parse_expr(parser);
		expect(parser, TK_R_BRACKET);
		if (peek(parser) == TK_CONST || peek(parser) == TK_VOLATILE || peek(parser) == TK_ALLOWZERO || peek(parser) == TK_ALIGN || peek(parser) == TK_ADDRSPACE)
			fail_at(parser, peek_token(parser, 0).start, "pointer modifier is not allowed on an array type");
		array->child = parse_type_expr(parser);
		result = &array->base;
		break;
	}
	default:
		break;
	}

	if (result != NULL && result->end == 0)
		finish(parser, result);
	if (result == NULL) {
		AstNode *lhs = parse_suffix_expr(parser);
		Token bang = peek_token(parser, 0);
		if (accept(parser, TK_BANG)) {
			AstErrorUnionType *node = NEW_NODE(parser, AstErrorUnionType, AST_ERROR_UNION_TYPE, bang.start);
			node->error_set = lhs;
			node->payload = parse_type_expr(parser);
			start_at(&node->base, lhs);
			result = finish(parser, &node->base);
		} else {
			result = lhs;
		}
	}
	leave_nesting(parser);
	return result;
}

/* FnCallArguments <- LPAREN ExprList RPAREN ; ExprList <- (Expr COMMA)* Expr? */
static AstNodeList
parse_call_arguments(Parser *parser)
{
	NodeBuilder args = {0};
	expect(parser, TK_L_PAREN);
	while (peek(parser) != TK_R_PAREN) {
		node_builder_push(parser, &args, parse_expr(parser));
		if (!accept(parser, TK_COMMA))
			break;
	}
	expect(parser, TK_R_PAREN);
	return node_builder_finish(&args);
}

/*
 * SuffixExpr <- PrimaryTypeExpr SuffixOp* !SuffixOpPrefix
 * SuffixOp
 *     <- LBRACKET Expr (DOT2 (Expr / !ExprPrefix) (COLON Expr)?)? RBRACKET
 *      / DOT IDENTIFIER
 *      / DOTASTERISK
 *      / DOT QUESTIONMARK
 *      / FnCallArguments
 */
static AstNode *
parse_suffix_expr(Parser *parser)
{
	AstNode *result = parse_primary_type_expr(parser);

	for (;;) {
		Token token = peek_token(parser, 0);
		switch (token.kind) {
		case TK_L_BRACKET: {
			advance(parser);
			AstNode *start = parse_expr(parser);
			if (accept(parser, TK_ELLIPSIS2)) {
				AstSlice *slice = NEW_NODE(parser, AstSlice, AST_SLICE, token.start);
				slice->lhs = result;
				slice->start = start;
				if (peek(parser) != TK_R_BRACKET && peek(parser) != TK_COLON)
					slice->end = parse_expr(parser);
				if (accept(parser, TK_COLON))
					slice->sentinel = parse_expr(parser);
				expect(parser, TK_R_BRACKET);
				start_at(&slice->base, result);
				result = finish(parser, &slice->base);
			} else {
				AstIndex *index = NEW_NODE(parser, AstIndex, AST_INDEX, token.start);
				index->lhs = result;
				index->index = start;
				expect(parser, TK_R_BRACKET);
				start_at(&index->base, result);
				result = finish(parser, &index->base);
			}
			break;
		}
		case TK_PERIOD_ASTERISK:
			advance(parser);
			result = new_wrap(parser, AST_DEREF, token.start, result);
			break;
		case TK_PERIOD: {
			TokenKind next = peek_at(parser, 1);
			if (next == TK_IDENTIFIER) {
				advance(parser);
				AstFieldAccess *access = NEW_NODE(parser, AstFieldAccess, AST_FIELD_ACCESS, token.start);
				access->lhs = result;
				access->name = token_span(advance(parser));
				start_at(&access->base, result);
				result = finish(parser, &access->base);
				break;
			}
			if (next == TK_QUESTION_MARK) {
				advance(parser);
				advance(parser);
				result = new_wrap(parser, AST_UNWRAP_OPTIONAL, token.start, result);
				break;
			}
			advance(parser);
			fail_expected(parser, "field name, '*' or '?' after '.'");
		}
		case TK_L_PAREN: {
			AstCall *call = NEW_NODE(parser, AstCall, AST_CALL, token.start);
			call->callee = result;
			call->args = parse_call_arguments(parser);
			start_at(&call->base, result);
			result = finish(parser, &call->base);
			break;
		}
		default:
			return result;
		}
	}
}

/*
 * ContainerDeclAuto / ContainerTypeAuto <- ContainerTypeKind LBRACE ContainerMembers RBRACE
 * ContainerType <- (KEYWORD_extern / KEYWORD_packed)? ContainerTypeAuto
 * ContainerTypeKind
 *     <- KEYWORD_struct (LPAREN Expr RPAREN)?
 *      / KEYWORD_opaque
 *      / KEYWORD_enum (LPAREN Expr RPAREN)?
 *      / KEYWORD_union (LPAREN (KEYWORD_enum (LPAREN Expr RPAREN)? / Expr) RPAREN)?
 */
static AstNode *
parse_container_type(Parser *parser)
{
	Token first = peek_token(parser, 0);
	AstContainerDecl *node = NEW_NODE(parser, AstContainerDecl, AST_CONTAINER_DECL, first.start);
	node->layout = AST_LAYOUT_AUTO;
	if (accept(parser, TK_EXTERN))
		node->layout = AST_LAYOUT_EXTERN;
	else if (accept(parser, TK_PACKED))
		node->layout = AST_LAYOUT_PACKED;

	Token kind_token = peek_token(parser, 0);
	switch (kind_token.kind) {
	case TK_STRUCT:
		advance(parser);
		node->container_kind = AST_CONTAINER_STRUCT;
		if (peek(parser) == TK_L_PAREN)
			node->arg = parse_paren_expr(parser);
		break;
	case TK_OPAQUE:
		advance(parser);
		node->container_kind = AST_CONTAINER_OPAQUE;
		break;
	case TK_ENUM:
		advance(parser);
		node->container_kind = AST_CONTAINER_ENUM;
		if (peek(parser) == TK_L_PAREN)
			node->arg = parse_paren_expr(parser);
		break;
	case TK_UNION:
		advance(parser);
		node->container_kind = AST_CONTAINER_UNION;
		if (accept(parser, TK_L_PAREN)) {
			if (accept(parser, TK_ENUM)) {
				node->union_enum_tag = true;
				if (peek(parser) == TK_L_PAREN)
					node->arg = parse_paren_expr(parser);
			} else {
				node->arg = parse_expr(parser);
			}
			expect(parser, TK_R_PAREN);
		}
		break;
	default:
		fail_expected(parser, "'struct', 'enum', 'union' or 'opaque'");
	}

	expect(parser, TK_L_BRACE);
	node->members = parse_container_members(parser, false);
	expect(parser, TK_R_BRACE);
	return finish(parser, &node->base);
}

/*
 * ErrorSetDecl <- KEYWORD_error LBRACE IdentifierList RBRACE
 * IdentifierList <- (doc_comment? IDENTIFIER COMMA)* (doc_comment? IDENTIFIER)?
 */
static AstNode *
parse_error_set_decl(Parser *parser)
{
	Token error_token = expect(parser, TK_ERROR);
	AstErrorSetDecl *node = NEW_NODE(parser, AstErrorSetDecl, AST_ERROR_SET_DECL, error_token.start);
	uint32_t capacity = 0;
	expect(parser, TK_L_BRACE);
	while (peek(parser) != TK_R_BRACE) {
		Token doc;
		eat_doc_comments(parser, &doc);
		Span *name = GROW_ONE(parser, node->names, node->name_count, capacity, Span);
		*name = token_span(expect(parser, TK_IDENTIFIER));
		if (!accept(parser, TK_COMMA))
			break;
	}
	expect(parser, TK_R_BRACE);
	return finish(parser, &node->base);
}

static AstNode *
new_literal(Parser *parser, AstKind kind, Token token)
{
	AstLiteral *node = NEW_NODE(parser, AstLiteral, kind, token.start);
	node->text = token_span(token);
	return finish(parser, &node->base);
}

static AstNode *
new_named_value(Parser *parser, AstKind kind, uint32_t offset, Token name)
{
	AstNamedValue *node = NEW_NODE(parser, AstNamedValue, kind, offset);
	node->name = token_span(name);
	return finish(parser, &node->base);
}

/*
 * PrimaryTypeExpr
 *     <- BUILTINIDENTIFIER FnCallArguments
 *      / CHAR_LITERAL
 *      / ContainerType
 *      / DOT IDENTIFIER
 *      / DOT InitList
 *      / ErrorSetDecl
 *      / FnProto
 *      / GroupedExpr
 *      / LabeledTypeExpr
 *      / IDENTIFIER
 *      / IfTypeExpr
 *      / KEYWORD_comptime TypeExpr
 *      / KEYWORD_error DOT IDENTIFIER
 *      / KEYWORD_anyframe
 *      / KEYWORD_unreachable
 *      / NUMBERLITERAL
 *      / STRINGLITERAL
 * GroupedExpr <- LPAREN Expr RPAREN
 * LabeledTypeExpr
 *     <- BlockLabel Block
 *      / BlockLabel? LoopTypeExpr
 *      / BlockLabel? SwitchExpr
 * LoopTypeExpr <- KEYWORD_inline? (ForTypeExpr / WhileTypeExpr)
 */
static AstNode *
parse_primary_type_expr(Parser *parser)
{
	Token token = peek_token(parser, 0);

	switch (token.kind) {
	case TK_BUILTIN: {
		advance(parser);
		AstBuiltinCall *call = NEW_NODE(parser, AstBuiltinCall, AST_BUILTIN_CALL, token.start);
		call->name = token_span(token);
		call->args = parse_call_arguments(parser);
		return finish(parser, &call->base);
	}
	case TK_CHAR_LITERAL:
		advance(parser);
		return new_literal(parser, AST_CHAR_LITERAL, token);
	case TK_NUMBER_LITERAL:
		advance(parser);
		return new_literal(parser, AST_NUMBER_LITERAL, token);
	case TK_STRING_LITERAL:
		advance(parser);
		return new_literal(parser, AST_STRING_LITERAL, token);
	case TK_MULTILINE_STRING_LITERAL_LINE: {
		AstMultilineString *node = NEW_NODE(parser, AstMultilineString, AST_MULTILINE_STRING_LITERAL, token.start);
		uint32_t capacity = 0;
		while (peek(parser) == TK_MULTILINE_STRING_LITERAL_LINE) {
			Span *line = GROW_ONE(parser, node->lines, node->line_count, capacity, Span);
			*line = token_span(advance(parser));
		}
		return finish(parser, &node->base);
	}
	case TK_EXTERN:
	case TK_PACKED:
	case TK_STRUCT:
	case TK_OPAQUE:
	case TK_ENUM:
	case TK_UNION:
		return parse_container_type(parser);
	case TK_PERIOD:
		if (peek_at(parser, 1) == TK_IDENTIFIER) {
			advance(parser);
			return new_named_value(parser, AST_ENUM_LITERAL, token.start, advance(parser));
		}
		if (peek_at(parser, 1) == TK_L_BRACE) {
			advance(parser);
			return parse_init_list(parser, NULL, token.start);
		}
		advance(parser);
		fail_expected(parser, "an identifier or '{' after '.'");
	case TK_ERROR:
		if (peek_at(parser, 1) == TK_L_BRACE)
			return parse_error_set_decl(parser);
		advance(parser);
		expect(parser, TK_PERIOD);
		return new_named_value(parser, AST_ERROR_VALUE, token.start, expect(parser, TK_IDENTIFIER));
	case TK_FN:
		return &parse_fn_proto(parser)->base;
	case TK_L_PAREN: {
		advance(parser);
		AstNode *inner = parse_expr(parser);
		expect(parser, TK_R_PAREN);
		return new_wrap(parser, AST_GROUPED, token.start, inner);
	}
	case TK_IDENTIFIER:
		if (at_block_label(parser)) {
			AstLabel label = parse_block_label(parser);
			bool is_inline = accept(parser, TK_INLINE);
			if (peek(parser) == TK_FOR)
				return parse_for(parser, label, is_inline, BODY_TYPE_EXPR);
			if (peek(parser) == TK_WHILE)
				return parse_while(parser, label, is_inline, BODY_TYPE_EXPR);
			if (is_inline)
				fail_expected(parser, "'for' or 'while' after 'inline'");
			if (peek(parser) == TK_SWITCH)
				return parse_switch(parser, label);
			return parse_block(parser, label);
		}
		advance(parser);
		{
			AstIdentifier *identifier = NEW_NODE(parser, AstIdentifier, AST_IDENTIFIER, token.start);
			identifier->name = token_span(token);
			return finish(parser, &identifier->base);
		}
	case TK_IF:
		return parse_if(parser, BODY_TYPE_EXPR);
	case TK_COMPTIME:
		advance(parser);
		return new_wrap(parser, AST_COMPTIME, token.start, parse_type_expr(parser));
	case TK_ANYFRAME: {
		advance(parser);
		AstSimple *node = NEW_NODE(parser, AstSimple, AST_ANYFRAME_LITERAL, token.start);
		return finish(parser, &node->base);
	}
	case TK_UNREACHABLE: {
		advance(parser);
		AstSimple *node = NEW_NODE(parser, AstSimple, AST_UNREACHABLE, token.start);
		return finish(parser, &node->base);
	}
	case TK_INLINE:
		advance(parser);
		if (peek(parser) == TK_FOR)
			return parse_for(parser, parse_block_label(parser), true, BODY_TYPE_EXPR);
		if (peek(parser) == TK_WHILE)
			return parse_while(parser, parse_block_label(parser), true, BODY_TYPE_EXPR);
		fail_expected(parser, "'for' or 'while' after 'inline'");
	case TK_FOR:
		return parse_for(parser, parse_block_label(parser), false, BODY_TYPE_EXPR);
	case TK_WHILE:
		return parse_while(parser, parse_block_label(parser), false, BODY_TYPE_EXPR);
	case TK_SWITCH:
		return parse_switch(parser, parse_block_label(parser));
	default:
		fail_expected(parser, "an expression");
	}
}

/* ---------- Control flow ---------- */

/* BlockExprPrefix <- BlockLabel? LBRACE */
static bool
at_block_expr(Parser *parser)
{
	if (peek(parser) == TK_L_BRACE)
		return true;
	return peek(parser) == TK_IDENTIFIER && peek_at(parser, 1) == TK_COLON && peek_at(parser, 2) == TK_L_BRACE;
}

/* BlockExpr <- BlockLabel? Block */
static AstNode *
parse_block_expr(Parser *parser)
{
	AstLabel label = parse_block_label(parser);
	return parse_block(parser, label);
}

/* Body of an if/while/for in expression or type mode. */
static AstNode *
parse_body(Parser *parser, BodyMode mode)
{
	if (mode == BODY_TYPE_EXPR)
		return parse_type_expr(parser);
	return parse_expr(parser);
}

/*
 * Statement-mode body:
 *     BlockExpr (KEYWORD_else ... / !KEYWORD_else)
 *   / !BlockExprPrefix AssignExpr (SEMICOLON / KEYWORD_else ...)
 * Returns the body; *needs_semicolon is set when the body was an AssignExpr.
 */
static AstNode *
parse_statement_body(Parser *parser, bool *needs_semicolon)
{
	if (at_block_expr(parser)) {
		*needs_semicolon = false;
		return parse_block_expr(parser);
	}
	*needs_semicolon = true;
	return parse_assign_expr(parser);
}

/*
 * IfPrefix <- KEYWORD_if LPAREN Expr RPAREN PtrPayload?
 * IfExpr <- IfPrefix Expr (KEYWORD_else Payload? Expr / !KEYWORD_else)
 * IfTypeExpr <- IfPrefix TypeExpr (KEYWORD_else Payload? TypeExpr / !KEYWORD_else)
 * IfStatement
 *     <- IfPrefix BlockExpr (KEYWORD_else Payload? Statement / !KEYWORD_else)
 *      / IfPrefix !BlockExprPrefix AssignExpr (SEMICOLON / KEYWORD_else Payload? Statement)
 */
static AstNode *
parse_if(Parser *parser, BodyMode mode)
{
	Token if_token = expect(parser, TK_IF);
	AstIf *node = NEW_NODE(parser, AstIf, AST_IF, if_token.start);
	node->condition = parse_paren_expr(parser);
	node->has_capture = parse_ptr_payload(parser, &node->capture);

	if (mode == BODY_STATEMENT) {
		bool needs_semicolon = false;
		node->then_branch = parse_statement_body(parser, &needs_semicolon);
		if (accept(parser, TK_ELSE)) {
			node->has_else_capture = parse_payload(parser, &node->else_capture);
			node->else_branch = parse_statement(parser, false);
			/* The else statement may have consumed a ';' that is not part of this node. */
			node->base.end = node->else_branch->end;
		} else {
			finish(parser, &node->base);
			if (needs_semicolon)
				expect(parser, TK_SEMICOLON);
		}
		return &node->base;
	}

	node->then_branch = parse_body(parser, mode);
	if (accept(parser, TK_ELSE)) {
		node->has_else_capture = parse_payload(parser, &node->else_capture);
		node->else_branch = parse_body(parser, mode);
	}
	return finish(parser, &node->base);
}

/*
 * WhilePrefix <- KEYWORD_while LPAREN Expr RPAREN PtrPayload? WhileContinueExpr?
 * WhileContinueExpr <- COLON LPAREN AssignExpr RPAREN
 * WhileExpr <- WhilePrefix Expr (KEYWORD_else Payload? Expr)?
 * WhileTypeExpr <- WhilePrefix TypeExpr (KEYWORD_else Payload? TypeExpr / !KEYWORD_else)
 * WhileStatement
 *     <- WhilePrefix BlockExpr (KEYWORD_else Payload? Statement / !KEYWORD_else)
 *      / WhilePrefix !BlockExprPrefix AssignExpr (SEMICOLON / KEYWORD_else Payload? Statement)
 */
static AstNode *
parse_while(Parser *parser, AstLabel label, bool is_inline, BodyMode mode)
{
	uint32_t start = loop_start(parser, label, is_inline);
	Token while_token = expect(parser, TK_WHILE);
	AstWhile *node = NEW_NODE(parser, AstWhile, AST_WHILE, while_token.start);
	node->base.start = start;
	node->label = label;
	node->is_inline = is_inline;
	node->condition = parse_paren_expr(parser);
	node->has_capture = parse_ptr_payload(parser, &node->capture);
	if (accept(parser, TK_COLON)) {
		expect(parser, TK_L_PAREN);
		node->continue_expr = parse_assign_expr(parser);
		expect(parser, TK_R_PAREN);
	}

	if (mode == BODY_STATEMENT) {
		bool needs_semicolon = false;
		node->body = parse_statement_body(parser, &needs_semicolon);
		if (accept(parser, TK_ELSE)) {
			node->has_else_capture = parse_payload(parser, &node->else_capture);
			node->else_branch = parse_statement(parser, false);
			/* The else statement may have consumed a ';' that is not part of this node. */
			node->base.end = node->else_branch->end;
		} else {
			finish(parser, &node->base);
			if (needs_semicolon)
				expect(parser, TK_SEMICOLON);
		}
		return &node->base;
	}

	node->body = parse_body(parser, mode);
	if (accept(parser, TK_ELSE)) {
		node->has_else_capture = parse_payload(parser, &node->else_capture);
		node->else_branch = parse_body(parser, mode);
	}
	return finish(parser, &node->base);
}

/*
 * ForPrefix <- KEYWORD_for LPAREN ForArgumentsList RPAREN PtrListPayload
 * ForArgumentsList <- ForItem (COMMA ForItem)* COMMA?
 * ForItem <- Expr (DOT2 (Expr / !ExprPrefix) / !DOT2)
 * PtrListPayload <- PIPE ASTERISK? IDENTIFIER (COMMA ASTERISK? IDENTIFIER)* COMMA? PIPE
 * ForExpr <- ForPrefix Expr (KEYWORD_else Expr / !KEYWORD_else)
 * ForTypeExpr <- ForPrefix TypeExpr (KEYWORD_else TypeExpr / !KEYWORD_else)
 * ForStatement
 *     <- ForPrefix BlockExpr (KEYWORD_else Statement / !KEYWORD_else)
 *      / ForPrefix !BlockExprPrefix AssignExpr (SEMICOLON / KEYWORD_else Statement)
 */
static AstNode *
parse_for(Parser *parser, AstLabel label, bool is_inline, BodyMode mode)
{
	uint32_t start = loop_start(parser, label, is_inline);
	Token for_token = expect(parser, TK_FOR);
	AstFor *node = NEW_NODE(parser, AstFor, AST_FOR, for_token.start);
	node->base.start = start;
	node->label = label;
	node->is_inline = is_inline;

	uint32_t input_capacity = 0;
	expect(parser, TK_L_PAREN);
	while (peek(parser) != TK_R_PAREN) {
		AstForInput *input = GROW_ONE(parser, node->inputs, node->input_count, input_capacity, AstForInput);
		input->expr = parse_expr(parser);
		if (accept(parser, TK_ELLIPSIS2)) {
			input->is_range = true;
			input->range_end = parse_optional_expr(parser);
		}
		if (!accept(parser, TK_COMMA))
			break;
	}
	expect(parser, TK_R_PAREN);
	if (node->input_count == 0)
		warn_at(parser, for_token.start, "for loop needs at least one input");

	uint32_t capture_capacity = 0;
	expect(parser, TK_PIPE);
	while (peek(parser) != TK_PIPE) {
		AstCapture *capture = GROW_ONE(parser, node->captures.items, node->captures.count, capture_capacity, AstCapture);
		capture->by_pointer = accept(parser, TK_ASTERISK);
		capture->name = token_span(expect(parser, TK_IDENTIFIER));
		if (!accept(parser, TK_COMMA))
			break;
	}
	expect(parser, TK_PIPE);

	if (mode == BODY_STATEMENT) {
		bool needs_semicolon = false;
		node->body = parse_statement_body(parser, &needs_semicolon);
		if (accept(parser, TK_ELSE)) {
			node->else_branch = parse_statement(parser, false);
			/* The else statement may have consumed a ';' that is not part of this node. */
			node->base.end = node->else_branch->end;
		} else {
			finish(parser, &node->base);
			if (needs_semicolon)
				expect(parser, TK_SEMICOLON);
		}
		return &node->base;
	}

	node->body = parse_body(parser, mode);
	if (accept(parser, TK_ELSE))
		node->else_branch = parse_body(parser, mode);
	return finish(parser, &node->base);
}

static bool parse_assign_op(Parser *parser, AstAssignOp *out);

/* SingleAssignExpr <- Expr (AssignOp Expr / !AssignOp) */
static AstNode *
parse_single_assign_expr(Parser *parser)
{
	AstNode *target = parse_expr(parser);
	Token op_token = peek_token(parser, 0);
	AstAssignOp op;
	if (!parse_assign_op(parser, &op))
		return target;
	AstAssign *node = NEW_NODE(parser, AstAssign, AST_ASSIGN, op_token.start);
	node->op = op;
	node->target = target;
	node->value = parse_expr(parser);
	start_at(&node->base, target);
	return finish(parser, &node->base);
}

/*
 * SwitchExpr <- KEYWORD_switch LPAREN Expr RPAREN LBRACE SwitchProngList RBRACE
 * SwitchProngList <- (SwitchProng COMMA)* SwitchProng?
 * SwitchProng <- KEYWORD_inline? SwitchCase EQUALRARROW PtrIndexPayload? SingleAssignExpr
 * SwitchCase <- SwitchItem (COMMA SwitchItem)* COMMA? / KEYWORD_else
 * SwitchItem <- Expr (DOT3 Expr)?
 * PtrIndexPayload <- PIPE ASTERISK? IDENTIFIER (COMMA IDENTIFIER)? PIPE
 */
static AstNode *
parse_switch(Parser *parser, AstLabel label)
{
	Token switch_token = expect(parser, TK_SWITCH);
	AstSwitch *node = NEW_NODE(parser, AstSwitch, AST_SWITCH, switch_token.start);
	if (label.has_label)
		node->base.start = label.name.start;
	node->label = label;
	node->operand = parse_paren_expr(parser);
	expect(parser, TK_L_BRACE);

	uint32_t prong_capacity = 0;
	while (peek(parser) != TK_R_BRACE) {
		AstSwitchProng *prong = GROW_ONE(parser, node->prongs, node->prong_count, prong_capacity, AstSwitchProng);
		prong->offset = peek_token(parser, 0).start;
		prong->is_inline = accept(parser, TK_INLINE);
		if (accept(parser, TK_ELSE)) {
			prong->is_else = true;
		} else {
			uint32_t item_capacity = 0;
			while (peek(parser) != TK_EQUAL_ANGLE_BRACKET_RIGHT) {
				AstSwitchItem *item = GROW_ONE(parser, prong->items, prong->item_count, item_capacity, AstSwitchItem);
				item->value = parse_expr(parser);
				if (accept(parser, TK_ELLIPSIS3))
					item->range_end = parse_expr(parser);
				if (!accept(parser, TK_COMMA))
					break;
			}
			if (prong->item_count == 0)
				fail_expected(parser, "a switch case");
		}
		expect(parser, TK_EQUAL_ANGLE_BRACKET_RIGHT);
		if (accept(parser, TK_PIPE)) {
			prong->has_capture = true;
			prong->capture.by_pointer = accept(parser, TK_ASTERISK);
			prong->capture.name = token_span(expect(parser, TK_IDENTIFIER));
			if (accept(parser, TK_COMMA)) {
				prong->has_index_capture = true;
				prong->index_capture = token_span(expect(parser, TK_IDENTIFIER));
			}
			expect(parser, TK_PIPE);
		}
		prong->body = parse_single_assign_expr(parser);
		if (!accept(parser, TK_COMMA))
			break;
	}
	expect(parser, TK_R_BRACE);
	return finish(parser, &node->base);
}

/* ---------- Statements ---------- */

static const AstAssignOp NOT_ASSIGN = AST_ASSIGN_OP_COUNT;

static AstAssignOp
assign_op_for_token(TokenKind kind)
{
	switch (kind) {
	case TK_EQUAL:
		return AST_ASSIGN_PLAIN;
	case TK_ASTERISK_EQUAL:
		return AST_ASSIGN_MUL;
	case TK_ASTERISK_PIPE_EQUAL:
		return AST_ASSIGN_MUL_SAT;
	case TK_SLASH_EQUAL:
		return AST_ASSIGN_DIV;
	case TK_PERCENT_EQUAL:
		return AST_ASSIGN_MOD;
	case TK_PLUS_EQUAL:
		return AST_ASSIGN_ADD;
	case TK_PLUS_PIPE_EQUAL:
		return AST_ASSIGN_ADD_SAT;
	case TK_MINUS_EQUAL:
		return AST_ASSIGN_SUB;
	case TK_MINUS_PIPE_EQUAL:
		return AST_ASSIGN_SUB_SAT;
	case TK_ANGLE_BRACKET_ANGLE_BRACKET_LEFT_EQUAL:
		return AST_ASSIGN_SHL;
	case TK_ANGLE_BRACKET_ANGLE_BRACKET_LEFT_PIPE_EQUAL:
		return AST_ASSIGN_SHL_SAT;
	case TK_ANGLE_BRACKET_ANGLE_BRACKET_RIGHT_EQUAL:
		return AST_ASSIGN_SHR;
	case TK_AMPERSAND_EQUAL:
		return AST_ASSIGN_BIT_AND;
	case TK_CARET_EQUAL:
		return AST_ASSIGN_BIT_XOR;
	case TK_PIPE_EQUAL:
		return AST_ASSIGN_BIT_OR;
	case TK_ASTERISK_PERCENT_EQUAL:
		return AST_ASSIGN_MUL_WRAP;
	case TK_PLUS_PERCENT_EQUAL:
		return AST_ASSIGN_ADD_WRAP;
	case TK_MINUS_PERCENT_EQUAL:
		return AST_ASSIGN_SUB_WRAP;
	default:
		return NOT_ASSIGN;
	}
}

/* AssignOp <- ASTERISKEQUAL / ... / EQUAL. Consumes the operator if present. */
static bool
parse_assign_op(Parser *parser, AstAssignOp *out)
{
	AstAssignOp op = assign_op_for_token(peek(parser));
	if (op == NOT_ASSIGN)
		return false;
	advance(parser);
	*out = op;
	return true;
}

/* VarDeclProto <- (KEYWORD_const / KEYWORD_var) IDENTIFIER (COLON TypeExpr)? ByteAlign? AddrSpace? LinkSection? */
static AstVarDecl *
parse_var_decl_proto(Parser *parser)
{
	Token keyword = advance(parser);
	AstVarDecl *decl = NEW_NODE(parser, AstVarDecl, AST_VAR_DECL, keyword.start);
	decl->is_const = keyword.kind == TK_CONST;
	decl->name = token_span(expect(parser, TK_IDENTIFIER));
	if (accept(parser, TK_COLON))
		decl->type = parse_type_expr(parser);
	if (accept(parser, TK_ALIGN))
		decl->align = parse_paren_expr(parser);
	if (accept(parser, TK_ADDRSPACE))
		decl->addrspace = parse_paren_expr(parser);
	if (accept(parser, TK_LINKSECTION))
		decl->linksection = parse_paren_expr(parser);
	finish(parser, &decl->base);
	return decl;
}

/* Builds a destructuring assignment once the first target and a ',' have been seen. */
static AstNode *
finish_destructure(Parser *parser, AstNode *first, bool allow_var_decls, bool is_comptime)
{
	AstAssignDestructure *node = NEW_NODE(parser, AstAssignDestructure, AST_ASSIGN_DESTRUCTURE, first->offset);
	node->is_comptime = is_comptime;
	NodeBuilder targets = {0};
	node_builder_push(parser, &targets, first);
	while (accept(parser, TK_COMMA)) {
		if (allow_var_decls && (peek(parser) == TK_CONST || peek(parser) == TK_VAR))
			node_builder_push(parser, &targets, &parse_var_decl_proto(parser)->base);
		else
			node_builder_push(parser, &targets, parse_expr(parser));
	}
	expect(parser, TK_EQUAL);
	node->value = parse_expr(parser);
	node->targets = node_builder_finish(&targets);
	start_at(&node->base, first);
	return finish(parser, &node->base);
}

/* AssignExpr <- Expr (AssignOp Expr / (COMMA Expr)+ EQUAL Expr / !AssignOp !COMMA) */
static AstNode *
parse_assign_expr(Parser *parser)
{
	AstNode *target = parse_expr(parser);
	if (peek(parser) == TK_COMMA)
		return finish_destructure(parser, target, false, false);

	Token op_token = peek_token(parser, 0);
	AstAssignOp op;
	if (!parse_assign_op(parser, &op))
		return target;
	AstAssign *node = NEW_NODE(parser, AstAssign, AST_ASSIGN, op_token.start);
	node->op = op;
	node->target = target;
	node->value = parse_expr(parser);
	start_at(&node->base, target);
	return finish(parser, &node->base);
}

/*
 * VarAssignStatement <- (VarDeclProto / Expr) (COMMA (VarDeclProto / Expr))* EQUAL Expr SEMICOLON
 * plus the expression statement form: AssignExpr SEMICOLON
 */
static AstNode *
parse_var_assign_statement(Parser *parser, bool is_comptime)
{
	uint32_t comptime_offset = parser->previous.start;

	if (peek(parser) == TK_CONST || peek(parser) == TK_VAR) {
		AstVarDecl *decl = parse_var_decl_proto(parser);
		decl->is_comptime = is_comptime;
		if (is_comptime)
			decl->base.start = comptime_offset;
		if (peek(parser) == TK_COMMA) {
			AstNode *destructure = finish_destructure(parser, &decl->base, true, is_comptime);
			expect(parser, TK_SEMICOLON);
			return destructure;
		}
		/* Unlike container-level declarations, local ones need an initializer. */
		expect(parser, TK_EQUAL);
		decl->init = parse_expr(parser);
		finish(parser, &decl->base);
		expect(parser, TK_SEMICOLON);
		return &decl->base;
	}

	AstNode *first = parse_expr(parser);
	AstNode *result = first;
	if (peek(parser) == TK_COMMA) {
		result = finish_destructure(parser, first, true, is_comptime);
		expect(parser, TK_SEMICOLON);
		return result;
	}
	Token op_token = peek_token(parser, 0);
	AstAssignOp op;
	if (parse_assign_op(parser, &op)) {
		AstAssign *assign = NEW_NODE(parser, AstAssign, AST_ASSIGN, op_token.start);
		assign->op = op;
		assign->target = first;
		assign->value = parse_expr(parser);
		start_at(&assign->base, first);
		result = finish(parser, &assign->base);
	}
	if (is_comptime)
		result = new_wrap(parser, AST_COMPTIME, comptime_offset, result);
	expect(parser, TK_SEMICOLON);
	return result;
}

/* BlockExprStatement <- BlockExpr / !BlockExprPrefix AssignExpr SEMICOLON */
static AstNode *
parse_block_expr_statement(Parser *parser)
{
	if (at_block_expr(parser))
		return parse_block_expr(parser);
	AstNode *expr = parse_assign_expr(parser);
	expect(parser, TK_SEMICOLON);
	return expr;
}

/*
 * BlockStatement
 *     <- Statement
 *      / KEYWORD_defer BlockExprStatement
 *      / KEYWORD_errdefer Payload? BlockExprStatement
 *      / !StatementPrefix KEYWORD_comptime? VarAssignStatement
 * Statement
 *     <- IfStatement
 *      / LabeledStatement
 *      / KEYWORD_nosuspend BlockExprStatement
 *      / KEYWORD_comptime BlockExpr
 *      / KEYWORD_suspend BlockExprStatement
 *      / !StatementPrefix KEYWORD_comptime? AssignExpr SEMICOLON
 * LabeledStatement <- BlockLabel? (Block / LoopStatement / SwitchExpr)
 * LoopStatement <- KEYWORD_inline? (ForStatement / WhileStatement)
 *
 * `allow_defer_and_var` is true for BlockStatement and false for Statement
 * (the `else` branch of an if/while/for statement).
 */
static AstNode *
parse_statement(Parser *parser, bool allow_defer_and_var)
{
	Token token = peek_token(parser, 0);
	AstNode *result;

	enter_nesting(parser);
	switch (token.kind) {
	case TK_COMPTIME:
		advance(parser);
		if (at_block_expr(parser)) {
			result = new_wrap(parser, AST_COMPTIME, token.start, parse_block_expr(parser));
			break;
		}
		if (allow_defer_and_var) {
			result = parse_var_assign_statement(parser, true);
			break;
		}
		result = new_wrap(parser, AST_COMPTIME, token.start, parse_assign_expr(parser));
		expect(parser, TK_SEMICOLON);
		break;
	case TK_NOSUSPEND:
		advance(parser);
		result = new_wrap(parser, AST_NOSUSPEND, token.start, parse_block_expr_statement(parser));
		result->end = AST_CAST(AstWrap, result, AST_NOSUSPEND)->operand->end;
		break;
	case TK_SUSPEND: {
		advance(parser);
		AstSuspend *node = NEW_NODE(parser, AstSuspend, AST_SUSPEND, token.start);
		node->body = parse_block_expr_statement(parser);
		node->base.end = node->body->end;
		result = &node->base;
		break;
	}
	case TK_DEFER:
	case TK_ERRDEFER: {
		if (!allow_defer_and_var)
			fail_at(parser, token.start, "defer is not allowed here");
		advance(parser);
		AstDefer *node = NEW_NODE(parser, AstDefer, AST_DEFER, token.start);
		node->is_errdefer = token.kind == TK_ERRDEFER;
		node->body = parse_block_expr_statement(parser);
		node->base.end = node->body->end;
		result = &node->base;
		break;
	}
	case TK_IF:
		result = parse_if(parser, BODY_STATEMENT);
		break;
	case TK_INLINE:
		advance(parser);
		if (peek(parser) == TK_FOR)
			result = parse_for(parser, parse_block_label(parser), true, BODY_STATEMENT);
		else if (peek(parser) == TK_WHILE)
			result = parse_while(parser, parse_block_label(parser), true, BODY_STATEMENT);
		else
			fail_expected(parser, "'for' or 'while' after 'inline'");
		break;
	case TK_FOR:
		result = parse_for(parser, parse_block_label(parser), false, BODY_STATEMENT);
		break;
	case TK_WHILE:
		result = parse_while(parser, parse_block_label(parser), false, BODY_STATEMENT);
		break;
	case TK_SWITCH:
		result = parse_switch(parser, parse_block_label(parser));
		break;
	case TK_L_BRACE:
		result = parse_block(parser, parse_block_label(parser));
		break;
	case TK_CONST:
	case TK_VAR:
		if (!allow_defer_and_var)
			fail_at(parser, token.start, "variable declaration is not allowed here");
		result = parse_var_assign_statement(parser, false);
		break;
	default:
		if (at_block_label(parser)) {
			AstLabel label = parse_block_label(parser);
			bool is_inline = accept(parser, TK_INLINE);
			if (peek(parser) == TK_FOR)
				result = parse_for(parser, label, is_inline, BODY_STATEMENT);
			else if (peek(parser) == TK_WHILE)
				result = parse_while(parser, label, is_inline, BODY_STATEMENT);
			else if (is_inline)
				fail_expected(parser, "'for' or 'while' after 'inline'");
			else if (peek(parser) == TK_SWITCH)
				result = parse_switch(parser, label);
			else
				result = parse_block(parser, label);
			break;
		}
		if (allow_defer_and_var) {
			result = parse_var_assign_statement(parser, false);
			break;
		}
		result = parse_assign_expr(parser);
		expect(parser, TK_SEMICOLON);
		break;
	}
	leave_nesting(parser);
	return result;
}

/* Skips tokens up to the end of the broken statement: a ';' or the enclosing '}' at the same depth. */
static void
skip_to_next_statement(Parser *parser)
{
	uint32_t level = 0;
	for (;;) {
		TokenKind kind = peek(parser);
		switch (kind) {
		case TK_EOF:
			return;
		case TK_SEMICOLON:
			advance(parser);
			if (level == 0)
				return;
			break;
		case TK_L_BRACE:
		case TK_L_PAREN:
		case TK_L_BRACKET:
			level = level + 1;
			advance(parser);
			break;
		case TK_R_PAREN:
		case TK_R_BRACKET:
			if (level > 0)
				level = level - 1;
			advance(parser);
			break;
		case TK_R_BRACE:
			if (level == 0)
				return;
			level = level - 1;
			advance(parser);
			break;
		default:
			advance(parser);
			break;
		}
	}
}

/* Parses one statement; on a syntax error, recovers and returns NULL. */
static AstNode *
parse_statement_recovering(Parser *parser)
{
	jmp_buf here;
	jmp_buf *saved_recovery = parser->recovery;
	uint32_t saved_nesting = parser->nesting;
	uint32_t start = peek_token(parser, 0).start;

	parser->recovery = &here;
	if (setjmp(here) != 0) {
		parser->recovery = saved_recovery;
		parser->nesting = saved_nesting;
		skip_to_next_statement(parser);
		if (peek_token(parser, 0).start == start && peek(parser) != TK_R_BRACE)
			advance(parser);
		return NULL;
	}
	AstNode *statement = parse_statement(parser, true);
	parser->recovery = saved_recovery;
	return statement;
}

/* Block <- LBRACE BlockStatement* RBRACE */
static AstNode *
parse_block(Parser *parser, AstLabel label)
{
	Token brace = expect(parser, TK_L_BRACE);
	AstBlock *node = NEW_NODE(parser, AstBlock, AST_BLOCK, brace.start);
	if (label.has_label)
		node->base.start = label.name.start;
	node->label = label;

	NodeBuilder statements = {0};
	while (peek(parser) != TK_R_BRACE && peek(parser) != TK_EOF) {
		AstNode *statement = parse_statement_recovering(parser);
		if (statement != NULL)
			node_builder_push(parser, &statements, statement);
	}
	node->statements = node_builder_finish(&statements);
	expect(parser, TK_R_BRACE);
	return finish(parser, &node->base);
}

/* ---------- Declarations ---------- */

/*
 * ParamDecl <- doc_comment? (KEYWORD_noalias / KEYWORD_comptime)? (IDENTIFIER COLON)? ParamType
 *            / DOT3
 * ParamType <- KEYWORD_anytype / TypeExpr
 */
static void
parse_param(Parser *parser, AstParam *param)
{
	param->kind = AST_PARAM_NORMAL;
	Token doc;
	eat_doc_comments(parser, &doc);
	if (accept(parser, TK_NOALIAS))
		param->kind = AST_PARAM_NOALIAS;
	else if (accept(parser, TK_COMPTIME))
		param->kind = AST_PARAM_COMPTIME;

	if (peek(parser) == TK_IDENTIFIER && peek_at(parser, 1) == TK_COLON) {
		param->has_name = true;
		param->name = token_span(advance(parser));
		advance(parser);
	}
	if (accept(parser, TK_ANYTYPE)) {
		param->is_anytype = true;
		return;
	}
	param->type = parse_type_expr(parser);
}

/*
 * FnProto <- KEYWORD_fn IDENTIFIER? ParamDeclList ByteAlign? AddrSpace? LinkSection? CallConv? EXCLAMATIONMARK? TypeExpr
 * ParamDeclList <- LPAREN ParamDeclListRest
 * ParamDeclListRest <- RPAREN / DOT3 COMMA? RPAREN / ParamDecl (COMMA ParamDeclListRest / RPAREN)
 */
static AstFnProto *
parse_fn_proto(Parser *parser)
{
	Token fn_token = expect(parser, TK_FN);
	AstFnProto *proto = NEW_NODE(parser, AstFnProto, AST_FN_PROTO, fn_token.start);
	if (peek(parser) == TK_IDENTIFIER) {
		proto->has_name = true;
		proto->name = token_span(advance(parser));
	}

	uint32_t capacity = 0;
	expect(parser, TK_L_PAREN);
	while (peek(parser) != TK_R_PAREN) {
		if (accept(parser, TK_ELLIPSIS3)) {
			proto->is_var_args = true;
			accept(parser, TK_COMMA);
			break;
		}
		AstParam *param = GROW_ONE(parser, proto->params, proto->param_count, capacity, AstParam);
		parse_param(parser, param);
		if (!accept(parser, TK_COMMA))
			break;
	}
	expect(parser, TK_R_PAREN);

	if (accept(parser, TK_ALIGN))
		proto->align = parse_paren_expr(parser);
	if (accept(parser, TK_ADDRSPACE))
		proto->addrspace = parse_paren_expr(parser);
	if (accept(parser, TK_LINKSECTION))
		proto->linksection = parse_paren_expr(parser);
	if (accept(parser, TK_CALLCONV))
		proto->callconv = parse_paren_expr(parser);
	proto->inferred_error = accept(parser, TK_BANG);
	proto->return_type = parse_type_expr(parser);
	finish(parser, &proto->base);
	return proto;
}

/*
 * Decl
 *     <- (KEYWORD_export / KEYWORD_inline / KEYWORD_noinline)? FnProto (SEMICOLON / Block)
 *      / KEYWORD_extern STRINGLITERALSINGLE? FnProto SEMICOLON
 *      / (KEYWORD_export / KEYWORD_extern STRINGLITERALSINGLE?)? KEYWORD_threadlocal? GlobalVarDecl
 * GlobalVarDecl <- VarDeclProto (EQUAL Expr)? SEMICOLON
 */
static AstNode *
parse_decl(Parser *parser, bool is_pub)
{
	Token first = peek_token(parser, 0);
	uint32_t decl_start = is_pub ? parser->previous.start : first.start;
	AstFnModifier fn_modifier = AST_FN_MODIFIER_NONE;
	AstVarModifier var_modifier = AST_VAR_MODIFIER_NONE;
	bool has_extern_lib = false;
	Span extern_lib = {0, 0};
	bool allows_var = true;

	switch (first.kind) {
	case TK_EXPORT:
		advance(parser);
		fn_modifier = AST_FN_MODIFIER_EXPORT;
		var_modifier = AST_VAR_MODIFIER_EXPORT;
		break;
	case TK_EXTERN:
		advance(parser);
		fn_modifier = AST_FN_MODIFIER_EXTERN;
		var_modifier = AST_VAR_MODIFIER_EXTERN;
		if (peek(parser) == TK_STRING_LITERAL) {
			has_extern_lib = true;
			extern_lib = token_span(advance(parser));
		}
		break;
	case TK_INLINE:
		advance(parser);
		fn_modifier = AST_FN_MODIFIER_INLINE;
		allows_var = false;
		break;
	case TK_NOINLINE:
		advance(parser);
		fn_modifier = AST_FN_MODIFIER_NOINLINE;
		allows_var = false;
		break;
	default:
		break;
	}

	if (peek(parser) == TK_FN) {
		AstFnDecl *decl = NEW_NODE(parser, AstFnDecl, AST_FN_DECL, first.start);
		decl->base.start = decl_start;
		decl->is_pub = is_pub;
		decl->modifier = fn_modifier;
		decl->has_extern_lib = has_extern_lib;
		decl->extern_lib = extern_lib;
		decl->proto = parse_fn_proto(parser);
		decl->proto->base.start = decl_start;
		if (accept(parser, TK_SEMICOLON))
			return finish(parser, &decl->base);
		if (fn_modifier == AST_FN_MODIFIER_EXTERN)
			fail_expected(parser, "';' after extern function prototype");
		AstLabel no_label = {false, {0, 0}};
		decl->body = parse_block(parser, no_label);
		return finish(parser, &decl->base);
	}

	if (!allows_var)
		fail_expected(parser, "'fn'");
	bool is_threadlocal = accept(parser, TK_THREADLOCAL);
	if (peek(parser) != TK_CONST && peek(parser) != TK_VAR)
		fail_expected(parser, "'fn', 'const' or 'var'");

	AstVarDecl *decl = parse_var_decl_proto(parser);
	decl->base.offset = first.start;
	decl->base.start = decl_start;
	decl->is_pub = is_pub;
	decl->is_threadlocal = is_threadlocal;
	decl->modifier = var_modifier;
	decl->has_extern_lib = has_extern_lib;
	decl->extern_lib = extern_lib;
	if (accept(parser, TK_EQUAL))
		decl->init = parse_expr(parser);
	finish(parser, &decl->base);
	expect(parser, TK_SEMICOLON);
	return &decl->base;
}

/* ContainerField <- doc_comment? KEYWORD_comptime? (IDENTIFIER COLON)? TypeExpr ByteAlign? (EQUAL Expr)? */
static AstNode *
parse_container_field(Parser *parser)
{
	Token first = peek_token(parser, 0);
	AstContainerField *field = NEW_NODE(parser, AstContainerField, AST_CONTAINER_FIELD, first.start);
	field->is_comptime = accept(parser, TK_COMPTIME);
	if (peek(parser) == TK_IDENTIFIER && peek_at(parser, 1) == TK_COLON) {
		field->has_name = true;
		field->name = token_span(advance(parser));
		advance(parser);
	}
	field->type = parse_type_expr(parser);
	if (accept(parser, TK_ALIGN))
		field->align = parse_paren_expr(parser);
	if (accept(parser, TK_EQUAL))
		field->default_value = parse_expr(parser);
	return finish(parser, &field->base);
}

/*
 * ContainerDecl <- TestDecl / ComptimeDecl / doc_comment? KEYWORD_pub? Decl
 * TestDecl <- KEYWORD_test (STRINGLITERALSINGLE / IDENTIFIER)? Block
 * ComptimeDecl <- KEYWORD_comptime Block
 * ContainerMembersRest <- !ContainerDeclPrefix ContainerField (COMMA ContainerMembersRest)? / ContainerDecl*
 *
 * Parses one member. A field must be followed by ',' unless it is the last member.
 */
static AstNode *
parse_container_member(Parser *parser)
{
	Token token = peek_token(parser, 0);
	AstLabel no_label = {false, {0, 0}};

	switch (token.kind) {
	case TK_TEST: {
		advance(parser);
		AstTestDecl *decl = NEW_NODE(parser, AstTestDecl, AST_TEST_DECL, token.start);
		if (peek(parser) == TK_STRING_LITERAL || peek(parser) == TK_IDENTIFIER) {
			Token name = advance(parser);
			decl->has_name = true;
			decl->name_is_identifier = name.kind == TK_IDENTIFIER;
			decl->name = token_span(name);
		}
		decl->body = parse_block(parser, no_label);
		return finish(parser, &decl->base);
	}
	case TK_COMPTIME:
		if (peek_at(parser, 1) == TK_L_BRACE) {
			advance(parser);
			AstComptimeDecl *decl = NEW_NODE(parser, AstComptimeDecl, AST_COMPTIME_DECL, token.start);
			decl->body = parse_block(parser, no_label);
			return finish(parser, &decl->base);
		}
		break;
	case TK_PUB:
		advance(parser);
		return parse_decl(parser, true);
	case TK_EXPORT:
	case TK_EXTERN:
		/* `extern struct { ... }` as a tuple field type is a field, not a decl. */
		if (token.kind == TK_EXTERN && (peek_at(parser, 1) == TK_STRUCT || peek_at(parser, 1) == TK_UNION || peek_at(parser, 1) == TK_ENUM))
			break;
		return parse_decl(parser, false);
	case TK_INLINE:
	case TK_NOINLINE:
	case TK_THREADLOCAL:
	case TK_CONST:
	case TK_VAR:
	case TK_FN:
		return parse_decl(parser, false);
	default:
		break;
	}

	AstNode *field = parse_container_field(parser);
	if (!accept(parser, TK_COMMA) && peek(parser) != TK_R_BRACE && peek(parser) != TK_EOF)
		fail_expected(parser, "',' after field");
	return field;
}

static bool
starts_container_member(TokenKind kind)
{
	switch (kind) {
	case TK_PUB:
	case TK_TEST:
	case TK_COMPTIME:
	case TK_EXPORT:
	case TK_EXTERN:
	case TK_INLINE:
	case TK_NOINLINE:
	case TK_THREADLOCAL:
	case TK_CONST:
	case TK_VAR:
	case TK_FN:
		return true;
	default:
		return false;
	}
}

/* Skips to where the next container member probably starts. */
static void
skip_to_next_member(Parser *parser)
{
	uint32_t level = 0;
	for (;;) {
		TokenKind kind = peek(parser);
		if (kind == TK_EOF)
			return;
		if (level == 0 && starts_container_member(kind))
			return;
		switch (kind) {
		case TK_COMMA:
		case TK_SEMICOLON:
			advance(parser);
			if (level == 0)
				return;
			break;
		case TK_L_BRACE:
		case TK_L_PAREN:
		case TK_L_BRACKET:
			level = level + 1;
			advance(parser);
			break;
		case TK_R_PAREN:
		case TK_R_BRACKET:
			if (level > 0)
				level = level - 1;
			advance(parser);
			break;
		case TK_R_BRACE:
			if (level == 0)
				return;
			level = level - 1;
			advance(parser);
			break;
		default:
			advance(parser);
			break;
		}
	}
}

/* Parses one member; on a syntax error, recovers and returns NULL. */
static AstNode *
parse_member_recovering(Parser *parser)
{
	jmp_buf here;
	jmp_buf *saved_recovery = parser->recovery;
	uint32_t saved_nesting = parser->nesting;
	uint32_t start = peek_token(parser, 0).start;

	parser->recovery = &here;
	if (setjmp(here) != 0) {
		parser->recovery = saved_recovery;
		parser->nesting = saved_nesting;
		if (peek_token(parser, 0).start == start)
			advance(parser);
		skip_to_next_member(parser);
		return NULL;
	}
	AstNode *member = parse_container_member(parser);
	parser->recovery = saved_recovery;
	return member;
}

/* Where a container's members are relative to its fields. */
typedef enum FieldState {
	FIELDS_NONE,	/* no field seen yet */
	FIELDS_SEEN,	/* in the run of fields */
	FIELDS_ENDED,	/* a declaration followed the fields */
	FIELDS_REPORTED,	/* a declaration between fields was already reported */
} FieldState;

typedef struct FieldOrder {
	FieldState state;
	uint32_t   decl_offset;	/* FIELDS_ENDED: the declaration after the fields */
} FieldOrder;

/* Fields must be contiguous: reports a declaration that separates two fields. */
static void
track_field_order(Parser *parser, FieldOrder *order, const AstNode *member)
{
	bool is_field = member->kind == AST_CONTAINER_FIELD;

	switch (order->state) {
	case FIELDS_NONE:
		if (is_field)
			order->state = FIELDS_SEEN;
		break;
	case FIELDS_SEEN:
		if (!is_field) {
			order->state = FIELDS_ENDED;
			order->decl_offset = member->offset;
		}
		break;
	case FIELDS_ENDED:
		if (is_field) {
			warn_at(parser, order->decl_offset, "declarations are not allowed between container fields");
			order->state = FIELDS_REPORTED;
		}
		break;
	case FIELDS_REPORTED:
		break;
	}
}

/*
 * ContainerMembers <- container_doc_comment? ContainerDecl* ContainerMembersRest
 * Stops at '}' (or EOF). At the root a stray '}' is reported and ends the parse.
 */
static AstNodeList
parse_container_members(Parser *parser, bool is_root)
{
	NodeBuilder members = {0};
	FieldOrder field_order = {FIELDS_NONE, 0};

	while (peek(parser) == TK_CONTAINER_DOC_COMMENT)
		advance(parser);
	for (;;) {
		Token doc;
		bool has_doc = eat_doc_comments(parser, &doc);
		Token token = peek_token(parser, 0);
		if (token.kind == TK_EOF || token.kind == TK_R_BRACE) {
			if (has_doc)
				warn_at(parser, doc.start, "unattached documentation comment");
			/* Like the reference parser, stop at a stray '}' in the root: what follows is likely garbage. */
			if (token.kind == TK_R_BRACE && is_root)
				warn_at(parser, token.start, "unexpected '}'");
			break;
		}
		if (has_doc && token.kind == TK_TEST)
			warn_at(parser, doc.start, "documentation comments cannot be attached to tests");
		if (has_doc && token.kind == TK_COMPTIME && peek_at(parser, 1) == TK_L_BRACE)
			warn_at(parser, doc.start, "documentation comments cannot be attached to comptime blocks");
		AstNode *member = parse_member_recovering(parser);
		if (member == NULL)
			continue;
		node_builder_push(parser, &members, member);
		track_field_order(parser, &field_order, member);
	}
	return node_builder_finish(&members);
}

/* Root <- skip container_doc_comment? ContainerMembers eof */
AstContainerDecl *
parse_file(Arena *arena, Diag *diag, SourceTable *sources, FileId file)
{
	SourceFile *source = source_get(sources, file);
	Parser parser;
	memset(&parser, 0, sizeof(parser));
	parser.arena = arena;
	parser.diag = diag;
	parser.file = file;
	parser.source = source->text;
	parser.source_length = source->length;
	lexer_init(&parser.lexer, source->text, source->length);

	AstContainerDecl *root = NEW_NODE(&parser, AstContainerDecl, AST_CONTAINER_DECL, 0);
	root->container_kind = AST_CONTAINER_STRUCT;
	root->layout = AST_LAYOUT_AUTO;

	jmp_buf here;
	parser.recovery = &here;
	if (setjmp(here) != 0)
		return root;
	root->members = parse_container_members(&parser, true);
	return root;
}
