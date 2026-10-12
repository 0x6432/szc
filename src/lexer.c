/*
 * lexer.c: on-demand tokenizer for Zig 0.17.0.
 *
 * It matches lib/std/zig/tokenizer.zig: the same token kinds, the same
 * boundaries and the same recovery (after an invalid token, start again
 * at the next newline). Number literals are scanned loosely; their exact
 * format is checked later when the literal is decoded.
 */
#include "lexer.h"

#include <string.h>

typedef enum LexState {
	LS_START,
	LS_DONE,
	LS_EXPECT_NEWLINE,
	LS_IDENTIFIER,
	LS_BUILTIN,
	LS_STRING_LITERAL,
	LS_STRING_LITERAL_BACKSLASH,
	LS_MULTILINE_STRING_LITERAL_LINE,
	LS_CHAR_LITERAL,
	LS_CHAR_LITERAL_BACKSLASH,
	LS_BACKSLASH,
	LS_EQUAL,
	LS_BANG,
	LS_PIPE,
	LS_MINUS,
	LS_MINUS_PERCENT,
	LS_MINUS_PIPE,
	LS_ASTERISK,
	LS_ASTERISK_PERCENT,
	LS_ASTERISK_PIPE,
	LS_SLASH,
	LS_LINE_COMMENT_START,
	LS_LINE_COMMENT,
	LS_DOC_COMMENT_START,
	LS_DOC_COMMENT,
	LS_INT,
	LS_INT_EXPONENT,
	LS_INT_PERIOD,
	LS_FLOAT,
	LS_FLOAT_EXPONENT,
	LS_AMPERSAND,
	LS_CARET,
	LS_PERCENT,
	LS_PLUS,
	LS_PLUS_PERCENT,
	LS_PLUS_PIPE,
	LS_ANGLE_BRACKET_LEFT,
	LS_ANGLE_BRACKET_ANGLE_BRACKET_LEFT,
	LS_ANGLE_BRACKET_ANGLE_BRACKET_LEFT_PIPE,
	LS_ANGLE_BRACKET_RIGHT,
	LS_ANGLE_BRACKET_ANGLE_BRACKET_RIGHT,
	LS_PERIOD,
	LS_PERIOD_2,
	LS_PERIOD_ASTERISK,
	LS_SAW_AT_SIGN,
	LS_INVALID,
} LexState;

static const char *const token_names[TK_COUNT] = {
#define SZC_TOKEN_NAME(name, lexeme) "TK_" #name,
	SZC_TOKEN_LIST(SZC_TOKEN_NAME)
#undef SZC_TOKEN_NAME
};

static const char *const token_lexemes[TK_COUNT] = {
#define SZC_TOKEN_LEXEME(name, lexeme) lexeme,
	SZC_TOKEN_LIST(SZC_TOKEN_LEXEME)
#undef SZC_TOKEN_LEXEME
};

const char *
token_kind_name(TokenKind kind)
{
	if (kind >= TK_COUNT)
		return "TK_?";
	return token_names[kind];
}

const char *
token_kind_lexeme(TokenKind kind)
{
	if (kind >= TK_COUNT)
		return NULL;
	return token_lexemes[kind];
}

bool
token_kind_is_keyword(TokenKind kind)
{
	return kind >= TK_ADDRSPACE && kind <= TK_WHILE;
}

/* Human readable description used in "expected X, found Y" diagnostics. */
const char *
token_kind_describe(TokenKind kind)
{
	switch (kind) {
	case TK_INVALID:
		return "invalid token";
	case TK_IDENTIFIER:
		return "an identifier";
	case TK_STRING_LITERAL:
	case TK_MULTILINE_STRING_LITERAL_LINE:
		return "a string literal";
	case TK_CHAR_LITERAL:
		return "a character literal";
	case TK_EOF:
		return "EOF";
	case TK_BUILTIN:
		return "a builtin function";
	case TK_NUMBER_LITERAL:
		return "a number literal";
	case TK_DOC_COMMENT:
	case TK_CONTAINER_DOC_COMMENT:
		return "a document comment";
	default:
		break;
	}
	const char *lexeme = token_kind_lexeme(kind);
	if (lexeme == NULL)
		return "a token";
	return lexeme;
}

typedef struct KeywordEntry {
	const char *text;
	uint8_t    length;
	TokenKind  kind;
} KeywordEntry;

