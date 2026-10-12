/*
 * main.c: szc command line driver.
 *
 *   szc tokens <file>        print every token (kind, position, text)
 *   szc tokens-kinds <file>  print only token kinds (used by tests)
 *   szc ast <file>           parse and print the syntax tree
 *   szc parse <file>...      parse files and report syntax errors only
 *   szc version
 */
#include <stdio.h>
#include <string.h>

#include "arena.h"
#include "ast_dump.h"
#include "diag.h"
#include "lexer.h"
#include "parser.h"
#include "source.h"

#define SZC_VERSION "0.0.1"
#define SZC_ZIG_VERSION "0.17.0"

typedef enum TokenDumpMode {
	TOKEN_DUMP_FULL,
	TOKEN_DUMP_KINDS,
} TokenDumpMode;

static void
print_usage(FILE *out)
{
	fputs("usage: szc <command> [arguments]\n"
	      "\n"
	      "commands:\n"
	      "  tokens <file>        dump tokens with positions and text\n"
	      "  tokens-kinds <file>  dump token kinds only\n"
	      "  ast <file>           dump the syntax tree\n"
	      "  parse <file>...      check files for syntax errors\n"
	      "  version              print version\n", out);
}

/* Prints token text with control and non-ASCII bytes escaped, so the output is one line. */
static void
print_escaped(FILE *out, const char *text, uint32_t length)
{
	for (uint32_t i = 0; i < length; i++) {
		unsigned char c = (unsigned char)text[i];
		if (c == '\\')
			fputs("\\\\", out);
		else if (c == '"')
			fputs("\\\"", out);
		else if (c == '\n')
			fputs("\\n", out);
		else if (c == '\r')
			fputs("\\r", out);
		else if (c == '\t')
			fputs("\\t", out);
		else if (c < 0x20 || c >= 0x7f)
			fprintf(out, "\\x%02x", c);
		else
			fputc(c, out);
	}
}

static int
command_tokens(const char *path, TokenDumpMode mode)
{
	Arena arena;
	arena_init(&arena);

	SourceTable sources;
	source_table_init(&sources, &arena);

	Diag diag;
	diag_init(&diag, &arena, &sources);

	FileId file_id;
	if (!source_load_file(&sources, path, &file_id)) {
		arena_free(&arena);
		return 1;
	}

	SourceFile *file = source_get(&sources, file_id);
	Lexer lexer;
	lexer_init(&lexer, file->text, file->length);

	for (;;) {
		Token token = lexer_next(&lexer);
		if (mode == TOKEN_DUMP_KINDS) {
			printf("%s\n", token_kind_name(token.kind));
		} else {
			LineColumn position = source_line_column(file, token.start);
			printf("%u:%u %s \"", position.line, position.column, token_kind_name(token.kind));
			print_escaped(stdout, file->text + token.start, token.end - token.start);
			printf("\"\n");
		}
		if (token.kind == TK_INVALID)
			diag_report(&diag, DIAG_ERROR, file_id, token.start, "invalid token");
		if (token.kind == TK_EOF)
			break;
	}

	int status = 0;
	if (mode == TOKEN_DUMP_FULL) {
		diag_print(&diag, stderr);
		if (diag_has_errors(&diag))
			status = 1;
	}
	arena_free(&arena);
	return status;
}

typedef enum ParseMode {
	PARSE_DUMP_AST,
	PARSE_CHECK_ONLY,
} ParseMode;

/* Parses each file; prints the tree in PARSE_DUMP_AST mode. Returns 1 if any file had errors. */
static int
command_parse(char **paths, int count, ParseMode mode)
{
	int status = 0;
	for (int i = 0; i < count; i++) {
		Arena arena;
		arena_init(&arena);
		SourceTable sources;
		source_table_init(&sources, &arena);
		Diag diag;
		diag_init(&diag, &arena, &sources);

		FileId file_id;
		if (!source_load_file(&sources, paths[i], &file_id)) {
			arena_free(&arena);
			status = 1;
			continue;
		}
		AstContainerDecl *root = parse_file(&arena, &diag, &sources, file_id);
		if (mode == PARSE_DUMP_AST)
			ast_dump(stdout, source_get(&sources, file_id), &root->base);
		diag_print(&diag, stderr);
		if (diag_has_errors(&diag))
			status = 1;
		arena_free(&arena);
	}
	return status;
}

int
main(int argc, char **argv)
{
	if (argc < 2) {
		print_usage(stderr);
		return 2;
	}

	const char *command = argv[1];
	if (strcmp(command, "version") == 0) {
		printf("szc %s (zig %s)\n", SZC_VERSION, SZC_ZIG_VERSION);
		return 0;
	}
	if (strcmp(command, "help") == 0 || strcmp(command, "--help") == 0) {
		print_usage(stdout);
		return 0;
	}
	if (strcmp(command, "tokens") == 0 || strcmp(command, "tokens-kinds") == 0) {
		if (argc != 3) {
			print_usage(stderr);
			return 2;
		}
		TokenDumpMode mode = TOKEN_DUMP_FULL;
		if (strcmp(command, "tokens-kinds") == 0)
			mode = TOKEN_DUMP_KINDS;
		return command_tokens(argv[2], mode);
	}

	if (strcmp(command, "ast") == 0) {
		if (argc != 3) {
			print_usage(stderr);
			return 2;
		}
		return command_parse(argv + 2, 1, PARSE_DUMP_AST);
	}
	if (strcmp(command, "parse") == 0) {
		if (argc < 3) {
			print_usage(stderr);
			return 2;
		}
		return command_parse(argv + 2, argc - 2, PARSE_CHECK_ONLY);
	}

	fprintf(stderr, "szc: unknown command '%s'\n", command);
	print_usage(stderr);
	return 2;
}
