#ifndef SZC_LEXER_H
#define SZC_LEXER_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Token kinds. Names follow std.zig.Token.Tag of Zig 0.17.0, upper-cased,
 * with the "keyword_" prefix dropped (keyword_pub -> TK_PUB).
 * X(name, lexeme): lexeme is NULL for tokens whose text varies.
 */
#define SZC_TOKEN_LIST(X) \
	X(INVALID, NULL) \
	X(INVALID_PERIODASTERISKS, ".**") \
	X(IDENTIFIER, NULL) \
	X(STRING_LITERAL, NULL) \
	X(MULTILINE_STRING_LITERAL_LINE, NULL) \
	X(CHAR_LITERAL, NULL) \
	X(EOF, NULL) \
	X(BUILTIN, NULL) \
	X(BANG, "!") \
	X(PIPE, "|") \
	X(PIPE_PIPE, "||") \
	X(PIPE_EQUAL, "|=") \
	X(EQUAL, "=") \
	X(EQUAL_EQUAL, "==") \
	X(EQUAL_ANGLE_BRACKET_RIGHT, "=>") \
	X(BANG_EQUAL, "!=") \
	X(L_PAREN, "(") \
	X(R_PAREN, ")") \
	X(SEMICOLON, ";") \
	X(PERCENT, "%") \
	X(PERCENT_EQUAL, "%=") \
	X(L_BRACE, "{") \
	X(R_BRACE, "}") \
	X(L_BRACKET, "[") \
	X(R_BRACKET, "]") \
	X(PERIOD, ".") \
	X(PERIOD_ASTERISK, ".*") \
	X(ELLIPSIS2, "..") \
	X(ELLIPSIS3, "...") \
	X(CARET, "^") \
	X(CARET_EQUAL, "^=") \
	X(PLUS, "+") \
	X(PLUS_PLUS, "++") \
	X(PLUS_EQUAL, "+=") \
	X(PLUS_PERCENT, "+%") \
	X(PLUS_PERCENT_EQUAL, "+%=") \
	X(PLUS_PIPE, "+|") \
	X(PLUS_PIPE_EQUAL, "+|=") \
	X(MINUS, "-") \
	X(MINUS_EQUAL, "-=") \
	X(MINUS_PERCENT, "-%") \
	X(MINUS_PERCENT_EQUAL, "-%=") \
	X(MINUS_PIPE, "-|") \
	X(MINUS_PIPE_EQUAL, "-|=") \
	X(ASTERISK, "*") \
	X(ASTERISK_EQUAL, "*=") \
	X(ASTERISK_ASTERISK, "**") \
	X(ASTERISK_PERCENT, "*%") \
	X(ASTERISK_PERCENT_EQUAL, "*%=") \
	X(ASTERISK_PIPE, "*|") \
	X(ASTERISK_PIPE_EQUAL, "*|=") \
	X(ARROW, "->") \
	X(COLON, ":") \
	X(SLASH, "/") \
	X(SLASH_EQUAL, "/=") \
	X(COMMA, ",") \
	X(AMPERSAND, "&") \
	X(AMPERSAND_EQUAL, "&=") \
	X(QUESTION_MARK, "?") \
	X(ANGLE_BRACKET_LEFT, "<") \
	X(ANGLE_BRACKET_LEFT_EQUAL, "<=") \
	X(ANGLE_BRACKET_ANGLE_BRACKET_LEFT, "<<") \
	X(ANGLE_BRACKET_ANGLE_BRACKET_LEFT_EQUAL, "<<=") \
	X(ANGLE_BRACKET_ANGLE_BRACKET_LEFT_PIPE, "<<|") \
	X(ANGLE_BRACKET_ANGLE_BRACKET_LEFT_PIPE_EQUAL, "<<|=") \
	X(ANGLE_BRACKET_RIGHT, ">") \
	X(ANGLE_BRACKET_RIGHT_EQUAL, ">=") \
	X(ANGLE_BRACKET_ANGLE_BRACKET_RIGHT, ">>") \
	X(ANGLE_BRACKET_ANGLE_BRACKET_RIGHT_EQUAL, ">>=") \
	X(TILDE, "~") \
	X(NUMBER_LITERAL, NULL) \
	X(DOC_COMMENT, NULL) \
	X(CONTAINER_DOC_COMMENT, NULL) \
	X(ADDRSPACE, "addrspace") \
	X(ALIGN, "align") \
	X(ALLOWZERO, "allowzero") \
	X(AND, "and") \
	X(ANYFRAME, "anyframe") \
	X(ANYTYPE, "anytype") \
	X(ASM, "asm") \
	X(BREAK, "break") \
	X(CALLCONV, "callconv") \
	X(CATCH, "catch") \
	X(COMPTIME, "comptime") \
	X(CONST, "const") \
	X(CONTINUE, "continue") \
	X(DEFER, "defer") \
	X(ELSE, "else") \
	X(ENUM, "enum") \
	X(ERRDEFER, "errdefer") \
	X(ERROR, "error") \
	X(EXPORT, "export") \
	X(EXTERN, "extern") \
	X(FN, "fn") \
	X(FOR, "for") \
	X(IF, "if") \
	X(INLINE, "inline") \
	X(NOALIAS, "noalias") \
	X(NOINLINE, "noinline") \
	X(NOSUSPEND, "nosuspend") \
	X(OPAQUE, "opaque") \
	X(OR, "or") \
	X(ORELSE, "orelse") \
	X(PACKED, "packed") \
	X(PUB, "pub") \
	X(RESUME, "resume") \
	X(RETURN, "return") \
	X(LINKSECTION, "linksection") \
	X(STRUCT, "struct") \
	X(SUSPEND, "suspend") \
	X(SWITCH, "switch") \
	X(TEST, "test") \
	X(THREADLOCAL, "threadlocal") \
	X(TRY, "try") \
	X(UNION, "union") \
	X(UNREACHABLE, "unreachable") \
	X(VAR, "var") \
	X(VOLATILE, "volatile") \
	X(WHILE, "while")

typedef enum TokenKind {
#define SZC_TOKEN_ENUM(name, lexeme) TK_##name,
	SZC_TOKEN_LIST(SZC_TOKEN_ENUM)
#undef SZC_TOKEN_ENUM
	TK_COUNT
} TokenKind;

/* A token refers to bytes [start, end) of the source; text is never copied. */
typedef struct Token {
	TokenKind kind;
	uint32_t  start;
	uint32_t  end;
} Token;

/*
 * On-demand lexer: each lexer_next() call scans exactly one token.
 * `source` must be NUL-terminated at source[length].
 * After an invalid token the lexer resumes at the next newline.
 * Once the end is reached, lexer_next keeps returning TK_EOF.
 */
typedef struct Lexer {
	const char *source;
	uint32_t   length;
	uint32_t   index;
} Lexer;

void        lexer_init(Lexer *lexer, const char *source, uint32_t length);
Token       lexer_next(Lexer *lexer);
const char *token_kind_name(TokenKind kind);
const char *token_kind_lexeme(TokenKind kind);
const char *token_kind_describe(TokenKind kind);
bool        token_kind_is_keyword(TokenKind kind);
TokenKind   lexer_keyword_lookup(const char *text, uint32_t length);

#endif
