/*
 * driver: runs the compiler stages and the external tools.
 *
 * QBE is a separate executable: `$SZC_QBE` if set, else `qbe` from PATH. The C
 * compiler that assembles and links is `$SZC_CC` if set, else `cc`.
 */
#define _POSIX_C_SOURCE 200809L

#include "driver.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "arena.h"
#include "codegen.h"
#include "diag.h"
#include "intern.h"
#include "ir.h"
#include "parser.h"
#include "sema.h"
#include "source.h"

static const char *
tool_path(const char *variable, const char *fallback)
{
	const char *value = getenv(variable);
	return value != NULL && value[0] != '\0' ? value : fallback;
}

/* Runs argv[0] with arguments, waits, and returns its exit status (or -1). */
static int
run_process(char **argv)
{
	pid_t pid = fork();
	if (pid < 0) {
		fprintf(stderr, "szc: fork failed: %s\n", strerror(errno));
		return -1;
	}
	if (pid == 0) {
		execvp(argv[0], argv);
		fprintf(stderr, "szc: cannot run '%s': %s\n", argv[0], strerror(errno));
		_exit(127);
	}
	int status;
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR) {
			fprintf(stderr, "szc: waitpid failed: %s\n", strerror(errno));
			return -1;
		}
	}
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	if (WIFSIGNALED(status))
		return 128 + WTERMSIG(status);
	return -1;
}

int
driver_emit_qbe(const char *input, FILE *out)
{
	Arena arena;
	arena_init(&arena);
	SourceTable sources;
	source_table_init(&sources, &arena);
	Diag diag;
	diag_init(&diag, &arena, &sources);
	Interner interner;
	intern_init(&interner, &arena);

	int status = 0;
	FileId file;
	if (!source_load_file(&sources, input, &file)) {
		arena_free(&arena);
		return 1;
	}
	AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
	if (!diag_has_errors(&diag)) {
		IrFile *ir = irgen_file(&arena, &diag, &interner, source_get(&sources, file), file, root);
		if (!diag_has_errors(&diag)) {
			Sema *sema = sema_create(&arena, &diag, &interner, &sources);
			if (sema_analyze_executable(sema, file, ir))
				codegen_qbe(out, sema, &diag);
		}
	}
	diag_print(&diag, stderr);
	if (diag_has_errors(&diag))
		status = 1;
	if (fflush(out) != 0 || ferror(out)) {
		fprintf(stderr, "szc: cannot write QBE IL: %s\n", strerror(errno));
		status = 2;
	}
	arena_free(&arena);
	return status;
}

static char *
concat(const char *a, const char *b)
{
	size_t la = strlen(a);
	size_t lb = strlen(b);
	char *result = malloc(la + lb + 1);
	if (result == NULL) {
		fputs("szc: out of memory\n", stderr);
		exit(2);
	}
	memcpy(result, a, la);
	memcpy(result + la, b, lb + 1);
	return result;
}

int
driver_build_exe(const BuildOptions *options)
{
	char *ssa_path = concat(options->output, ".ssa");
	char *asm_path = concat(options->output, ".s");
	int status = 0;
	FILE *ssa = fopen(ssa_path, "w");
	if (ssa == NULL) {
		fprintf(stderr, "szc: cannot write '%s': %s\n", ssa_path, strerror(errno));
		status = 2;
	} else {
		status = driver_emit_qbe(options->input, ssa);
		if (fclose(ssa) != 0 && status == 0)
			status = 2;
	}
	if (status == 0) {
		char *qbe_argv[] = {(char *)tool_path("SZC_QBE", "qbe"), "-o", asm_path, ssa_path, NULL};
		int qbe_status = run_process(qbe_argv);
		if (qbe_status != 0) {
			fprintf(stderr, "szc: qbe failed (status %d) on '%s'\n", qbe_status, ssa_path);
			status = 2;
		}
	}
	if (status == 0) {
		char *cc_argv[] = {(char *)tool_path("SZC_CC", "cc"), "-o", (char *)options->output, asm_path, NULL};
		int cc_status = run_process(cc_argv);
		if (cc_status != 0) {
			fprintf(stderr, "szc: the C compiler failed (status %d) on '%s'\n", cc_status, asm_path);
			status = 2;
		}
	}
	if (!options->keep_temporaries) {
		remove(ssa_path);
		remove(asm_path);
	}
	free(ssa_path);
	free(asm_path);
	return status;
}

int
driver_run(const char *input, char **args, int arg_count)
{
	char directory[] = "/tmp/szc-run-XXXXXX";
	if (mkdtemp(directory) == NULL) {
		fprintf(stderr, "szc: cannot create a temporary directory: %s\n", strerror(errno));
		return 2;
	}
	char *exe_path = concat(directory, "/main");
	BuildOptions options;
	options.input = input;
	options.output = exe_path;
	options.keep_temporaries = false;
	int status = driver_build_exe(&options);
	if (status == 0) {
		char **argv = malloc(((size_t)arg_count + 2) * sizeof(char *));
		if (argv == NULL) {
			fputs("szc: out of memory\n", stderr);
			exit(2);
		}
		argv[0] = exe_path;
		for (int i = 0; i < arg_count; i++)
			argv[i + 1] = args[i];
		argv[arg_count + 1] = NULL;
		status = run_process(argv);
		free(argv);
	}
	remove(exe_path);
	char *ssa_path = concat(exe_path, ".ssa");
	char *asm_path = concat(exe_path, ".s");
	remove(ssa_path);
	remove(asm_path);
	free(ssa_path);
	free(asm_path);
	rmdir(directory);
	free(exe_path);
	return status;
}