/* Keywords grouped by first letter so lookup does at most a handful of compares. */
static const KeywordEntry keywords_a[] = {
	{"addrspace", 9, TK_ADDRSPACE}, {"align", 5, TK_ALIGN}, {"allowzero", 9, TK_ALLOWZERO},
	{"and", 3, TK_AND}, {"anyframe", 8, TK_ANYFRAME}, {"anytype", 7, TK_ANYTYPE},
	{"asm", 3, TK_ASM},
};
static const KeywordEntry keywords_b[] = {
	{"break", 5, TK_BREAK},
};
static const KeywordEntry keywords_c[] = {
	{"callconv", 8, TK_CALLCONV}, {"catch", 5, TK_CATCH}, {"comptime", 8, TK_COMPTIME},
	{"const", 5, TK_CONST}, {"continue", 8, TK_CONTINUE},
};
static const KeywordEntry keywords_d[] = {
	{"defer", 5, TK_DEFER},
};
static const KeywordEntry keywords_e[] = {
	{"else", 4, TK_ELSE}, {"enum", 4, TK_ENUM}, {"errdefer", 8, TK_ERRDEFER},
	{"error", 5, TK_ERROR}, {"export", 6, TK_EXPORT}, {"extern", 6, TK_EXTERN},
};
static const KeywordEntry keywords_f[] = {
	{"fn", 2, TK_FN}, {"for", 3, TK_FOR},
};
static const KeywordEntry keywords_i[] = {
	{"if", 2, TK_IF}, {"inline", 6, TK_INLINE},
};
static const KeywordEntry keywords_l[] = {
	{"linksection", 11, TK_LINKSECTION},
};
static const KeywordEntry keywords_n[] = {
	{"noalias", 7, TK_NOALIAS}, {"noinline", 8, TK_NOINLINE}, {"nosuspend", 9, TK_NOSUSPEND},
};
static const KeywordEntry keywords_o[] = {
	{"opaque", 6, TK_OPAQUE}, {"or", 2, TK_OR}, {"orelse", 6, TK_ORELSE},
};
static const KeywordEntry keywords_p[] = {
	{"packed", 6, TK_PACKED}, {"pub", 3, TK_PUB},
};
static const KeywordEntry keywords_r[] = {
	{"resume", 6, TK_RESUME}, {"return", 6, TK_RETURN},
};
static const KeywordEntry keywords_s[] = {
	{"struct", 6, TK_STRUCT}, {"suspend", 7, TK_SUSPEND}, {"switch", 6, TK_SWITCH},
};
static const KeywordEntry keywords_t[] = {
	{"test", 4, TK_TEST}, {"threadlocal", 11, TK_THREADLOCAL}, {"try", 3, TK_TRY},
};
static const KeywordEntry keywords_u[] = {
	{"union", 5, TK_UNION}, {"unreachable", 11, TK_UNREACHABLE},
};
static const KeywordEntry keywords_v[] = {
	{"var", 3, TK_VAR}, {"volatile", 8, TK_VOLATILE},
};
static const KeywordEntry keywords_w[] = {
	{"while", 5, TK_WHILE},
};

#define KEYWORD_GROUP(array) (array), (sizeof(array) / sizeof((array)[0]))

static TokenKind
search_group(const KeywordEntry *group, size_t count, const char *text, uint32_t length)
{
	for (size_t i = 0; i < count; i++) {
		if (group[i].length == length && memcmp(group[i].text, text, length) == 0)
			return group[i].kind;
	}
	return TK_IDENTIFIER;
}

/* Returns the keyword kind for an identifier's text, or TK_IDENTIFIER. */
TokenKind
lexer_keyword_lookup(const char *text, uint32_t length)
{
	if (length < 2 || length > 11)
		return TK_IDENTIFIER;

	switch (text[0]) {
	case 'a':
		return search_group(KEYWORD_GROUP(keywords_a), text, length);
	case 'b':
		return search_group(KEYWORD_GROUP(keywords_b), text, length);
	case 'c':
		return search_group(KEYWORD_GROUP(keywords_c), text, length);
	case 'd':
		return search_group(KEYWORD_GROUP(keywords_d), text, length);
	case 'e':
		return search_group(KEYWORD_GROUP(keywords_e), text, length);
	case 'f':
		return search_group(KEYWORD_GROUP(keywords_f), text, length);
	case 'i':
		return search_group(KEYWORD_GROUP(keywords_i), text, length);
	case 'l':
		return search_group(KEYWORD_GROUP(keywords_l), text, length);
	case 'n':
		return search_group(KEYWORD_GROUP(keywords_n), text, length);
	case 'o':
		return search_group(KEYWORD_GROUP(keywords_o), text, length);
	case 'p':
		return search_group(KEYWORD_GROUP(keywords_p), text, length);
	case 'r':
		return search_group(KEYWORD_GROUP(keywords_r), text, length);
	case 's':
		return search_group(KEYWORD_GROUP(keywords_s), text, length);
	case 't':
		return search_group(KEYWORD_GROUP(keywords_t), text, length);
	case 'u':
		return search_group(KEYWORD_GROUP(keywords_u), text, length);
	case 'v':
		return search_group(KEYWORD_GROUP(keywords_v), text, length);
	case 'w':
		return search_group(KEYWORD_GROUP(keywords_w), text, length);
	default:
		return TK_IDENTIFIER;
	}
}

void
lexer_init(Lexer *lexer, const char *source, uint32_t length)
{
	lexer->source = source;
	lexer->length = length;
	lexer->index = 0;
	if (length >= 3 && (unsigned char)source[0] == 0xEF && (unsigned char)source[1] == 0xBB && (unsigned char)source[2] == 0xBF)
		lexer->index = 3;
}

static unsigned char
current(const Lexer *lexer)
{
	return (unsigned char)lexer->source[lexer->index];
}

static unsigned char
peek_next(const Lexer *lexer)
{
	if (lexer->index >= lexer->length)
		return 0;
	return (unsigned char)lexer->source[lexer->index + 1];
}

static bool
is_at_end(const Lexer *lexer)
{
	return lexer->index >= lexer->length;
}

static bool
is_ident_start(unsigned char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool
is_ident_char(unsigned char c)
{
	return is_ident_start(c) || (c >= '0' && c <= '9');
}

/* Control characters rejected in comments and multiline strings (TAB included; CR is handled by callers). */
static bool
is_comment_control(unsigned char c)
{
	return (c >= 0x01 && c <= 0x09) || c == 0x0b || c == 0x0c || (c >= 0x0e && c <= 0x1f) || c == 0x7f;
}

/* Characters that are rejected inside single-line string and char literals. */
static bool
is_string_control(unsigned char c)
{
	return (c >= 0x01 && c <= 0x09) || (c >= 0x0b && c <= 0x1f) || c == 0x7f;
}

/* Digits and letters that may continue a number literal (excluding exponent letters e/E/p/P). */
static bool
is_number_continue(unsigned char c)
{
	if (c == '_' || (c >= '0' && c <= '9'))
		return true;
	if (c == 'e' || c == 'E' || c == 'p' || c == 'P')
		return false;
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool
is_exponent_marker(unsigned char c)
{
	return c == 'e' || c == 'E' || c == 'p' || c == 'P';
}

/* For "operator or operator=" states: consume '=' if present. */
static void
finish_with_equal(Lexer *lexer, Token *result, TokenKind plain, TokenKind with_equal)
{
	if (current(lexer) == '=') {
		result->kind = with_equal;
		lexer->index = lexer->index + 1;
	} else {
		result->kind = plain;
	}
}

static void
single(Lexer *lexer, Token *result, TokenKind kind)
{
	result->kind = kind;
	lexer->index = lexer->index + 1;
}

/* Handles a NUL byte inside a literal or comment: end of input or an invalid embedded NUL. */
static LexState
nul_in_token(const Lexer *lexer, Token *result)
{
	if (is_at_end(lexer)) {
		result->kind = TK_INVALID;
		return LS_DONE;
	}
	return LS_INVALID;
}

static LexState
lex_start(Lexer *lexer, Token *result)
{
	unsigned char c = current(lexer);

	switch (c) {
	case 0:
		if (is_at_end(lexer)) {
			result->kind = TK_EOF;
			result->start = lexer->index;
			return LS_DONE;
		}
		return LS_INVALID;
	case ' ':
	case '\n':
	case '\t':
	case '\r':
		lexer->index = lexer->index + 1;
		result->start = lexer->index;
		return LS_START;
	case '"':
		result->kind = TK_STRING_LITERAL;
		return LS_STRING_LITERAL;
	case '\'':
		result->kind = TK_CHAR_LITERAL;
		return LS_CHAR_LITERAL;
	case '@':
		return LS_SAW_AT_SIGN;
	case '=':
		return LS_EQUAL;
	case '!':
		return LS_BANG;
	case '|':
		return LS_PIPE;
	case '(':
		single(lexer, result, TK_L_PAREN);
		return LS_DONE;
	case ')':
		single(lexer, result, TK_R_PAREN);
		return LS_DONE;
	case '[':
		single(lexer, result, TK_L_BRACKET);
		return LS_DONE;
	case ']':
		single(lexer, result, TK_R_BRACKET);
		return LS_DONE;
	case ';':
		single(lexer, result, TK_SEMICOLON);
		return LS_DONE;
	case ',':
		single(lexer, result, TK_COMMA);
		return LS_DONE;
	case '?':
		single(lexer, result, TK_QUESTION_MARK);
		return LS_DONE;
	case ':':
		single(lexer, result, TK_COLON);
		return LS_DONE;
	case '{':
		single(lexer, result, TK_L_BRACE);
		return LS_DONE;
	case '}':
		single(lexer, result, TK_R_BRACE);
		return LS_DONE;
	case '~':
		single(lexer, result, TK_TILDE);
		return LS_DONE;
	case '%':
		return LS_PERCENT;
	case '*':
		return LS_ASTERISK;
	case '+':
		return LS_PLUS;
	case '<':
		return LS_ANGLE_BRACKET_LEFT;
	case '>':
		return LS_ANGLE_BRACKET_RIGHT;
	case '^':
		return LS_CARET;
	case '\\':
		result->kind = TK_MULTILINE_STRING_LITERAL_LINE;
		return LS_BACKSLASH;
	case '.':
		return LS_PERIOD;
	case '-':
		return LS_MINUS;
	case '/':
		return LS_SLASH;
	case '&':
		return LS_AMPERSAND;
	default:
		break;
	}

	if (is_ident_start(c)) {
		result->kind = TK_IDENTIFIER;
		return LS_IDENTIFIER;
	}
	if (c >= '0' && c <= '9') {
		result->kind = TK_NUMBER_LITERAL;
		lexer->index = lexer->index + 1;
		return LS_INT;
	}
	return LS_INVALID;
}

/* After "\r" in a comment: only "\r\n" is allowed. */
static LexState
lex_expect_newline(Lexer *lexer, Token *result)
{
	lexer->index = lexer->index + 1;
	unsigned char c = current(lexer);

	if (c == 0) {
		if (is_at_end(lexer)) {
			result->kind = TK_INVALID;
			return LS_DONE;
		}
		return LS_INVALID;
	}
	if (c == '\n') {
		lexer->index = lexer->index + 1;
		result->start = lexer->index;
		return LS_START;
	}
	return LS_INVALID;
}

/* Skip to the end of the line; the whole range is one invalid token. */
static LexState
lex_invalid(Lexer *lexer, Token *result)
{
	lexer->index = lexer->index + 1;
	unsigned char c = current(lexer);

	if (c == 0) {
		if (is_at_end(lexer)) {
			result->kind = TK_INVALID;
			return LS_DONE;
		}
		return LS_INVALID;
	}
	if (c == '\n') {
		result->kind = TK_INVALID;
		return LS_DONE;
	}
	return LS_INVALID;
}

static LexState
lex_saw_at_sign(Lexer *lexer, Token *result)
{
	lexer->index = lexer->index + 1;
	unsigned char c = current(lexer);

	if (c == 0 || c == '\n') {
		result->kind = TK_INVALID;
		return LS_DONE;
	}
	if (c == '"') {
		result->kind = TK_IDENTIFIER;
		return LS_STRING_LITERAL;
	}
	if (is_ident_start(c)) {
		result->kind = TK_BUILTIN;
		return LS_BUILTIN;
	}
	return LS_INVALID;
}

static LexState
lex_identifier(Lexer *lexer, Token *result)
{
	lexer->index = lexer->index + 1;
	if (is_ident_char(current(lexer)))
		return LS_IDENTIFIER;

	uint32_t length = lexer->index - result->start;
	result->kind = lexer_keyword_lookup(lexer->source + result->start, length);
	return LS_DONE;
}

static LexState
lex_builtin(Lexer *lexer)
{
	lexer->index = lexer->index + 1;
	if (is_ident_char(current(lexer)))
		return LS_BUILTIN;
	return LS_DONE;
}

/* After one '\': a second '\' starts a multiline string line. */
static LexState
lex_backslash(Lexer *lexer, Token *result)
{
	lexer->index = lexer->index + 1;
	unsigned char c = current(lexer);

	if (c == 0 || c == '\n') {
		result->kind = TK_INVALID;
		return LS_DONE;
	}
	if (c == '\\')
		return LS_MULTILINE_STRING_LITERAL_LINE;
	return LS_INVALID;
}

static LexState
lex_string_literal(Lexer *lexer, Token *result)
{
	lexer->index = lexer->index + 1;
	unsigned char c = current(lexer);

	if (c == 0)
		return nul_in_token(lexer, result);
	if (c == '\n') {
		result->kind = TK_INVALID;
		return LS_DONE;
	}
	if (c == '\\')
		return LS_STRING_LITERAL_BACKSLASH;
	if (c == '"') {
		lexer->index = lexer->index + 1;
		return LS_DONE;
	}
	if (is_string_control(c))
		return LS_INVALID;
	return LS_STRING_LITERAL;
}

static LexState
lex_string_literal_backslash(Lexer *lexer, Token *result)
{
	lexer->index = lexer->index + 1;
	unsigned char c = current(lexer);

	if (c == 0 || c == '\n') {
		result->kind = TK_INVALID;
		return LS_DONE;
	}
	return LS_STRING_LITERAL;
}

static LexState
lex_char_literal(Lexer *lexer, Token *result)
{
	lexer->index = lexer->index + 1;
	unsigned char c = current(lexer);

	if (c == 0)
		return nul_in_token(lexer, result);
	if (c == '\n') {
		result->kind = TK_INVALID;
		return LS_DONE;
	}
	if (c == '\\')
		return LS_CHAR_LITERAL_BACKSLASH;
	if (c == '\'') {
		lexer->index = lexer->index + 1;
		return LS_DONE;
	}
	if (is_string_control(c))
		return LS_INVALID;
	return LS_CHAR_LITERAL;
}

static LexState
lex_char_literal_backslash(Lexer *lexer, Token *result)
{
	lexer->index = lexer->index + 1;
	unsigned char c = current(lexer);

	if (c == 0)
		return nul_in_token(lexer, result);
	if (c == '\n') {
		result->kind = TK_INVALID;
		return LS_DONE;
	}
	if (is_string_control(c))
		return LS_INVALID;
	return LS_CHAR_LITERAL;
}

/* Body of a "\\" line: runs to the end of the line, which is not included. */
static LexState
lex_multiline_string_literal_line(Lexer *lexer)
{
	lexer->index = lexer->index + 1;
	unsigned char c = current(lexer);

	if (c == 0) {
		if (is_at_end(lexer))
			return LS_DONE;
		return LS_INVALID;
	}
	if (c == '\n')
		return LS_DONE;
	if (c == '\r') {
		if (peek_next(lexer) != '\n')
			return LS_INVALID;
		return LS_DONE;
	}
	if (is_comment_control(c))
		return LS_INVALID;
	return LS_MULTILINE_STRING_LITERAL_LINE;
}

static LexState
lex_equal(Lexer *lexer, Token *result)
{
	lexer->index = lexer->index + 1;
	unsigned char c = current(lexer);

	if (c == '=') {
		single(lexer, result, TK_EQUAL_EQUAL);
		return LS_DONE;
	}
	if (c == '>') {
		single(lexer, result, TK_EQUAL_ANGLE_BRACKET_RIGHT);
		return LS_DONE;
	}
	result->kind = TK_EQUAL;
	return LS_DONE;
}

static LexState
lex_pipe(Lexer *lexer, Token *result)
{
	lexer->index = lexer->index + 1;
	unsigned char c = current(lexer);

	if (c == '=') {
		single(lexer, result, TK_PIPE_EQUAL);
		return LS_DONE;
	}
	if (c == '|') {
		single(lexer, result, TK_PIPE_PIPE);
		return LS_DONE;
	}
	result->kind = TK_PIPE;
	return LS_DONE;
}

static LexState
lex_minus(Lexer *lexer, Token *result)
{
	lexer->index = lexer->index + 1;
	unsigned char c = current(lexer);

	if (c == '>') {
		single(lexer, result, TK_ARROW);
		return LS_DONE;
	}
	if (c == '=') {
		single(lexer, result, TK_MINUS_EQUAL);
		return LS_DONE;
	}
	if (c == '%')
		return LS_MINUS_PERCENT;
	if (c == '|')
		return LS_MINUS_PIPE;
	result->kind = TK_MINUS;
	return LS_DONE;
}

static LexState
lex_asterisk(Lexer *lexer, Token *result)
{
	lexer->index = lexer->index + 1;
	unsigned char c = current(lexer);

	if (c == '=') {
		single(lexer, result, TK_ASTERISK_EQUAL);
		return LS_DONE;
	}
	if (c == '*') {
		single(lexer, result, TK_ASTERISK_ASTERISK);
		return LS_DONE;
	}
	if (c == '%')
		return LS_ASTERISK_PERCENT;
	if (c == '|')
		return LS_ASTERISK_PIPE;
	result->kind = TK_ASTERISK;
	return LS_DONE;
}

static LexState
lex_plus(Lexer *lexer, Token *result)
{
	lexer->index = lexer->index + 1;
	unsigned char c = current(lexer);

	if (c == '=') {
		single(lexer, result, TK_PLUS_EQUAL);
		return LS_DONE;
	}
	if (c == '+') {
		single(lexer, result, TK_PLUS_PLUS);
		return LS_DONE;
	}
	if (c == '%')
		return LS_PLUS_PERCENT;
	if (c == '|')
		return LS_PLUS_PIPE;
	result->kind = TK_PLUS;
	return LS_DONE;
}

static LexState
lex_angle_bracket_left(Lexer *lexer, Token *result)
{
	lexer->index = lexer->index + 1;
	unsigned char c = current(lexer);

	if (c == '<')
		return LS_ANGLE_BRACKET_ANGLE_BRACKET_LEFT;
	if (c == '=') {
		single(lexer, result, TK_ANGLE_BRACKET_LEFT_EQUAL);
		return LS_DONE;
	}
	result->kind = TK_ANGLE_BRACKET_LEFT;
	return LS_DONE;
}

static LexState
lex_angle_bracket_angle_bracket_left(Lexer *lexer, Token *result)
{
	lexer->index = lexer->index + 1;
	unsigned char c = current(lexer);

	if (c == '=') {
		single(lexer, result, TK_ANGLE_BRACKET_ANGLE_BRACKET_LEFT_EQUAL);
		return LS_DONE;
	}
	if (c == '|')
		return LS_ANGLE_BRACKET_ANGLE_BRACKET_LEFT_PIPE;
	result->kind = TK_ANGLE_BRACKET_ANGLE_BRACKET_LEFT;
	return LS_DONE;
}

static LexState
lex_angle_bracket_right(Lexer *lexer, Token *result)
{
	lexer->index = lexer->index + 1;
	unsigned char c = current(lexer);

	if (c == '>')
		return LS_ANGLE_BRACKET_ANGLE_BRACKET_RIGHT;
	if (c == '=') {
		single(lexer, result, TK_ANGLE_BRACKET_RIGHT_EQUAL);
		return LS_DONE;
	}
	result->kind = TK_ANGLE_BRACKET_RIGHT;
	return LS_DONE;
}

static LexState
lex_period(Lexer *lexer, Token *result)
{
	lexer->index = lexer->index + 1;
	unsigned char c = current(lexer);

	if (c == '.')
		return LS_PERIOD_2;
	if (c == '*')
		return LS_PERIOD_ASTERISK;
	result->kind = TK_PERIOD;
	return LS_DONE;
}

static LexState
lex_period_2(Lexer *lexer, Token *result)
{
	lexer->index = lexer->index + 1;
	if (current(lexer) == '.') {
		single(lexer, result, TK_ELLIPSIS3);
		return LS_DONE;
	}
	result->kind = TK_ELLIPSIS2;
	return LS_DONE;
}

/* ".*" followed by '*' is the invalid ".**" token; the second '*' is not consumed. */
static LexState
lex_period_asterisk(Lexer *lexer, Token *result)
{
	lexer->index = lexer->index + 1;
	if (current(lexer) == '*')
		result->kind = TK_INVALID_PERIODASTERISKS;
	else
		result->kind = TK_PERIOD_ASTERISK;
	return LS_DONE;
}

static LexState
lex_slash(Lexer *lexer, Token *result)
{
	lexer->index = lexer->index + 1;
	unsigned char c = current(lexer);

	if (c == '/')
		return LS_LINE_COMMENT_START;
	if (c == '=') {
		single(lexer, result, TK_SLASH_EQUAL);
		return LS_DONE;
	}
	result->kind = TK_SLASH;
	return LS_DONE;
}

/* A comment that runs to end of input is followed by EOF. */
static LexState
comment_reached_nul(Lexer *lexer, Token *result)
{
	if (!is_at_end(lexer))
		return LS_INVALID;
	result->kind = TK_EOF;
	result->start = lexer->index;
	return LS_DONE;
}

/* After "//": decides between a plain comment, "///" doc comment and "//!" container doc comment. */
static LexState
lex_line_comment_start(Lexer *lexer, Token *result)
{
	lexer->index = lexer->index + 1;
	unsigned char c = current(lexer);

	if (c == 0)
		return comment_reached_nul(lexer, result);
	if (c == '!') {
		result->kind = TK_CONTAINER_DOC_COMMENT;
		return LS_DOC_COMMENT;
	}
	if (c == '\n') {
		lexer->index = lexer->index + 1;
		result->start = lexer->index;
		return LS_START;
	}
	if (c == '/')
		return LS_DOC_COMMENT_START;
	if (c == '\r')
		return LS_EXPECT_NEWLINE;
	if (is_comment_control(c))
		return LS_INVALID;
	return LS_LINE_COMMENT;
}

/* After "///": a fourth '/' makes it a plain comment again. */
static LexState
lex_doc_comment_start(Lexer *lexer, Token *result)
{
	lexer->index = lexer->index + 1;
	unsigned char c = current(lexer);

	if (c == 0 || c == '\n') {
		result->kind = TK_DOC_COMMENT;
		return LS_DONE;
	}
	if (c == '\r') {
		if (peek_next(lexer) == '\n') {
			result->kind = TK_DOC_COMMENT;
			return LS_DONE;
		}
		return LS_INVALID;
	}
	if (c == '/')
		return LS_LINE_COMMENT;
	if (is_comment_control(c))
		return LS_INVALID;
	result->kind = TK_DOC_COMMENT;
	return LS_DOC_COMMENT;
}

static LexState
lex_line_comment(Lexer *lexer, Token *result)
{
	lexer->index = lexer->index + 1;
	unsigned char c = current(lexer);

	if (c == 0)
		return comment_reached_nul(lexer, result);
	if (c == '\n') {
		lexer->index = lexer->index + 1;
		result->start = lexer->index;
		return LS_START;
	}
	if (c == '\r')
		return LS_EXPECT_NEWLINE;
	if (is_comment_control(c))
		return LS_INVALID;
	return LS_LINE_COMMENT;
}

static LexState
lex_doc_comment(Lexer *lexer)
{
	lexer->index = lexer->index + 1;
	unsigned char c = current(lexer);

	if (c == 0 || c == '\n')
		return LS_DONE;
	if (c == '\r') {
		if (peek_next(lexer) != '\n')
			return LS_INVALID;
		return LS_DONE;
	}
	if (is_comment_control(c))
		return LS_INVALID;
	return LS_DOC_COMMENT;
}

/* Integer part of a number literal. Note: this state looks at the current byte without advancing first. */
static LexState
lex_int(Lexer *lexer)
{
	unsigned char c = current(lexer);

	if (c == '.')
		return LS_INT_PERIOD;
	if (is_number_continue(c)) {
		lexer->index = lexer->index + 1;
		return LS_INT;
	}
	if (is_exponent_marker(c))
		return LS_INT_EXPONENT;
	return LS_DONE;
}

static LexState
lex_int_exponent(Lexer *lexer)
{
	lexer->index = lexer->index + 1;
	unsigned char c = current(lexer);

	if (c == '-' || c == '+') {
		lexer->index = lexer->index + 1;
		return LS_FLOAT;
	}
	return LS_INT;
}

/* After "<digits>.": a digit/letter makes it a float; otherwise the '.' belongs to the next token (e.g. "1..2"). */
static LexState
lex_int_period(Lexer *lexer)
{
	lexer->index = lexer->index + 1;
	unsigned char c = current(lexer);

	if (is_number_continue(c)) {
		lexer->index = lexer->index + 1;
		return LS_FLOAT;
	}
	if (is_exponent_marker(c))
		return LS_FLOAT_EXPONENT;
	lexer->index = lexer->index - 1;
	return LS_DONE;
}

static LexState
lex_float(Lexer *lexer)
{
	unsigned char c = current(lexer);

	if (is_number_continue(c)) {
		lexer->index = lexer->index + 1;
		return LS_FLOAT;
	}
	if (is_exponent_marker(c))
		return LS_FLOAT_EXPONENT;
	return LS_DONE;
}

static LexState
lex_float_exponent(Lexer *lexer)
{
	lexer->index = lexer->index + 1;
	unsigned char c = current(lexer);

	if (c == '-' || c == '+')
		lexer->index = lexer->index + 1;
	return LS_FLOAT;
}

/* Advance past the first byte of an operator and decide "op" vs "op=". */
static LexState
lex_op_or_equal(Lexer *lexer, Token *result, TokenKind plain, TokenKind with_equal)
{
	lexer->index = lexer->index + 1;
	finish_with_equal(lexer, result, plain, with_equal);
	return LS_DONE;
}

static LexState
lex_bang(Lexer *lexer, Token *result)
{
	return lex_op_or_equal(lexer, result, TK_BANG, TK_BANG_EQUAL);
}

Token
lexer_next(Lexer *lexer)
{
	Token result;
	result.kind = TK_INVALID;
	result.start = lexer->index;
	result.end = lexer->index;

	LexState state = LS_START;
	while (state != LS_DONE) {
		switch (state) {
		case LS_START:
			state = lex_start(lexer, &result);
			break;
		case LS_DONE:
			break;
		case LS_EXPECT_NEWLINE:
			state = lex_expect_newline(lexer, &result);
			break;
		case LS_INVALID:
			state = lex_invalid(lexer, &result);
			break;
		case LS_SAW_AT_SIGN:
			state = lex_saw_at_sign(lexer, &result);
			break;
		case LS_IDENTIFIER:
			state = lex_identifier(lexer, &result);
			break;
		case LS_BUILTIN:
			state = lex_builtin(lexer);
			break;
		case LS_BACKSLASH:
			state = lex_backslash(lexer, &result);
			break;
		case LS_STRING_LITERAL:
			state = lex_string_literal(lexer, &result);
			break;
		case LS_STRING_LITERAL_BACKSLASH:
			state = lex_string_literal_backslash(lexer, &result);
			break;
		case LS_CHAR_LITERAL:
			state = lex_char_literal(lexer, &result);
			break;
		case LS_CHAR_LITERAL_BACKSLASH:
			state = lex_char_literal_backslash(lexer, &result);
			break;
		case LS_MULTILINE_STRING_LITERAL_LINE:
			state = lex_multiline_string_literal_line(lexer);
			break;
		case LS_EQUAL:
			state = lex_equal(lexer, &result);
			break;
		case LS_BANG:
			state = lex_bang(lexer, &result);
			break;
		case LS_PIPE:
			state = lex_pipe(lexer, &result);
			break;
		case LS_MINUS:
			state = lex_minus(lexer, &result);
			break;
		case LS_MINUS_PERCENT:
			state = lex_op_or_equal(lexer, &result, TK_MINUS_PERCENT, TK_MINUS_PERCENT_EQUAL);
			break;
		case LS_MINUS_PIPE:
			state = lex_op_or_equal(lexer, &result, TK_MINUS_PIPE, TK_MINUS_PIPE_EQUAL);
			break;
		case LS_ASTERISK:
			state = lex_asterisk(lexer, &result);
			break;
		case LS_ASTERISK_PERCENT:
			state = lex_op_or_equal(lexer, &result, TK_ASTERISK_PERCENT, TK_ASTERISK_PERCENT_EQUAL);
			break;
		case LS_ASTERISK_PIPE:
			state = lex_op_or_equal(lexer, &result, TK_ASTERISK_PIPE, TK_ASTERISK_PIPE_EQUAL);
			break;
		case LS_SLASH:
			state = lex_slash(lexer, &result);
			break;
		case LS_LINE_COMMENT_START:
			state = lex_line_comment_start(lexer, &result);
			break;
		case LS_LINE_COMMENT:
			state = lex_line_comment(lexer, &result);
			break;
		case LS_DOC_COMMENT_START:
			state = lex_doc_comment_start(lexer, &result);
			break;
		case LS_DOC_COMMENT:
			state = lex_doc_comment(lexer);
			break;
		case LS_INT:
			state = lex_int(lexer);
			break;
		case LS_INT_EXPONENT:
			state = lex_int_exponent(lexer);
			break;
		case LS_INT_PERIOD:
			state = lex_int_period(lexer);
			break;
		case LS_FLOAT:
			state = lex_float(lexer);
			break;
		case LS_FLOAT_EXPONENT:
			state = lex_float_exponent(lexer);
			break;
		case LS_AMPERSAND:
			state = lex_op_or_equal(lexer, &result, TK_AMPERSAND, TK_AMPERSAND_EQUAL);
			break;
		case LS_CARET:
			state = lex_op_or_equal(lexer, &result, TK_CARET, TK_CARET_EQUAL);
			break;
		case LS_PERCENT:
			state = lex_op_or_equal(lexer, &result, TK_PERCENT, TK_PERCENT_EQUAL);
			break;
		case LS_PLUS:
			state = lex_plus(lexer, &result);
			break;
		case LS_PLUS_PERCENT:
			state = lex_op_or_equal(lexer, &result, TK_PLUS_PERCENT, TK_PLUS_PERCENT_EQUAL);
			break;
		case LS_PLUS_PIPE:
			state = lex_op_or_equal(lexer, &result, TK_PLUS_PIPE, TK_PLUS_PIPE_EQUAL);
			break;
		case LS_ANGLE_BRACKET_LEFT:
			state = lex_angle_bracket_left(lexer, &result);
			break;
		case LS_ANGLE_BRACKET_ANGLE_BRACKET_LEFT:
			state = lex_angle_bracket_angle_bracket_left(lexer, &result);
			break;
		case LS_ANGLE_BRACKET_ANGLE_BRACKET_LEFT_PIPE:
			state = lex_op_or_equal(lexer, &result, TK_ANGLE_BRACKET_ANGLE_BRACKET_LEFT_PIPE, TK_ANGLE_BRACKET_ANGLE_BRACKET_LEFT_PIPE_EQUAL);
			break;
		case LS_ANGLE_BRACKET_RIGHT:
			state = lex_angle_bracket_right(lexer, &result);
			break;
		case LS_ANGLE_BRACKET_ANGLE_BRACKET_RIGHT:
			state = lex_op_or_equal(lexer, &result, TK_ANGLE_BRACKET_ANGLE_BRACKET_RIGHT, TK_ANGLE_BRACKET_ANGLE_BRACKET_RIGHT_EQUAL);
			break;
		case LS_PERIOD:
			state = lex_period(lexer, &result);
			break;
		case LS_PERIOD_2:
			state = lex_period_2(lexer, &result);
			break;
		case LS_PERIOD_ASTERISK:
			state = lex_period_asterisk(lexer, &result);
			break;
		}
	}

	result.end = lexer->index;
	return result;
}
